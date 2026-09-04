#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "core/machine.h"
#include "cpu/m68000.h"
#include "sound/okim6295.h"
#include "video/atari_mo.h"
#include "video/gfx.h"

namespace dsp {

// Shuuz (Atari Games, 1990), ported from the MAME shuuz driver.
// 68000 @ 7.159 MHz, Atari VAD playfield + motion objects, OKIM6295 only.
class Shuuz : public Machine {
public:
    static constexpr int kScreenWidth = 336;
    static constexpr int kScreenHeight = 240;
    static constexpr int kScanlines = 262;
    static constexpr int kCpuSync = 4;
    static constexpr double kFramesPerSecond = 59.922743;
    static constexpr uint32_t kAtariClock = 14318181;
    static constexpr uint32_t kMainClock = kAtariClock / 2;
    static constexpr uint32_t kOkiClock = kAtariClock / 16;
    static constexpr int kSampleRate = 44100;

    Shuuz();

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
    int sample_rate() const override { return kSampleRate; }

    const char* title() const override { return "Shuuz"; }
    bool uses_pointer() const override { return true; }

    uint32_t debug_pc() const { return main_cpu_.pc(); }
    int debug_palette_used() const;
    int debug_motion_object_pixels() const;
    uint16_t debug_palette_word(int index) const { return palette_ram_[size_t(index) & 0x3ff]; }
    uint32_t debug_palette_rgb(int index) const { return palette_[size_t(index) & 0x3ff]; }
    uint16_t debug_pf_word(int index) const { return playfield_[size_t(index) & 0xfff]; }
    uint16_t debug_pf_ext(int index) const { return playfield_ext_[size_t(index) & 0xfff]; }
    int debug_pf_scrollx() const { return int(pf0_xscroll_raw_ + (pf1_xscroll_raw_ & 7)); }
    int debug_pf_scrolly() const { return int(pf0_yscroll_); }

private:
    uint16_t main_read(uint32_t address);
    void main_write(uint32_t address, uint16_t value);
    void on_cycles(int cycles);

    bool load_roms(const std::string& rom_path, std::string* error);
    void set_palette(int index, uint16_t value);

    uint16_t vad_control_read(int offset);
    void vad_control_write(int offset, uint16_t value);
    void vad_update_parameter(uint16_t word);
    void apply_eof();

    uint16_t leta_r(int offset);
    uint16_t special_port0_r() const;

    void draw_motion_object_band(int line);
    void render_line(int line);

    static constexpr int kMoPlaneWidth = 512;
    static constexpr int kMoPlaneHeight = 512;
    static constexpr uint16_t kMoTransparent = 0xffff;

    M68000 main_cpu_;
    OKIM6295 oki_;

    std::vector<uint16_t> rom_;
    std::array<uint16_t, 0x1000> playfield_{};
    std::array<uint16_t, 0x1000> playfield_ext_{};
    std::array<uint16_t, 0x40> eof_{};
    std::array<uint16_t, 0x40> slip_{};
    std::array<uint16_t, 0x4000> ram_{};
    std::array<uint8_t, 0x800> eeprom_{};
    std::array<uint16_t, 0x20> vad_control_{};
    std::array<uint16_t, 0x400> palette_ram_{};
    std::array<uint32_t, 0x400> palette_{};

    GfxSet playfield_gfx_;
    GfxSet sprite_gfx_;
    std::unique_ptr<AtariMotionObjects> motion_objects_;

    std::vector<uint32_t> framebuffer_;
    std::vector<uint16_t> mo_index_;

    int scanline_ = 0;
    bool in_hblank_ = false;
    bool eeprom_unlocked_ = false;

    uint32_t pf0_xscroll_raw_ = 0;
    uint32_t pf1_xscroll_raw_ = 0;
    uint32_t pf0_yscroll_ = 0;
    uint32_t mo_xscroll_ = 0;
    uint32_t mo_yscroll_ = 0;
    int irq_scanline_ = 0;
    bool irq_armed_ = false;

    uint16_t system_port_ = 0xffff;
    uint16_t buttons_port_ = 0xffff;
    bool service_ = false;
    int16_t leta_cur_[2]{};
    int8_t track_dx_ = 0;
    int8_t track_dy_ = 0;
    int last_pointer_x_ = 0;
    int last_pointer_y_ = 0;
    bool pointer_seen_ = false;

    int64_t audio_accumulator_ = 0;
    int64_t oki_accumulator_ = 0;
    int32_t last_oki_ = 0;
    std::vector<int16_t> audio_;
};

}  // namespace dsp
