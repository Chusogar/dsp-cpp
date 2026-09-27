#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "core/machine.h"
#include "cpu/m6502.h"
#include "cpu/m68000.h"
#include "sound/ym2151.h"
#include "video/atari_mo.h"
#include "video/gfx.h"

namespace dsp {

// Bad Lands (Atari Games, 1989), ported from badlands_hw.pas.
// 68000 @ 7.159 MHz, M6502 + YM2151 sound and Atari split motion objects.
class BadLands : public Machine {
public:
    static constexpr int kScreenWidth = 336;
    static constexpr int kScreenHeight = 240;
    static constexpr int kScanlines = 262;
    static constexpr int kCpuSync = 4;
    static constexpr double kFramesPerSecond = 59.922743;
    static constexpr uint32_t kAtariClock = 14318180;
    static constexpr uint32_t kMainClock = kAtariClock / 2;
    static constexpr uint32_t kSoundClock = kAtariClock / 8;
    static constexpr uint32_t kYmClock = kAtariClock / 4;

    BadLands();
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
    const char* title() const override { return "Bad Lands"; }

    uint32_t debug_main_pc() const { return main_cpu_.pc(); }
    uint16_t debug_sound_pc() const { return sound_cpu_.pc(); }

private:
    bool load_roms(const std::string& path, std::string* error);
    uint16_t main_read(uint32_t address);
    void main_write(uint32_t address, uint16_t value);
    uint8_t sound_read(uint16_t address);
    void sound_write(uint16_t address, uint8_t value);
    void on_sound_cycles(int cycles);
    void update_sound_irq();
    void set_palette(int index, uint16_t value);
    void update_video();
    void draw_motion_objects(int priority);

    M68000 main_cpu_;
    M6502 sound_cpu_;
    YM2151 ym_;
    std::vector<uint16_t> main_rom_;
    std::array<uint8_t, 0x10000> sound_memory_{};
    std::array<std::array<uint8_t, 0x1000>, 4> sound_banks_{};
    std::array<uint16_t, 0x1000> ram_{};
    std::array<uint8_t, 0x1000> eeprom_{};
    std::array<uint16_t, 0x200> palette_ram_{};
    std::array<uint32_t, 0x100> palette_{};
    GfxSet playfield_gfx_;
    GfxSet sprite_gfx_;
    std::unique_ptr<AtariMotionObjects> motion_objects_;
    std::vector<uint16_t> playfield_pixels_;
    std::vector<uint32_t> framebuffer_;

    uint8_t sound_bank_ = 0;
    uint8_t playfield_bank_ = 0;
    uint8_t sound_latch_ = 0;
    uint8_t main_latch_ = 0;
    bool eeprom_unlocked_ = false;
    bool main_pending_ = false;
    bool sound_pending_ = false;
    bool sound_halted_ = false;
    bool ym_irq_ = false;
    bool timed_irq_ = false;
    uint16_t main_inputs_ = 0xffbf;
    uint8_t sound_inputs_ = 0;
    uint8_t pedal1_ = 0x80;
    uint8_t pedal2_ = 0x80;
    uint8_t steering1_ = 0x80;
    uint8_t steering2_ = 0x80;
    bool pedal1_pressed_ = false;
    bool pedal2_pressed_ = false;
    bool service_ = false;
    int64_t audio_accumulator_ = 0;
    std::vector<int16_t> audio_;
};

}  // namespace dsp
