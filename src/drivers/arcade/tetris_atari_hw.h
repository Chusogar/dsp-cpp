#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "core/machine.h"
#include "cpu/m6502.h"
#include "machine/slapstic.h"
#include "sound/pokey.h"

namespace dsp {

// Atari Tetris (1988), ported from tetris_atari_hw.pas.
// M6502 main CPU (1.79 MHz) with Atari SLAPSTIC bank switching, two POKEY
// sound chips and a 64x32 tilemap of 8x8 characters on a 336x240 screen.
//
// Small enough for boards without PSRAM: both ROMs are read in place when
// the board maps them, the characters are drawn straight from their ROM, the
// picture is 8-bit (palette indices) composed a whole row at a time, and the
// 32-bit framebuffer is only built when asked for.
class AtariTetris : public Machine {
public:
    MachineType machine_type() const override { return MachineType::Arcade; }
    static constexpr int kScreenWidth = 336;
    static constexpr int kScreenHeight = 240;
    static constexpr double kFramesPerSecond = 59.922743;
    static constexpr int kScanlines = 262;
    static constexpr uint32_t kCpuClock = 1789772;
    static constexpr int kSampleRate = 44100;

    AtariTetris();

    bool init(const std::string& rom_path, std::string* error) override;
    void reset() override;
    void run_frame() override;

    void set_inputs(const MachineInputs& inputs) override;
    void set_dip_switch(int bank, uint8_t value) override;

    const uint32_t* framebuffer() const override;
    const uint8_t* indexed_framebuffer() const override { return screen_.data(); }
    const uint32_t* indexed_palette() const override { return shown_palette_.data(); }
    void set_draw_enabled(bool enabled) override { draw_enabled_ = enabled; }
    int screen_width() const override { return kScreenWidth; }
    int screen_height() const override { return kScreenHeight; }
    double frames_per_second() const override { return kFramesPerSecond; }

    void drain_audio(std::vector<int16_t>& out) override;
    int sample_rate() const override { return int(sample_rate_); }
    void set_sample_rate_limit(int rate) override {
        if (rate > 0 && rate < kSampleRate) sample_rate_ = uint32_t(rate);
    }

    const char* title() const override { return "Tetris (Atari)"; }
    bool uses_pointer() const override { return false; }

private:
    uint8_t main_read(uint16_t address);
    void main_write(uint16_t address, uint8_t value);
    void on_sound_cycles(int cycles);
    void sync_pokeys();
    void set_palette(int index, uint8_t value);
    void update_video();

    M6502 cpu_;
    Slapstic slapstic_;
    Pokey pokey0_;
    Pokey pokey1_;

    std::array<uint8_t, 0x2000> ram_{};   // $0000-$1fff (tilemap at $1000)
    const uint8_t* main_rom_ = nullptr;    // 64 KB: banks 0/1 at $4000, fixed $8000-$ffff
    const uint8_t* char_rom_ = nullptr;    // 64 KB: 0x800 8x8 4bpp characters
    std::vector<uint8_t> main_rom_owned_, char_rom_owned_;
    std::array<uint8_t, 0x200> nv_ram_{};
    std::array<uint8_t, 0x100> buffer_paleta_{};
    std::array<uint32_t, 0x100> palette_{};
    std::array<uint32_t, 0x100> shown_palette_{};  // palette_ when the picture was drawn
    std::vector<uint8_t> screen_;                  // shown_palette_ indices
    mutable std::vector<uint32_t> framebuffer_;
    mutable bool framebuffer_stale_ = true;
    bool draw_enabled_ = true;

    uint8_t rom_bank_ = 1;
    bool nvram_write_enable_ = false;
    uint8_t in0_ = 0x40;
    uint8_t in1_ = 0;

	// DIP:
    // bit 2  Freeze
    // bit 3  Freeze Step
    // bit 7  Service
    //
    // Service OFF = 0x00
    uint8_t dsw_a_ = 0x00;

    int64_t audio_accumulator_ = 0;
    uint32_t sample_rate_ = kSampleRate;
    int pokey_pending_ = 0;  // CPU cycles the POKEYs have still to run
    std::vector<int16_t> audio_;
};

}  // namespace dsp