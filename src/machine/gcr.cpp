#include "machine/gcr.h"

#include <cstring>

namespace dsp {
namespace {

uint8_t make_decode_table() {
    return 0;  // placeholder to force static init below
}

const uint8_t kDecode[32] = {
    // index = 5-bit code
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,  // 0x00-0x07
    0xFF, 0x08, 0x00, 0x01, 0xFF, 0x0C, 0x04, 0x05,  // 0x08-0x0F
    0xFF, 0xFF, 0x02, 0x03, 0xFF, 0x0F, 0x06, 0x07,  // 0x10-0x17
    0xFF, 0x09, 0x0A, 0x0B, 0xFF, 0x0D, 0x0E, 0xFF,  // 0x18-0x1F
};

}  // namespace

uint8_t Gcr::decode_nibble(uint8_t five_bit) {
    return kDecode[five_bit & 0x1F];
}

void Gcr::encode_4_to_5(const uint8_t in[4], uint8_t out[5]) {
    const uint8_t a = kEncode[in[0] >> 4];
    const uint8_t b = kEncode[in[0] & 0x0F];
    const uint8_t c = kEncode[in[1] >> 4];
    const uint8_t d = kEncode[in[1] & 0x0F];
    const uint8_t e = kEncode[in[2] >> 4];
    const uint8_t f = kEncode[in[2] & 0x0F];
    const uint8_t g = kEncode[in[3] >> 4];
    const uint8_t h = kEncode[in[3] & 0x0F];

    out[0] = uint8_t((a << 3) | (b >> 2));
    out[1] = uint8_t((b << 6) | (c << 1) | (d >> 4));
    out[2] = uint8_t((d << 4) | (e >> 1));
    out[3] = uint8_t((e << 7) | (f << 2) | (g >> 3));
    out[4] = uint8_t((g << 5) | h);
}

bool Gcr::decode_5_to_4(const uint8_t in[5], uint8_t out[4]) {
    // Extract eight 5-bit codes from five bytes
    const uint8_t n0 = uint8_t((in[0] >> 3) & 0x1F);
    const uint8_t n1 = uint8_t(((in[0] & 0x07) << 2) | ((in[1] >> 6) & 0x03));
    const uint8_t n2 = uint8_t((in[1] >> 1) & 0x1F);
    const uint8_t n3 = uint8_t(((in[1] & 0x01) << 4) | ((in[2] >> 4) & 0x0F));
    const uint8_t n4 = uint8_t(((in[2] & 0x0F) << 1) | ((in[3] >> 7) & 0x01));
    const uint8_t n5 = uint8_t((in[3] >> 2) & 0x1F);
    const uint8_t n6 = uint8_t(((in[3] & 0x03) << 3) | ((in[4] >> 5) & 0x07));
    const uint8_t n7 = uint8_t(in[4] & 0x1F);

    const uint8_t d0 = decode_nibble(n0);
    const uint8_t d1 = decode_nibble(n1);
    const uint8_t d2 = decode_nibble(n2);
    const uint8_t d3 = decode_nibble(n3);
    const uint8_t d4 = decode_nibble(n4);
    const uint8_t d5 = decode_nibble(n5);
    const uint8_t d6 = decode_nibble(n6);
    const uint8_t d7 = decode_nibble(n7);
    if ((d0 | d1 | d2 | d3 | d4 | d5 | d6 | d7) == 0xFF) return false;
    if (d0 == 0xFF || d1 == 0xFF || d2 == 0xFF || d3 == 0xFF || d4 == 0xFF ||
        d5 == 0xFF || d6 == 0xFF || d7 == 0xFF)
        return false;

    out[0] = uint8_t((d0 << 4) | d1);
    out[1] = uint8_t((d2 << 4) | d3);
    out[2] = uint8_t((d4 << 4) | d5);
    out[3] = uint8_t((d6 << 4) | d7);
    return true;
}

std::vector<uint8_t> Gcr::encode_bytes(const uint8_t* data, size_t len) {
    std::vector<uint8_t> out;
    uint8_t buf[4] = {};
    for (size_t i = 0; i < len; i += 4) {
        buf[0] = buf[1] = buf[2] = buf[3] = 0;
        for (size_t j = 0; j < 4 && i + j < len; j++) buf[j] = data[i + j];
        uint8_t g5[5];
        encode_4_to_5(buf, g5);
        out.insert(out.end(), g5, g5 + 5);
    }
    return out;
}

void Gcr::append_bits(std::vector<bool>& bits, uint8_t value, int nbits) {
    for (int i = nbits - 1; i >= 0; i--)
        bits.push_back(((value >> i) & 1) != 0);
}

void Gcr::append_sync(std::vector<bool>& bits, int ones) {
    for (int i = 0; i < ones; i++) bits.push_back(true);
}

void Gcr::append_gcr_bytes(std::vector<bool>& bits, const uint8_t* gcr, size_t n) {
    // Each GCR storage byte is 8 bits on the wire (as the 1541 shift register
    // presents them after grouping). encode_4_to_5 already packs five 5-bit
    // symbols into five 8-bit bytes — the drive shifts those 8-bit values.
    for (size_t i = 0; i < n; i++) append_bits(bits, gcr[i], 8);
}

void Gcr::append_gap55(std::vector<bool>& bits, int gcr_bytes) {
    for (int i = 0; i < gcr_bytes; i++) append_bits(bits, 0x55, 8);
}

int Gcr::track_capacity_gcr_bytes(int track) {
    // Standard 1541 raw density (approx. bytes of GCR per track).
    if (track <= 17) return 7692;
    if (track <= 24) return 7142;
    if (track <= 30) return 6666;
    return 6250;
}

std::vector<bool> Gcr::build_track(int track, int sectors,
                                   const uint8_t* const* sector_data,
                                   uint8_t id1, uint8_t id2) {
    std::vector<bool> bits;
    if (track < 1 || track > 35 || sectors <= 0) return bits;

    const int capacity_bits = track_capacity_gcr_bytes(track) * 8;
    bits.reserve(size_t(capacity_bits));

    // Evenly distribute sectors with gaps.
    for (int sec = 0; sec < sectors; sec++) {
        // ---- Header ----
        append_sync(bits, 40);  // ≥10 ones required; 40 is typical

        // 8 binary header bytes → 10 GCR bytes
        uint8_t hdr[8];
        hdr[0] = 0x08;
        hdr[1] = uint8_t(sec ^ track ^ id2 ^ id1);  // checksum
        hdr[2] = uint8_t(sec);
        hdr[3] = uint8_t(track);
        hdr[4] = id2;
        hdr[5] = id1;
        hdr[6] = 0x0F;
        hdr[7] = 0x0F;
        const auto hdr_gcr = encode_bytes(hdr, 8);
        append_gcr_bytes(bits, hdr_gcr.data(), hdr_gcr.size());

        // GAP 1 (header gap) — ~9–12 × $55
        append_gap55(bits, 9);

        // ---- Data ----
        append_sync(bits, 40);

        uint8_t block[260];
        block[0] = 0x07;
        if (sector_data && sector_data[sec])
            std::memcpy(block + 1, sector_data[sec], 256);
        else
            std::memset(block + 1, 0, 256);

        uint8_t chk = 0;
        for (int i = 1; i <= 256; i++) chk = uint8_t(chk ^ block[i]);
        block[257] = chk;
        block[258] = 0x00;
        block[259] = 0x00;

        const auto data_gcr = encode_bytes(block, 260);
        append_gcr_bytes(bits, data_gcr.data(), data_gcr.size());

        // GAP 3 inter-sector — provisional small gap; padded at end
        append_gap55(bits, 8);
    }

    // Pad to track capacity with $55 so rotational timing is stable.
    while (int(bits.size()) + 8 <= capacity_bits) append_bits(bits, 0x55, 8);
    while (int(bits.size()) < capacity_bits) bits.push_back(false);

    return bits;
}

}  // namespace dsp
