#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "core/machine.h"
#include "cpu/m6502.h"
#include "cpu/m6809.h"
#include "machine/taito_68705.h"
#include "sound/msm5205.h"
#include "sound/ym3812.h"
#include "video/gfx.h"

namespace dsp {

// Technos Renegade / Nekketsu Kouha Kunio-kun (1986)
// M6502 main + M6809 sound + Taito MC68705 + YM3526 + MSM5205
class Renegade : public Machine {
public:
    static constexpr int kScreenWidth = 256;
    static constexpr int kScreenHeight = 238;
    static constexpr int kScanlines = 272;
    static constexpr double kFramesPerSecond = 57.444853;
    static constexpr uint32_t kMainClock = 1500000;
    static constexpr uint32_t kSoundClock = 1500000;
    static constexpr uint32_t kMcuClock = 3000000;
    static constexpr uint32_t kYmClock = 3000000;
    // 12 MHz / 32, S48_4B
    static constexpr uint32_t kMsmClock = 12000000 / 32;

    Renegade();

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
    int sample_rate() const override { return YM3812::kSampleRate; }

    const char* title() const override { return "Renegade"; }

private:
    uint8_t main_read(uint16_t address);
    void main_write(uint16_t address, uint8_t value);
    uint8_t sound_read(uint16_t address);
    void sound_write(uint16_t address, uint8_t value);
    void update_palette(uint8_t index);
    void update_video();
    void msm_vclk();
    void on_sound_cycles(int cycles);

    M6502 main_cpu_;
    M6809 sound_cpu_;
    Taito68705 mcu_;
    YM3812 ym_;
    MSM5205 msm_;

    std::array<uint8_t, 0x10000> memory_{};
    std::array<std::array<uint8_t, 0x4000>, 2> rom_bank_{};
    std::array<uint8_t, 0x10000> sound_mem_{};
    std::array<uint8_t, 0x200> palette_ram_{};

    GfxSet chars_;
    GfxSet tiles_;
    GfxSet sprites_;

    std::array<uint32_t, 256> palette_{};
    std::vector<uint32_t> bg_layer_;   // 1024 x 256
    std::vector<uint32_t> fg_layer_;   // 256 x 256
    std::vector<uint32_t> sprite_layer_;
    std::vector<uint32_t> framebuffer_;

    uint8_t in0_ = 0xff;
    uint8_t in1_ = 0xff;
    uint8_t dswa_ = 0xbf;
    uint8_t dswb_ = 0x8f;
    uint8_t rom_bank_sel_ = 0;
    uint8_t sound_latch_ = 0;
    uint16_t scroll_x_ = 0;
    int scroll_comp_ = 256;

    int64_t audio_accum_ = 0;
    std::vector<int16_t> audio_;
};

}  // namespace dsp
