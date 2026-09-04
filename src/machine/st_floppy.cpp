#include "machine/st_floppy.h"

#include <cstring>
#include <fstream>

namespace dsp {
namespace {

uint16_t be16(const uint8_t* p) { return uint16_t((p[0] << 8) | p[1]); }

bool read_file(const std::string& path, std::vector<uint8_t>& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    in.seekg(0, std::ios::end);
    const auto n = in.tellg();
    if (n <= 0) return false;
    in.seekg(0);
    out.resize(size_t(n));
    in.read(reinterpret_cast<char*>(out.data()), n);
    return bool(in);
}

std::string lower_ext(const std::string& path) {
    std::string e;
    const auto dot = path.find_last_of('.');
    if (dot == std::string::npos) return e;
    e = path.substr(dot);
    for (char& c : e) c = char(c | 0x20);
    return e;
}

void msa_rle(const uint8_t* src, int slen, uint8_t* dest, int dlen) {
    int s = 0, d = 0;
    while (s < slen && d < dlen) {
        const uint8_t b = src[s++];
        if (b == 0xe5 && s + 2 <= slen) {
            const uint8_t fill = src[s++];
            int count = (src[s] << 8) | src[s + 1];
            s += 2;
            while (count-- > 0 && d < dlen) dest[d++] = fill;
        } else {
            dest[d++] = b;
        }
    }
}

}  // namespace

void StFloppy::reset() {
    dma_mode_ = 0;
    dma_addr_ = 0;
    dma_count_ = 0;
    fdc_track_ = 0;
    fdc_sector_ = 1;
    fdc_data_ = 0;
    fdc_status_ = 0;
    fdc_irq_ = false;
    dma_error_ = false;
    psg_a_ = 0xff;
}

bool StFloppy::decode_geometry(size_t bytes) {
    const int k = int(bytes / kSectorSize);
    const struct {
        int tracks, sides, spt;
    } cands[] = {
        {80, 2, 9},  {80, 2, 10}, {80, 1, 9}, {80, 1, 10}, {81, 2, 10},
        {82, 2, 10}, {79, 2, 9},  {40, 2, 9}, {80, 2, 11},
    };
    for (const auto& c : cands) {
        if (c.tracks * c.sides * c.spt == k) {
            tracks_ = c.tracks;
            sides_ = c.sides;
            spt_ = c.spt;
            return true;
        }
    }
    if (k >= 9 * 80 && (k % 9) == 0) {
        tracks_ = 80;
        spt_ = 9;
        sides_ = k / (80 * 9);
        if (sides_ < 1) sides_ = 1;
        if (sides_ > 2) sides_ = 2;
        return true;
    }
    return false;
}

const uint8_t* StFloppy::sector(int track, int side, int sector) const {
    if (!loaded_ || track < 0 || track >= tracks_ || side < 0 || side >= sides_) return nullptr;
    if (sector < 1 || sector > spt_) return nullptr;
    const int index = ((track * sides_ + side) * spt_ + (sector - 1));
    const size_t off = size_t(index) * kSectorSize;
    if (off + kSectorSize > image_.size()) return nullptr;
    return image_.data() + off;
}

uint8_t* StFloppy::sector(int track, int side, int sector) {
    return const_cast<uint8_t*>(static_cast<const StFloppy*>(this)->sector(track, side, sector));
}

bool StFloppy::load_st(const uint8_t* data, size_t size, std::string* error) {
    if (!decode_geometry(size)) {
        if (error) *error = "ST image size is not a known floppy geometry";
        return false;
    }
    image_.assign(data, data + size);
    loaded_ = true;
    return true;
}

bool StFloppy::load_msa(const uint8_t* data, size_t size, std::string* error) {
    if (size < 10 || be16(data) != 0x0e0f) {
        if (error) *error = "not an MSA disk";
        return false;
    }
    const int spt = be16(data + 2);
    const int sides = be16(data + 4) + 1;
    const int start = be16(data + 6);
    const int end = be16(data + 8);
    if (spt < 1 || spt > 11 || sides < 1 || sides > 2 || end < start) {
        if (error) *error = "MSA header is corrupt";
        return false;
    }
    tracks_ = end + 1;
    sides_ = sides;
    spt_ = spt;
    image_.assign(size_t(tracks_ * sides_ * spt_ * kSectorSize), 0);
    size_t pos = 10;
    for (int t = start; t <= end; t++) {
        for (int s = 0; s < sides_; s++) {
            if (pos + 2 > size) {
                if (error) *error = "MSA track data is truncated";
                return false;
            }
            const int packed = be16(data + pos);
            pos += 2;
            if (pos + size_t(packed) > size) {
                if (error) *error = "MSA track data is truncated";
                return false;
            }
            uint8_t* dest = this->sector(t, s, 1);
            if (!dest) {
                pos += size_t(packed);
                continue;
            }
            const int raw = spt_ * kSectorSize;
            if (packed == raw) {
                std::memcpy(dest, data + pos, size_t(raw));
            } else {
                msa_rle(data + pos, packed, dest, raw);
            }
            pos += size_t(packed);
        }
    }
    loaded_ = true;
    return true;
}

bool StFloppy::load_file(const std::string& path, std::string* error) {
    std::vector<uint8_t> raw;
    if (!read_file(path, raw)) {
        if (error) *error = "cannot read " + path;
        return false;
    }
    loaded_ = false;
    const std::string ext = lower_ext(path);
    if (ext == ".msa" || (raw.size() >= 2 && raw[0] == 0x0e && raw[1] == 0x0f)) {
        return load_msa(raw.data(), raw.size(), error);
    }
    return load_st(raw.data(), raw.size(), error);
}

uint16_t StFloppy::dma_status() const {
    uint16_t v = 0;
    if (!dma_error_) v |= 1;          // no error (Hatari: bit 0 set = OK)
    if (dma_count_ == 0) v |= 2;      // sector count zero
    return v;
}

void StFloppy::dma_mode_w(uint16_t value) { dma_mode_ = value; }

void StFloppy::dma_addr_w(int which, uint8_t value) {
    if (which == 0) dma_addr_ = (dma_addr_ & 0x00ffff) | (uint32_t(value) << 16);
    else if (which == 1) dma_addr_ = (dma_addr_ & 0xff00ff) | (uint32_t(value) << 8);
    else dma_addr_ = (dma_addr_ & 0xffff00) | value;
}

uint8_t StFloppy::dma_addr_r(int which) const {
    if (which == 0) return uint8_t(dma_addr_ >> 16);
    if (which == 1) return uint8_t(dma_addr_ >> 8);
    return uint8_t(dma_addr_);
}

uint16_t StFloppy::dma_data_r() {
    if (dma_mode_ & 0x10) return dma_count_;
    const int reg = (dma_mode_ >> 1) & 3;
    if (reg == 0) return fdc_status();
    if (reg == 1) return fdc_track_;
    if (reg == 2) return fdc_sector_;
    return fdc_data_;
}

void StFloppy::dma_data_w(uint16_t value) {
    if (dma_mode_ & 0x10) {
        dma_count_ = uint8_t(value);
        return;
    }
    const int reg = (dma_mode_ >> 1) & 3;
    if (reg == 0) fdc_command(uint8_t(value));
    else if (reg == 1) fdc_track_ = uint8_t(value);
    else if (reg == 2) fdc_sector_ = uint8_t(value);
    else fdc_data_ = uint8_t(value);
}

uint8_t StFloppy::fdc_status() const {
    uint8_t v = fdc_status_;
    const bool selected = ((psg_a_ >> 1) & 1) == 0 || ((psg_a_ >> 2) & 1) == 0;
    if (!loaded_ || !selected) v |= 0x80;  // not ready
    if (fdc_track_ == 0) v |= 0x04;        // track 0
    return v;
}

void StFloppy::fdc_command(uint8_t cmd) {
    fdc_irq_ = false;
    dma_error_ = false;
    fdc_status_ = 0;
    const uint8_t type = uint8_t(cmd & 0xf0);
    if (type < 0x80) {
        // Type I: restore / seek / step.
        if (type == 0x00) fdc_track_ = 0;
        else if (type == 0x10) fdc_track_ = fdc_data_;
        else if (type == 0x40 || type == 0x60) {
            if (fdc_track_ < 90) fdc_track_++;
        } else if (type == 0x20 || type == 0x50 || type == 0x70) {
            if (fdc_track_ > 0) fdc_track_--;
        }
        fdc_irq_ = true;
        return;
    }
    if (type == 0xd0) {
        fdc_irq_ = (cmd & 8) != 0;
        return;
    }
    if (type == 0x80) {
        do_dma_read();
        return;
    }
    if (type == 0xa0) {
        do_dma_write();
        return;
    }
    if (type == 0xc0) {
        // Read address: 6-byte ID field through DMA if a count is set.
        fdc_data_ = fdc_track_;
        fdc_irq_ = true;
        return;
    }
    fdc_status_ = 0x10;  // RNF
    fdc_irq_ = true;
}

void StFloppy::do_dma_read() {
    const int side = (psg_a_ & 1) ? 1 : 0;
    while (dma_count_) {
        const uint8_t* src = sector(fdc_track_, side, fdc_sector_);
        if (!src || ram_ == nullptr || dma_addr_ + kSectorSize > ram_size_) {
            dma_error_ = true;
            fdc_status_ = 0x10;
            fdc_irq_ = true;
            return;
        }
        std::memcpy(ram_ + dma_addr_, src, kSectorSize);
        dma_addr_ += kSectorSize;
        dma_count_--;
        fdc_sector_++;
        if (fdc_sector_ > spt_) {
            fdc_sector_ = 1;
            break;
        }
    }
    fdc_irq_ = true;
}

void StFloppy::do_dma_write() {
    const int side = (psg_a_ & 1) ? 1 : 0;
    while (dma_count_) {
        uint8_t* dest = sector(fdc_track_, side, fdc_sector_);
        if (!dest || ram_ == nullptr || dma_addr_ + kSectorSize > ram_size_) {
            dma_error_ = true;
            fdc_status_ = 0x10;
            fdc_irq_ = true;
            return;
        }
        std::memcpy(dest, ram_ + dma_addr_, kSectorSize);
        dma_addr_ += kSectorSize;
        dma_count_--;
        fdc_sector_++;
        if (fdc_sector_ > spt_) {
            fdc_sector_ = 1;
            break;
        }
    }
    fdc_irq_ = true;
}

}  // namespace dsp
