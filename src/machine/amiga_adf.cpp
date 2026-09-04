#include "machine/amiga_adf.h"

#include <cstring>
#include <fstream>

namespace dsp {
namespace {

uint32_t be32(const uint8_t* p) {
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
}

void poke_be32(uint8_t* p, uint32_t v) {
    p[0] = uint8_t(v >> 24);
    p[1] = uint8_t(v >> 16);
    p[2] = uint8_t(v >> 8);
    p[3] = uint8_t(v);
}

// Insert MFM clock bits into a 0x55555555-masked data long. `prev` is the last
// output bit of the previous long (LSB).
uint32_t encode_clocks(uint32_t data, uint32_t* prev) {
    data &= 0x55555555u;
    uint32_t out = data;
    uint32_t p = *prev;
    for (int i = 31; i >= 0; i--) {
        if (i & 1) {
            const uint32_t next = (data >> (i - 1)) & 1u;
            const uint32_t clock = (p == 0 && next == 0) ? 1u : 0u;
            out |= clock << i;
            p = clock;
        } else {
            p = (data >> i) & 1u;
        }
    }
    *prev = p;
    return out;
}

void emit_long(std::vector<uint16_t>& mfm, uint32_t word) {
    mfm.push_back(uint16_t(word >> 16));
    mfm.push_back(uint16_t(word));
}

void encode_block(std::vector<uint16_t>& mfm, const uint32_t* data, int n, uint32_t* prev) {
    for (int i = 0; i < n; i++) {
        emit_long(mfm, encode_clocks((data[i] >> 1) & 0x55555555u, prev));
    }
    for (int i = 0; i < n; i++) {
        emit_long(mfm, encode_clocks(data[i] & 0x55555555u, prev));
    }
}

uint32_t xor_block(const uint32_t* data, int n) {
    uint32_t s = 0;
    for (int i = 0; i < n; i++) s ^= data[i];
    return s;
}

}  // namespace

bool AmigaAdf::load_file(const std::string& path, std::string* error) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        if (error) *error = "cannot open ADF " + path;
        return false;
    }
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return load_bytes(data, error);
}

bool AmigaAdf::load_bytes(const std::vector<uint8_t>& data, std::string* error) {
    if (data.size() < 11 * 512) {
        if (error) *error = "ADF is too small";
        return false;
    }
    if (data.size() % 512 != 0) {
        if (error) *error = "ADF is not a multiple of 512 bytes";
        return false;
    }
    const int sectors = int(data.size() / 512);
    if (sectors % kSectorsPerTrack != 0) {
        if (error) *error = "ADF is not an 11-sector AmigaDOS image";
        return false;
    }
    const int track_total = sectors / kSectorsPerTrack;
    if (track_total >= 160 && track_total % 2 == 0) {
        sides_ = 2;
        tracks_ = track_total / 2;
    } else {
        sides_ = 1;
        tracks_ = track_total;
    }
    if (tracks_ < 40 || tracks_ > 84) {
        if (error) *error = "ADF cylinder count is out of range";
        return false;
    }
    image_ = data;
    return true;
}

void AmigaAdf::unload() {
    image_.clear();
    tracks_ = 80;
    sides_ = 2;
}

int AmigaAdf::offset(int cyl, int side, int sec) const {
    if (cyl < 0 || cyl >= tracks_ || side < 0 || side >= sides_ || sec < 0 ||
        sec >= kSectorsPerTrack) {
        return -1;
    }
    return ((cyl * sides_ + side) * kSectorsPerTrack + sec) * kBytesPerSector;
}

const uint8_t* AmigaAdf::sector(int cyl, int side, int sec) const {
    const int off = offset(cyl, side, sec);
    if (off < 0) return nullptr;
    return image_.data() + off;
}

uint8_t* AmigaAdf::sector(int cyl, int side, int sec) {
    const int off = offset(cyl, side, sec);
    if (off < 0) return nullptr;
    return image_.data() + off;
}

