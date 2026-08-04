#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace dsp {

// Tape player for .tap and .tzx images, the pulse timings of tap_tzx.pas.
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
    // A .tzx pause is given in milliseconds of the 3.5 MHz clock.
    static constexpr int kCyclesPerMs = 3500;
    // "Stop the tape" blocks only pause here: the machine restarts the tape by
    // itself whenever the ROM loader runs.
    static constexpr int kStopCycles = 2 * kPauseCycles;

    bool load(const std::string& path, std::string* error);
    bool load_from_memory(std::vector<uint8_t> data, std::string* error);
    void rewind();

    bool loaded() const { return !blocks_.empty(); }
    bool playing() const { return playing_; }
    void set_playing(bool state) { playing_ = state && loaded(); }

    // Advances the tape by `cycles` T states when it is playing.
    void advance(int cycles);

    bool ear() const { return level_; }
    // True once every block has been played.
    bool finished() const { return finished_; }

    size_t block_count() const { return blocks_.size(); }

private:
    // One entry of the block list built when the image is parsed.
    struct Block {
        enum class Kind { Data, Tone, Pulses, Direct, Pause, SetLevel, Jump, LoopStart, LoopEnd };

        Kind kind = Kind::Data;
        int pilot_pulse = kPilotPulse;
        int pilot_pulses = 0;
        int sync1 = 0;
        int sync2 = 0;
        int zero = kBit0Pulse;
        int one = kBit1Pulse;
        int last_byte_bits = 8;
        int sample_cycles = 0;  // Direct: T states of one sample
        int pause_cycles = 0;
        size_t data_start = 0;
        size_t data_end = 0;
        std::vector<int> pulses;  // Pulses: the length of each one
        bool level = false;       // SetLevel
        int count = 0;            // LoopStart: repetitions
        int target = 0;           // Jump: signed block offset
    };

    enum class Phase {
        Pilot,
        Sync1,
        Sync2,
        BitFirst,
        BitSecond,
        Tail,
        Pause,
        PulseList,
        Direct,
        Done
    };

    bool parse_tap(std::string* error);
    bool parse_tzx(std::string* error);
    void add_standard_block(size_t start, size_t end);

    void begin_block(size_t index);
    void next_block();
    void begin_data_bits();
    void begin_pause(int cycles);
    void next_pulse();

    std::vector<uint8_t> data_;
    std::vector<Block> blocks_;
    std::vector<std::pair<size_t, int>> loops_;  // block index, remaining repetitions

    size_t block_index_ = 0;
    size_t byte_index_ = 0;
    size_t pulse_index_ = 0;
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
