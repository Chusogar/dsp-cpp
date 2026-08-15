#pragma once

#include <array>
#include <cstdint>

namespace dsp {

// Game Boy APU (4 channels: pulse+sweep, pulse, wave, noise), ported from
// gb_sound.pas. That file precomputes every timing constant (envelope step,
// sweep step, period, length) as a sample count at a fixed output rate
// rather than stepping a cycle-driven frame sequencer, so `update()` is
// called once per output sample (like SN76496/AY8910's `update()`) instead
// of being fed CPU cycles.
class GbApu {
public:
    static constexpr int kSampleRate = 44100;

    GbApu();
    void reset();

    uint8_t read(uint8_t offset) const;   // $ff10-$ff26 (NR10-NR52), offset = reg - $ff10
    void write(uint8_t offset, uint8_t value);
    uint8_t wave_read(uint8_t offset) const;   // $ff30-$ff3f, offset 0-15
    void wave_write(uint8_t offset, uint8_t value);

    // Produces one stereo sample pair, mixed down to mono to match this
    // project's mono `drain_audio` convention (see gameboy.cpp).
    int16_t update();

private:
    struct Channel {
        bool on = false;
        int channel_index = 0;
        uint32_t length = 0;
        uint32_t pos = 0;
        int64_t period = 0;
        int32_t count = 0;
        bool length_enabled = false;  // "mode" in gb_sound.pas
        int duty = 0;
        int32_t env_value = 0;
        int env_direction = 0;
        uint32_t env_length = 0;
        int32_t env_count = 0;
        int32_t signal = 0;
        uint32_t frequency = 0;
        int swp_shift = 0;
        int swp_direction = 0;
        uint32_t swp_time = 0;
        int32_t swp_count = 0;
        uint8_t level = 0;
        uint8_t offset = 0;
        uint32_t dutycount = 0;
        bool ply_step = false;
        uint32_t ply_value = 0;
    };
    struct Control {
        bool on = false;
        uint8_t vol_left = 0, vol_right = 0;
        std::array<bool, 4> mode_left{}, mode_right{};
    };

    void write_internal(uint8_t offset, uint8_t value);

    std::array<Channel, 4> channel_{};  // index 0-3 = channels 1-4
    Control control_{};
    std::array<uint8_t, 0x30> regs_{};

    static std::array<uint16_t, 8> kEnvLengthTable;
    static std::array<uint16_t, 8> kSwpTimeTable;
    static std::array<uint32_t, 2048> kPeriodTable;
    static std::array<uint32_t, 2048> kPeriodMode3Table;
    static std::array<std::array<uint32_t, 16>, 8> kPeriodMode4Table;
    static std::array<uint16_t, 64> kLengthTable;
    static std::array<uint32_t, 256> kLengthMode3Table;
    static bool tables_ready_;
    static void build_tables();
};

}  // namespace dsp
