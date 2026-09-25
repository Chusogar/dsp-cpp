#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace dsp {

// Exelvision cassette (.k7) for the EXL-100 and EXELTEL.
//
// The tape routines live in the TMS7020/7040 internal ROM (TRAP 14): a
// byte is sent MSB first, each bit as two square-wave cycles on port B bit 3,
// with half periods of $53 (bit 0) or $28 (bit 1) delay-loop turns - 976 and
// 503 CPU cycles at 2.4576 MHz, about 1260 / 2440 Hz. A recording is
//
//     255 x $55 (leader), $70 (sync), 4 name bytes, 5 header bytes
//     (flags, end address LSB/MSB, start address LSB/MSB), the data bytes,
//     and the byte checksum twice.
//
// A .k7 file holds those bytes. Files without a leader get one, so images that
// start at the $70 sync byte or at the name also load. The player rebuilds the
// waveform the ROM expects on port A bit 4; the recorder decodes port B bit 3
// back into bytes.
class ExelTape {
public:
    static constexpr int kHalfBit0 = 976;  // CPU cycles per half period
    static constexpr int kHalfBit1 = 503;

    bool load(std::vector<uint8_t> data);
    // A cassette recording (.wav, 8/16-bit PCM): its square waves are decoded
    // into the same bytes a .k7 holds.
    bool load_wav(const std::vector<uint8_t>& file, std::string* error);
    void rewind();
    bool loaded() const { return !bytes_.empty(); }
    bool at_end() const { return pos_ >= bytes_.size(); }
    const std::vector<uint8_t>& bytes() const { return bytes_; }

    // Fast load: skip to just after the next $70 sync byte (false if there
    // is none), and hand out the following bytes one at a time.
    bool seek_sync();
    int next_byte();

    // Advance by CPU cycles and return the input level (port A bit 4).
    bool advance(int cycles);
    bool level() const { return level_; }

    // Recorder: feed every port B bit 3 change with the CPU cycle count;
    // take_recording() returns and clears the bytes decoded so far.
    void record_edge(uint64_t cycle, bool level);
    std::vector<uint8_t> take_recording();
    bool recording() const { return !halves_.empty() || !recorded_.empty(); }

    // Decode a sequence of half periods (CPU cycles) into bytes.
    static std::vector<uint8_t> decode_halves(const std::vector<int>& halves);

private:
    void start_bit();

    std::vector<uint8_t> bytes_;
    size_t pos_ = 0;
    int bit_ = 0;       // 7..0, MSB first
    int half_ = 0;      // 0..3 within a bit
    int remain_ = 0;    // cycles left in the current half
    bool level_ = true;

    std::vector<int> halves_;
    std::vector<uint8_t> recorded_;
    uint64_t last_edge_ = 0;
    bool have_edge_ = false;
};

}  // namespace dsp