std::vector<uint16_t> AmigaAdf::encode_track(int cyl, int side) const {
    std::vector<uint16_t> mfm;
    mfm.reserve(kMfmWords);
    // Leading gap of 0xAAAA (encoded zeros).
    for (int i = 0; i < 165; i++) mfm.push_back(0xAAAA);

    const int amiga_track = cyl * 2 + side;
    for (int sec = 0; sec < kSectorsPerTrack; sec++) {
        mfm.push_back(0x4489);
        mfm.push_back(0x4489);
        uint32_t prev = 0x4489 & 1;

        uint32_t header[5];
        header[0] = 0xFF000000u | (uint32_t(amiga_track) << 16) | (uint32_t(sec) << 8) |
                    uint32_t(kSectorsPerTrack - sec);
        header[1] = header[2] = header[3] = header[4] = 0;
        const uint32_t hsum = xor_block(header, 5);

        uint32_t data[128];
        const uint8_t* src = sector(cyl, side, sec);
        if (src) {
            for (int i = 0; i < 128; i++) data[i] = be32(src + i * 4);
        } else {
            std::memset(data, 0, sizeof(data));
        }
        const uint32_t dsum = xor_block(data, 128);

        encode_block(mfm, header, 5, &prev);
        encode_block(mfm, &hsum, 1, &prev);
        encode_block(mfm, &dsum, 1, &prev);
        encode_block(mfm, data, 128, &prev);

        mfm.push_back(0xAAAA);
        mfm.push_back(0xAAAA);
    }
    while (int(mfm.size()) < kMfmWords) mfm.push_back(0xAAAA);
    if (int(mfm.size()) > kMfmWords) mfm.resize(kMfmWords);
    return mfm;
}

uint32_t AmigaAdf::bootblock_checksum(const uint8_t* block) {
    uint32_t sum = 0;
    for (int i = 0; i < 256; i++) {
        uint32_t v = be32(block + i * 4);
        if (i == 1) v = 0;
        const uint32_t old = sum;
        sum += v;
        if (sum < old) sum++;
    }
    return ~sum;
}

void AmigaAdf::set_bootblock_checksum(uint8_t* block) {
    poke_be32(block + 4, bootblock_checksum(block));
}

bool AmigaAdf::write_color_boot(const std::string& path, uint16_t color00, std::string* error) {
    std::vector<uint8_t> img(80 * 2 * 11 * 512, 0);
    uint8_t* bb = img.data();
    bb[0] = 'D';
    bb[1] = 'O';
    bb[2] = 'S';
    bb[3] = 0;
    poke_be32(bb + 8, 880);
    // Boot code at offset 12: disable DMA/ints, set COLOR00, loop.
    //   lea     $00dff000,a0
    //   move.w  #$7fff,$9a(a0)
    //   move.w  #$7fff,$9c(a0)
    //   move.w  #$7fff,$96(a0)
    //   move.w  #color,$180(a0)
    //   bra.s   *
    uint8_t* c = bb + 12;
    const uint8_t code[] = {
        0x41, 0xF9, 0x00, 0xDF, 0xF0, 0x00, 0x31, 0x7C, 0x7F, 0xFF, 0x00, 0x9A,
        0x31, 0x7C, 0x7F, 0xFF, 0x00, 0x9C, 0x31, 0x7C, 0x7F, 0xFF, 0x00, 0x96,
        0x31, 0x7C, 0x00, 0x00, 0x01, 0x80, 0x60, 0xFE};
    std::memcpy(c, code, sizeof(code));
    c[24] = uint8_t(color00 >> 8);
    c[25] = uint8_t(color00);
    set_bootblock_checksum(bb);

    std::ofstream out(path, std::ios::binary);
    if (!out) {
        if (error) *error = "cannot write " + path;
        return false;
    }
    out.write(reinterpret_cast<const char*>(img.data()), std::streamsize(img.size()));
    return bool(out);
}

}  // namespace dsp
