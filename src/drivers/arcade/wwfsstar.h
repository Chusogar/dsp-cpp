#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "core/machine.h"
#include "cpu/m68000.h"
#include "cpu/z80.h"
#include "sound/okim6295.h"
#include "sound/ym2151.h"
#include "video/gfx.h"

namespace dsp {

// Technos WWF Superstars (1989), ported from
// https://github.com/leniad/dsp-emulator/blob/master/src/arcade/wwfsuperstars_hw.pas
//
//   M68000 @ 10 MHz  – main CPU
//   Z80    @ 3.579545 MHz – sound CPU
//   YM2151 @ 3.579545 MHz
//   OKI6295 @ 1.056 MHz (pin 7 high)
//
// Layers: 16x16 background (scrollable), 16x16 sprites (16x16 / 16x32),
// 8x8 text/foreground. Visible area 256x240 @ ~57.44 Hz, 272 scanlines.
class Wwfsstar : public Machine {
public:
    static constexpr int kScreenWidth = 256;
    static constexpr int kScreenHeight = 240;
    static constexpr int kScanlines = 272;
    static constexpr int kVBlankLine = 240;
    static constexpr double kFramesPerSecond = 57.444853;
    static constexpr uint32_t kMainClock = 10000000;
    static constexpr uint32_t kSoundClock = 3579545;
    static constexpr uint32_t kYmClock = 3579545;
    static constexpr uint32_t kOkiClock = 1056000;

    Wwfsstar();

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
    int sample_rate() const override { return YM2151::kSampleRate; }

    const char* title() const override { return "WWF Superstars"; }

private:
    uint16_t main_read(uint32_t address);
    void main_write(uint32_t address, uint16_t value);
    uint8_t sound_read(uint16_t address);
    void sound_write(uint16_t address, uint8_t value);

    bool load_roms(const std::string& rom_path, std::string* error);
    void decode_graphics(const std::vector<uint8_t>& chars, const std::vector<uint8_t>& bg,
                         const std::vector<uint8_t>& sprites);
    void write_palette(int index, uint16_t data);
    void update_video();
    void draw_bg();
    void draw_fg();
    void draw_sprites();
    void blit_tile(const GfxSet& set, int code, int color_base, int dx, int dy, bool flipx,
                   bool flipy, bool transparent);

    uint8_t p1_inputs() const;
    uint8_t p2_inputs() const;
    uint8_t system_inputs() const;

    M68000 main_cpu_{kMainClock};
    Z80 sound_cpu_{kSoundClock};
    YM2151 ym_{kYmClock};
    OKIM6295 oki_{kOkiClock, /*pin7_high=*/true};

    std::vector<uint16_t> rom_;
    std::array<uint16_t, 0x2000> ram_{};
    std::array<uint16_t, 0x800> fg_ram_{};
    std::array<uint16_t, 0x800> bg_ram_{};
    std::array<uint16_t, 0x200> sprite_ram_{};
    std::array<uint16_t, 0x800> palette_ram_{};
    std::array<uint8_t, 0x8800> sound_mem_{};

    GfxSet chars_;
    GfxSet bg_tiles_;
    GfxSet sprites_;

    std::vector<uint32_t> framebuffer_;
    std::array<uint32_t, 0x200> palette_rgb_{};

    uint16_t scroll_x_ = 0;
    uint16_t scroll_y_ = 0;
    uint8_t sound_latch_ = 0;
    bool flip_screen_ = false;
    bool vblank_ = false;

    uint8_t dsw_a_ = 0xff;
    uint8_t dsw_b_ = 0xff;
    MachineInputs inputs_{};

    std::vector<int16_t> audio_buf_;
    int64_t audio_acc_ = 0;
};


}  // namespace dsp
