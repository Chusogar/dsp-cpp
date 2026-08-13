#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "core/machine.h"
#include "cpu/m6502.h"
#include "cpu/m68000.h"
#include "machine/slapstic.h"
#include "sound/pokey.h"
#include "sound/ym2151.h"
#include "sound/tms5220.h"
#include "video/atari_mo.h"
#include "video/gfx.h"

namespace dsp {

// Gauntlet (Atari, 1985), ported from gauntlet_hw.pas.
// Main CPU: 68010 behind a SLAPSTIC 107, sound CPU: M6502 with a YM2151 and a POKEY.
class Gauntlet : public Machine {
public:
    static constexpr int kScreenWidth = 336;
    static constexpr int kScreenHeight = 240;
    static constexpr double kFramesPerSecond = 59.922743;
    static constexpr int kScanlines = 262;
    static constexpr int kCpuSync = 4;
    static constexpr uint32_t kAtariClock = 14318180;
    static constexpr uint32_t kMainClock = kAtariClock / 2;
    static constexpr uint32_t kSoundClock = kAtariClock / 8;
    static constexpr uint32_t kYmClock = kAtariClock / 4;
    static constexpr uint32_t kPokeyClock = kAtariClock / 8;

    Gauntlet();

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

    const char* title() const override { return "Gauntlet"; }

private:
    uint16_t main_read(uint32_t address);
    void main_write(uint32_t address, uint16_t value);
    uint8_t sound_read(uint16_t address);
    void sound_write(uint16_t address, uint8_t value);
    void on_sound_cycles(int cycles);
    void set_sound_reset(uint16_t value);

    bool load_roms(const std::string& rom_path, std::string* error);
    void decode_graphics(const std::vector<uint8_t>& char_rom, std::vector<uint8_t>& tile_rom);
    void set_palette(int index, uint16_t value);
    void update_video();
    void draw_char(int offset);
    void draw_tile(int offset);

    static constexpr int kCharPlaneWidth = 512;
    static constexpr int kCharPlaneHeight = 256;
    static constexpr int kTilePlaneWidth = 512;
    static constexpr int kTilePlaneHeight = 512;
    static constexpr int16_t kTransparent = -1;

    M68000 main_cpu_;
    M6502 sound_cpu_;
    YM2151 ym_;
    Pokey pokey_;
    Tms5220 tms_;
    Slapstic slapstic_;

    std::vector<uint16_t> rom_;                       // 0x40000 big endian words
    std::array<std::array<uint16_t, 0x1000>, 4> slapstic_rom_{};
    std::array<uint16_t, 0x1000> ram_{};              // 0x800000-0x801fff
    std::array<uint16_t, 0x3000> ram2_{};             // 0x900000-0x905fff
    std::array<uint8_t, 0x800> eeprom_{};
    std::array<uint8_t, 0x10000> sound_memory_{};

    GfxSet chars_;
    GfxSet tiles_;
    std::unique_ptr<AtariMotionObjects> motion_objects_;

    std::array<uint16_t, 0x400> palette_ram_{};
    std::array<uint32_t, 0x400> palette_{};

    std::vector<int16_t> char_back_, char_front_;
    std::vector<int16_t> tile_back_, tile_front_;
    std::vector<uint32_t> composite_;
    std::vector<uint32_t> framebuffer_;
    std::array<bool, 0x800> char_dirty_{};
    std::array<bool, 0x1000> tile_dirty_{};
    uint8_t tile_bank_ = 0;

    uint8_t rom_bank_ = 0;
    uint16_t scroll_x_ = 0;
    uint16_t sound_reset_value_ = 1;
    bool sound_cpu_halted_ = false;
    bool write_eeprom_ = false;
    bool sound_to_main_ready_ = false;
    bool main_to_sound_ready_ = false;
    uint8_t sound_to_main_data_ = 0;
    uint8_t main_to_sound_data_ = 0;
    uint8_t soundctl_ = 0xff;  // LS259 Q0..Q7 (active handling on bit7 writes)
    float ym_gain_ = 1.0f, pokey_gain_ = 1.0f;
    uint8_t vblank_ = 0x40;

    uint16_t in0_ = 0xffff;
    uint16_t in1_ = 0xffff;
    uint8_t in2_ = 0xff;
    uint8_t dsw_a_ = 0x08;

    int64_t audio_accumulator_ = 0;
    std::vector<int16_t> audio_;
};

}  // namespace dsp
