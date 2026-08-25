#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "core/machine.h"
#include "cpu/m6502.h"
#include "cpu/m68000.h"
#include "sound/okim6295.h"
#include "sound/ym2151.h"
#include "video/atari_mo.h"
#include "video/gfx.h"

namespace dsp {

// Atari Skull & Crossbones (1989), from MAME src/mame/atari/skullxbo.cpp.
// 68000 + Atari JSA-II (6502, YM2151, OKI M6295), playfield + alpha + motion
// objects. There is no Pascal unit for this board in dsp-emulator.
class SkullXbo : public Machine {
public:
    static constexpr int kScreenWidth = 336;
    static constexpr int kScreenHeight = 240;
    static constexpr int kScanlines = 262;
    static constexpr int kVBlankLine = 240;
    static constexpr double kFramesPerSecond = 59.922743;
    static constexpr uint32_t kAtariClock = 14318180;
    static constexpr uint32_t kMainClock = kAtariClock / 2;
    static constexpr uint32_t kJsaClock = 3579545;
    static constexpr uint32_t kSoundClock = kJsaClock / 2;
    static constexpr uint32_t kOkiClock = kJsaClock / 3;
    static constexpr int kCpuSync = 8;

    SkullXbo();

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

    const char* title() const override { return "Skull & Crossbones"; }

    uint32_t debug_pc() const { return main_cpu_.pc(); }
    uint8_t debug_ipl() const { return main_cpu_.cc.im; }

private:
    uint16_t main_read(uint32_t address);
    void main_write(uint32_t address, uint16_t value);
    uint8_t main_read_byte(uint32_t address);
    void main_write_byte(uint32_t address, uint8_t value);
    uint8_t sound_read(uint16_t address);
    void sound_write(uint16_t address, uint8_t value);
    void on_sound_cycles(int cycles);
    void update_sound_irq();
    void update_volumes();
    size_t alpha_index(uint32_t address) const;

    bool load_roms(const std::string& rom_path, std::string* error);
    void decode_graphics();
    void init_blank_eeprom();
    uint8_t eeprom_read(uint32_t address) const;
    void eeprom_write(uint32_t address, uint8_t value);
    void set_palette(int index, uint16_t value);
    void scanline_update(int scanline);
    void render_frame();
    uint32_t pal_color(int index) const;
    void pump_sound(int slices);

    M68000 main_cpu_;
    M6502 sound_cpu_;
    YM2151 ym_;
    OKIM6295 oki_;

    std::array<uint16_t, 0x40000> rom_{};
    std::array<uint16_t, 0x800> playfield_{};
    std::array<uint16_t, 0x800> playfield_ext_{};
    std::array<uint16_t, 0x7c0> alpha_{};
    std::array<uint16_t, 0x40> slip_ram_{};
    std::array<uint16_t, 0x800> sprite_ram_{};
    std::array<uint16_t, 0x1000> work_ram_{};
    std::array<uint16_t, 0x800> palette_ram_{};
    std::array<uint32_t, 0x800> palette_{};
    std::array<uint8_t, 0x800> eeprom_{};
    std::array<uint8_t, 0x10000> sound_rom_{};
    std::array<uint8_t, 0x2000> sound_ram_{};

    std::vector<uint8_t> playfield_rom_;
    std::vector<uint8_t> sprite_rom_;
    std::vector<uint8_t> char_rom_;
    std::vector<uint8_t> oki_rom_;

    GfxSet playfield_gfx_;
    GfxSet alpha_gfx_;
    GfxSet sprite_gfx_;
    std::unique_ptr<AtariMotionObjects> motion_objects_;

    uint16_t xscroll_ = 0;
    uint16_t yscroll_ = 0;
    int playfield_latch_ = -1;
    int mob_bank_ = 0;
    bool eeprom_unlocked_ = false;

    uint16_t in0_ = 0xffff;
    uint16_t in1_ = 0xffff;
    uint8_t coin_bits_ = 0;
    bool service_ = false;

    uint8_t main_to_sound_data_ = 0;
    uint8_t sound_to_main_data_ = 0;
    bool main_to_sound_ready_ = false;
    bool sound_to_main_ready_ = false;
    bool timed_int_ = false;
    bool ym_int_ = false;
    uint8_t sound_bank_ = 0;
    uint8_t wrio_ = 0xff;
    uint8_t mix_ = 0x0e;
    uint8_t ym_ct_ = 0;
    float ym_gain_ = 1.0f;
    float oki_gain_ = 0.0f;

    int irq1_line_ = -1;
    bool vblank_ = false;

    std::vector<uint16_t> pf_index_;
    std::vector<uint16_t> mo_index_;
    std::vector<uint32_t> framebuffer_;
    std::vector<int16_t> audio_;
    int64_t audio_accumulator_ = 0;
    int64_t oki_accumulator_ = 0;
    int32_t last_oki_ = 0;
    int main_cycles_per_line_ = 0;
    int sound_cycles_per_line_ = 0;
    int sound_irq_lines_ = 0;
};

}  // namespace dsp
