#pragma once

#include <array>
#include <cstdint>
#include <functional>

namespace dsp {

// Texas Instruments TMS9918A Video Display Processor, ported from tms99xx.pas.
// Supports Graphics I, Graphics II, Multicolor and Text modes plus sprites
// (8x8/16x16, magnification, 5th sprite flag and collision), which covers the
// large majority of ColecoVision software.
class TMS9918 {
public:
    using InterruptHandler = std::function<void(bool asserted)>;

    static constexpr int kScreenWidth = 256;
    static constexpr int kScreenHeight = 192;
    static constexpr int kVramSize = 0x4000;  // 16 KiB

    // `player` is unused (kept to mirror tms99xx_chip.create(player, callback) in
    // the Pascal core, which supported two VDPs for split-screen arcade boards).
    explicit TMS9918(int player, InterruptHandler on_interrupt);

    void reset();

    // CPU facing ports ($a0/$a1 on the ColecoVision, i.e. vram_r/register_r and
    // vram_w/register_w in coleco.pas).
    uint8_t vram_read();
    void vram_write(uint8_t value);
    uint8_t register_read();
    void register_write(uint8_t value);

    // Advances the VDP by one scanline; `line` runs 0..261 (NTSC), matching the
    // `f` loop in coleco_principal / tms_0.refresh_ntsc(f).
    void refresh_ntsc(int line);

    const uint32_t* framebuffer() const { return framebuffer_.data(); }

private:
    void render_scanline(int line);
    void render_graphics1(int line, std::array<uint8_t, kScreenWidth>& pen);
    void render_graphics2(int line, std::array<uint8_t, kScreenWidth>& pen);
    void render_multicolor(int line, std::array<uint8_t, kScreenWidth>& pen);
    void render_text(int line, std::array<uint8_t, kScreenWidth>& pen);
    void render_sprites(int line, std::array<uint8_t, kScreenWidth>& pen);
    void plot_row(int line, const std::array<uint8_t, kScreenWidth>& pen);

    bool graphics2_mode() const { return (registers_[0] & 0x02) != 0; }
    bool multicolor_mode() const { return (registers_[1] & 0x08) != 0; }
    bool text_mode() const { return (registers_[1] & 0x10) != 0; }
    bool display_enabled() const { return (registers_[1] & 0x40) != 0; }
    bool interrupt_enabled() const { return (registers_[1] & 0x20) != 0; }
    bool sprites_large() const { return (registers_[1] & 0x02) != 0; }
    bool sprites_magnified() const { return (registers_[1] & 0x01) != 0; }

    uint16_t name_table_base() const { return uint16_t((registers_[2] & 0x0f) << 10); }
    uint16_t color_table_base() const { return uint16_t(registers_[3] << 6); }
    uint16_t pattern_table_base() const { return uint16_t((registers_[4] & 0x07) << 11); }
    uint16_t sprite_attr_base() const { return uint16_t((registers_[5] & 0x7f) << 7); }
    uint16_t sprite_pattern_base() const { return uint16_t((registers_[6] & 0x07) << 11); }
    uint8_t backdrop_color() const { return uint8_t(registers_[7] & 0x0f); }
    uint8_t text_color() const { return uint8_t(registers_[7] >> 4); }

    InterruptHandler on_interrupt_;

    std::array<uint8_t, kVramSize> vram_{};
    std::array<uint8_t, 8> registers_{};
    std::array<uint32_t, kScreenWidth * kScreenHeight> framebuffer_{};

    void update_interrupt_line();

    uint16_t address_ = 0;
    uint8_t read_buffer_ = 0;
    uint8_t status_ = 0;
    uint8_t latch_byte_ = 0;
    bool latch_pending_ = false;
    bool last_int_line_ = false;

    static const std::array<uint32_t, 16> kPalette;
};

}  // namespace dsp
