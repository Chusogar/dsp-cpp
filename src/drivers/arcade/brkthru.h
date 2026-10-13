#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "core/machine.h"
#include "cpu/m6809.h"
#include "sound/ym2203.h"
#include "sound/ym3812.h"
#include "video/gfx.h"

namespace dsp {

// Break Thru (Data East, 1986) and Darwin 4078 (Data East, 1986), ported
// from dsp-emulator breakthru_hw.pas (MAME brkthru.cpp).
//
// A 6809 at 1.5 MHz with eight banks of 8 KB ROM, a 32x32 text layer, a
// 512x256 layer of 16x16 tiles (3 bpp, 8 groups of 128 tiles, two of them in
// the scrolling window) scrolled horizontally, and 64 sprites of 16x16 or
// 16x32 with a priority bit that puts them behind the tile layer. 256 colours
// from two PROMs. A second 6809 plays a YM2203 and a YM3526; it takes its
// commands through a latch and NMI. The coins raise an IRQ on the main CPU.
// Darwin 4078 moves the I/O around and is played on a vertical monitor.
class BreakThru : public Machine {
public:
    MachineType machine_type() const override { return MachineType::Arcade; }
    enum class Game { BreakThru, Darwin };

    static constexpr uint32_t kMainClock = 12000000 / 8;
    static constexpr uint32_t kSoundClock = 12000000 / 8;  // MC6809: 6 MHz / 4
    static constexpr int kScanlines = 272;
    static constexpr double kFramesPerSecond = 57.444885;
    static constexpr int kSize = 240;  // 240x240 visible area

    explicit BreakThru(Game game = Game::BreakThru);

    bool init(const std::string& rom_path, std::string* error) override;
    void reset() override;
    void run_frame() override;

    void set_inputs(const MachineInputs& inputs) override;
    void set_dip_switch(int bank, uint8_t value) override;

    const uint32_t* framebuffer() const override { return framebuffer_.data(); }
    int screen_width() const override { return kSize; }
    int screen_height() const override { return kSize; }
    double frames_per_second() const override { return kFramesPerSecond; }

    void drain_audio(std::vector<int16_t>& out) override;
    int sample_rate() const override { return YM2203::kSampleRate; }

    const char* title() const override {
        return game_ == Game::Darwin ? "Darwin 4078" : "Break Thru";
    }

    // Test hooks.
    uint16_t main_pc() const { return main_cpu_.pc(); }
    int sound_commands() const { return sound_commands_; }

private:
    uint8_t read_common(uint16_t address);
    void write_common(uint16_t address, uint8_t value);
    void write_control(uint8_t value);
    uint8_t main_read(uint16_t address);
    void main_write(uint16_t address, uint8_t value);
    uint8_t sound_read(uint16_t address);
    void sound_write(uint16_t address, uint8_t value);
    void on_sound_cycles(int cycles);
    void draw_sprites(int priority);
    void update_video();

    Game game_;
    M6809 main_cpu_{kMainClock};
    M6809 sound_cpu_{kSoundClock};
    YM2203 ym2203_{1500000, 1.0f, 0.5f};
    YM3812 ym3526_{3000000, YM3812::kYM3526};

    std::vector<uint8_t> rom_;    // 4000-ffff
    std::vector<uint8_t> banks_;  // 8 x 8 KB at 2000-3fff
    std::array<uint8_t, 0x10000> sound_mem_{};
    std::array<uint8_t, 0x1800> ram_{};  // 0400-0bff and 1100-17ff
    std::array<uint8_t, 0x400> bg_video_{}, fg_video_{};
    std::array<uint8_t, 0x100> sprite_ram_{};
    std::array<uint32_t, 0x100> palette_{};

    GfxSet chars_, tiles_, sprites_;
    std::vector<uint32_t> screen_;  // 256x256
    std::vector<uint32_t> framebuffer_;

    uint8_t in0_ = 0xff, in1_ = 0xff, in2_ = 0xe0, dsw_a_ = 0x3f, dsw_b_ = 0x1f;
    uint8_t sound_latch_ = 0, rom_bank_ = 0, bg_color_ = 0;
    uint16_t scroll_x_ = 0;
    bool nmi_enabled_ = true;
    bool old_coin1_ = false, old_coin2_ = false;
    double main_debt_ = 0, sound_debt_ = 0;
    int64_t audio_accum_ = 0;
    int sound_commands_ = 0;
    std::vector<int16_t> audio_;
};

}  // namespace dsp
