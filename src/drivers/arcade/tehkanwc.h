#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "core/machine.h"
#include "cpu/z80.h"
#include "sound/ay8910.h"
#include "sound/msm5205.h"
#include "video/gfx.h"

namespace dsp {

// Tehkan World Cup (tehkanworldcup_hw.pas)
// 3×Z80 @ 4.608 MHz, 2×AY-3-8910, MSM5205 ADPCM, trackball inputs.
class TehkanWc : public Machine {
public:
    static constexpr int kScreenWidth = 256;
    static constexpr int kScreenHeight = 224;
    static constexpr double kFramesPerSecond = 60.0;
    static constexpr int kScanlines = 256;
    static constexpr uint32_t kCpuClock = 4608000;
    static constexpr uint32_t kAyClock = 1536000;
    static constexpr uint32_t kMsmClock = 384000;

    TehkanWc();

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

    const char* title() const override { return "Tehkan World Cup"; }

private:
    uint8_t main_read(uint16_t address);
    void main_write(uint16_t address, uint8_t value);
    uint8_t sub_read(uint16_t address);
    void sub_write(uint16_t address, uint8_t value);
    uint8_t sound_read(uint16_t address);
    void sound_write(uint16_t address, uint8_t value);
    uint8_t sound_in(uint16_t port);
    void sound_out(uint16_t port, uint8_t value);

    void shared_write(uint16_t address, uint8_t value);
    void set_palette_color(int index);
    void msm_advance();

    void decode_graphics(const std::vector<uint8_t>& chars,
                         const std::vector<uint8_t>& sprites,
                         const std::vector<uint8_t>& tiles);
    void update_video();
    void draw_background();
    void draw_chars_low();
    void draw_sprites();
    void draw_chars_high();

    Z80 main_cpu_;
    Z80 sub_cpu_;
    Z80 sound_cpu_;
    AY8910 ay0_;
    AY8910 ay1_;
    MSM5205 msm_;

    std::array<uint8_t, 0x10000> memory_{};
    std::array<uint8_t, 0x10000> mem_sub_{};
    std::array<uint8_t, 0x10000> mem_snd_{};
    std::array<uint8_t, 0x800> palette_ram_{};
    std::array<uint32_t, 0x400> palette_{};

    GfxSet chars_;
    GfxSet sprites_;
    GfxSet tiles_;

    // Layer surfaces: bg 512×256, fg layers 256×256, composite
    std::vector<uint32_t> layer_bg_;
    std::vector<uint32_t> layer_fg_lo_;
    std::vector<uint32_t> layer_fg_hi_;
    std::vector<uint32_t> composite_;
    std::vector<uint32_t> framebuffer_;

    uint16_t scroll_x_ = 0;
    uint8_t scroll_y_ = 0;
    uint8_t sound_latch_ = 0;
    uint8_t sound_latch2_ = 0;
    bool sub_reset_ = true;

    // Trackball latches (digital approximation)
    uint8_t track0_[2]{};
    uint8_t track1_[2]{};
    int8_t stick0_x_ = 0, stick0_y_ = 0;
    int8_t stick1_x_ = 0, stick1_y_ = 0;

    uint8_t in0_ = 0x20;
    uint8_t in1_ = 0x20;
    uint8_t in2_ = 0x0f;
    uint8_t dsw_a_ = 0xff;
    uint8_t dsw_b_ = 0xff;
    uint8_t dsw_c_ = 0x0f;

    // MSM5205 position (shared via AY ports)
    uint16_t msm_pos_ = 0;
    int msm_data_val_ = -1;

    int64_t audio_accumulator_ = 0;
    int64_t msm_accumulator_ = 0;
    std::vector<int16_t> audio_;
};

}  // namespace dsp
