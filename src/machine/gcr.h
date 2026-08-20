#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace dsp {

// Commodore 1541 GCR (4-to-5) encode/decode and track image builder.
class Gcr {
public:
    // 5-bit codes for nibbles 0x0..0xF
    static constexpr uint8_t kEncode[16] = {
        0x0A, 0x0B, 0x12, 0x13, 0x0E, 0x0F, 0x16, 0x17,
        0x09, 0x19, 0x1A, 0x1B, 0x0D, 0x1D, 0x1E, 0x15,
    };

    // Decode table: 5-bit code → nibble, 0xFF = invalid
    static uint8_t decode_nibble(uint8_t five_bit);

    // 4 data bytes → 5 GCR bytes
    static void encode_4_to_5(const uint8_t in[4], uint8_t out[5]);
    // 5 GCR bytes → 4 data bytes (false if illegal code)
    static bool decode_5_to_4(const uint8_t in[5], uint8_t out[4]);

    // Encode an arbitrary number of data bytes (pad to multiple of 4 with 0).
    static std::vector<uint8_t> encode_bytes(const uint8_t* data, size_t len);

    // Expand GCR bytes to a bit stream (MSB of each 8-bit storage first).
    // For on-disk stream we emit only the meaningful bits from encode;
    // `append_gcr_stream` writes 5-bit groups from encode_4_to_5 output
    // as a continuous bit string.
    static void append_bits(std::vector<bool>& bits, uint8_t value, int nbits);
    static void append_sync(std::vector<bool>& bits, int ones = 40);
    static void append_gcr_bytes(std::vector<bool>& bits, const uint8_t* gcr, size_t n);
    static void append_gap55(std::vector<bool>& bits, int gcr_bytes);

    // Build a full track bit image for 1541.
    // `sector_data[sec]` must point to 256 bytes (or nullptr → zeros).
    // id1/id2 = disk ID from BAM.
    static std::vector<bool> build_track(int track /*1..35*/,
                                         int sectors,
                                         const uint8_t* const* sector_data,
                                         uint8_t id1, uint8_t id2);

    // Approximate raw GCR byte capacity per track (for gap padding).
    static int track_capacity_gcr_bytes(int track);
};

}  // namespace dsp
