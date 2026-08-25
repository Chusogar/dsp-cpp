#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "core/machine.h"
#include "cpu/m68000.h"
#include "machine/eeprom93c46.h"
#include "sound/okim6295.h"
#include "video/gfx.h"

namespace dsp {

// NIX "Pirates" hardware (also used by "Genix Family"), ported
// from pirates_hw.pas.
//
// One M68000 running everything (there is no separate sound CPU): a single
// OKI M6295 is driven straight from the main CPU's address space, sample
// generation is paced from the CPU's cycle handler. A 93C46 serial EEPROM
// backs settings/high scores. Three tile layers share one 8x8 4bpp graphics
// set (a fixed "text" layer plus two independently scrolling background
// layers that share a single X scroll register) and one bank of 16x16 4bpp
// sprites, all with heavy per-plane bit/address scrambling on the ROMs that
// is undone once at load time.
class Pirates : public Machine {
public:
    enum class Game { Pirates, Genix };

    static constexpr int kScreenWidth = 288;
    static constexpr int kScreenHeight = 224;
    static constexpr int kScanlines = 256;
    static constexpr int kVBlankLine = 240;
    static constexpr double kFramesPerSecond = 60.0;
    static constexpr uint32_t kMainClock = 16000000;
    static constexpr uint32_t kOkiClock = 1333333;

    explicit Pirates(Game game);

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
    int sample_rate() const override { return 44100; }

    const char* title() const override;

private:
    // ---------------------------------------------------------------------
    // Main CPU memory map
    // ---------------------------------------------------------------------
    uint16_t main_read(uint32_t addr);
    void main_write(uint32_t addr, uint16_t value);
    void on_main_cycles(int cycles);

    void write_palette(int index, uint16_t value);
    void select_oki_bank(int bank);
    uint32_t pal_color(int index) const;

    // ---------------------------------------------------------------------
    // Loading / decryption
    // ---------------------------------------------------------------------
    bool load_program(const std::string& rom_path, std::string* error);
    bool load_graphics(const std::string& rom_path, std::string* error);
    bool load_sound(const std::string& rom_path, std::string* error);

    // ---------------------------------------------------------------------
    // Video
    // ---------------------------------------------------------------------
    void render_frame();
    void draw_layer(int base_word, int width_tiles, int height_tiles, int color_add,
                    bool transparent, int scroll_x, int dest_width);
    void draw_sprites();

    Game game_;
    M68000 main_cpu_;
    OKIM6295 oki_;
    Eeprom93C46 eeprom_;

    GfxSet tiles_;    // 8x8, 4bpp
    GfxSet sprites_;  // 16x16, 4bpp

    std::array<uint16_t, 0x80000> rom_{};        // decrypted program ROM, word addressed
    std::array<uint16_t, 0x8000> work_ram_{};    // $100000-$10ffff
    std::array<uint16_t, 0x800> sprite_ram_{};   // $500000-$500fff
    std::array<uint16_t, 0x2000> palette_ram_{}; // $800000-$803fff, raw RGB555 words
    std::array<uint16_t, 0x4000> tile_ram_{};    // $900000-$907fff
    std::array<uint32_t, 0x2000> palette_{};     // converted ARGB8888

    // Decrypted OKI sample ROM, banked in two 256 KB halves selected through
    // the EEPROM control latch at $600000 bit 6.
    std::array<std::array<uint8_t, 0x40000>, 2> sound_banks_{};

    uint8_t in0_ = 0x0f;
    uint16_t in1_ = 0xffff;
    uint16_t scroll_x_ = 0;

    std::vector<uint32_t> framebuffer_;
    std::vector<uint32_t> canvas_;  // 512x256 scratch composite, rebuilt every frame
    std::vector<int16_t> audio_;
    int64_t oki_accumulator_ = 0;
    int64_t audio_accumulator_ = 0;
    int32_t last_oki_ = 0;
    int last_oki_bank_ = -1;

    int main_cycles_per_line_ = 0;
};

}  // namespace dsp
