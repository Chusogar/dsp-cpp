#pragma once

#include <array>
#include <cstdint>

namespace dsp {

// Game Boy APU (4 channels), ported from
// leniad/dsp-emulator src/snd/gb_sound.pas.
//
// Channels:
//   1 – square + sweep + envelope
//   2 – square + envelope
//   3 – programmable wave
//   4 – noise + envelope
//
// Sample rate is fixed at 44100 Hz (same as the other dsp-cpp chips).
// update() returns a mono mixed sample; call it once per output sample.
class GBSound {
public:
    static constexpr int kSampleRate = 44100;

    GBSound();

    void reset();

    // Memory-mapped registers $FF10-$FF26 (offset 0..$16) and wave RAM
    // $FF30-$FF3F (offset 0..$F).
    uint8_t sound_r(uint8_t offset) const;
    void sound_w(uint8_t offset, uint8_t value);
    uint8_t wave_r(uint8_t offset) const;
    void wave_w(uint8_t offset, uint8_t value);

    // Generate the next mixed mono sample (signed 16-bit range).
    int16_t update();

private:
    struct Channel {
        bool on = false;
        uint16_t length = 0;
        uint32_t pos = 0;
        uint32_t period = 0;
        int count = 0;
        bool mode = false;  // length enable
        int8_t duty = 0;
        int env_value = 0;
        int8_t env_direction = 0;
        uint16_t env_length = 0;
        int env_count = 0;
        int8_t signal = 1;
        // Channel 1
        uint32_t frequency = 0;
        int swp_shift = 0;
        int swp_direction = 0;
        uint16_t swp_time = 0;
        int swp_count = 0;
        // Channel 3
        uint8_t level = 0;
        uint8_t offset = 0;
        uint32_t dutycount = 0;
        // Channel 4
        bool ply_step = false;  // 7-bit LFSR when true
        uint32_t ply_value = 0;
    };

    struct Control {
        bool on = false;
        uint8_t vol_left = 0;
        uint8_t vol_right = 0;
        bool mode_left[4] = {};
        bool mode_right[4] = {};
    };

    void write_internal(uint8_t offset, uint8_t value);
    void build_tables();

    Channel ch_[4];
    Control control_;
    std::array<uint8_t, 0x30> regs_{};

    // Lookup tables (built once).
    std::array<uint16_t, 8> env_length_table_{};
    std::array<uint16_t, 8> swp_time_table_{};
    std::array<uint32_t, 2048> period_table_{};
    std::array<uint32_t, 2048> period_mode3_table_{};
    std::array<std::array<uint32_t, 16>, 8> period_mode4_table_{};
    std::array<uint16_t, 64> length_table_{};
    std::array<uint32_t, 256> length_mode3_table_{};

    static constexpr float kDutyTable[4] = {8.f, 4.f, 2.f, 1.33f};
};

}  // namespace dsp
