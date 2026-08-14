#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "core/machine.h"
#include "cpu/z80.h"

namespace dsp {

// Casio PV-1000, ported from leniad/dsp-emulator src/consolas/pv1000.pas.
//
// Z80 @ 3.579545 MHz, 32×24 tile map, 3 square voices, matrix keyboard.
// Visible output is 224×192 inside a 224×244 frame (coloured border).
class Pv1000 : public Machine {
public:
    static constexpr int kActiveWidth = 224;   // 28 tiles × 8
    static constexpr int kActiveHeight = 192;  // 24 tiles × 8
    static constexpr int kScreenWidth = 224;
    static constexpr int kScreenHeight = 244;  // 26 px border top + 192 + 26 bottom
    static constexpr int kBorderTop = 26;

    // Master XTAL 17.897725 MHz / 5
    static constexpr uint32_t kCpuClock = 17897725u / 5u;  // 3_579_545
    static constexpr int kScanlines = 262;
    static constexpr double kFramesPerSecond = 59.92274;
    static constexpr int kSampleRate = 44100;

    // Cycles per scanline ≈ clock / (fps * lines)
    static constexpr int kCyclesPerLine =
        int(double(kCpuClock) / (kFramesPerSecond * double(kScanlines)) + 0.5);

    Pv1000();

    bool init(const std::string& rom_path, std::string* error) override;
    void reset() override;
    void run_frame() override;

    void set_inputs(const MachineInputs& inputs) override;
    void set_dip_switch(int bank, uint8_t value) override;

    const uint32_t* framebuffer() const override { return framebuffer_.data(); }
    int screen_width() const override { return kScreenWidth; }
    int screen_height() const override { return kScreenHeight; }
    double frames_per_second() const override { return kFramesPerSecond; }

    void drain_audio(std::vector<int16_t>& out) override;
    int sample_rate() const override { return kSampleRate; }

    const char* title() const override { return "Casio PV-1000"; }

    bool load_media(const std::string& path, std::string* error) override;

private:
    uint8_t read_mem(uint16_t addr);
    void write_mem(uint16_t addr, uint8_t value);
    uint8_t in_port(uint16_t port);
    void out_port(uint16_t port, uint8_t value);
    void on_cycles(int cycles);

    void update_video();
    void draw_tile(int sx, int sy, uint16_t pattern_base);
    void sound_tick();  // advance internal sound by one high-rate tick
    void push_audio_samples(int cpu_cycles);

    static constexpr uint32_t kPalette[8] = {
        0xFF000000, 0xFF0000FF, 0xFF00FF00, 0xFF00FFFF,
        0xFFFF0000, 0xFFFF00FF, 0xFFFFFF00, 0xFFFFFFFF,
    };

    Z80 cpu_;

    // $0000-$7FFF cart ROM (up to 32 KB mirrored/padded)
    std::array<uint8_t, 0x8000> rom_{};
    // $B800-$BBFF tile map (1 KB), $BC00-$BFFF pattern RAM (1 KB)
    std::array<uint8_t, 0x400> tile_map_{};
    std::array<uint8_t, 0x400> pattern_ram_{};
    std::array<bool, 0x400> dirty_{};

    std::array<uint8_t, 8> io_ram_{};
    bool force_pattern_ = false;
    bool fd_buffer_flag_ = false;
    uint8_t fd_data_ = 0;
    uint8_t pcg_bank_ = 0;
    uint8_t border_col_ = 0;

    // Matrix keyboard: 4 rows selected via io_ram[5]
    std::array<uint8_t, 4> keys_{};

    // 3 square voices
    struct Voice {
        uint32_t count = 0;
        uint16_t period = 0;
        uint8_t val = 1;
    };
    std::array<Voice, 3> voice_{};
    uint8_t sound_control_ = 0;
    int16_t sound_out_ = 0;

    // Sound runs at XTAL/1024 ≈ 17478 Hz internally; we resample to 44100.
    double sound_phase_ = 0;
    double audio_acc_ = 0;
    static constexpr double kSoundClock = 17897725.0 / 1024.0;

    std::vector<uint32_t> framebuffer_;
    std::vector<int16_t> audio_;

    int scanline_ = 0;
};

}  // namespace dsp
