#pragma once

#include "core/machine.h"
#include "cpu/m6809.h"
#include "sound/ay8910.h"
#include "sound/ym2203.h"
#include "video/gfx.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace dsp {

// Jaleco City Connection (cityconnection_hw.pas).
// Dual MC6809, AY-3-8910 + YM2203, scrolling BG + line-coloured FG + sprites.
class CityCon : public Machine {
public:
    static constexpr int kScreenWidth = 240;
    static constexpr int kScreenHeight = 224;
    static constexpr double kFramesPerSecond = 59.637405;
    static constexpr int kScanlines = 262;
    static constexpr uint32_t kMainClock = 8000000;
    static constexpr uint32_t kSoundClock = 20000000 / 28;  // ~714286
    static constexpr uint32_t kAyClock = 20000000 / 16;     // 1.25 MHz
    static constexpr uint32_t kYmClock = 20000000 / 16;

    CityCon();

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
    int sample_rate() const override { return YM2203::kSampleRate; }

    const char* title() const override { return "City Connection"; }

private:
    uint8_t main_read(uint16_t address);
    void main_write(uint16_t address, uint8_t value);
    uint8_t sound_read(uint16_t address);
    void sound_write(uint16_t address, uint8_t value);
    void on_sound_cycles(int cycles);

    bool load_roms(const std::string& rom_path, std::string* error);
    void decode_graphics();
    void set_color(int index);

    void update_video();
    void draw_background();
    void draw_foreground();
    void draw_sprites();

    M6809 main_cpu_;
    M6809 sound_cpu_;
    YM2203 ym_;
    AY8910 ay_;

    std::array<uint8_t, 0x10000> memory_{};
    std::array<uint8_t, 0x10000> mem_snd_{};
    std::array<uint8_t, 0xe000> memoria_fondo_{};
    std::array<uint8_t, 0x100> lines_color_{};
    std::array<uint8_t, 0x800> palette_ram_{};
    std::array<uint32_t, 0x400> palette_{};

    GfxSet chars_;
    GfxSet tiles_;
    GfxSet sprites_;

    std::vector<uint8_t> gfx_char_;
    std::vector<uint8_t> gfx_tiles_;
    std::vector<uint8_t> gfx_sprites_;

    // Work surfaces: bg 1024x256, fg 1024x256, composite 256x256
    std::vector<uint32_t> layer_bg_;
    std::vector<uint32_t> layer_fg_;
    std::vector<uint32_t> composite_;
    std::vector<uint32_t> framebuffer_;

    uint8_t fondo_ = 0;
    uint8_t soundlatch_ = 0;
    uint8_t soundlatch2_ = 0;
    uint16_t scroll_x_ = 0;
    bool bg_dirty_ = true;
    bool flip_screen_ = false;

    uint8_t in0_ = 0xff;
    uint8_t in1_ = 0x80;
    uint8_t in2_ = 0xff;
    uint8_t dsw_a_ = 0x00;
    uint8_t dsw_b_ = 0x80;

    int64_t audio_accum_ = 0;
    std::vector<int16_t> audio_;
};

}  // namespace dsp
