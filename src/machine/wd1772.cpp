#include "machine/wd1772.h"

#include <utility>
#include <cstring>

namespace dsp {

namespace {
constexpr uint8_t kBusy = 0x01;
constexpr uint8_t kDrq = 0x02;
constexpr uint8_t kLostData = 0x04;
constexpr uint8_t kCrcError = 0x08;
constexpr uint8_t kRecordNotFound = 0x10;
constexpr uint8_t kRecordType = 0x20;  // set = deleted data mark, on read-sector status
constexpr uint8_t kTrack00 = 0x04;     // type1 status bit2
constexpr uint8_t kNotReady = 0x80;
}  // namespace

void Wd1772::reset() {
    side_ = 0;
    cur_cyl_ = 0;
    status_ = 0;
    track_ = 0;
    sector_ = 1;
    data_ = 0;
    command_ = 0;
    drq_ = intrq_ = busy_ = writing_ = multiple_ = false;
    type1_ = true;
    buf_.clear();
    buf_pos_ = 0;
    index_state_ = false;
    status_reads_with_data_ = 0;
}

uint8_t Wd1772::status_r() {
    intrq_ = false;
    if (type1_) {
        uint8_t s = status_;
        if (busy_) s |= kBusy; else s &= uint8_t(~kBusy);
        if (!disk_ || !disk_->loaded()) s |= kNotReady; else s &= uint8_t(~kNotReady);
        if (cur_cyl_ == 0) s |= kTrack00; else s &= uint8_t(~kTrack00);
        if (index_state_) s |= kDrq; else s &= uint8_t(~kDrq);  // bit1 = INDEX for type1
        return s;
    }
    // Type 2/3: give up if the CPU keeps polling status without ever
    // reading DATA while a byte is waiting. Confirmed against SimCoupe
    // (the reference SAM emulator): some commercial titles deliberately
    // poll a bounded number of times and rely on this timeout -- rather
    // than an immediate data read -- to detect "no more data" and move on.
    // Without it, such loaders hang forever instead of seeing the error
    // they're actually watching for.
    if (drq_ && ++status_reads_with_data_ >= kMaxStatusPollsWithData) {
        status_reads_with_data_ = 0;
        busy_ = false;
        drq_ = false;
        buf_pos_ = 0;
        status_ = kLostData;
    }
    uint8_t s = status_;
    if (busy_) s |= kBusy; else s &= uint8_t(~kBusy);
    if (!disk_ || !disk_->loaded()) s |= kNotReady; else s &= uint8_t(~kNotReady);
    if (drq_) s |= kDrq; else s &= uint8_t(~kDrq);
    return s;
}

uint8_t Wd1772::data_r() {
    if (!drq_) return data_;
    status_reads_with_data_ = 0;
    data_ = buf_pos_ < buf_.size() ? buf_[buf_pos_++] : 0xff;
    if (buf_pos_ >= buf_.size()) {
        drq_ = false;
        if (multiple_ && !writing_) {
            sector_++;
            start_read_sector();
        } else {
            finish();
        }
    }
    return data_;
}

void Wd1772::data_w(uint8_t value) {
    data_ = value;
    if (!drq_ || !writing_) return;
    status_reads_with_data_ = 0;
    if (buf_pos_ < buf_.size()) buf_[buf_pos_++] = value;
    if (buf_pos_ >= buf_.size()) {
        drq_ = false;
        // Commit the written sector back into the disk image in memory.
        if (const SamSector* s = find_sector(true)) {
            const_cast<SamSector*>(s)->data = buf_;
            const_cast<SamSector*>(s)->data_missing = false;
        }
        if (multiple_) {
            sector_++;
            start_write_sector();
        } else {
            finish();
        }
    }
}

const SamSector* Wd1772::find_sector(bool by_id) const {
    if (!disk_) return nullptr;
    if (by_id) return disk_->find(track_, sector_, side_);
    // READ ADDRESS: report whichever sector the head would meet next. Try
    // both track-numbering conventions (see SamDisk::find) and return the
    // first sector of whichever candidate track exists.
    for (const auto& c : {std::pair<int,int>{int(cur_cyl_), side_ & 1},
                          std::pair<int,int>{cur_cyl_ / 2, cur_cyl_ & 1}}) {
        const SamTrack* t = disk_->track(c.first, c.second);
        if (t && !t->sectors.empty()) return &t->sectors.front();
    }
    return nullptr;
}

void Wd1772::finish(uint8_t extra_status) {
    busy_ = false;
    drq_ = false;
    intrq_ = true;
    status_ = extra_status;
}

void Wd1772::start_read_sector() {
    const SamSector* s = find_sector(true);
    if (!s) {
        finish(kRecordNotFound);
        return;
    }
    if (s->id_crc_error) {
        finish(kCrcError);
        return;
    }
    buf_ = s->data;
    buf_.resize(size_t(s->size_bytes()), 0);
    buf_pos_ = 0;
    busy_ = true;
    drq_ = true;
    status_ = uint8_t((s->deleted ? kRecordType : 0) | (s->data_crc_error ? kCrcError : 0));
}

void Wd1772::start_write_sector() {
    const SamSector* s = find_sector(true);
    if (!s) {
        finish(kRecordNotFound);
        return;
    }
    buf_.assign(size_t(s->size_bytes()), 0);
    buf_pos_ = 0;
    busy_ = true;
    drq_ = true;
    writing_ = true;
    status_ = 0;
}

void Wd1772::start_read_address() {
    const SamSector* found = find_sector(false);
    if (!found) {
        finish(kRecordNotFound);
        return;
    }
    const SamSector& s = *found;
    buf_ = {s.cyl, s.head, s.sector, s.size_code, 0, 0};  // CRC bytes unused by emulation
    sector_ = s.cyl;  // WD1772 quirk: sector register mirrors the ID's cyl byte after this command
    buf_pos_ = 0;
    busy_ = true;
    drq_ = true;
    writing_ = false;
    status_ = 0;
}

void Wd1772::command_w(uint8_t value) {
    command_ = value;
    intrq_ = false;
    const uint8_t top = uint8_t(value >> 4);

    // Type I: RESTORE(0x0), SEEK(0x1), STEP(0x2-0x3), STEP-IN(0x4-0x5),
    // STEP-OUT(0x6-0x7). Bit4 (within STEP/IN/OUT) is the "update track
    // register" flag; head-load/verify/rate bits are not modelled since we
    // don't emulate FDC timing precisely.
    if (top <= 0x07) {
        type1_ = true;
        writing_ = multiple_ = false;
        if (top == 0x00) {  // RESTORE
            cur_cyl_ = 0;
            track_ = 0;
        } else if (top == 0x01) {  // SEEK: target track was written to `data_`
            cur_cyl_ = data_;
            track_ = data_;
        } else if (top == 0x02 || top == 0x03) {  // STEP (direction unchanged)
            // Direction from the last SEEK/STEP-IN/STEP-OUT isn't tracked
            // separately; treat as a no-op step, which is harmless for the
            // sequences real boot loaders use (RESTORE/SEEK-based).
            if (value & 0x10) track_ = uint8_t(cur_cyl_);
        } else if (top == 0x04 || top == 0x05) {  // STEP-IN
            if (cur_cyl_ < 255) cur_cyl_++;
            if (value & 0x10) track_ = uint8_t(cur_cyl_);
        } else {  // STEP-OUT
            if (cur_cyl_ > 0) cur_cyl_--;
            if (value & 0x10) track_ = uint8_t(cur_cyl_);
        }
        finish(uint8_t(cur_cyl_ == 0 ? kTrack00 : 0));
        return;
    }

    type1_ = false;
    if ((value & 0xc0) == 0x80) {  // READ SECTOR (100x xxxx)
        multiple_ = (value & 0x10) != 0;
        writing_ = false;
        start_read_sector();
    } else if ((value & 0xc0) == 0xa0) {  // WRITE SECTOR (101x xxxx)
        multiple_ = (value & 0x10) != 0;
        start_write_sector();
    } else if ((value & 0xf0) == 0xc0) {  // READ ADDRESS
        multiple_ = false;
        start_read_address();
    } else if ((value & 0xf0) == 0xd0) {  // FORCE INTERRUPT
        busy_ = false;
        drq_ = false;
        writing_ = multiple_ = false;
        // The controller reverts to type-1 (seek-style) status reporting
        // once idle, regardless of what command it interrupted.
        type1_ = true;
        intrq_ = (value & 0x0f) != 0;
        status_ = 0;
    } else {
        // READ TRACK / WRITE TRACK not implemented: report not-found rather
        // than hang a caller waiting for DRQ.
        finish(kRecordNotFound);
    }
}

}  // namespace dsp
