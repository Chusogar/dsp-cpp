#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "core/machine.h"
#include "cpu/m68000.h"
#include "cpu/z80.h"
#include "sound/sega_pcm.h"
#include "sound/ym2151.h"
#include "video/gfx.h"

namespace dsp {

// Sega X-Board (After Burner II), after MAME sega/segaxbd.cpp.
//
// Two 68000s at 12.5 MHz: the main CPU owns the System 16B-style tilemaps,
// the frame-buffered zooming sprites and the palette; the sub CPU draws
// the road (sky / ground layer) through its double-buffered road RAM.
// Each CPU has a 315-5248 multiplier, a 315-5249 divider and a 315-5250
// compare/timer; the main one also carries the sound latch to the Z80
// (NMI) and the timer IRQ.  I/O goes through two CXD1095 port expanders
// and an ADC0804 (stick X/Y, throttle).  Sound: Z80 at 4 MHz with a
// YM2151 and a 315-5218 Sega PCM.
class XBoard : public Machine {
public:
    enum class Game { Aburner2 };

    static constexpr int kScreenWidth = 320;
    static constexpr int kScreenHeight = 224;
    static constexpr int kScanlines = 262;
    static constexpr uint32_t kMainClock = 50000000 / 4;
    static constexpr uint32_t kSoundClock = 16000000 / 4;
    // 50 MHz / 8 pixel clock, 400 x 262 raster.
    static constexpr double kFramesPerSecond = 50000000.0 / 8.0 / (400.0 * 262.0);
    static constexpr int kSampleRate = 44100;
    static constexpr int kPaletteEntries = 0x2000;

    explicit XBoard(Game game = Game::Aburner2);

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
    const char* title() const override { return "After Burner II"; }

    // Test hooks.
    uint32_t debug_pc() const { return main_cpu_.pc(); }
    uint32_t debug_sub_pc() const { return sub_cpu_.pc(); }
    uint16_t debug_sound_pc() const { return sound_cpu_.pc(); }
    bool debug_display_enabled() const { return display_enable_; }
    uint8_t debug_adc(int channel) const { return adc_value(channel); }
    uint8_t debug_io1_porta() const { return io1_porta_; }
    int debug_sound_commands() const { return sound_commands_; }
    int debug_sprites_drawn() const { return sprites_drawn_; }

private:
    // One 315-5250 compare/timer chip.
    struct CompareTimer {
        std::array<uint16_t, 16> regs{};
        uint16_t counter = 0;
        int bit = 0;
        bool exck = false;
        bool irq = false;
        bool zint = false;
        void reset();
        void execute(bool update_history = false);
        uint16_t read(int offset);
        void write(int offset, uint16_t data, uint16_t mask);
        // Clocked by V0; returns true on the edge that raises the IRQ.
        void clock(bool state);
    };
    struct Multiplier {
        std::array<uint16_t, 2> regs{};
        uint16_t read(int offset) const;
        void write(int offset, uint16_t data, uint16_t mask);
    };
    struct Divider {
        std::array<uint16_t, 8> regs{};
        uint16_t read(int offset) const;
        void write(int offset, uint16_t data, uint16_t mask);
        void execute(int mode);
    };
    // CXD1095 I/O expander: ports A-D (and E), each half configurable.
    struct IoChip {
        std::array<uint8_t, 5> latch{};
        std::array<uint8_t, 5> dir{};  // 1 = input
        void reset();
    };

    bool load_roms(const std::string& rom_path, std::string* error);
    void build_palette_luts();
    void palette_write(int index, uint16_t value);

    uint16_t main_read(uint32_t address);
    void main_write(uint32_t address, uint16_t value, uint16_t mask);
    uint16_t sub_read(uint32_t address);
    void sub_write(uint32_t address, uint16_t value, uint16_t mask);
    uint8_t sound_read(uint16_t address);
    void sound_write(uint16_t address, uint8_t value);
    uint8_t sound_in(uint16_t port);
    void sound_out(uint16_t port, uint8_t value);
    void on_sound_cycles(int cycles);

