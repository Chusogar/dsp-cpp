#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "core/machine.h"
#include "cpu/z80.h"
#include "machine/namco06.h"
#include "machine/namco51.h"
#include "machine/namco53.h"
#include "machine/namco54.h"
#include "sound/namco_wsg.h"
#include "video/gfx.h"

namespace dsp {

// Namco/Midway "Galaga" hardware family, ported from galaga_hw.pas: three
// Z80s (main + two subs) sharing one address bus view, a 06XX bridge to a
// handful of Namco MB88xx MCU peripherals (51xx for coin/inputs, 53xx for
// DIP/misc I/O on Dig Dug, 54xx for sound-effect triggers), and a 3-voice
// Namco WSG for music. Each game on this board has its own tile/sprite
// layout and video quirks (Galaga's parallax starfield, Dig Dug's paletted
// background map ROM, Bosconian's radar + starfield + dot cannon sprites,
// Xevious's dual scrolling tilemaps with a custom "BB" collision/lookup
// table), so video is handled per game.
class GalagaHw : public Machine {
public:
    enum class Game { Galaga, DigDug, Xevious, SuperXevious, Bosconian };

    static constexpr uint32_t kCpuClock = 3072000;
    static constexpr int kScanlines = 264;
    static constexpr double kFramesPerSecond = 60.606060606;

    explicit GalagaHw(Game game);

    bool init(const std::string& rom_path, std::string* error) override;
    void reset() override;
    void run_frame() override;

    void set_inputs(const MachineInputs& inputs) override;
    void set_dip_switch(int bank, uint8_t value) override;

    const uint32_t* framebuffer() const override { return framebuffer_.data(); }
    int screen_width() const override { return screen_width_; }
    int screen_height() const override { return screen_height_; }
    double frames_per_second() const override { return kFramesPerSecond; }

    void drain_audio(std::vector<int16_t>& out) override;
    int sample_rate() const override { return NamcoWsg::kSampleRate; }

    const char* title() const override;

private:
    // --- Main/sub CPU address space (shared "memoria" view, like the Pascal) ---
    uint8_t main_read(uint16_t addr);
    void main_write(uint16_t addr, uint8_t value);
    uint8_t sub_read(uint16_t addr);
    uint8_t sub2_read(uint16_t addr);

    void latch_write(int bit, uint8_t value);
    uint8_t dip_bits(uint8_t bank_a_bit, uint8_t bank_b_bit) const;

    void on_main_cycles(int cycles);

    // --- Loading ---
    bool load_galaga(const std::string& rom_path, std::string* error);
    bool load_digdug(const std::string& rom_path, std::string* error);
    bool load_bosconian(const std::string& rom_path, std::string* error);
    bool load_xevious(const std::string& rom_path, std::string* error);

    // --- Video, one path per game ---
    void render_galaga();
    void render_digdug();
    void render_bosconian();
    void render_xevious();
    void draw_star(int set_a, int set_b, int scroll_x, int scroll_y, bool wrap_xy);
    static std::vector<uint8_t> rotate_cw(const GfxSet& src, int count, int size);

    Game game_;
    int screen_width_ = 224;
    int screen_height_ = 288;

    Z80 main_cpu_;
    Z80 sub_cpu_;   // z80_2 in the reference driver
    Z80 sub2_cpu_;  // z80_1 in the reference driver

    NamcoWsg wsg_;
    Namco06xx bridge_;
    Namco06xx bridge2_;  // Bosconian only: second 06xx for the IO50XX_1 slot
    Namco51xx io51_;
    Namco53xx io53_;  // Dig Dug only
    Namco54xx io54_;

    GfxSet chars_;
    GfxSet sprites_;
    GfxSet tiles_;  // Dig Dug background / Bosconian radar+bg / Xevious fg

    // Galaga's char/sprite ROMs are decoded then rotated 90 degrees at load
    // time on real hardware (see galaga_hw.pas's convert_gfx rot90 flag);
    // GfxSet has no such hook, so we decode normally and rotate the pixel
    // data ourselves into these buffers.
    std::vector<uint8_t> chars_rotated_;
    std::vector<uint8_t> sprites_rotated_;

    std::array<uint8_t, 0x10000> mem_{};       // main CPU space (ROM + RAM)
    std::array<uint8_t, 0x4000> sub_rom_{};    // sub1 private ROM window
    std::array<uint8_t, 0x4000> sub2_rom_{};   // sub2 private ROM window
    std::array<uint32_t, 32 + 64> palette_{};  // base colours + star palette
    std::array<uint8_t, 0x100> char_lut_{};
    std::array<uint8_t, 0x200> tile_lut_{};

    std::vector<uint8_t> digdug_bg_map_;    // raw background lookup ROM
    std::vector<uint8_t> xevious_tiles_;    // "BB" lookup ROM
    std::array<uint8_t, 2> xevious_bs_{};

    std::array<uint8_t, 6> star_control_{};
    int scrollx_bg_ = 0, scrolly_bg_ = 0;
    int scrollx_fg_ = 0, scrolly_fg_ = 0;

    bool main_irq_ = false, sub_irq_ = false, sub2_nmi_ = false;
    bool sub_held_ = true, sub2_held_ = true;
    bool flip_screen_ = false;
    uint8_t custom_mod_ = 0;

    // Dig Dug background controller
    uint8_t bg_select_ = 0, bg_color_bank_ = 0;
    bool bg_disable_ = false, bg_repaint_ = false;

    uint8_t in0_ = 0xff, in1_ = 0xff, in2_ = 0xff;
    uint8_t dsw_a_ = 0xff, dsw_b_ = 0xff;

    std::vector<uint32_t> framebuffer_;
    std::vector<int16_t> audio_;
    int64_t wsg_accumulator_ = 0;
    int main_cycles_per_line_ = 0;
};

}  // namespace dsp
