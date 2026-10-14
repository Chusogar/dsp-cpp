#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "core/machine.h"
#include "cpu/hu6280.h"
#include "cpu/m68000.h"
#include "machine/deco146.h"
#include "sound/okim6295.h"
#include "sound/ym2151.h"
#include "sound/ym2203.h"
#include "video/deco16ic.h"
#include "video/gfx.h"

namespace dsp {

// Caveman Ninja / Joe & Mac and Robocop 2 (Data East, 1991), ported from
// dsp-emulator cavemanninja_hw.pas (MAME cninja.cpp / robocop2.cpp).
//
// Shared DECO board: 68000, dual DECO 16IC tilemaps, DECO 52 sprites,
// HuC6280 + YM2203 + YM2151 + 2×OKIM6295, raster IRQ + VBLANK IRQ5.
// Caveman Ninja uses a DECO 104 and 256×240; Robocop 2 uses a DECO 146,
// 320×240, a priority register and an optional 8 bpp playfield mode.
class CavemanNinja : public Machine {
public:
    MachineType machine_type() const override { return MachineType::Arcade; }

    enum class Game { Cninja, Robocop2 };

    static constexpr uint32_t kSoundXtal = 32220000;
    static constexpr uint32_t kSoundClock = kSoundXtal / 8;
    static constexpr uint32_t kYm2151Clock = kSoundXtal / 9;
    static constexpr uint32_t kYm2203Clock = kSoundXtal / 8;
    static constexpr int kScanlines = 274;
    static constexpr double kFramesPerSecond = 58.0;
    static constexpr int kHeight = 240;
    static constexpr int kFirstLine = 8;
    static constexpr int kMaxWidth = 320;

    explicit CavemanNinja(Game game = Game::Cninja);

    bool init(const std::string& rom_path, std::string* error) override;
    void reset() override;
    void run_frame() override;

    void set_inputs(const MachineInputs& inputs) override;
    void set_dip_switch(int bank, uint8_t value) override;

    const uint32_t* framebuffer() const override { return framebuffer_.data(); }
    int screen_width() const override { return width_; }
    int screen_height() const override { return kHeight; }
    double frames_per_second() const override { return kFramesPerSecond; }

    void drain_audio(std::vector<int16_t>& out) override;
    int sample_rate() const override { return YM2151::kSampleRate; }

    const char* title() const override {
        return game_ == Game::Robocop2 ? "Robocop 2" : "Caveman Ninja";
    }

private:
    uint16_t main_read(uint32_t address);
    void main_write(uint32_t address, uint16_t value);
    uint8_t sound_read(uint32_t address);
    void sound_write(uint32_t address, uint8_t value);
    void on_sound_cycles(int cycles);
    void update_palette_entry(int index);
    void draw_sprites(int priority);
    void update_video();
    void update_video_cninja();
    void update_video_robocop2();
    void configure_tilegens();
    void apply_robocop2_priority();
    uint16_t prot_address(uint32_t real_address) const;

    Game game_;
    int width_;
    uint32_t main_clock_;

    M68000 main_cpu_;
    HuC6280 sound_cpu_{kSoundClock};
    YM2203 ym2203_{kYm2203Clock, 0.7f, 0.7f};
    YM2151 ym2151_{kYm2151Clock, 0.7f};
    OKIM6295 oki1_{kSoundXtal / 32, true};
    OKIM6295 oki2_{kSoundXtal / 16, true};
    Deco146 deco_prot_;
    Deco16ic tilegen_[2];

    std::vector<uint16_t> rom_;
    std::vector<uint8_t> sound_rom_;
    std::array<uint16_t, 0x2000> ram_{};
    std::array<uint16_t, 0x400> sprite_buf_{};
    std::array<uint16_t, 0x400> sprite_ram_{};
    std::array<uint16_t, 0x1000> pal_ram_{};
    std::array<uint32_t, 0x800> palette_{};
    std::array<uint8_t, 0x2000> sound_ram_{};

    GfxSet chars_, tiles1_, tiles2_, tiles2_8bpp_, sprites_;
    std::vector<uint32_t> screen_;
    std::vector<uint32_t> framebuffer_;

    uint16_t in0_ = 0xffff;
    uint16_t in1_ = 0x00f7;
    uint16_t dsw_ = 0x7fff;   // port C / DIP A
    uint16_t dsw_b_ = 0x00ff;  // Robocop 2 only
    uint16_t priority_ = 0;
    uint8_t irq_mask_ = 0;
    uint8_t irq_line_ = 0;
    bool raster_irq_ = false;
    bool flip_screen_ = false;
    uint8_t sound_latch_ = 0;
    uint32_t frame_counter_ = 0;
    int scanline_ = 0;

    double main_debt_ = 0, sound_debt_ = 0;
    int64_t ym_accum_ = 0, oki1_accum_ = 0, oki2_accum_ = 0, audio_accum_ = 0;
    int32_t oki1_level_ = 0, oki2_level_ = 0;
    std::vector<int16_t> audio_;
};

}  // namespace dsp
