#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "core/machine.h"
#include "cpu/m6809.h"
#include "cpu/z80.h"
#include "sound/ym2203.h"
#include "video/gfx.h"

namespace dsp {

// Ghosts'n Goblins (Capcom, 1985) — port of gng_hw.pas
// Main: M6809 @ 6 MHz. Sound: Z80 @ 3 MHz + 2× YM2203 @ 1.5 MHz.
// Screen 256×224 (crop y=16), ~59.59 Hz, 262 scanlines.
class Gng : public Machine {
public:
    static constexpr int kScreenWidth = 256;
    static constexpr int kScreenHeight = 224;
    static constexpr double kFramesPerSecond = 12000000.0 / 2.0 / 384.0 / 262.0;  // ~59.59
    static constexpr int kScanlines = 262;
    static constexpr uint32_t kMainClock = 6000000;
    static constexpr uint32_t kSoundClock = 3000000;
    static constexpr uint32_t kYmClock = 1500000;

    Gng();

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

    const char* title() const override { return "Ghosts'n Goblins"; }

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

    M6809 main_cpu_{kMainClock};
    Z80 sound_cpu_{kSoundClock};
    YM2203 ym0_{kYmClock, 1.0f, 1.0f};
    YM2203 ym1_{kYmClock, 1.0f, 1.0f};

    std::array<uint8_t, 0x10000> memory_{};
    std::array<uint8_t, 0x10000> mem_snd_{};
    std::array<std::array<uint8_t, 0x2000>, 5> bank_rom_{};
    std::array<uint8_t, 0x200> palette_ram_{};
    std::array<uint32_t, 0x100> palette_{};
    std::array<uint8_t, 0x200> sprite_buffer_{};

    GfxSet chars_;
    GfxSet tiles_;
    GfxSet sprites_;
    std::vector<uint8_t> gfx_char_;
    std::vector<uint8_t> gfx_tiles_;
    std::vector<uint8_t> gfx_sprites_;

    // Layer canvases: BG/FG 512×512, char 256×256, composite 256×256
    std::vector<uint32_t> layer_bg_;
    std::vector<uint32_t> layer_fg_;
    std::vector<uint32_t> layer_char_;
    std::vector<uint32_t> composite_;
    std::vector<uint32_t> framebuffer_;
    std::vector<int16_t> audio_buffer_;

    std::array<bool, 0x400> bg_dirty_{};
    std::array<bool, 0x400> fg_dirty_{};
    std::array<bool, 32> color_dirty_{};

    uint8_t bank_ = 0;
    uint8_t soundlatch_ = 0;
    uint16_t scroll_x_ = 0;
    uint16_t scroll_y_ = 0;
    bool flip_screen_ = false;

    uint8_t in0_ = 0xff;
    uint8_t in1_ = 0xff;
    uint8_t in2_ = 0xff;
    uint8_t dsw_a_ = 0xff;
    uint8_t dsw_b_ = 0xff;

    double frame_main_ = 0;
    double frame_snd_ = 0;
    int sound_irq_accum_ = 0;
};

}  // namespace dsp
