#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace dsp {

// Standard speed tape player for .tap images, the pulse timings of tap_tzx.pas.
// The ULA reads the current level through ear().
class SpectrumTape {
public:
    // T states of the pulses of a standard ROM block.
    static constexpr int kPilotPulse = 2168;
    static constexpr int kSync1Pulse = 667;
    static constexpr int kSync2Pulse = 735;
    static constexpr int kBit0Pulse = 855;
    static constexpr int kBit1Pulse = 1710;
    static constexpr int kHeaderPilotPulses = 8063;
    static constexpr int kDataPilotPulses = 3223;
    static constexpr int kTailPulse = 945;        // closing edge of a block
    static constexpr int kPauseCycles = 3500000;  // one second between blocks

    bool load(const std::string& path, std::string* error);
    bool load_from_memory(std::vector<uint8_t> data, std::string* error);
    void rewind();

    bool loaded() const { return !data_.empty(); }
    bool playing() const { return playing_; }
    void set_playing(bool state) { playing_ = state && loaded(); }

    // Advances the tape by `cycles` T states when it is playing.
    void advance(int cycles);

    bool ear() const { return level_; }
    // True once every block has been played.
    bool finished() const { return finished_; }

private:
    enum class Phase { Pilot, Sync1, Sync2, BitFirst, BitSecond, Tail, Pause, Done };

    bool start_block(size_t offset);
    void next_pulse();

    std::vector<uint8_t> data_;
    size_t block_start_ = 0;   // first byte of the current block
    size_t block_end_ = 0;     // one past its last byte
    size_t byte_index_ = 0;
    uint8_t bit_mask_ = 0x80;
    int pulses_left_ = 0;
    int pulse_cycles_ = 0;
    int remaining_ = 0;
    Phase phase_ = Phase::Done;
    bool level_ = false;
    bool playing_ = false;
    bool finished_ = true;
};

}  // namespace dsp
