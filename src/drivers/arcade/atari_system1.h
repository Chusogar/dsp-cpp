#pragma once

#include <array>
#include <bitset>
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
//
// Small enough for boards without PSRAM: every ROM is read in place when the
// board maps it (the 68000 program straight from its even/odd chip pairs),
// the tiles are decoded from the graphics ROMs as the picture is drawn, a
// band of 8 lines at a time, and the picture is 8-bit: the up to 1024
// palette entries a frame uses are given 256 slots that stay put from frame
// to frame. The 32-bit framebuffer is only built when asked for.
class AtariSystem1 : public Machine {
public:
    MachineType machine_type() const override { return MachineType::Arcade; }
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

    const uint32_t* framebuffer() const override;
    const uint8_t* indexed_framebuffer() const override { return screen_.data(); }
    const uint32_t* indexed_palette() const override { return slot_palette_.data(); }
    void set_draw_enabled(bool enabled) override { draw_enabled_ = enabled; }
    void set_picture_limit(int width, int height) override;
    int screen_width() const override { return width_; }
    int screen_height() const override { return height_; }
    double frames_per_second() const override { return kFramesPerSecond; }

    void drain_audio(std::vector<int16_t>& out) override;
    int sample_rate() const override { return int(sample_rate_); }
    void set_sample_rate_limit(int rate) override;
    void set_audio_enabled(bool enabled) override { audio_enabled_ = enabled; }

    const char* title() const override;

    uint32_t debug_pc() const { return main_cpu_.pc(); }
    uint16_t debug_sound_pc() const { return sound_cpu_.pc(); }
    bool debug_sound_halted() const { return sound_cpu_halted_; }
    uint16_t debug_bankselect() const { return bankselect_; }
    uint8_t debug_in2() const { return in2_; }
    uint8_t debug_sound_ram(uint16_t address) const { return sound_ram_[address & 0x0fff]; }
    // Every tile row decoded straight from the ROMs equals the GfxSet decode.
    bool debug_tiles_match() const;

private:
    uint16_t main_read(uint32_t address);
    void main_write(uint32_t address, uint16_t value);
    uint8_t sound_read(uint16_t address);
    void sound_write(uint16_t address, uint8_t value);
    void on_sound_cycles(int cycles);
    void sync_pokey();
    void set_sound_reset(bool running);
    bool has_speech() const;
    bool has_adc() const;
    void adc_start(uint32_t address);
    void adc_complete();
    uint8_t adc_channel_value(int channel) const;
    bool via_selected(uint16_t address) const;

    bool load_roms(const std::string& rom_path, std::string* error);
    void convert_background(const std::vector<uint8_t>& proms);
    uint8_t decode_bank(uint8_t prom1, uint8_t prom2, int bpp);
    void set_palette(int index, uint16_t value);
    void update_video();
    // One band of lines [y0, y1): playfield, motion objects, alphanumerics.
    // False when the palette slots ran out.
    bool compose_band(int y0, int y1);
    void mix_motion_object_pixel(uint16_t& pf, uint16_t mo);
    void draw_motion_tile(int code, int color, bool hflip, bool vflip, int x, int y, int gfx,
                          int priority);
    // Pens of one 8-pixel row of a tile of graphics set `gfx` (0 = the
    // 2bpp alphanumerics), as the GfxSet decode of the ROMs would give.
    void tile_row(int gfx, int code, int row, uint8_t* pens) const;
    bool gfx_valid(int gfx) const { return gfx == 0 || (gfx < next_gfx_index_ && gfx_bank_[size_t(gfx)] != 0); }
    void reschedule_int3(int scanline);
    uint16_t rom_word(uint32_t address) const;

    static constexpr int16_t kTransparent = -1;

    Game game_;
    M68000 main_cpu_;
    M6502 sound_cpu_;
    YM2151 ym_;
    Pokey pokey_;
    Via6522 via_;
    Tms5220 tms_;
    Slapstic slapstic_;

