#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "core/machine.h"
#include "cpu/m6502.h"
#include "cpu/t11.h"
#include "machine/slapstic.h"
#include "sound/pokey.h"
#include "sound/tms5220.h"
#include "sound/ym2151.h"
#include "video/atari_mo.h"
#include "video/gfx.h"

namespace dsp {

// Atari System 2 (Paperboy). The main CPU is a DEC T-11 whose 020000-037777
// window is banked by a SLAPSTIC 105 between the alpha/motion object RAM and
// the two halves of the playfield; sound is an M6502 with a YM2151, two POKEYs
// and a TMS5220C.
class AtariSystem2 : public Machine {
public:
    static constexpr int kScreenWidth = 512;
    static constexpr int kScreenHeight = 384;
    static constexpr int kPlayfieldWidth = 1024;
    static constexpr int kPlayfieldHeight = 512;
    static constexpr int kScanlines = 416;
    static constexpr uint32_t kMasterClock = 20000000;
    static constexpr uint32_t kVideoClock = 32000000;
    static constexpr uint32_t kSoundXtal = 14318181;
    static constexpr uint32_t kMainClock = kMasterClock / 2;
    static constexpr uint32_t kSoundClock = kSoundXtal / 8;
    static constexpr uint32_t kYmClock = kSoundXtal / 4;
    static constexpr uint32_t kPokeyClock = kSoundXtal / 8;
    static constexpr double kFramesPerSecond =
        double(kVideoClock) / 2.0 / 640.0 / double(kScanlines);

    enum class Game { Paperboy };

    explicit AtariSystem2(Game game = Game::Paperboy);

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

    const char* title() const override;

    uint16_t debug_pc() const { return main_cpu_.pc(); }
    uint16_t debug_sound_pc() const { return sound_cpu_.pc(); }
    uint8_t debug_vram_bank() const { return vram_bank_; }
    uint8_t debug_interrupt_enable() const { return interrupt_enable_; }
    uint16_t debug_ram(uint16_t word_offset) const { return ram_[word_offset & 0x7ff]; }

private:
    uint16_t main_read(uint16_t address);
    void main_write(uint16_t address, uint16_t value, uint16_t mem_mask);
    uint8_t sound_read(uint16_t address);
    void sound_write(uint16_t address, uint8_t value);
    void on_sound_cycles(int cycles);

    bool load_roms(const std::string& rom_path, std::string* error);

    void update_interrupts();
    void write_sound_chip_reset(uint8_t value);
    void set_sound_reset(bool in_reset);
    void bank_select(int index, uint16_t data);
    void set_palette(int index, uint16_t value);
    void write_xscroll(uint16_t value);
    void write_yscroll(uint16_t value);
    uint16_t switch_r() const;
    uint8_t switch_6502_r() const;
    uint8_t adc_channel_value(int channel) const;

    void update_video();
    void draw_alpha_tile(int offset);
    void draw_playfield_tile(int offset);
    void draw_motion_objects();
    void compose_frame();

    static constexpr int16_t kTransparent = -1;
    static constexpr uint16_t kMoTransparent = 0xffff;

    Game game_;
    T11 main_cpu_;
    M6502 sound_cpu_;
    YM2151 ym_;
    Pokey pokey1_;
    Pokey pokey2_;
    Tms5220 tms_;
    Slapstic slapstic_;

    // T-11 program ROM, 0x90000 bytes as little endian words.
    std::vector<uint16_t> rom_;
    std::array<uint16_t, 0x800> ram_{};
    std::array<uint16_t, 0x100> palette_ram_{};
    std::array<uint32_t, 0x100> palette_{};
    std::array<uint16_t, 0xc00> alpha_ram_{};
    std::array<uint16_t, 0x400> mob_ram_{};
    std::array<uint16_t, 0x1000> playfield_top_{};
    std::array<uint16_t, 0x1000> playfield_bottom_{};
    std::array<uint8_t, 0x10000> sound_memory_{};
    std::array<uint8_t, 0x200> eeprom_{};

    GfxSet alpha_gfx_;
    GfxSet playfield_gfx_;
    GfxSet motion_gfx_;
    std::unique_ptr<AtariMotionObjects> motion_objects_;

    std::vector<int16_t> alpha_;
    std::vector<uint16_t> playfield_;
    std::vector<uint8_t> playfield_category_;
    std::vector<uint16_t> mo_pen_;
    std::vector<uint8_t> mo_priority_;
    std::vector<uint32_t> framebuffer_;
    std::array<bool, 0xc00> alpha_dirty_{};
    std::array<bool, 0x2000> playfield_dirty_{};

    std::array<uint32_t, 2> rom_bank_{};
    uint8_t vram_bank_ = 3;

    uint16_t xscroll_ = 0;
    uint16_t yscroll_reg_ = 0;
    uint16_t yscroll_ = 0;
    uint16_t yscroll_pending_ = 0;
    bool yscroll_reset_ = false;
    std::array<uint16_t, 2> playfield_tile_bank_{};
    std::array<uint16_t, kScreenHeight> xscroll_line_{};
    std::array<uint16_t, kScreenHeight> yscroll_line_{};

    uint8_t interrupt_enable_ = 0;
    bool video_int_state_ = false;
    bool scanline_int_state_ = false;
    bool p2portwr_state_ = false;
    bool p2portrd_state_ = false;

    bool sound_cpu_in_reset_ = true;
    bool sound_reset_state_ = false;
    uint8_t sound_latch_ = 0;
    uint8_t main_latch_ = 0;
    bool sound_pending_ = false;
    bool main_pending_ = false;
    int sound_irq_counter_ = 0;

    uint8_t adc_value_ = 0;
    std::array<uint8_t, 4> adc_input_{};
    uint8_t dsw_[2] = {0, 0};
    uint8_t buttons_ = 0;
    bool coin1_ = false;
    bool coin2_ = false;
    bool coin3_ = false;
    bool service_coin_ = false;
    bool self_test_ = false;

    int line_ = 0;
    int64_t audio_accumulator_ = 0;
    std::vector<int16_t> audio_;
};

}  // namespace dsp
