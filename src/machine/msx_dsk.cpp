#include "machine/msx_dsk.h"

#include <algorithm>
#include <cstring>
#include <fstream>

namespace dsp {
namespace {

constexpr uint8_t kBusy = 0x01;
constexpr uint8_t kDrq = 0x02;
constexpr uint8_t kTrack0 = 0x04;
constexpr uint8_t kRnf = 0x10;
constexpr uint8_t kWp = 0x40;
constexpr uint8_t kNotReady = 0x80;

bool starts_with(const uint8_t* data, size_t size, const char* magic) {
    size_t n = std::strlen(magic);
    return size >= n && std::memcmp(data, magic, n) == 0;
}

}  // namespace

void MsxDisk::eject() {
    image_.clear();
    tracks_ = 0;
    heads_ = 0;
    spt_ = 9;
}

size_t MsxDisk::offset(int track, int head, int sector) const {
    if (track < 0 || track >= tracks_ || head < 0 || head >= heads_) return size_t(-1);
    if (sector < 1 || sector > spt_) return size_t(-1);
    return (size_t(track) * size_t(heads_) + size_t(head)) * size_t(spt_) * kSectorSize +
           size_t(sector - 1) * kSectorSize;
}

const uint8_t* MsxDisk::sector(int track, int head, int sector) const {
    size_t off = offset(track, head, sector);
    if (off == size_t(-1) || off + kSectorSize > image_.size()) return nullptr;
    return image_.data() + off;
}

uint8_t* MsxDisk::sector(int track, int head, int sector) {
    size_t off = offset(track, head, sector);
    if (off == size_t(-1) || off + kSectorSize > image_.size()) return nullptr;
    return image_.data() + off;
}

bool MsxDisk::load_file(const std::string& path, std::string* error) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        if (error) *error = "cannot read " + path;
        return false;
    }
    in.seekg(0, std::ios::end);
    std::streamoff size = in.tellg();
    in.seekg(0, std::ios::beg);
    if (size <= 0) {
        if (error) *error = "empty disk image";
        return false;
    }
    std::vector<uint8_t> data(size_t(size));
    in.read(reinterpret_cast<char*>(data.data()), size);
    if (!in) {
        if (error) *error = "cannot read " + path;
        return false;
    }
    return load_bytes(data.data(), data.size(), error);
}

bool MsxDisk::load_raw(const uint8_t* data, size_t size, std::string* error) {
    struct Geom {
        int tracks, heads, spt;
        size_t bytes;
    };
    const Geom geoms[] = {
        {80, 2, 9, 737280}, {80, 2, 8, 655360}, {40, 2, 9, 368640},
        {80, 1, 9, 368640}, {40, 1, 9, 184320}, {80, 2, 9, 720 * 1024},
    };
    for (const Geom& g : geoms) {
        if (size == g.bytes) {
            tracks_ = g.tracks;
            heads_ = g.heads;
            spt_ = g.spt;
            image_.assign(data, data + size);
            return true;
        }
    }
    // Pad a slightly short dump up to 720 KiB (common with truncated images).
    if (size > 100000 && size < 737280) {
        tracks_ = 80;
        heads_ = 2;
        spt_ = 9;
        image_.assign(737280, 0xe5);
        std::copy(data, data + size, image_.begin());
        return true;
    }
    if (error) *error = "unrecognised MSX disk size";
    return false;
}

bool MsxDisk::load_bytes(const uint8_t* data, size_t size, std::string* error) {
    if (starts_with(data, size, "MV - CPC") || starts_with(data, size, "EXTENDED")) {
        // Flatten a CPC DSK into 512-byte sectors, track/head/sector IDs as stored.
        if (size < 256) {
            if (error) *error = "truncated DSK header";
            return false;
        }
        int ntracks = data[0x30];
        int nheads = data[0x31];
        if (ntracks <= 0 || ntracks > 82 || nheads < 1 || nheads > 2) {
            if (error) *error = "bad DSK geometry";
            return false;
        }
        tracks_ = ntracks;
        heads_ = nheads;
        spt_ = 9;
        image_.assign(size_t(ntracks) * size_t(nheads) * 9 * kSectorSize, 0xe5);
        size_t pos = 0x100;
        bool extended = starts_with(data, size, "EXTENDED");
        for (int t = 0; t < ntracks; t++) {
            for (int h = 0; h < nheads; h++) {
                if (extended) {
                    uint8_t track_size_hi = data[0x34 + t * nheads + h];
                    if (track_size_hi == 0) continue;
                }
                if (pos + 0x100 > size) break;
                if (std::memcmp(data + pos, "Track-Info", 10) != 0) {
                    pos += 0x100;
                    continue;
                }
                int nsec = data[pos + 0x15];
                size_t data_pos = pos + 0x100;
                for (int s = 0; s < nsec && s < 29; s++) {
                    const uint8_t* id = data + pos + 0x18 + s * 8;
                    int c = id[0], hnum = id[1], r = id[2], n = id[3];
                    int len = 128 << std::min(int(n), 6);
                    uint16_t actual = uint16_t(id[6] | (id[7] << 8));
                    if (extended && actual) len = actual;
                    if (n == 2 && r >= 1 && r <= 9 && c == t) {
                        uint8_t* dest = sector(c, hnum, r);
                        if (dest && data_pos + 512 <= size) {
                            std::memcpy(dest, data + data_pos, 512);
                        }
                    }
                    data_pos += size_t(len);
                }
                pos = data_pos;
                if (!extended) {
                    uint16_t track_len = uint16_t(data[0x32] | (data[0x33] << 8));
                    pos = 0x100 + size_t((t * nheads + h + 1) * track_len);
                }
            }
        }
        return true;
    }
    return load_raw(data, size, error);
}

