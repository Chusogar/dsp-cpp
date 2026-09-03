#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "core/machine.h"
#include "cpu/m6502.h"
#include "video/dvg.h"

namespace dsp {

// Atari Asteroids (1979): MOS 6502 + DVG, port of asteroids_hw.pas (tipo 23).
// Visible vector window 400×320 at 12096000/4096/12/4 Hz (~61.52).
class Asteroid : public Machine {
public:
    static constexpr int kScreenWidth = 400;
    static constexpr int kScreenHeight = 320;
    static constexpr uint32_t kMasterClock = 12096000;
    static constexpr uint32_t kCpuClock = 1512000;
    static constexpr int kScanlines = 300;
    static constexpr double kFramesPerSecond = double(kMasterClock) / 4096.0 / 12.0 / 4.0;
    static constexpr int kNmiPeriod = 6144;  // 1512000 / (12096000/4096/12)
    static constexpr int kSampleRate = 44100;

    Asteroid();

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

    const char* title() const override { return "Asteroids"; }

    uint16_t debug_pc() const { return cpu_.pc(); }
    size_t debug_dvg_lines() const { return dvg_.lines().size(); }

private:
    uint8_t read_byte(uint16_t address);
    void write_byte(uint16_t address, uint8_t value);
    void on_cycles(int cycles);
    void update_video();
    void draw_line(int x0, int y0, int x1, int y1, int intensity);
    void mix_audio(int cpu_cycles);

    M6502 cpu_{kCpuClock};
    Dvg dvg_{0x4000, 40};

    std::array<uint8_t, 0x8000> memory_{};
    std::array<uint8_t, 0x100> ram_[2]{};
    uint8_t ram_bank_ = 0;

    uint8_t in0_ = 0;
    uint8_t in1_ = 0;
    uint8_t dsw_ = 0x84;

    uint64_t total_cycles_ = 0;
    uint64_t next_nmi_ = kNmiPeriod;
    double leftover_ = 0;

    uint8_t explode_ = 0;
    uint8_t thump_ = 0;
    std::array<bool, 8> sound_bit_{};
    uint32_t noise_lfsr_ = 1;
    std::array<uint32_t, 8> tone_phase_{};
    int explode_remain_ = 0;
    int shot_remain_ = 0;
    int64_t audio_cycle_accum_ = 0;

    std::vector<uint32_t> framebuffer_;
    std::vector<int16_t> audio_;
};

}  // namespace dsp
