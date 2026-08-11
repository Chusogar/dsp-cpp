#pragma once

#include <array>
#include <cstdint>
#include <functional>

namespace dsp {

// Sega 315-5124 / 315-5246 VDP (Master System / Game Gear / Mark III).
// Ported from sega_vdp.pas (dsp-emulator). Focus is Mode 4 (SMS);
// TMS9918 modes are stubbed for SG-1000 compatibility later.
//
// The chip owns 16 KiB VRAM + CRAM, 16 control registers and renders one
// scanline at a time into an ARGB8888 line buffer of kVisibleWidth pixels
// (284 = 13 left border + 256 active + 15 right border).
class SegaVdp {
public:
    static constexpr int kVramSize = 0x4000;
    static constexpr int kCramSize = 0x40;  // GG uses 64 bytes; SMS uses 32
    static constexpr int kRegs = 16;

    static constexpr int kLinesNtsc = 262;
    static constexpr int kLinesPal = 313;

    static constexpr int kPixelsLeftBorder = 13;
    static constexpr int kPixelsActive = 256;
    static constexpr int kPixelsRightBorder = 15;
    static constexpr int kVisibleWidth = kPixelsLeftBorder + kPixelsActive + kPixelsRightBorder;  // 284

    static constexpr uint16_t kPriorityBit = 0x1000;
    static constexpr uint8_t kStatusSprOvr = 0x40;
    static constexpr uint8_t kStatusSprCol = 0x20;
    static constexpr uint8_t kStatusFrame = 0x80;

    // IRQ line callback: true = assert, false = clear.
    using IrqHandler = std::function<void(bool)>;

    explicit SegaVdp(IrqHandler irq = nullptr);

    void reset();
    void set_gg(bool is_gg);
    void set_irq_handler(IrqHandler handler) { irq_handler_ = std::move(handler); }

    // NTSC / PAL geometry (192 / 224 / 240 line modes).
    void video_ntsc(int mode);
    void video_pal(int mode);

    // CPU interface (ports $BE data, $BF control on SMS).
    uint8_t vram_r();
    void vram_w(uint8_t value);
    int register_r();  // status (clears IRQ flags)
    void register_w(uint8_t value);

    // Called once per scanline by the machine driver (0 .. video_y_total()-1).
    // Fills line_buffer() with ARGB8888 pixels when the line is visible.
    void refresh(int line);

    // H-counter latch (used by port $7E / TH pin timing).
    void set_hpos(int cpu_cycles_on_line);
    uint8_t hpos() const { return hpos_; }
    uint8_t linea_back() const { return linea_back_; }

    // Geometry queries for the driver frame loop.
    int video_y_total() const { return video_y_total_; }
    int video_visible_y_total() const { return video_visible_y_total_; }
    int border_diff() const { return border_diff_; }
    int y_pixels() const { return y_pixels_; }
    int lines_top_border() const { return lines_top_border_; }
    bool is_pal() const { return is_pal_; }
    bool is_gg() const { return gg_; }
    int video_mode() const { return video_mode_; }

    // Rendered scanline (ARGB8888, kVisibleWidth pixels). Valid after refresh().
    const uint32_t* line_buffer() const { return line_buf_.data(); }

    // Direct VRAM access (tests / snapshots).
    uint8_t read_vram(uint16_t addr) const { return vram_[addr & 0x3fff]; }
    void write_vram(uint16_t addr, uint8_t value) { vram_[addr & 0x3fff] = value; }
    const uint8_t* regs() const { return regs_.data(); }

private:
    void select_sprites(int line);
    void draw_sprites();
    void draw_mode_sms(int line);
    void video_change();
    void cram_write(uint8_t value);
    void update_palette_entry(int index);
    void fill_line(uint32_t color);
    void assert_irq(bool state);

    static uint16_t name_table_row(const SegaVdp& vdp, int row);

    // ARGB helpers matching original pal2bit / pal4bit.
    static uint32_t rgb_sms(uint8_t cram_byte);
    static uint32_t rgb_gg(uint16_t cram_word);
    static uint32_t rgb_tms(int index);

    IrqHandler irq_handler_;

    std::array<uint8_t, kVramSize> vram_{};
    std::array<uint8_t, kCramSize> cram_{};
    std::array<uint8_t, kRegs> regs_{};
    std::array<uint32_t, 32> current_pal_{};  // SMS: 32 pens (2×16); GG reuses low 32
    std::array<uint32_t, 16> tms_pal_{};

    // Port state
    uint16_t addr_ = 0;
    uint8_t addr_mode_ = 0;  // 0=VRAM read, 1=VRAM write, 2=reg, 3=CRAM
    uint8_t buffer_ = 0;
    bool second_byte_ = false;
    uint8_t status_ = 0;
    bool irq_pending_ = false;
    bool hint_ = false;

    // Video timing
    int video_mode_ = 0;  // 0=192, 1=224, 2=240
    int video_y_total_ = kLinesNtsc;
    int video_visible_y_total_ = 243;
    int border_diff_ = 0;
    int y_pixels_ = 192;
    int lines_top_border_ = 27;
    int line_border_down_ = 216;
    bool is_pal_ = false;
    bool gg_ = false;
    bool vdp_mode_ = true;  // true = Mode 4 (SMS)
    bool display_disabled_ = false;
    uint8_t cram_mask_ = 0x1f;
    uint8_t linea_back_ = 0;
    uint8_t hpos_ = 0;
    uint8_t hpos_temp_ = 0;
    uint8_t line_counter_ = 0;
    uint8_t reg8_tmp_ = 0;
    uint8_t reg9_tmp_ = 0;

    // Sprites (max 8 per line)
    uint8_t sprite_count_ = 0;
    uint8_t sprite_zoom_ = 1;
    std::array<uint16_t, 8> sprite_x_{};
    std::array<uint16_t, 8> sprite_tile_{};
    std::array<uint16_t, 8> sprite_pattern_line_{};

    // Per-line priority buffer (pen | priority bit)
    std::array<uint16_t, 256> priority_{};
    std::array<uint32_t, kVisibleWidth> line_buf_{};

    // H-counter conversion table (228 → 8-bit hpos), from original.
    static const uint8_t kHposConv[228];
};

}  // namespace dsp
