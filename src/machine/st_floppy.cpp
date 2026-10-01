#include "machine/st_floppy.h"

#include <cstring>
#include <fstream>

namespace dsp {
namespace {

uint16_t be16(const uint8_t* p) { return uint16_t((p[0] << 8) | p[1]); }
uint16_t le16(const uint8_t* p) { return uint16_t(p[0] | (p[1] << 8)); }
uint32_t le32(const uint8_t* p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}

// WD1772 timing: 8 MHz CPU, 250 kbit/s MFM (256 cycles per byte), 300 rpm.
constexpr uint32_t kCyclesPerByte = 256;
constexpr uint32_t kCyclesPerRev = 1600000;
// Standard track layout (Hatari fdc.h): GAP1, then per sector GAP2, sync,
// IDAM, ID, CRC, GAP3a, GAP3b, sync, DAM, data, CRC, GAP4.
constexpr int kGap1 = 60;
constexpr int kGap2 = 12;
constexpr int kRawSector512 = 12 + 3 + 1 + 4 + 2 + 22 + 12 + 3 + 1 + 512 + 2 + 40;
// From the end of an ID field (after its CRC) to the first data byte.
constexpr int kIdToData = 22 + 12 + 3 + 1;

uint16_t crc16_add(uint16_t crc, uint8_t b) {
    crc ^= uint16_t(b) << 8;
    for (int i = 0; i < 8; i++) crc = (crc & 0x8000) ? uint16_t((crc << 1) ^ 0x1021) : uint16_t(crc << 1);
    return crc;
}

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
    fdc_busy_ = false;
    motor_on_ = false;
    irq_delay_ = 0;
    dma_error_ = false;
    psg_a_ = 0xff;
    last_cmd_ = 0;
    head_ = 0;
    step_dir_ = 1;
    dma_bytes_ = 0;
}

bool StFloppy::decode_geometry(size_t bytes) {
    const int k = int(bytes / kSectorSize);
    const struct {
        int tracks, sides, spt;
    } cands[] = {
        {80, 2, 9},  {80, 2, 10}, {80, 1, 9}, {80, 1, 10}, {81, 2, 9},
        {81, 2, 10}, {82, 2, 9},  {82, 2, 10}, {79, 2, 9}, {40, 2, 9},
        {80, 2, 11}, {83, 2, 9},
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
    stx_ = false;
    stx_tracks_.clear();
    const std::string ext = lower_ext(path);
    if (ext == ".stx" || (raw.size() >= 4 && std::memcmp(raw.data(), "RSY\0", 4) == 0)) {
        return load_stx(raw.data(), raw.size(), error);
    }
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

void StFloppy::dma_mode_w(uint16_t value) {
    // Toggling the DMA read/write bit (8) resets the DMA status, as TOS does
    // with the $90 / $190 pair before a transfer.
    if ((dma_mode_ ^ value) & 0x100) {
        dma_error_ = false;
        dma_bytes_ = 0;
    }
    dma_mode_ = value;
}

void StFloppy::tick(int cycles) {
    cycles_ += uint64_t(cycles);
    if (irq_delay_ <= 0) return;
    irq_delay_ -= cycles;
    if (irq_delay_ <= 0) {
        irq_delay_ = 0;
        fdc_busy_ = false;
        fdc_irq_ = true;
    }
}

int StFloppy::selected_drive() const {
    if ((psg_a_ & 2) == 0) return 0;
    if ((psg_a_ & 4) == 0) return 1;
    return -1;
}

int StFloppy::selected_side() const { return (psg_a_ & 1) ? 0 : 1; }

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
    // Mode bit 3 selects the ACSI (hard disk) bus instead of the WD1772.
    // No ACSI device is attached: nothing answers.
    if (dma_mode_ & 0x08) return 0xff;
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
    // ACSI command bytes (mode bit 3 set) go to the hard disk bus, not the
    // FDC. TOS probes ACSI targets 0-7 at boot with $08,$28,...,$E8; fed
    // to the WD1772 they were Write Sector commands that overwrote the
    // second FAT sector of the floppy in drive A.
    if (dma_mode_ & 0x08) return;
    const int reg = (dma_mode_ >> 1) & 3;
    if (reg == 0) fdc_command(uint8_t(value));
    else if (reg == 1) fdc_track_ = uint8_t(value);
    else if (reg == 2) fdc_sector_ = uint8_t(value);
    else fdc_data_ = uint8_t(value);
}

uint8_t StFloppy::fdc_status() {
    uint8_t v = fdc_status_;
    if (fdc_busy_) v |= 0x01;
    const bool type1 = (last_cmd_ & 0x80) == 0;
    if (type1 && stx_) {
        if (head_ == 0) v |= 0x04;
        if (motor_on_ && loaded_ && selected_drive() == 0 && rotation_byte() < 140) v |= 0x02;  // index
        if (motor_on_) v |= 0x20;
    } else if (type1) {
        if (fdc_track_ == 0) v |= 0x04;  // TR00 (type II uses this bit as Lost Data)
        if (motor_on_) v |= 0x20;        // spin-up done
    }
    if (motor_on_) v |= 0x80;
    fdc_irq_ = false;
    return v;
}

void StFloppy::finish_command() {
    // ~1 ms at 8 MHz so TOS can arm the MFP before the falling GPIP5 edge.
    fdc_busy_ = true;
    fdc_irq_ = false;
    irq_delay_ = 8000;
}

void StFloppy::fdc_command(uint8_t cmd) {
    last_cmd_ = cmd;
    fdc_irq_ = false;
    irq_delay_ = 0;
    dma_error_ = false;
    fdc_status_ = 0;
    motor_on_ = true;
    const uint8_t type = uint8_t(cmd & 0xf0);
    if (stx_ && type < 0x80) {
        // Type I on a real head: restore, seek, step, step-in, step-out.
        // Step commands only touch the track register when bit 4 is set.
        int steps = 0;
        if (type == 0x00 || type == 0x10) {
            if (type == 0x00) {
                step_dir_ = -1;
                steps = head_;
                head_ = 0;
                fdc_track_ = 0;
            } else {
                const int delta = int(fdc_data_) - int(fdc_track_);
                if (delta != 0) step_dir_ = delta > 0 ? 1 : -1;
                head_ += delta;
                steps = delta < 0 ? -delta : delta;
                fdc_track_ = fdc_data_;
            }
        } else {
            if (type == 0x40 || type == 0x50) step_dir_ = 1;
            else if (type == 0x60 || type == 0x70) step_dir_ = -1;
            if (cmd & 0x10) fdc_track_ = uint8_t(fdc_track_ + step_dir_);
            if (!(step_dir_ < 0 && head_ == 0)) head_ += step_dir_;
            steps = 1;
        }
        if (head_ > 85) head_ = 85;
        if (head_ < 0) head_ = 0;
        static const int kStepMs[4] = {6, 12, 2, 3};
        uint32_t cycles = 8000 + uint32_t(steps) * uint32_t(kStepMs[cmd & 3]) * 8000;
        if (cmd & 0x04) {
            // Verify: an ID with the track register's value must turn up.
            const StxTrack* t = stx_track(head_, selected_side());
            bool ok = false;
            if (t && selected_drive() == 0) {
                for (const auto& sec : t->sectors) {
                    if (sec.id[0] == fdc_track_) ok = true;
                }
            }
            if (ok) {
                cycles += 15000 * 8;  // head settle
            } else {
                fdc_status_ = 0x10;  // seek error
                cycles += 5 * kCyclesPerRev;
            }
        }
        stx_finish(cycles);
        return;
    }
    if (type < 0x80) {
        // Type I: restore / seek / step. Both drives exist; only A has media.
        if (type == 0x00) fdc_track_ = 0;
        else if (type == 0x10) fdc_track_ = fdc_data_;
        else if (type == 0x40 || type == 0x60) {
            if (fdc_track_ < 90) fdc_track_++;
        } else if (type == 0x20 || type == 0x50 || type == 0x70) {
            if (fdc_track_ > 0) fdc_track_--;
        }
        head_ = fdc_track_;
        finish_command();
        return;
    }
    if (type == 0xd0) {
        fdc_busy_ = false;
        irq_delay_ = 0;
        fdc_irq_ = (cmd & 8) != 0;
        return;
    }
    if (stx_) {
        if ((cmd & 0xe0) == 0x80) stx_read_sector(cmd);
        else if ((cmd & 0xe0) == 0xa0) stx_write_sector(cmd);
        else if (type == 0xc0) stx_read_address();
        else if (type == 0xe0) stx_read_track();
        else stx_finish(8000);  // Write Track: not supported on STX
        return;
    }
    if ((cmd & 0xe0) == 0x80) {
        do_dma_read();
        return;
    }
    if ((cmd & 0xe0) == 0xa0) {
        do_dma_write();
        return;
    }
    if (type == 0xc0) {
        do_read_address();
        return;
    }
    if (type == 0xe0) {
        // Read track: TOS probes with this; treat as a successful dummy.
        finish_command();
        return;
    }
    fdc_status_ = 0x10;  // RNF
    finish_command();
}

void StFloppy::do_read_address() {
    if (selected_drive() != 0 || !loaded_) {
        fdc_status_ = 0x10;
        finish_command();
        return;
    }
    const uint8_t id[6] = {fdc_track_, uint8_t(selected_side()), fdc_sector_, 2, 0, 0};
    fdc_data_ = id[0];
    if (dma_count_ && ram_ != nullptr && dma_addr_ + 6 <= ram_size_) {
        std::memcpy(ram_ + dma_addr_, id, 6);
        dma_addr_ += 6;
        dma_count_--;
    }
    finish_command();
}

void StFloppy::do_dma_read() {
    const int side = selected_side();
    if (selected_drive() != 0 || !loaded_) {
        dma_error_ = true;
        fdc_status_ = 0x10;
        finish_command();
        return;
    }
    while (dma_count_) {
        const uint8_t* src = sector(fdc_track_, side, fdc_sector_);
        if (!src || ram_ == nullptr || dma_addr_ + kSectorSize > ram_size_) {
            dma_error_ = true;
            fdc_status_ = 0x10;
            finish_command();
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
    finish_command();
}

void StFloppy::do_dma_write() {
    const int side = selected_side();
    if (selected_drive() != 0 || !loaded_) {
        dma_error_ = true;
        fdc_status_ = 0x10;
        finish_command();
        return;
    }
    while (dma_count_) {
        uint8_t* dest = sector(fdc_track_, side, fdc_sector_);
        if (!dest || ram_ == nullptr || dma_addr_ + kSectorSize > ram_size_) {
            dma_error_ = true;
            fdc_status_ = 0x10;
            finish_command();
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
    finish_command();
}

// ---------------------------------------------------------------------------
// Pasti .STX images

bool StFloppy::load_stx(const uint8_t* data, size_t size, std::string* error) {
    auto fail = [&](const char* msg) {
        if (error) *error = msg;
        stx_tracks_.clear();
        return false;
    };
    if (size < 16 || std::memcmp(data, "RSY\0", 4) != 0) return fail("not an STX disk");
    if (le16(data + 4) != 3) return fail("unsupported STX version");
    const int track_blocks = data[10];
    stx_tracks_.assign(86 * 2, StxTrack{});
    int max_track = 0;
    bool two_sides = false;
    size_t pos = 16;
    for (int i = 0; i < track_blocks; i++) {
        if (pos + 16 > size) return fail("STX track header is truncated");
        const uint8_t* h = data + pos;
        const uint32_t block_size = le32(h);
        const uint32_t fuzzy_size = le32(h + 4);
        const int count = le16(h + 8);
        const int flags = le16(h + 10);
        const uint32_t mfm_size = le16(h + 12);
        const int number = h[14];
        if (block_size < 16 || pos + block_size > size) return fail("STX track block is truncated");
        const uint8_t* end = data + pos + block_size;
        const int track = number & 0x7f;
        const int side = (number >> 7) & 1;
        if (track >= 86) {
            pos += block_size;
            continue;
        }
        StxTrack& t = stx_tracks_[size_t(track * 2 + side)];
        t = StxTrack{};
        t.present = true;
        t.bytes = mfm_size ? mfm_size : 6250;
        if (track > max_track) max_track = track;
        if (side) two_sides = true;
        const uint8_t* p = h + 16;

        if (count > 0 && (flags & 0x01) == 0) {
            // Plain track: `count` 512-byte sectors in standard layout; the
            // MFM size of such tracks is given in bits.
            t.bytes = mfm_size ? (mfm_size + 7) / 8 : 6250;
            if (t.bytes < 6000) t.bytes = 6250;
            int byte_pos = kGap1 + kGap2 + 4;
            for (int sct = 0; sct < count; sct++) {
                if (p + size_t(sct + 1) * 512 > end) return fail("STX sector data is truncated");
                StxSector sec;
                sec.id[0] = uint8_t(track);
                sec.id[1] = uint8_t(side);
                sec.id[2] = uint8_t(sct + 1);
                sec.id[3] = 2;
                uint16_t crc = 0xffff;
                for (uint8_t b : {uint8_t(0xa1), uint8_t(0xa1), uint8_t(0xa1), uint8_t(0xfe), sec.id[0], sec.id[1],
                                  sec.id[2], sec.id[3]})
                    crc = crc16_add(crc, b);
                sec.crc = crc;
                sec.bit_pos = uint32_t(byte_pos) * 8;
                sec.data.assign(p + sct * 512, p + (sct + 1) * 512);
                t.sectors.push_back(std::move(sec));
                byte_pos += kRawSector512;
            }
            pos += block_size;
            continue;
        }

        const uint8_t* sector_blocks = p;
        const uint8_t* fuzzy = p + size_t(count) * 16;
        const uint8_t* track_data = fuzzy + fuzzy_size;
        if (track_data > end) return fail("STX track block is corrupt");
        if (flags & 0x40) {
            const uint8_t* img = track_data;
            if (flags & 0x80) img += 2;  // sync position
            if (img + 2 > end) return fail("STX track image is truncated");
            const uint32_t img_size = le16(img);
            img += 2;
            if (img + img_size > end) return fail("STX track image is truncated");
            t.image.assign(img, img + img_size);
        }
        const uint8_t* fz = fuzzy;
        for (int sct = 0; sct < count; sct++) {
            const uint8_t* b = sector_blocks + sct * 16;
            StxSector sec;
            const uint32_t data_offset = le32(b);
            sec.bit_pos = le16(b + 4);
            sec.read_time = le16(b + 6);
            std::memcpy(sec.id, b + 8, 4);
            sec.crc = be16(b + 12);
            sec.status = b[14];
            if ((sec.status & 0x10) == 0) {
                const int n = sec.size();
                const uint8_t* src = track_data + data_offset;
                if (src + n > end) return fail("STX sector data is truncated");
                sec.data.assign(src, src + n);
                if (sec.status & 0x80) {
                    if (fz + n > track_data) return fail("STX fuzzy mask is truncated");
                    sec.fuzzy.assign(fz, fz + n);
                    fz += n;
                }
            }
            t.sectors.push_back(std::move(sec));
        }
        pos += block_size;
    }
    tracks_ = max_track + 1;
    sides_ = two_sides ? 2 : 1;
    spt_ = 9;
    if (const StxTrack* t = stx_track(1, 0)) {
        if (!t->sectors.empty()) spt_ = int(t->sectors.size());
    }
    image_.clear();
    stx_ = true;
    loaded_ = true;
    return true;
}

StFloppy::StxTrack* StFloppy::stx_track(int track, int side) {
    if (!stx_ || track < 0 || track >= 86 || side < 0 || side > 1) return nullptr;
    StxTrack& t = stx_tracks_[size_t(track * 2 + side)];
    return t.present ? &t : nullptr;
}

int StFloppy::stx_sector_count(int track, int side) const {
    if (!stx_ || track < 0 || track >= 86 || side < 0 || side > 1) return -1;
    const StxTrack& t = stx_tracks_[size_t(track * 2 + side)];
    return t.present ? int(t.sectors.size()) : -1;
}

uint32_t StFloppy::rotation_byte() const {
    return uint32_t((cycles_ % kCyclesPerRev) / kCyclesPerByte);
}

uint8_t StFloppy::random_byte() {
    rng_ ^= rng_ << 13;
    rng_ ^= rng_ >> 17;
    rng_ ^= rng_ << 5;
    return uint8_t(rng_ >> 11);
}

void StFloppy::stx_finish(uint32_t cycles) {
    fdc_busy_ = true;
    fdc_irq_ = false;
    irq_delay_ = int(cycles ? cycles : 1);
}

// Next ID field to pass under the head (optionally one whose track and
// sector match the FDC registers).  `wait_bytes` gets the distance in
// bytes from the current rotation position to the end of that ID field.
int StFloppy::stx_next_sector(const StxTrack& t, bool match_sector, uint32_t* wait_bytes) {
    const uint32_t len = t.bytes ? t.bytes : 6250;
    const uint32_t now = rotation_byte() % len;
    int best = -1;
    uint32_t best_dist = 0;
    for (size_t i = 0; i < t.sectors.size(); i++) {
        const StxSector& sec = t.sectors[i];
        if (match_sector && (sec.id[0] != fdc_track_ || sec.id[2] != fdc_sector_)) continue;
        // bit_pos points just after the IDAM; the ID + CRC take 6 bytes.
        const uint32_t id_end = (sec.bit_pos / 8 + 6) % len;
        uint32_t dist = (id_end + len - now) % len;
        if (dist == 0) dist = len;
        if (best < 0 || dist < best_dist) {
            best = int(i);
            best_dist = dist;
        }
    }
    if (wait_bytes) *wait_bytes = best_dist;
    return best;
}

// The DMA chip buffers 16 bytes before writing them to RAM and decrements
// its sector count every 512 bytes; with a zero count nothing is stored.
void StFloppy::dma_push(const uint8_t* data, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (dma_count_ == 0 || ram_ == nullptr) return;
        if (dma_addr_ < ram_size_) ram_[dma_addr_] = data[i];
        dma_addr_ = (dma_addr_ + 1) & 0xffffff;
        if (++dma_bytes_ >= kSectorSize) {
            dma_bytes_ = 0;
            dma_count_--;
        }
    }
}

void StFloppy::stx_read_sector(uint8_t cmd) {
    StxTrack* t = (selected_drive() == 0) ? stx_track(head_, selected_side()) : nullptr;
    if (t == nullptr || t->sectors.empty()) {
        fdc_status_ = 0x10;
        stx_finish(5 * kCyclesPerRev);
        return;
    }
    uint64_t total = 0;
    const uint64_t saved = cycles_;
    for (;;) {
        uint32_t wait = 0;
        const int idx = stx_next_sector(*t, true, &wait);
        if (idx < 0 || (t->sectors[size_t(idx)].status & 0x10)) {
            fdc_status_ |= 0x10;  // record not found after 5 revolutions
            total += 5 * kCyclesPerRev;
            break;
        }
        const StxSector& sec = t->sectors[size_t(idx)];
        const int n = sec.size();
        uint64_t cycles = uint64_t(wait + kIdToData) * kCyclesPerByte;
        if (sec.read_time) cycles += uint64_t(sec.read_time) * 8;
        else cycles += uint64_t(n + 2) * kCyclesPerByte;
        total += cycles;
        cycles_ += cycles;  // so a multi-sector read continues from here
        std::vector<uint8_t> buf(sec.data);
        if (!sec.fuzzy.empty()) {
            for (int i = 0; i < n; i++) buf[size_t(i)] = uint8_t((buf[size_t(i)] & sec.fuzzy[size_t(i)]) |
                                                                (random_byte() & ~sec.fuzzy[size_t(i)]));
        }
        dma_push(buf.data(), buf.size());
        if (!buf.empty()) fdc_data_ = buf.back();
        if (sec.status & 0x20) fdc_status_ |= 0x20;  // deleted data mark
        if (sec.status & 0x08) {
            fdc_status_ |= 0x08;  // CRC error ends the command
            break;
        }
        if ((cmd & 0x10) == 0) break;
        fdc_sector_++;
    }
    cycles_ = saved;
    stx_finish(uint32_t(total));
}

void StFloppy::stx_write_sector(uint8_t cmd) {
    StxTrack* t = (selected_drive() == 0) ? stx_track(head_, selected_side()) : nullptr;
    if (t == nullptr || t->sectors.empty()) {
        fdc_status_ = 0x10;
        stx_finish(5 * kCyclesPerRev);
        return;
    }
    uint64_t total = 0;
    const uint64_t saved = cycles_;
    for (;;) {
        uint32_t wait = 0;
        const int idx = stx_next_sector(*t, true, &wait);
        if (idx < 0) {
            fdc_status_ |= 0x10;
            total += 5 * kCyclesPerRev;
            break;
        }
        StxSector& sec = t->sectors[size_t(idx)];
        const int n = sec.size();
        sec.data.resize(size_t(n));
        for (int i = 0; i < n; i++) {
            if (dma_count_ == 0 || ram_ == nullptr) break;
            if (dma_addr_ < ram_size_) sec.data[size_t(i)] = ram_[dma_addr_];
            dma_addr_ = (dma_addr_ + 1) & 0xffffff;
            if (++dma_bytes_ >= kSectorSize) {
                dma_bytes_ = 0;
                dma_count_--;
            }
        }
        sec.status &= uint8_t(~(0x08 | 0x10 | 0x20 | 0x80));
        sec.fuzzy.clear();
        const uint64_t cycles = uint64_t(wait + kIdToData + n + 2) * kCyclesPerByte;
        total += cycles;
        cycles_ += cycles;
        if ((cmd & 0x10) == 0) break;
        fdc_sector_++;
    }
    cycles_ = saved;
    stx_finish(uint32_t(total));
}

void StFloppy::stx_read_address() {
    StxTrack* t = (selected_drive() == 0) ? stx_track(head_, selected_side()) : nullptr;
    uint32_t wait = 0;
    const int idx = t ? stx_next_sector(*t, false, &wait) : -1;
    if (idx < 0) {
        fdc_status_ = 0x10;
        stx_finish(5 * kCyclesPerRev);
        return;
    }
    const StxSector& sec = t->sectors[size_t(idx)];
    const uint8_t id[6] = {sec.id[0], sec.id[1], sec.id[2], sec.id[3], uint8_t(sec.crc >> 8), uint8_t(sec.crc)};
    dma_push(id, 6);
    fdc_data_ = id[5];
    fdc_sector_ = sec.id[0];  // the WD1772 copies the ID's track into the sector register
    if (sec.status & 0x08) {
        // A CRC error in the data field does not affect the ID field unless
        // the ID's own CRC is wrong.
        uint16_t crc = 0xffff;
        for (uint8_t b : {uint8_t(0xa1), uint8_t(0xa1), uint8_t(0xa1), uint8_t(0xfe), sec.id[0], sec.id[1], sec.id[2],
                          sec.id[3]})
            crc = crc16_add(crc, b);
        if (crc != sec.crc) fdc_status_ |= 0x08;
    }
    stx_finish(wait * kCyclesPerByte);
}

void StFloppy::stx_read_track() {
    StxTrack* t = (selected_drive() == 0) ? stx_track(head_, selected_side()) : nullptr;
    std::vector<uint8_t> raw;
    const uint32_t len = t && t->bytes ? t->bytes : 6250;
    if (t && !t->image.empty()) {
        raw = t->image;
    } else if (t && !t->sectors.empty()) {
        // Rebuild a standard track around the recorded sectors.
        raw.assign(kGap1, 0x4e);
        for (const auto& sec : t->sectors) {
            const int n = sec.size();
            if (raw.size() + size_t(kRawSector512 - 512 + n) > len) break;
            raw.insert(raw.end(), 12, 0x00);
            raw.insert(raw.end(), 3, 0xa1);
            raw.push_back(0xfe);
            raw.insert(raw.end(), sec.id, sec.id + 4);
            raw.push_back(uint8_t(sec.crc >> 8));
            raw.push_back(uint8_t(sec.crc));
            raw.insert(raw.end(), 22, 0x4e);
            raw.insert(raw.end(), 12, 0x00);
            raw.insert(raw.end(), 3, 0xa1);
            const uint8_t dam = (sec.status & 0x20) ? 0xf8 : 0xfb;
            raw.push_back(dam);
            uint16_t crc = 0xffff;
            for (uint8_t b : {uint8_t(0xa1), uint8_t(0xa1), uint8_t(0xa1), dam}) crc = crc16_add(crc, b);
            for (int i = 0; i < n; i++) {
                const uint8_t b = i < int(sec.data.size()) ? sec.data[size_t(i)] : 0;
                raw.push_back(b);
                crc = crc16_add(crc, b);
            }
            if (sec.status & 0x08) crc = uint16_t(~crc);
            raw.push_back(uint8_t(crc >> 8));
            raw.push_back(uint8_t(crc));
            raw.insert(raw.end(), 40, 0x4e);
        }
        if (raw.size() < len) raw.resize(len, 0x4e);
    } else {
        // Unformatted: the FDC decodes noise.
        raw.resize(len);
        for (auto& b : raw) b = random_byte();
    }
    dma_push(raw.data(), raw.size());
    if (!raw.empty()) fdc_data_ = raw.back();
    // Wait for the index pulse, then one full revolution.
    const uint32_t to_index = (len - rotation_byte() % len) % len;
    stx_finish((to_index + len) * kCyclesPerByte);
}

}  // namespace dsp
