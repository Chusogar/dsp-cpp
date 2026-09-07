#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "video/gfx.h"
#include "core/machine.h"
#include "cpu/z80.h"
#include "sound/ay8910.h"

namespace dsp {

// Ambush (Tehkan, 1983) — port of dsp-emulator ambush_hw.pas
class Ambush : public Machine {
public:
    static constexpr int kScreenWidth = 256;
    static constexpr int kScreenHeight = 224;
    static constexpr int kScanlines = 264;
    static constexpr double kFramesPerSecond = 60.60606060;  // ~264 * ~60
    static constexpr uint32_t kCpuClock = 18432000 / 6;       // 3_072_000
    static constexpr uint32_t kAyClock = kCpuClock / 2;       // 1_536_000

    Ambush();

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
    int sample_rate() const override { return AY8910::kSampleRate; }

    const char* title() const override { return "Ambush"; }

private:
    uint8_t main_read(uint16_t address);
    void main_write(uint16_t address, uint8_t value);
    uint8_t main_in(uint16_t port);
    void main_out(uint16_t port, uint8_t value);
    void update_video();
    void on_cycles(int cycles);

    Z80 cpu_;
    AY8910 ay0_;
    AY8910 ay1_;

    std::array<uint8_t, 0x10000> memory_{};
    GfxSet chars_;    // 8x8, 0x400 tiles
    GfxSet sprites_;  // 16x16, 0x100 sprites
    std::array<uint32_t, 256> palette_{};

    std::array<uint32_t, 256 * 256> layer_bg_{};
    std::array<uint32_t, 256 * 256> layer_pri_{};
    std::array<uint32_t, 256 * 256> layer_spr_{};
    std::vector<uint32_t> framebuffer_;

    std::array<uint16_t, 32> scroll_y_{};
    uint8_t color_bank_ = 0;
    bool flip_screen_ = false;

    uint8_t in0_ = 0xff;
    uint8_t in1_ = 0xff;
    uint8_t dsw_ = 0xc4;

    int64_t audio_accum_ = 0;
    std::vector<int16_t> audio_;
};

}  // namespace dsp