    // 68000 program: 4 KB pages of $00000-$87FFF (the last 8 are the four
    // slapstic banks, $2000 bytes each), the high and the low bytes from a
    // pair of byte-wide ROMs (2 KB of each); null: reads as 0.
    struct RomPair {
        const uint8_t* even = nullptr;
        const uint8_t* odd = nullptr;
    };
    std::array<RomPair, 0x80> main_pages_{};
    std::array<RomPair, 8> slapstic_pages_{};
    std::vector<std::vector<uint8_t>> owned_roms_;  // ROMs the board does not map
    // The RAMs are blocks of their own: a small board's heap has room for
    // several mid-sized blocks more easily than for one large object.
    std::vector<uint16_t> ram_;  // 0x1000 words
    // $900000-$9FFFF RAM: 4 KB pages made when first written (no game here
    // uses it; it reads as 0 until then).
    std::vector<std::unique_ptr<std::array<uint16_t, 0x800>>> ram2_;
    std::vector<uint16_t> ram3_;      // 0x2000 words
    std::vector<uint8_t> eeprom_;     // 0x800
    std::vector<uint8_t> sound_ram_;  // 0x1000
    std::array<const uint8_t*, 4> sound_rom_{};  // 16 KB pages of the 6502 space

    const uint8_t* char_rom_ = nullptr;  // 8 KB, 0x200 2bpp alphanumerics
    // Graphics ROM image (bit-inverted when read): 16 KB chunks, null = $FF.
    std::vector<const uint8_t*> gfx_chunks_;
    uint32_t gfx_bytes_ = 0;
    // Graphics sets 1..15: the 512 KB bank of the image they come from and
    // their planes (bit k of a pen from bank + k * $10000).
    std::array<uint8_t, 16> gfx_bank_{};
    std::array<uint8_t, 16> gfx_bpp_{};
    std::array<std::array<std::array<const uint8_t*, 2>, 8>, 16> gfx_planes_{};
    std::array<uint8_t, 16> bank_color_shift_{};
    std::array<std::array<uint8_t, 8>, 3> bank_gfx_{};
    uint8_t next_gfx_index_ = 1;
    std::array<uint16_t, 256> playfield_lookup_{};
    std::array<uint16_t, 256> motable_{};

    std::unique_ptr<AtariMotionObjects> motion_objects_;

    std::vector<uint16_t> palette_ram_;  // 0x400
    std::bitset<0x400> palette_set_;  // entries set_palette() has written
    uint32_t palette_colour(int index) const;

    // The picture: slot numbers; slot_index_ gives the palette entry of each
    // slot and slot_palette_ its colour when the picture was drawn.
    static constexpr int kBandLines = 8;
    std::vector<uint8_t> screen_;
    int width_ = kScreenWidth, height_ = kScreenHeight;
    int crop_x_ = 0, crop_y_ = 0;
    std::vector<uint16_t> band_;  // kBandLines x kScreenWidth palette entries
    int band_y0_ = 0, band_y1_ = 0;
    std::array<uint16_t, 0x400> slot_of_{};     // palette entry -> slot + 1 (0: none)
    std::array<uint16_t, 256> slot_index_{};
    int slots_used_ = 0;
    std::array<uint32_t, 256> slot_palette_{};
    mutable std::vector<uint32_t> framebuffer_;
    mutable bool framebuffer_stale_ = true;
    bool draw_enabled_ = true;

    uint8_t rom_bank_ = 0;
    uint16_t scroll_x_ = 0;
    uint16_t scroll_y_ = 0;
    uint16_t scroll_y_latch_ = 0;
    std::array<uint16_t, kScanlines> scroll_x_line_{};
    std::array<uint16_t, kScanlines> scroll_y_line_{};
    uint16_t bankselect_ = 0;
    uint8_t playfield_tile_bank_ = 0;
    uint16_t playfield_priority_pens_ = 0;
    uint8_t vblank_ = 0x10;
    int int3_line_ = -1;
    int int3_off_line_ = -1;
    bool int3_state_ = false;
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
    bool sound_cpu_halted_ = false;
    int line_ = 0;

    int64_t audio_accumulator_ = 0;
    uint32_t sample_rate_ = YM2151::kSampleRate;
    bool audio_enabled_ = true;
    int pokey_pending_ = 0;  // 6502 cycles the POKEY has still to run
    uint32_t tms_table_clock_ = 0;
    std::array<uint16_t, 16> tms_clocks_{};  // TMS5220 clocks in n 6502 cycles
    std::vector<int16_t> audio_;
};

}  // namespace dsp