void MsxFdc::reset() {
    status_ = 0;
    track_ = 0;
    sector_ = 1;
    data_ = 0;
    side_ = 0;
    drive_ = 0;
    motor_ = false;
    drq_ = false;
    intrq_ = false;
    busy_ = false;
    type1_ = true;
    writing_ = false;
    index_pulse_ = false;
    buf_.clear();
    buf_pos_ = 0;
}

const uint8_t* MsxFdc::current_sector() const {
    if (disk_ == nullptr || !disk_->present() || drive_ != 0) return nullptr;
    return disk_->sector(track_, side_, sector_);
}

uint8_t* MsxFdc::current_sector() {
    if (disk_ == nullptr || !disk_->present() || drive_ != 0) return nullptr;
    return disk_->sector(track_, side_, sector_);
}

uint8_t MsxFdc::status_r() {
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
    }
    if (disk_ && disk_->present()) {
        // ready
    } else {
        value |= kNotReady;
    }
    return value;
}

void MsxFdc::finish_type1() {
    type1_ = true;
    busy_ = false;
    drq_ = false;
    intrq_ = true;
    status_ = 0;
}

void MsxFdc::complete_io(bool rnf) {
    type1_ = false;
    busy_ = false;
    drq_ = false;
    intrq_ = true;
    writing_ = false;
    status_ = rnf ? kRnf : 0;
}

void MsxFdc::start_read_sector() {
    type1_ = false;
    writing_ = false;
    const uint8_t* src = current_sector();
    if (src == nullptr) {
        complete_io(true);
        return;
    }
    buf_.assign(src, src + MsxDisk::kSectorSize);
    buf_pos_ = 0;
    busy_ = true;
    drq_ = true;
    intrq_ = false;
    status_ = 0;
}

void MsxFdc::start_write_sector() {
    type1_ = false;
    writing_ = true;
    if (current_sector() == nullptr) {
        complete_io(true);
        return;
    }
    buf_.assign(MsxDisk::kSectorSize, 0);
    buf_pos_ = 0;
    busy_ = true;
    drq_ = true;
    intrq_ = false;
    status_ = 0;
}

void MsxFdc::start_read_address() {
    type1_ = false;
    writing_ = false;
    if (current_sector() == nullptr) {
        complete_io(true);
        return;
    }
    buf_ = {track_, uint8_t(side_), sector_, 2, 0, 0};
    buf_pos_ = 0;
    busy_ = true;
    drq_ = true;
    intrq_ = false;
    status_ = 0;
}

uint8_t MsxFdc::data_r() {
    if (buf_pos_ < buf_.size()) {
        data_ = buf_[buf_pos_++];
        if (buf_pos_ >= buf_.size()) complete_io(false);
        else drq_ = true;
    }
    return data_;
}

void MsxFdc::data_w(uint8_t value) {
    data_ = value;
    if (!writing_) return;
    if (buf_pos_ < buf_.size()) buf_[buf_pos_++] = value;
    if (buf_pos_ >= buf_.size()) {
        uint8_t* dest = current_sector();
        if (dest) std::memcpy(dest, buf_.data(), buf_.size());
        complete_io(false);
    } else {
        drq_ = true;
    }
}

void MsxFdc::command_w(uint8_t value) {
    uint8_t cmd = value & 0xf0;
    busy_ = true;
    drq_ = false;
    intrq_ = false;
    if (cmd < 0x80) {
        // Type I: restore / seek / step
        if (cmd == 0x00) track_ = 0;
        else if (cmd == 0x10) track_ = data_;
        else if (cmd == 0x40 || cmd == 0x60) {
            if (track_ < 80) track_++;
        } else if (cmd == 0x20 || cmd == 0x50 || cmd == 0x70) {
            if (track_ > 0) track_--;
        }
        finish_type1();
        return;
    }
    if (cmd == 0x80 || cmd == 0xa0) {
        if (cmd == 0x80) start_read_sector();
        else start_write_sector();
        return;
    }
    if (cmd == 0xc0) {
        start_read_address();
        return;
    }
    if (cmd == 0xd0) {
        // Force interrupt
        busy_ = false;
        type1_ = true;
        drq_ = false;
        if (value & 0x0f) intrq_ = true;
        return;
    }
    if (cmd == 0xe0 || cmd == 0xf0) {
        // Read/write track: treat as RNF
        complete_io(true);
        return;
    }
    complete_io(true);
}

uint8_t MsxFdc::read_reg(int index) {
    switch (index & 3) {
        case 0: return status_r();
        case 1: return track_r();
        case 2: return sector_r();
        default: return data_r();
    }
}

void MsxFdc::write_reg(int index, uint8_t value) {
    switch (index & 3) {
        case 0: command_w(value); break;
        case 1: track_w(value); break;
        case 2: sector_w(value); break;
        default: data_w(value); break;
    }
}

}  // namespace dsp
