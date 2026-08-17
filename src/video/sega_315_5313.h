#pragma once

#include <array>
#include <cstdint>
#include <functional>

#include "sound/sn76496.h"

namespace dsp {

// Sega 315-5313 / YM7101 VDP (Genesis / Mega Drive). Completes the WIP
// sega_315_5313.pas: register / DMA / HV protocol from the Pascal chip,
// plus scanline rendering of planes A/B, the window, sprites, CRAM and
// shadow/highlight.
class Sega3155313 {
public:
    static constexpr int kVramSize = 0x10000;
    static constexpr int kCramSize = 64;
    static constexpr int kVsramSize = 40;
    static constexpr int kRegs = 32;
    static constexpr int kMaxWidth = 320;
    static constexpr int kMaxHeight = 240;
    static constexpr int kLinesNtsc = 262;
    static constexpr int kLinesPal = 313;
    static constexpr uint32_t kPsgClock = 3579545;

    using IrqHandler = std::function<void(bool)>;
    using DmaRead = std::function<uint16_t(uint32_t)>;
    using Z80IrqHandler = std::function<void(bool)>;

    explicit Sega3155313(bool pal = false);

    void reset();
    void set_irq_handlers(IrqHandler hint, IrqHandler vint) {
        hint_ = std::move(hint);
        vint_ = std::move(vint);
    }
    void set_z80_irq_handler(Z80IrqHandler handler) { z80_irq_ = std::move(handler); }
    void set_dma_reader(DmaRead reader) { dma_read_ = std::move(reader); }

    uint16_t read(uint8_t address);
    void write(uint8_t address, uint16_t value);
    // Byte write used by the Z80 / 68k MOVE.B to the PSG port.
    void write_byte(uint8_t address, uint8_t value);

    void handle_scanline(int line);
    void handle_eof();
    void set_hpos_cycles(int cycles) { hpos_cycles_ = cycles; }

    const uint32_t* line_buffer() const { return line_buf_.data(); }
    int screen_width() const { return h40_ ? 320 : 256; }
    int screen_height() const { return visible_scanlines_; }
    int total_scanlines() const { return total_scanlines_; }
    bool is_pal() const { return pal_; }
    bool display_enabled() const { return (regs_[0x01] & 0x40) != 0; }

    SN76496& psg() { return psg_; }
    const SN76496& psg() const { return psg_; }

    uint8_t reg(int index) const { return uint8_t(regs_[size_t(index) & 0x1f]); }
    uint8_t vram(uint16_t addr) const { return vram_[addr]; }
    void set_vram(uint16_t addr, uint8_t value) { vram_[addr] = value; }
    uint16_t cram(int index) const { return cram_[size_t(index) & 0x3f]; }
    uint16_t vsram(int index) const { return vsram_[size_t(index) % kVsramSize]; }

    // Direct VRAM/CRAM helpers used by tests.
    void poke_vram_word(uint16_t addr, uint16_t value);
    void poke_cram(int index, uint16_t value);
    void poke_reg(int index, uint8_t value);

private:
    uint16_t control_port_read();
    void control_port_write(uint16_t value);
    void data_port_write(uint16_t value);
    uint16_t data_port_read();
    void update_code_and_address();
    void set_register(int regnum, uint8_t value);
    void handle_dma();
    void dma_68k_copy();
    void dma_vram_copy();
    void dma_fill(uint16_t value);
    void write_vram_word(uint16_t value);
    uint16_t read_vram_word(uint16_t addr) const;
    void write_cram_word(uint16_t value);
    void write_vsram_word(uint16_t value);
    void increment_address();
    void render_line(int line);
    void draw_plane(int line, bool plane_a, uint8_t* color, uint8_t* pri);
    void draw_window(int line, uint8_t* color, uint8_t* pri);
    void draw_sprites(int line, uint8_t* color, uint8_t* pri);
    void blit_tile_row(int x, uint32_t row, int palette, bool hflip, bool priority,
                       uint8_t* color, uint8_t* pri, int width) const;
    uint32_t tile_row_bits(uint16_t tile, int row) const;
    uint32_t palette_rgb(uint8_t index, bool shadow, bool highlight) const;
    uint8_t v_counter(int line) const;
    int plane_width() const;
    int plane_height() const;
    bool h40() const { return (regs_[0x0c] & 0x81) != 0; }

    void raise_hint(bool state);
    void raise_vint(bool state);
    void raise_z80_irq(bool state);

    IrqHandler hint_;
    IrqHandler vint_;
    Z80IrqHandler z80_irq_;
    DmaRead dma_read_;
    SN76496 psg_;

    bool pal_ = false;
    bool vblank_flag_ = false;
    bool sprite_collision_ = false;
    bool sprite_overflow_ = false;
    bool command_pending_ = false;
    bool irq4_pending_ = false;
    bool irq6_pending_ = false;
    bool vram_fill_pending_ = false;
    bool dma_active_ = false;
    uint8_t imode_ = 0;
    uint8_t imode_odd_frame_ = 0;
    uint8_t vdp_code_ = 0;
    uint16_t command_part1_ = 0;
    uint16_t command_part2_ = 0;
    uint16_t vdp_address_ = 0;
    uint16_t vram_fill_length_ = 0;
    uint16_t data_read_buffer_ = 0;
    int total_scanlines_ = kLinesNtsc;
    int visible_scanlines_ = 224;
    int irq6_scanline_ = 224;
    int z80irq_scanline_ = 226;
    int hint_counter_ = 0;
    int hpos_cycles_ = 0;
    int current_line_ = 0;
    bool h40_ = true;

    std::array<uint16_t, kRegs> regs_{};
    std::array<uint8_t, kVramSize> vram_{};
    std::array<uint16_t, kCramSize> cram_{};
    std::array<uint16_t, kVsramSize> vsram_{};
    std::array<uint32_t, 64> palette_{};
    std::array<uint32_t, kMaxWidth> line_buf_{};
};

}  // namespace dsp
