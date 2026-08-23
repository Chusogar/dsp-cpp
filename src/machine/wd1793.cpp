#include "machine/wd1793.h"

#include <cstring>

namespace dsp {
namespace {

constexpr uint8_t kBusy = 0x01;
constexpr uint8_t kDrq = 0x02;
constexpr uint8_t kTrack0 = 0x04;
constexpr uint8_t kRnf = 0x10;
constexpr uint8_t kWp = 0x40;
constexpr uint8_t kNotReady = 0x80;

}  // namespace

void Wd1793::reset() {
    status_ = 0;
    track_ = 0;
    sector_ = 1;
    data_ = 0;
    command_ = 0;
    drq_ = false;
    intrq_ = false;
    busy_ = false;
    type1_ = true;
    writing_ = false;
    index_pulse_ = false;
    buf_.clear();
    buf_pos_ = 0;
}

const uint8_t* Wd1793::current_sector() const {
    if (disk_ == nullptr || !disk_->present() || drive_ != 0) return nullptr;
    return disk_->sector(track_, side_, sector_);
}

uint8_t* Wd1793::current_sector() {
    if (disk_ == nullptr || !disk_->present() || drive_ != 0) return nullptr;
    return disk_->sector(track_, side_, sector_);
}

uint8_t Wd1793::status_r() {
    uint8_t value = 0;
    if (disk_ == nullptr || !disk_->present() || drive_ != 0) value |= kNotReady;
    if (busy_) value |= kBusy;
    if (type1_) {
        if (track_ == 0) value |= kTrack0;
        value |= 0x20;  // head loaded
        index_pulse_ = !index_pulse_;
        if (index_pulse_) value |= 0x02;
    } else {
        if (drq_) value |= kDrq;
        if (status_ & kRnf) value |= kRnf;
        if (status_ & kWp) value |= kWp;
    }
    return value;
}

void Wd1793::finish_type1() {
    type1_ = true;
    busy_ = false;
    drq_ = false;
    intrq_ = true;
    status_ = 0;
}

void Wd1793::complete_io(bool rnf) {
    type1_ = false;
    busy_ = false;
    drq_ = false;
    intrq_ = true;
    writing_ = false;
    status_ = rnf ? kRnf : 0;
}

void Wd1793::start_read_sector() {
    type1_ = false;
    writing_ = false;
    const uint8_t* src = current_sector();
    if (src == nullptr) {
        complete_io(true);
        return;
    }
    buf_.assign(src, src + TrdosDisk::kSectorSize);
    buf_pos_ = 0;
    busy_ = true;
    drq_ = true;
    intrq_ = false;
    status_ = 0;
}

void Wd1793::start_write_sector() {
    type1_ = false;
    writing_ = true;
    if (current_sector() == nullptr) {
        complete_io(true);
        return;
    }
    buf_.assign(TrdosDisk::kSectorSize, 0);
    buf_pos_ = 0;
    busy_ = true;
    drq_ = true;
    intrq_ = false;
    status_ = 0;
}

void Wd1793::start_read_address() {
    type1_ = false;
    writing_ = false;
    if (current_sector() == nullptr) {
        complete_io(true);
        return;
    }
    buf_ = {track_, uint8_t(side_), sector_, 1, 0, 0};
    buf_pos_ = 0;
    busy_ = true;
    drq_ = true;
    intrq_ = false;
    status_ = 0;
}

void Wd1793::command_w(uint8_t value) {
    command_ = value;
    intrq_ = false;
    const uint8_t type = uint8_t(value & 0xf0);
    if (type == 0xd0) {  // Force interrupt
        busy_ = false;
        drq_ = false;
        writing_ = false;
        type1_ = true;
        if (value & 0x08) intrq_ = true;
        return;
    }
    busy_ = true;
    drq_ = false;
    if (type <= 0x70) {
        if (type == 0x00) track_ = 0;
        else if (type == 0x10) track_ = data_;
        else if (type == 0x40 || type == 0x50) {
            if (track_ < 255) track_++;
        } else if (type == 0x60 || type == 0x70) {
            if (track_ > 0) track_--;
        }
        finish_type1();
        return;
    }
    if (type == 0x80 || type == 0x90) {
        start_read_sector();
        return;
    }
    if (type == 0xa0 || type == 0xb0) {
        start_write_sector();
        return;
    }
    if (type == 0xc0) {
        start_read_address();
        return;
    }
    if (type == 0xe0 || type == 0xf0) {
        // Read/write track (FORMAT). Instant complete so TR-DOS does not stall.
        complete_io(false);
        return;
    }
    complete_io(true);
}

uint8_t Wd1793::data_r() {
    if (!drq_ || buf_pos_ >= buf_.size()) return data_;
    data_ = buf_[buf_pos_++];
    if (buf_pos_ >= buf_.size()) {
        const bool multi = (command_ & 0x10) != 0;
        if (multi && sector_ < TrdosDisk::kSectorsPerTrack) {
            sector_++;
            start_read_sector();
        } else {
            complete_io(false);
        }
    }
    return data_;
}

void Wd1793::data_w(uint8_t value) {
    data_ = value;
    if (!writing_ || !drq_) return;
    if (buf_pos_ < buf_.size()) buf_[buf_pos_++] = value;
    if (buf_pos_ >= buf_.size()) {
        uint8_t* dest = current_sector();
        if (dest != nullptr) std::memcpy(dest, buf_.data(), buf_.size());
        complete_io(false);
    }
}

}  // namespace dsp
