#pragma once

#include <array>
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

// Skull & Crossbones (Atari, 1989), ported from the MAME skullxbo driver.
// Main CPU: 68000, sound: an Atari JSA II board (M6502 + YM2151 + OKIM6295).
class Skullxbo : public Machine {
public:
    // The pixel clock is the full 14.318 MHz, so the visible area is 672 pixels
    // wide: 42 of the 16 pixel wide tile columns.
    static constexpr int kScreenWidth = 672;
    static constexpr int kScreenHeight = 240;
    static constexpr double kFramesPerSecond = 59.922743;
    static constexpr int kScanlines = 262;
    static constexpr int kCpuSync = 4;
    static constexpr uint32_t kAtariClock = 14318181;
    static constexpr uint32_t kMainClock = kAtariClock / 2;
    static constexpr uint32_t kJsaClock = 3579545;
    static constexpr uint32_t kSoundClock = kJsaClock / 2;
    static constexpr uint32_t kOkiClock = kJsaClock / 3;

    Skullxbo();

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

    // Debug helpers used by the regression tests.
    uint32_t debug_pc() const { return main_cpu_.pc(); }
    int debug_palette_used() const;
    int debug_motion_object_pixels() const;

private:
    uint16_t main_read(uint32_t address);
    void main_write(uint32_t address, uint16_t value);
    uint8_t sound_read(uint16_t address);
    void sound_write(uint16_t address, uint8_t value);
    void on_sound_cycles(int cycles);

    bool load_roms(const std::string& rom_path, std::string* error);
    void set_palette(int index, uint16_t value);

    void write_xscroll(uint16_t value);
    void write_yscroll(uint16_t value);
    void set_yscroll(int value);
    void scanline_update(int scanline);
    void render_line(int line);
    void draw_motion_object_band(int line);

    void write_sound_command(uint8_t value);
    uint8_t read_sound_response();
    void write_sound_response(uint8_t value);
    uint8_t read_sound_command();
    void update_sound_irq();
    void set_sound_reset();

    static constexpr int kMoPlaneWidth = 1024;
    static constexpr int kMoPlaneHeight = 512;
    static constexpr uint16_t kMoTransparent = 0xffff;

    M68000 main_cpu_;
    M6502 sound_cpu_;
    YM2151 ym_;
    OKIM6295 oki_;

    std::vector<uint16_t> rom_;             // 0x40000 big endian words
    std::vector<uint8_t> sound_rom_;        // 0x10000 bytes of 6502 code
    std::array<uint16_t, 0x1000> playfield_ram_{};
    std::array<uint16_t, 0x1000> playfield_ext_{};
    std::array<uint16_t, 0x800> alpha_ram_{};  // 0xffc000-0xffcfff, SLIP at 0x7c0
    std::array<uint16_t, 0x800> mob_ram_{};
    std::array<uint16_t, 0x1000> ram_{};
    std::array<uint8_t, 0x800> eeprom_{};
    std::array<uint8_t, 0x2000> sound_ram_{};

    GfxSet sprites_;
    GfxSet playfield_gfx_;
    GfxSet chars_;
    std::unique_ptr<AtariMotionObjects> motion_objects_;

    std::array<uint16_t, 0x800> palette_ram_{};
    std::array<uint32_t, 0x800> palette_{};

    std::vector<uint32_t> framebuffer_;
    std::vector<uint16_t> mo_index_;     // motion object palette index, screen space
    std::vector<uint8_t> mo_priority_;

    int scanline_ = 0;
    int pf_scrollx_ = 0;
    int pf_scrolly_ = 0;
    int mo_yscroll_ = 0;
    uint16_t xscroll_reg_ = 0;
    uint16_t yscroll_reg_ = 0;
    int playfield_latch_ = -1;
    bool eeprom_unlocked_ = false;
    bool halt_until_hblank_ = false;
    int pending_scanline_irq_ = -1;

    // JSA II state.
    uint8_t sound_bank_ = 0;
    bool sound_to_main_ready_ = false;
    bool main_to_sound_ready_ = false;
    uint8_t sound_to_main_data_ = 0;
    uint8_t main_to_sound_data_ = 0;
    bool timed_int_ = false;
    bool ym_int_ = false;
    float ym_gain_ = 1.0f;
    float oki_gain_ = 1.0f;
    float ym_ct1_ = 0.0f;
    int64_t sound_irq_accumulator_ = 0;

    uint16_t in0_ = 0xffff;
    uint16_t in1_ = 0xffff;
    bool coin1_ = false, coin2_ = false;
    bool service_ = false;

    int64_t audio_accumulator_ = 0;
    int64_t oki_accumulator_ = 0;
    int32_t last_oki_ = 0;
    std::vector<int16_t> audio_;
};

}  // namespace dsp
