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
#include "machine/via6522.h"
#include "sound/pokey.h"
#include "sound/tms5220.h"
#include "sound/ym2151.h"
#include "video/atari_mo.h"
#include "video/gfx.h"

namespace dsp {

// Atari System 1 (Marble Madness, Peter Pack Rat, Indiana Jones, Road Runner).
// Main CPU is a 68000 behind a SLAPSTIC; sound is an M6502 with a YM2151, a
// POKEY and, on Indy/Road Runner, a TMS5220C behind a 6522 VIA. Playfield/MO
// banks are selected by a pair of colour PROMs the same way convert_back does
// in the Pascal driver.
class AtariSystem1 : public Machine {
public:
    static constexpr int kScreenWidth = 336;
    static constexpr int kScreenHeight = 240;
    static constexpr int kPlayfieldWidth = 512;
    static constexpr int kPlayfieldHeight = 512;
    static constexpr int kAlphaWidth = 512;
    static constexpr int kAlphaHeight = 256;
    static constexpr int kScanlines = 262;
    static constexpr uint32_t kAtariClock = 14318180;
    static constexpr uint32_t kMainClock = kAtariClock / 2;
    static constexpr uint32_t kSoundClock = kAtariClock / 8;
    static constexpr uint32_t kYmClock = kAtariClock / 4;
    static constexpr double kFramesPerSecond = 59.922743;

    enum class Game { PeterPak, Indy, Marble, RoadRunner };

    explicit AtariSystem1(Game game = Game::Indy);

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

    uint32_t debug_pc() const { return main_cpu_.pc(); }

private:
    uint16_t main_read(uint32_t address);
    void main_write(uint32_t address, uint16_t value);
    uint8_t sound_read(uint16_t address);
    void sound_write(uint16_t address, uint8_t value);
    void on_sound_cycles(int cycles);
    void set_sound_reset(bool running);
    bool has_speech() const;
    bool has_adc() const;
    void adc_start(uint32_t address);
    void adc_complete();
    uint8_t adc_channel_value(int channel) const;
    bool via_selected(uint16_t address) const;

    bool load_roms(const std::string& rom_path, std::string* error);
    void convert_background(std::vector<uint8_t>& gfx_rom, const std::vector<uint8_t>& proms);
    uint8_t decode_bank(uint8_t prom1, uint8_t prom2, int bpp, const std::vector<uint8_t>& gfx_rom);
    void set_palette(int index, uint16_t value);
    void update_video();
    void draw_alpha_tile(int offset);
    void draw_playfield_tile(int offset);

    static constexpr int16_t kTransparent = -1;

    Game game_;
    M68000 main_cpu_;
    M6502 sound_cpu_;
    YM2151 ym_;
    Pokey pokey_;
    Via6522 via_;
    Tms5220 tms_;
    Slapstic slapstic_;

    std::array<uint16_t, 0x40000> rom_{};
    std::array<std::array<uint16_t, 0x1000>, 4> slapstic_rom_{};
    std::array<uint16_t, 0x1000> ram_{};
    std::array<uint16_t, 0x80000> ram2_{};
    std::array<uint16_t, 0x2000> ram3_{};
    std::array<uint8_t, 0x800> eeprom_{};
    std::array<uint8_t, 0x10000> sound_memory_{};

    GfxSet chars_;
    std::array<GfxSet, 16> gfx_{};
    std::array<uint8_t, 16> bank_color_shift_{};
    std::array<std::array<uint8_t, 8>, 3> bank_gfx_{};
    uint8_t next_gfx_index_ = 1;
    std::array<uint16_t, 256> playfield_lookup_{};
    std::array<uint16_t, 256> motable_{};

    std::unique_ptr<AtariMotionObjects> motion_objects_;

    std::array<uint16_t, 0x400> palette_ram_{};
    std::array<uint32_t, 0x400> palette_{};

    std::vector<int16_t> alpha_;
    std::vector<int16_t> playfield_;
    std::vector<uint32_t> composite_;
    std::vector<uint32_t> framebuffer_;
    std::array<bool, 0x800> alpha_dirty_{};
    std::array<bool, 0x1000> playfield_dirty_{};

    uint8_t rom_bank_ = 0;
    uint16_t scroll_x_ = 0;
    uint16_t scroll_y_ = 0;
    uint16_t scroll_y_latch_ = 0;
    uint16_t bankselect_ = 0;
    uint8_t playfield_tile_bank_ = 0;
    uint8_t vblank_ = 0x10;
    uint16_t in0_ = 0xff6f;
    uint8_t in2_ = 0x87;
    uint8_t analog_x_ = 0x80;
    uint8_t analog_y_ = 0x80;
    uint8_t joy_bits_ = 0;
    uint8_t adc_channel_ = 0;
    uint8_t adc_value_ = 0;
    bool adc_irq_enable_ = true;
    bool adc_busy_ = false;
    bool write_eeprom_ = false;
    bool sound_pending_ = false;
    bool main_pending_ = false;
    uint8_t sound_latch_ = 0;
    uint8_t main_latch_ = 0;
    bool sound_cpu_halted_ = true;
    int line_ = 0;

    int64_t audio_accumulator_ = 0;
    std::vector<int16_t> audio_;
};

}  // namespace dsp