    uint8_t iochip_read(int chip, int offset);
    void iochip_write(int chip, int offset, uint8_t value);
    uint8_t iochip_input(int chip, int port);
    void iochip_output(int chip, int port, uint8_t value);
    uint8_t adc_value(int channel) const;

    void update_main_irqs();
    void road_swap();
    void sprite_swap();
    void latch_tilemaps();

    void render();
    void draw_tile_layer(int which, int category, uint8_t mark);
    void draw_text_layer(int category, uint8_t mark);
    void draw_road(bool background);
    void draw_sprites();

    Game game_;
    M68000 main_cpu_;
    M68000 sub_cpu_;
    Z80 sound_cpu_;
    YM2151 ym_;
    SegaPcm pcm_;

    std::vector<uint16_t> main_rom_;
    std::vector<uint16_t> sub_rom_;
    std::vector<uint8_t> sound_rom_;
    std::vector<uint8_t> pcm_rom_;
    std::vector<uint32_t> sprite_rom_;
    std::vector<uint8_t> road_gfx_;
    GfxSet tiles_;

    std::array<uint16_t, 0x2000> backup1_{};
    std::array<uint16_t, 0x2000> backup2_{};
    std::array<uint16_t, 0x2000> subram0_{};
    std::array<uint16_t, 0x2000> subram1_{};
    std::array<uint16_t, 0x8000> tile_ram_{};
    std::array<uint16_t, 0x800> text_ram_{};
    std::array<uint16_t, 0x800> sprite_ram_{};
    std::array<uint16_t, 0x800> sprite_buffer_{};
    std::array<uint16_t, 0x2000> palette_ram_{};
    std::array<uint16_t, 0x800> road_ram_{};
    std::array<uint16_t, 0x800> road_buffer_{};
    std::array<uint8_t, 0x800> sound_ram_{};

    Multiplier mult_main_, mult_sub_;
    Divider div_main_, div_sub_;
    CompareTimer timer_main_, timer_sub_;
    std::array<IoChip, 2> iochip_{};

    // Palette: 0x2000 normal entries followed by 0x2000 shadow/hilight ones.
    std::array<uint32_t, kPaletteEntries * 2> palette_{};
    std::array<uint8_t, 32> pal_normal_{}, pal_shadow_{}, pal_hilight_{};

    // Tilemap scroll / page registers latched at scanline 261.
    std::array<uint16_t, 4> latched_pages_{}, latched_xscroll_{}, latched_yscroll_{};

    std::vector<uint16_t> bitmap_;
    std::vector<uint8_t> priority_;
    std::vector<uint16_t> sprite_bitmap_;
    std::vector<uint32_t> framebuffer_;

    uint8_t road_control_ = 0;
    bool display_enable_ = false;
    uint8_t pc0_ = 0;
    bool sound_reset_ = true;
    bool sound_mute_ = false;
    bool vblank_irq_ = false;
    uint8_t adc_latch_ = 0;
    int sound_commands_ = 0;
    int sprites_drawn_ = 0;

    // Inputs.
    uint8_t io1_porta_ = 0xff;
    uint8_t dsw_a_ = 0xff;
    uint8_t dsw_b_ = 0xdd;  // Upright 1, throttle lever, 3 lives, continue, normal
    int stick_x_ = 0x80, stick_y_ = 0x80, throttle_ = 0x80;

    double main_debt_ = 0, sub_debt_ = 0, sound_debt_ = 0;
    int64_t pcm_acc_ = 0, audio_acc_ = 0;
    int64_t pcm_sum_ = 0;
    int pcm_count_ = 0;
    int32_t pcm_last_ = 0;
    std::vector<int16_t> audio_;
};

}  // namespace dsp
