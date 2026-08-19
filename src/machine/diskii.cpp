#include "machine/diskii.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <iterator>

namespace dsp {
namespace {

// ~32.5 is authentic (4 µs bit cells). 40 keeps the P5 PROM 27-cycle data
// field loop from missing a nibble with post-instruction Disk II ticks.
constexpr int kCyclesPerNibble = 40;
constexpr uint8_t kVolume = 0xFE;
constexpr uint8_t kGap = 0xFF;

const uint8_t kDiskByte[0x40] = {
    0x96, 0x97, 0x9A, 0x9B, 0x9D, 0x9E, 0x9F, 0xA6, 0xA7, 0xAB, 0xAC, 0xAD, 0xAE, 0xAF, 0xB2, 0xB3,
    0xB4, 0xB5, 0xB6, 0xB7, 0xB9, 0xBA, 0xBB, 0xBC, 0xBD, 0xBE, 0xBF, 0xCB, 0xCD, 0xCE, 0xCF, 0xD3,
    0xD6, 0xD7, 0xD9, 0xDA, 0xDB, 0xDC, 0xDD, 0xDE, 0xDF, 0xE5, 0xE6, 0xE7, 0xE9, 0xEA, 0xEB, 0xEC,
    0xED, 0xEE, 0xEF, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6, 0xF7, 0xF9, 0xFA, 0xFB, 0xFC, 0xFD, 0xFE, 0xFF,
};

// Physical sector -> logical sector in a DOS 3.3-order image.
const uint8_t kDosSkew[16] = {0x00, 0x07, 0x0E, 0x06, 0x0D, 0x05, 0x0C, 0x04,
                              0x0B, 0x03, 0x0A, 0x02, 0x09, 0x01, 0x08, 0x0F};
const uint8_t kProdosSkew[16] = {0x00, 0x08, 0x01, 0x09, 0x02, 0x0A, 0x03, 0x0B,
                                 0x04, 0x0C, 0x05, 0x0D, 0x06, 0x0E, 0x07, 0x0F};

std::string to_lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

bool ends_with(const std::string& value, const char* ext) {
    const std::string lower = to_lower(value);
    const size_t n = std::strlen(ext);
    return lower.size() >= n && lower.compare(lower.size() - n, n, ext) == 0;
}

void append_44(std::vector<uint8_t>& dest, uint8_t value) {
    dest.push_back(static_cast<uint8_t>(((value >> 1) & 0x55) | 0xAA));
    dest.push_back(static_cast<uint8_t>((value & 0x55) | 0xAA));
}

}  // namespace

void DiskIi::encode_62(const uint8_t src[256], uint8_t dest[343]) {
    uint8_t nib[342];
    uint8_t offset = 0xAC;
    int idx = 0;
    while (offset != 0x02) {
        uint8_t value = 0;
        auto add = [&](uint8_t a) {
            value = static_cast<uint8_t>((value << 2) | ((a & 0x01) << 1) | ((a & 0x02) >> 1));
        };
        add(src[offset]);
        offset = static_cast<uint8_t>(offset - 0x56);
        add(src[offset]);
        offset = static_cast<uint8_t>(offset - 0x56);
        add(src[offset]);
        offset = static_cast<uint8_t>(offset - 0x53);
        nib[idx++] = static_cast<uint8_t>(value << 2);
    }
    nib[idx - 2] &= 0x3F;
    nib[idx - 1] &= 0x3F;
    for (int i = 0; i < 256; i++) {
        nib[idx++] = src[i];
    }

    uint8_t saved = 0;
    uint8_t xored[343];
    for (int i = 0; i < 342; i++) {
        xored[i] = static_cast<uint8_t>(saved ^ nib[i]);
        saved = nib[i];
    }
    xored[342] = saved;
    for (int i = 0; i < 343; i++) {
        dest[i] = kDiskByte[xored[i] >> 2];
    }
}

bool DiskIi::decode_62(const uint8_t src[343], uint8_t dest[256]) {
    static uint8_t sixbit[0x80];
    static bool table_ready = false;
    if (!table_ready) {
        std::memset(sixbit, 0, sizeof(sixbit));
        for (int i = 0; i < 0x40; i++) {
            sixbit[kDiskByte[i] - 0x80] = static_cast<uint8_t>(i << 2);
        }
        table_ready = true;
    }

    uint8_t raw[343];
    for (int i = 0; i < 343; i++) {
        if ((src[i] & 0x80) == 0) {
            return false;
        }
        raw[i] = sixbit[src[i] & 0x7F];
    }

    uint8_t saved = 0;
    uint8_t nib[342];
    for (int i = 0; i < 342; i++) {
        nib[i] = static_cast<uint8_t>(saved ^ raw[i]);
        saved = nib[i];
    }
    if (saved != raw[342]) {
        return false;
    }

    std::memset(dest, 0, 256);
    const uint8_t* low = nib;
    const uint8_t* sector = nib + 0x56;
    uint8_t offset = 0xAC;
    while (offset != 0x02) {
        if (offset >= 0xAC) {
            dest[offset] = static_cast<uint8_t>((sector[offset] & 0xFC) | ((low[0] & 0x80) >> 7) |
                                                ((low[0] & 0x40) >> 5));
        }
        offset = static_cast<uint8_t>(offset - 0x56);
        dest[offset] = static_cast<uint8_t>((sector[offset] & 0xFC) | ((low[0] & 0x20) >> 5) |
                                            ((low[0] & 0x10) >> 3));
        offset = static_cast<uint8_t>(offset - 0x56);
        dest[offset] = static_cast<uint8_t>((sector[offset] & 0xFC) | ((low[0] & 0x08) >> 3) |
                                            ((low[0] & 0x04) >> 1));
        offset = static_cast<uint8_t>(offset - 0x53);
        ++low;
    }
    return true;
}

void DiskIi::reset() {
    phases_ = 0;
    half_track_ = 0;
    motor_on_ = false;
    drive_ = 0;
    q6_ = false;
    q7_ = false;
    latch_ = 0;
    nibble_pos_ = 0;
    cycles_until_nibble_ = kCyclesPerNibble;
    write_mode_ = false;
    write_buf_.clear();
    encoded_track_ = -1;
    dirty_ = false;
    if (loaded_) {
        encode_track(0);
    } else {
        nibbles_.clear();
    }
}

int DiskIi::dos_sector_for_physical(int physical) const {
    physical &= 0x0F;
    if (kind_ == kProdosOrder) {
        return kProdosSkew[physical];
    }
    return kDosSkew[physical];
}

void DiskIi::encode_track(int track) {
    nibbles_.clear();
    encoded_track_ = track;
    nibble_pos_ = 0;
    latch_ = 0;
    cycles_until_nibble_ = kCyclesPerNibble;
    dirty_ = false;
    if (!loaded_ || track < 0 || track >= kTracks) {
        return;
    }
    if (kind_ == kNibble) {
        if (track < static_cast<int>(nibble_tracks_.size())) {
            nibbles_ = nibble_tracks_[static_cast<size_t>(track)];
        }
        return;
    }

    auto gap = [&](int count) {
        for (int i = 0; i < count; i++) {
            nibbles_.push_back(kGap);
        }
    };

    gap(48);
    for (int physical = 0; physical < kSectors; physical++) {
        const int logical = dos_sector_for_physical(physical);
        const uint8_t* sector = image_.data() + (track * kSectors + logical) * kSectorSize;

        nibbles_.push_back(0xD5);
        nibbles_.push_back(0xAA);
        nibbles_.push_back(0x96);
        append_44(nibbles_, kVolume);
        append_44(nibbles_, static_cast<uint8_t>(track));
        append_44(nibbles_, static_cast<uint8_t>(physical));
        append_44(nibbles_, static_cast<uint8_t>(kVolume ^ track ^ physical));
        nibbles_.push_back(0xDE);
        nibbles_.push_back(0xAA);
        nibbles_.push_back(0xEB);
        gap(6);
        nibbles_.push_back(0xD5);
        nibbles_.push_back(0xAA);
        nibbles_.push_back(0xAD);
        uint8_t encoded[343];
        encode_62(sector, encoded);
        nibbles_.insert(nibbles_.end(), encoded, encoded + 343);
        nibbles_.push_back(0xDE);
        nibbles_.push_back(0xAA);
        nibbles_.push_back(0xEB);
        gap(27);
    }
    while (static_cast<int>(nibbles_.size()) < kMaxNibbles) {
        nibbles_.push_back(kGap);
    }
    if (static_cast<int>(nibbles_.size()) > kMaxNibbles) {
        nibbles_.resize(kMaxNibbles);
    }
}

bool DiskIi::decode_track(int track) {
    if (!loaded_ || kind_ == kNibble || track < 0 || track >= kTracks || nibbles_.empty()) {
        return false;
    }

    int offset = 0;
    const int n = static_cast<int>(nibbles_.size());
    int parts = kSectors * 2 + 1;
    int physical = -1;
    while (parts-- > 0) {
        uint8_t byteval[3] = {0, 0, 0};
        int bytenum = 0;
        int loop = n;
        while (loop-- && bytenum < 3) {
            const uint8_t v = nibbles_[static_cast<size_t>(offset++)];
            if (offset >= n) {
                offset = 0;
            }
            if (bytenum) {
                byteval[bytenum++] = v;
            } else if (v == 0xD5) {
                bytenum = 1;
            }
        }
        if (bytenum != 3 || byteval[1] != 0xAA) {
            continue;
        }
        if (byteval[2] == 0x96) {
            uint8_t field[8];
            for (int i = 0; i < 8; i++) {
                field[i] = nibbles_[static_cast<size_t>(offset++)];
                if (offset >= n) {
                    offset = 0;
                }
            }
            physical = ((field[4] & 0x55) << 1) | (field[5] & 0x55);
        } else if (byteval[2] == 0xAD && physical >= 0 && physical < kSectors) {
            uint8_t encoded[343];
            for (int i = 0; i < 343; i++) {
                encoded[i] = nibbles_[static_cast<size_t>(offset++)];
                if (offset >= n) {
                    offset = 0;
                }
            }
            uint8_t decoded[256];
            if (decode_62(encoded, decoded)) {
                const int logical = dos_sector_for_physical(physical);
                std::memcpy(image_.data() + (track * kSectors + logical) * kSectorSize, decoded,
                            kSectorSize);
            }
            physical = -1;
        }
    }
    return true;
}

void DiskIi::rebuild_track() {
    encode_track(std::clamp(half_track_ / 2, 0, kTracks - 1));
}

void DiskIi::flush_write() {
    if (dirty_) {
        decode_track(encoded_track_);
        dirty_ = false;
    }
}

uint8_t DiskIi::next_nibble() {
    if (nibbles_.empty()) {
        return 0;
    }
    if (nibble_pos_ < 0 || nibble_pos_ >= static_cast<int>(nibbles_.size())) {
        nibble_pos_ = 0;
    }
    const uint8_t value = nibbles_[static_cast<size_t>(nibble_pos_)];
    nibble_pos_ = (nibble_pos_ + 1) % static_cast<int>(nibbles_.size());
    return value;
}

void DiskIi::write_nibble(uint8_t value) {
    if (nibbles_.empty()) {
        return;
    }
    if (nibble_pos_ < 0 || nibble_pos_ >= static_cast<int>(nibbles_.size())) {
        nibble_pos_ = 0;
    }
    nibbles_[static_cast<size_t>(nibble_pos_)] = value | 0x80;
    if (kind_ == kNibble && encoded_track_ >= 0 &&
        encoded_track_ < static_cast<int>(nibble_tracks_.size())) {
        nibble_tracks_[static_cast<size_t>(encoded_track_)] = nibbles_;
    }
    nibble_pos_ = (nibble_pos_ + 1) % static_cast<int>(nibbles_.size());
    dirty_ = true;
}

void DiskIi::step_phase(int phase, bool on) {
    if (on) {
        phases_ = static_cast<uint8_t>(phases_ | (1u << phase));
    } else {
        phases_ = static_cast<uint8_t>(phases_ & ~(1u << phase));
        return;
    }
    const int current = ((half_track_ % 4) + 4) % 4;
    const int delta = (phase - current + 4) % 4;
    if (delta == 1) {
        half_track_++;
    } else if (delta == 3) {
        half_track_--;
    }
    half_track_ = std::clamp(half_track_, 0, (kTracks - 1) * 2);
    const int track = half_track_ / 2;
    if (track != encoded_track_) {
        flush_write();
        encode_track(track);
    }
}

void DiskIi::tick(int cycles) {
    if (!motor_on_ || !loaded_) {
        return;
    }
    cycles_until_nibble_ -= cycles;
    while (cycles_until_nibble_ <= 0) {
        cycles_until_nibble_ += kCyclesPerNibble;
        if (!q7_) {
            latch_ = next_nibble();
        }
    }
}

uint8_t DiskIi::read_io(uint8_t offset) {
    const uint8_t n = offset & 0x0F;
    switch (n) {
        case 0x0:
        case 0x2:
        case 0x4:
        case 0x6:
            step_phase(n / 2, false);
            break;
        case 0x1:
        case 0x3:
        case 0x5:
        case 0x7:
            step_phase(n / 2, true);
            break;
        case 0x8:
            motor_on_ = false;
            flush_write();
            break;
        case 0x9:
            motor_on_ = true;
            break;
        case 0xA:
            drive_ = 0;
            break;
        case 0xB:
            drive_ = 1;
            break;
        case 0xC:
            q6_ = false;
            if (!q7_) {
                const uint8_t value = latch_;
                latch_ &= 0x7F;
                return value;
            }
            if (write_mode_) {
                write_nibble(latch_);
            }
            return latch_;
        case 0xD:
            q6_ = true;
            return 0x00;  // not write-protected
        case 0xE:
            q7_ = false;
            write_mode_ = false;
            break;
        case 0xF:
            q7_ = true;
            write_mode_ = true;
            break;
        default:
            break;
    }
    return latch_;
}

void DiskIi::write_io(uint8_t offset, uint8_t value) {
    const uint8_t n = offset & 0x0F;
    if (n == 0x0F || (n == 0x0D && q7_)) {
        latch_ = value | 0x80;
    }
    read_io(offset);
    if (n == 0x0F) {
        latch_ = value | 0x80;
        write_mode_ = true;
        q7_ = true;
    }
}

bool DiskIi::load_bytes(const uint8_t* data, size_t size, std::string* error) {
    return load_bytes(data, size, "", error);
}

bool DiskIi::load_bytes(const uint8_t* data, size_t size, const std::string& hint, std::string* error) {
    if (data == nullptr || size == 0) {
        if (error) *error = "empty disk image";
        return false;
    }

    reset();
    loaded_ = false;
    kind_ = kNone;
    image_.clear();
    nibbles_.clear();
    nibble_tracks_.clear();

    const std::string lower = to_lower(hint);
    const bool want_nib = ends_with(lower, ".nib") || size == static_cast<size_t>(kTracks * kMaxNibbles);
    if (want_nib) {
        if (size < static_cast<size_t>(kTracks * 6000)) {
            if (error) *error = "nibble image is too small";
            return false;
        }
        const int stride = static_cast<int>(size / kTracks);
        nibble_tracks_.assign(kTracks, {});
        for (int t = 0; t < kTracks; t++) {
            const uint8_t* src = data + t * stride;
            nibble_tracks_[static_cast<size_t>(t)].assign(src, src + std::min(stride, kMaxNibbles));
            while (static_cast<int>(nibble_tracks_[static_cast<size_t>(t)].size()) < kMaxNibbles) {
                nibble_tracks_[static_cast<size_t>(t)].push_back(kGap);
            }
        }
        kind_ = kNibble;
        loaded_ = true;
        encoded_track_ = 0;
        nibbles_ = nibble_tracks_[0];
        return true;
    }

    if (size < static_cast<size_t>(kDosSize)) {
        if (error) *error = "disk image is too small (need 140K DOS 3.3 / ProDOS)";
        return false;
    }

    image_.assign(data, data + kDosSize);
    kind_ = ends_with(lower, ".po") ? kProdosOrder : kDosOrder;
    loaded_ = true;
    encode_track(0);
    return true;
}

bool DiskIi::load_file(const std::string& path, std::string* error) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        if (error) *error = "cannot open " + path;
        return false;
    }
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return load_bytes(data.data(), data.size(), path, error);
}

}  // namespace dsp
