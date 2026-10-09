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

namespace dsp {

// Block Out (Technos / California Dreams, 1989) — port of dsp-emulator
// blockout_hw.pas, with the bitmap layout, palette and interrupt timing of
// MAME's blockout.cpp.
//
// 68000 @ 10 MHz drawing into two 512x256 8-bit bitmaps (front over back,
// 256 colours each) plus a 1-bit overlay with its own colour; the sound board
// is a Z80 @ 3.58 MHz with a YM2151 and an OKI MSM6295.
class BlockOut : public Machine {
public:
    MachineType machine_type() const override { return MachineType::Arcade; }
    static constexpr int kScreenWidth = 320;
    static constexpr int kScreenHeight = 240;
    static constexpr int kScanlines = 272;
    static constexpr int kFirstVisibleLine = 10;
    static constexpr double kFramesPerSecond = 28000000.0 / 4.0 / 448.0 / 272.0;  // 57.44 Hz
    static constexpr uint32_t kMainClock = 10000000;
    static constexpr uint32_t kSoundClock = 3579545;
    static constexpr uint32_t kOkiClock = 1056000;

    BlockOut();

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

    const char* title() const override { return "Block Out"; }

    // Test hooks.
    uint32_t main_pc() const { return main_cpu_.pc(); }
    int sound_commands() const { return sound_commands_; }

private:
    uint8_t read8(uint32_t address);
    void write8(uint32_t address, uint8_t value);
    uint16_t read16(uint32_t address);
    void write16(uint32_t address, uint16_t value);
    void write_videoram(uint32_t offset, uint8_t value);
    void set_color(int index, uint16_t raw);
    uint8_t sound_read(uint16_t address);
    void sound_write(uint16_t address, uint8_t value);
    void on_sound_cycles(int cycles);
    void update_video();

    M68000 main_cpu_{kMainClock};
    Z80 sound_cpu_{kSoundClock};
    YM2151 ym_{kSoundClock};
    OKIM6295 oki_{kOkiClock, /*pin7_high=*/true};

    std::vector<uint8_t> rom_;                  // 256 KB, big-endian
    std::vector<uint8_t> videoram_;             // 180000: front 0-1ffff, back 20000-3ffff
    std::vector<uint16_t> bitmap_;              // 512x256 resolved pens (front or 0x100|back)
    std::array<uint8_t, 0xc000> ram1_{};        // 1d4000-1dffff
    std::array<uint8_t, 0xc000> ram2_{};        // 1f4000-1fffff
    std::array<uint8_t, 0x8000> front_ram_{};   // 200000-207fff (1-bit overlay)
    std::array<uint8_t, 0x18000> ram3_{};       // 208000-21ffff
    std::array<uint8_t, 0x400> pal_ram_{};      // 280200-2805ff
    uint16_t front_color_ = 0;                  // 280002
    std::array<uint32_t, 513> palette_{};
    std::array<uint8_t, 0x8000> sound_rom_{};
    std::array<uint8_t, 0x800> sound_ram_{};
    std::vector<uint32_t> framebuffer_;

    uint8_t soundlatch_ = 0;
    uint8_t p1_ = 0xff, p2_ = 0xff, system_ = 0xff;
    uint8_t dsw1_ = 0xff, dsw2_ = 0xff, buttons_a_ = 0xc0;

    double main_debt_ = 0, sound_debt_ = 0;
    int64_t audio_accum_ = 0, oki_accum_ = 0;
    int32_t last_oki_ = 0;
    std::vector<int16_t> audio_;
    int sound_commands_ = 0;
};

}  // namespace dsp
