#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "core/machine.h"
#include "cpu/z80.h"
#include "machine/i8255.h"
#include "machine/z80pio.h"
#include "sound/sn76496.h"
#include "video/gfx.h"

namespace dsp {

// Sega "System 1" hardware, ported from system1_hw.pas / system1_hw_misc.pas.
//
// Main Z80 (encrypted opcode stream, decoded once at load time), sound Z80
// driving two SN76496 PSGs, a two layer tile background (fixed foreground +
// scrolling background, both fed from the same 4K tile RAM) and 32 streamed,
// bank switched sprites mixed through a priority PROM. I/O is either a Z80
// PIO (Pitfall II, Teddy Boy Blues, Wonder Boy, Sega Ninja, Flicky, Gardia)
// or an 8255 PPI (Mr. Viking, Up'n Down).
class SegaSystem1 : public Machine {
public:
    enum class Game {
        Pitfall2,
        TeddyBoy,
        WonderBoy,
        MrViking,
        SegaNinja,
        UpNDown,
        Flicky,
        Gardia,
    };

    static constexpr double kFramesPerSecond = 60.096154;
    static constexpr int kScanlines = 260;
    static constexpr int kIrqLine = 224;
    static constexpr uint32_t kMainClock = 20000000 / 5;  // 4 MHz
    static constexpr uint32_t kSoundClock = 4000000;
    static constexpr uint32_t kPsg0Clock = 2000000;
    static constexpr uint32_t kPsg1Clock = 4000000;
    static constexpr int kLayerSize = 256;  // 256x256 work tilemaps
    static constexpr int kScreenWidth = 256;
    static constexpr int kScreenHeight = 224;

    explicit SegaSystem1(Game game);

    bool init(const std::string& rom_path, std::string* error) override;
    void reset() override;
    void run_frame() override;

    void set_inputs(const MachineInputs& inputs) override;
    void set_dip_switch(int bank, uint8_t value) override;

    const uint32_t* framebuffer() const override { return framebuffer_.data(); }
    int screen_width() const override { return display_width_; }
    int screen_height() const override { return kScreenHeight; }
    double frames_per_second() const override { return kFramesPerSecond; }

    void drain_audio(std::vector<int16_t>& out) override;
    int sample_rate() const override { return SN76496::kSampleRate; }

    const char* title() const override;

private:
    bool uses_ppi() const { return game_ == Game::MrViking || game_ == Game::UpNDown; }
    bool banked() const { return game_ == Game::Gardia; }

    // Main CPU
    uint8_t read_data(uint16_t addr);
    uint8_t read_opcode(uint16_t addr);
    void write_byte(uint16_t addr, uint8_t value);
    uint8_t read_port(uint16_t port);
    void write_port(uint16_t port, uint8_t value);

    // Sound CPU
    uint8_t snd_read(uint16_t addr);
    void snd_write(uint16_t addr, uint8_t value);
    void on_sound_cycles(int cycles);

    // PPI/PIO glue (shared handlers for both I/O chips)
    void port_a_write(uint8_t value);       // sound_latch_
    void port_b_write(uint8_t value);       // rom_bank_ / videomode_
    void port_b_gardia_write(uint8_t value);
    void port_c_write(uint8_t value);       // PPI only: sound NMI + bg_ram_bank_
    void pio_ready_a(bool state);           // PIO only: pulses sound NMI

    // Graphics
    void decode_graphics(const std::vector<uint8_t>& char_rom);
    void build_palette_resistor();
    void set_color_weighted(int index, uint8_t value);
    void set_color_direct(int index, uint8_t value);
    std::vector<double> rweights_, gweights_, bweights_;

    void update_background(int layer);
    void draw_bg_tile(int layer, int offset);
    void draw_sprites();
    void render_frame();

    Game game_;
    Z80 main_cpu_;
    Z80 sound_cpu_;
    SN76496 psg0_;
    SN76496 psg1_;
    Z80Pio pio_;
    I8255 ppi_;

    // Main CPU address space. `memory_` is the "read as data" view (also the
    // live RAM window above 0xC000); `opcodes_` is the "read as instruction"
    // view for the encrypted 0x0000-0x7FFF window only.
    std::array<uint8_t, 0x10000> memory_{};
    std::array<uint8_t, 0x8000> opcodes_{};
    // Gardia: banked ROM window at $8000-$BFFF, 4 banks of 0x4000, unencrypted.
    std::array<std::array<uint8_t, 0x4000>, 4> banked_rom_{};
    int rom_bank_ = 0;

    std::array<uint8_t, 0x1000> bg_ram_{};
    std::array<bool, 0x1000 / 2> bg_ram_dirty_{};
    std::array<uint8_t, 0x800> palette_ram_{};

    std::array<uint8_t, 0x40> mix_collide_{};
    bool mix_collide_summary_ = false;
    std::array<uint8_t, 0x400> sprite_collide_{};
    bool sprite_collide_summary_ = false;

    // Sound CPU address space: 0x8000 ROM + 0x800 RAM mirrored at 0x8000-0x9FFF.
    std::array<uint8_t, 0x8000> sound_rom_{};
    std::array<uint8_t, 0x800> sound_ram_{};

    // Graphics ROM
    GfxSet chars_;                                    // 2048 8x8, 3bpp
    std::vector<uint8_t> sprite_rom_;                 // raw nibble-packed sprite data
    int sprite_num_banks_ = 1;
    std::array<uint8_t, 0x100> mix_lookup_{};         // priority PROM
    std::array<uint8_t, 0x300> direct_proms_{};       // Gardia RGB PROMs
    std::array<uint32_t, 0x600> palette_{};

    std::array<std::array<uint16_t, kLayerSize * kLayerSize>, 2> tile_layer_{};  // 0=bg 1=fg
    std::array<uint16_t, kLayerSize * kLayerSize> sprite_layer_{};
    std::vector<uint32_t> framebuffer_;

    int bg_xscroll_ = 0;
    uint8_t bg_yscroll_ = 0;
    uint8_t bg_ram_bank_ = 0;
    // MrViking, UpNDown and Gardia sit in a rotated cabinet and MAME/the
    // original driver crop the 256-wide internal bitmap to a 240-wide,
    // 8-pixel-inset visible window; everyone else shows the full 256.
    int display_width_ = kScreenWidth;
    int display_xoffset_ = 0;

    uint8_t sound_latch_ = 0;
    uint8_t videomode_ = 0;
    uint8_t in0_ = 0xff, in1_ = 0xff, in2_ = 0xff;
    uint8_t dsw_a_ = 0xff, dsw_b_ = 0xff;
    uint8_t ppi_c_shadow_ = 0;
    int sound_irq_accum_ = 0;
    int main_cycles_per_line_ = 0;
    int sound_cycles_per_line_ = 0;

    int64_t audio_accumulator_ = 0;
    std::vector<int16_t> audio_;
};

}  // namespace dsp
