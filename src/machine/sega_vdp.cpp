#include "machine/sega_vdp.h"

#include <algorithm>
#include <cstring>

namespace dsp {
namespace {

constexpr uint32_t kArgb(uint8_t r, uint8_t g, uint8_t b) {
    return 0xff000000u | (uint32_t(r) << 16) | (uint32_t(g) << 8) | uint32_t(b);
}

// Approximate TMS9918 palette used by the original (tms992X_palete).
constexpr uint8_t kTmsRgb[16][3] = {
    {0, 0, 0},     {0, 0, 0},     {0, 160, 0},   {0, 238, 0},
    {0, 0, 98},    {0, 0, 238},   {78, 0, 0},    {0, 238, 238},
    {160, 0, 0},   {238, 0, 0},   {78, 78, 0},   {238, 238, 0},
    {0, 78, 0},    {238, 0, 238}, {78, 78, 98},  {238, 238, 238},
};

uint8_t pal2bit(uint8_t n) {
    // 2-bit component → 8-bit (00,55,AA,FF style used by many SMS emulators).
    static const uint8_t table[4] = {0x00, 0x55, 0xaa, 0xff};
    return table[n & 3];
}

uint8_t pal4bit(uint8_t n) {
    n &= 0x0f;
    return uint8_t((n << 4) | n);
}

}  // namespace

// ---------------------------------------------------------------------------
// H-counter conversion (cycles 0..227 → SMS H-counter byte)
// ---------------------------------------------------------------------------
const uint8_t SegaVdp::kHposConv[228] = {
    0x00, 0x01, 0x02, 0x02, 0x03, 0x04, 0x05, 0x05, 0x06, 0x07, 0x08, 0x08, 0x09, 0x0a, 0x0b, 0x0b,
    0x0c, 0x0d, 0x0e, 0x0e, 0x0f, 0x10, 0x11, 0x11, 0x12, 0x13, 0x14, 0x14, 0x15, 0x16, 0x17, 0x17,
    0x18, 0x19, 0x1a, 0x1a, 0x1b, 0x1c, 0x1d, 0x1d, 0x1e, 0x1f, 0x20, 0x20, 0x21, 0x22, 0x23, 0x23,
    0x24, 0x25, 0x26, 0x26, 0x27, 0x28, 0x29, 0x29, 0x2a, 0x2b, 0x2c, 0x2c, 0x2d, 0x2e, 0x2f, 0x2f,
    0x30, 0x31, 0x32, 0x32, 0x33, 0x34, 0x35, 0x35, 0x36, 0x37, 0x38, 0x38, 0x39, 0x3a, 0x3b, 0x3b,
    0x3c, 0x3d, 0x3e, 0x3e, 0x3f, 0x40, 0x41, 0x41, 0x42, 0x43, 0x44, 0x44, 0x45, 0x46, 0x47, 0x47,
    0x48, 0x49, 0x4a, 0x4a, 0x4b, 0x4c, 0x4d, 0x4d, 0x4e, 0x4f, 0x50, 0x50, 0x51, 0x52, 0x53, 0x53,
    0x54, 0x55, 0x56, 0x56, 0x57, 0x58, 0x59, 0x59, 0x5a, 0x5b, 0x5c, 0x5c, 0x5d, 0x5e, 0x5f, 0x5f,
    0x60, 0x61, 0x62, 0x62, 0x63, 0x64, 0x65, 0x65, 0x66, 0x67, 0x68, 0x68, 0x69, 0x6a, 0x6b, 0x6b,
    0x6c, 0x6d, 0x6e, 0x6e, 0x6f, 0x70, 0x71, 0x71, 0x72, 0x73, 0x74, 0x74, 0x75, 0x76, 0x77, 0x77,
    0x78, 0x79, 0x7a, 0x7a, 0x7b, 0x7c, 0x7d, 0x7d, 0x7e, 0x7f, 0x80, 0x80, 0x81, 0x82, 0x83, 0x83,
    0x84, 0x85, 0x86, 0x86, 0x87, 0x88, 0x89, 0x89, 0x8a, 0x8b, 0x8c, 0x8c, 0x8d, 0x8e, 0x8f, 0x8f,
    0x90, 0x91, 0x92, 0x92, 0x93, 0xe9, 0xea, 0xea, 0xeb, 0xec, 0xed, 0xed, 0xee, 0xef, 0xf0, 0xf0,
    0xf1, 0xf2, 0xf3, 0xf3, 0xf4, 0xf5, 0xf6, 0xf6, 0xf7, 0xf8, 0xf9, 0xf9, 0xfa, 0xfb, 0xfc, 0xfc,
    0xfd, 0xfe, 0xff, 0xff,
};

// ---------------------------------------------------------------------------
// Construction / reset
// ---------------------------------------------------------------------------
SegaVdp::SegaVdp(IrqHandler irq) : irq_handler_(std::move(irq)) {
    for (int i = 0; i < 16; ++i) {
        tms_pal_[i] = rgb_tms(i);
    }
    set_gg(false);
    reset();
}

void SegaVdp::reset() {
    vram_.fill(0);
    cram_.fill(0);
    regs_.fill(0);
    regs_[0x0a] = 0xff;
    regs_[2] = 0x0e;
    regs_[1] = 0x20;
    current_pal_.fill(0);
    for (int i = 0; i < 32; ++i) update_palette_entry(i);

    addr_ = 0;
    addr_mode_ = 0;
    buffer_ = 0;
    second_byte_ = false;
    status_ = 0;
    irq_pending_ = false;
    hint_ = false;
    video_mode_ = 0;
    hpos_ = 0;
    hpos_temp_ = 0;
    reg8_tmp_ = 0;
    reg9_tmp_ = 0;
    vdp_mode_ = true;
    display_disabled_ = false;
    sprite_count_ = 0;
    sprite_zoom_ = 1;
    sprite_x_.fill(0);
    sprite_tile_.fill(0);
    sprite_pattern_line_.fill(0);
    line_counter_ = 0;
    linea_back_ = 0;

    if (is_pal_) {
        video_pal(0);
    } else {
        video_ntsc(0);
    }
}

void SegaVdp::set_gg(bool is_gg) {
    gg_ = is_gg;
    cram_mask_ = is_gg ? 0x3f : 0x1f;
    for (int i = 0; i < 32; ++i) update_palette_entry(i);
}

void SegaVdp::assert_irq(bool state) {
    irq_pending_ = state;
    if (irq_handler_) irq_handler_(state);
}

// ---------------------------------------------------------------------------
// Palette
// ---------------------------------------------------------------------------
uint32_t SegaVdp::rgb_tms(int index) {
    const auto& c = kTmsRgb[index & 15];
    return kArgb(c[0], c[1], c[2]);
}

uint32_t SegaVdp::rgb_sms(uint8_t cram_byte) {
    const uint8_t r = pal2bit(cram_byte & 3);
    const uint8_t g = pal2bit((cram_byte >> 2) & 3);
    const uint8_t b = pal2bit((cram_byte >> 4) & 3);
    return kArgb(r, g, b);
}

uint32_t SegaVdp::rgb_gg(uint16_t cram_word) {
    const uint8_t r = pal4bit(cram_word & 0xf);
    const uint8_t g = pal4bit((cram_word >> 4) & 0xf);
    const uint8_t b = pal4bit((cram_word >> 8) & 0xf);
    return kArgb(r, g, b);
}

void SegaVdp::update_palette_entry(int index) {
    index &= 0x1f;
    if (gg_) {
        const int base = (index & 0x1e);
        const uint16_t word = uint16_t(cram_[base] | (cram_[base + 1] << 8));
        current_pal_[index] = rgb_gg(word);
    } else {
        current_pal_[index] = rgb_sms(cram_[index] & 0x3f);
    }
}

void SegaVdp::cram_write(uint8_t value) {
    const uint8_t address = uint8_t(addr_ & cram_mask_);
    if (cram_[address] == value) return;
    cram_[address] = value;
    if (gg_) {
        update_palette_entry(address >> 1);
    } else {
        update_palette_entry(address);
    }
}

// ---------------------------------------------------------------------------
// NTSC / PAL geometry
// ---------------------------------------------------------------------------
void SegaVdp::video_pal(int mode) {
    video_visible_y_total_ = 294;
    video_y_total_ = kLinesPal;
    is_pal_ = true;
    border_diff_ = 27;
    video_mode_ = mode;
    switch (mode) {
        case 1:  // 256×224
            y_pixels_ = 224;
            lines_top_border_ = 38;
            line_border_down_ = 256;
            break;
        case 2:  // 256×240
            y_pixels_ = 240;
            lines_top_border_ = 30;
            line_border_down_ = 264;
            break;
        default:  // 256×192
            y_pixels_ = 192;
            lines_top_border_ = 54;
            line_border_down_ = 240;
            break;
    }
}

void SegaVdp::video_ntsc(int mode) {
    video_visible_y_total_ = 243;
    video_y_total_ = kLinesNtsc;
    is_pal_ = false;
    border_diff_ = 0;
    video_mode_ = mode;
    switch (mode) {
        case 1:
            y_pixels_ = 224;
            lines_top_border_ = 11;
            line_border_down_ = 232;
            break;
        case 2:
            y_pixels_ = 240;
            lines_top_border_ = 3;
            line_border_down_ = 240;
            break;
        default:
            y_pixels_ = 192;
            lines_top_border_ = 27;
            line_border_down_ = 216;
            break;
    }
}

void SegaVdp::video_change() {
    int new_video = video_mode_;
    if ((regs_[0] & 0x04) != 0) {
        vdp_mode_ = true;
        if ((regs_[0] & 0x02) != 0) {
            if ((regs_[1] & 0x10) != 0 && (regs_[1] & 0x08) == 0) new_video = 1;
            if ((regs_[1] & 0x10) == 0 && (regs_[1] & 0x08) != 0) new_video = 2;
        }
    } else {
        vdp_mode_ = false;
    }
    if (video_mode_ != new_video) {
        if (is_pal_) {
            video_pal(new_video);
        } else {
            video_ntsc(new_video);
        }
    }
}

// ---------------------------------------------------------------------------
// CPU ports
// ---------------------------------------------------------------------------
uint8_t SegaVdp::vram_r() {
    const uint8_t value = buffer_;
    buffer_ = vram_[addr_ & 0x3fff];
    addr_ = (addr_ + 1) & 0x3fff;
    second_byte_ = false;
    return value;
}

void SegaVdp::vram_w(uint8_t value) {
    if (addr_mode_ == 3) {
        cram_write(value);
        buffer_ = value;
        addr_ = (addr_ + 1) & 0x3fff;
        second_byte_ = false;
        return;
    }
    // VRAM write
    vram_[addr_ & 0x3fff] = value;
    buffer_ = value;
    addr_ = (addr_ + 1) & 0x3fff;
    second_byte_ = false;
}

int SegaVdp::register_r() {
    if (vdp_mode_) {
        // PGA Tour Golf hangs without the $1d low bits.
        const int value = (status_ & 0xe0) | 0x1d;
        status_ = 0;
        hint_ = false;
        if (irq_pending_) {
            assert_irq(false);
        }
        second_byte_ = false;
        return value;
    }
    // TMS status stub
    const int value = status_;
    status_ = 0;
    second_byte_ = false;
    return value;
}

void SegaVdp::register_w(uint8_t value) {
    if (!second_byte_) {
        addr_ = (addr_ & 0xff00) | value;
        addr_ &= 0x3fff;
        second_byte_ = true;
        return;
    }

    addr_mode_ = (value & 0xc0) >> 6;
    if (vdp_mode_) {
        second_byte_ = false;
        addr_ = ((addr_ & 0xff) | (uint16_t(value) << 8)) & 0x3fff;
        switch (addr_mode_) {
            case 0:  // VRAM read
                buffer_ = vram_[addr_ & 0x3fff];
                addr_ = (addr_ + 1) & 0x3fff;
                break;
            case 1:  // VRAM write
            case 3:  // CRAM write
                break;
            case 2: {  // register write
                const uint8_t reg = value & 0x0f;
                regs_[reg] = uint8_t(addr_ & 0xff);
                video_change();
                reg8_tmp_ = regs_[8];
                // IRQ enable/disable side effects
                if ((reg == 0 && hint_) || (reg == 1 && (status_ & kStatusFrame) != 0)) {
                    const bool disable =
                        (reg == 0 && (regs_[0] & 0x10) == 0) || (reg == 1 && (regs_[1] & 0x20) == 0);
                    if (disable) {
                        if (irq_pending_) assert_irq(false);
                    } else {
                        assert_irq(true);
                    }
                }
                addr_mode_ = 0;
                break;
            }
        }
    } else {
        // TMS register write path (minimal).
        second_byte_ = false;
        addr_ = ((addr_ & 0xff) | (uint16_t(value) << 8)) & 0x3fff;
        if (addr_mode_ == 2) {
            regs_[value & 0x07] = uint8_t(addr_ & 0xff);
        }
    }
}

void SegaVdp::set_hpos(int cpu_cycles_on_line) {
    if (cpu_cycles_on_line < 0) cpu_cycles_on_line = 0;
    if (cpu_cycles_on_line > 227) cpu_cycles_on_line = 227;
    hpos_temp_ = kHposConv[cpu_cycles_on_line];
}

// ---------------------------------------------------------------------------
// Sprites
// ---------------------------------------------------------------------------
void SegaVdp::select_sprites(int line) {
    const int sprite_height = (regs_[1] & 0x02) ? 16 : 8;
    sprite_zoom_ = (regs_[1] & 0x01) ? 2 : 1;
    sprite_count_ = 0;
    const uint16_t sprite_base = uint16_t((regs_[5] << 7) & 0x3f00);
    const int max_sprites = 8;

    for (int sprite_index = 0; sprite_index < 64; ++sprite_index) {
        int parse_line = (line - 1) & 0xff;
        int sprite_y = vram_[(sprite_base + sprite_index) & 0x3fff];

        if (y_pixels_ == 192 && sprite_y == 0xd0) break;

        int adj_parse = parse_line;
        int adj_y = sprite_y;
        if (sprite_zoom_ > 1 && sprite_count_ < 8) {
            adj_parse >>= 1;
            adj_y >>= 1;
        }

        int sprite_line = adj_parse - adj_y;
        if (adj_y > 0xe0) sprite_line = 0xff + sprite_line;

        if (sprite_line >= 0 && sprite_line < sprite_height) {
            if (sprite_count_ < max_sprites) {
                uint16_t sprite_x = vram_[(sprite_base + 0x80 + (sprite_index << 1)) & 0x3fff];
                uint16_t tile = vram_[(sprite_base + 0x81 + (sprite_index << 1)) & 0x3fff];
                if (regs_[0] & 0x08) sprite_x = uint16_t(int(sprite_x) - 8);
                if (regs_[6] & 0x04) tile |= 0x100;
                if (sprite_height == 16) tile &= 0x1fe;
                if (sprite_line > 7) tile = uint16_t(tile + 1);

                sprite_x_[sprite_count_] = sprite_x;
                sprite_tile_[sprite_count_] = tile;
                sprite_pattern_line_[sprite_count_] = uint16_t((sprite_line & 7) << 2);
                ++sprite_count_;
            } else {
                status_ |= kStatusSprOvr;
            }
        }
    }
}

void SegaVdp::draw_sprites() {
    if (sprite_count_ == 0) return;

    std::array<uint8_t, 256> collision{};
    bool sprite_col = false;

    for (int si = sprite_count_ - 1; si >= 0; --si) {
        const uint16_t sx = sprite_x_[si];
        const uint16_t tile = sprite_tile_[si];
        const uint16_t pattern_line = sprite_pattern_line_[si];
        const uint16_t base = uint16_t((tile << 5) + pattern_line);

        const uint8_t bp0 = vram_[(base + 0) & 0x3fff];
        const uint8_t bp1 = vram_[(base + 1) & 0x3fff];
        const uint8_t bp2 = vram_[(base + 2) & 0x3fff];
        const uint8_t bp3 = vram_[(base + 3) & 0x3fff];

        for (int px = 0; px < 8; ++px) {
            const uint8_t pen = uint8_t(
                (((bp3 >> (7 - px)) & 1) << 3) | (((bp2 >> (7 - px)) & 1) << 2) |
                (((bp1 >> (7 - px)) & 1) << 1) | ((bp0 >> (7 - px)) & 1) | 0x10);
            if (pen == 0x10) continue;  // transparent

            int plot_x = (sprite_zoom_ > 1) ? int(sx) + (px << 1) : int(sx) + px;
            for (int z = 0; z < sprite_zoom_; ++z) {
                const int x = plot_x + z;
                if (x < 0 || x > 255) continue;

                if ((priority_[x] & kPriorityBit) == 0) {
                    line_buf_[x + kPixelsLeftBorder] = current_pal_[pen & 0x1f];
                    priority_[x] = pen;
                } else if ((priority_[x] & 0x000f) == 0) {
                    // Background was transparent under priority bit.
                    line_buf_[x + kPixelsLeftBorder] = current_pal_[pen & 0x1f];
                }

                if (collision[x] != 1) {
                    collision[x] = 1;
                } else {
                    sprite_col = true;
                }
            }
        }
    }
    if (sprite_col) status_ |= kStatusSprCol;
}

// ---------------------------------------------------------------------------
// Background (Mode 4)
// ---------------------------------------------------------------------------
uint16_t SegaVdp::name_table_row(const SegaVdp& vdp, int row) {
    if (vdp.y_pixels_ == 192 && !vdp.gg_) {
        return uint16_t(((row >> 3) << 6) & (((vdp.regs_[2] & 1) << 10) | 0x3bff));
    }
    return uint16_t((row >> 3) << 6);
}

void SegaVdp::draw_mode_sms(int line) {
    // Left border colour from backdrop (sprite palette + reg7).
    const uint32_t border = current_pal_[0x10 + (regs_[7] & 0x0f)];

    // Clear left strip of the active area with colour 0 of the BG palette.
    for (int x = 0; x < 8; ++x) {
        line_buf_[kPixelsLeftBorder + x] = current_pal_[0];
    }

    uint8_t x_scroll = reg8_tmp_;
    if ((regs_[0] & 0x40) != 0 && line < 16) x_scroll = 0;

    const int x_scroll_start_column = 32 - (x_scroll >> 3);
    const int scroll_x_fine = x_scroll & 7;

    uint16_t name_table_address;
    int scroll_mod;
    if (y_pixels_ != 192) {
        name_table_address = uint16_t(((regs_[2] & 0x0c) << 10) | 0x0700);
        scroll_mod = 256;
    } else {
        name_table_address = uint16_t((regs_[2] << 10) & 0x3800);
        scroll_mod = 224;
    }

    for (int tile_column = 0; tile_column < 32; ++tile_column) {
        uint8_t y_scroll = reg9_tmp_;
        if ((regs_[0] & 0x80) != 0 && tile_column > 23) y_scroll = 0;

        const int tile_line_off = ((tile_column + x_scroll_start_column) & 0x1f) << 1;
        const int row = (line + y_scroll) % scroll_mod;
        const uint16_t addr =
            uint16_t(name_table_address + name_table_row(*this, row) + tile_line_off);

        const uint16_t tile_data =
            uint16_t(vram_[addr & 0x3fff] | (vram_[(addr + 1) & 0x3fff] << 8));
        const uint16_t tile_selected = tile_data & 0x1ff;
        const uint16_t priority_select = tile_data & kPriorityBit;
        const bool palette_selected = ((tile_data >> 11) & 1) != 0;
        const bool flip_y = ((tile_data >> 10) & 1) != 0;
        const bool flip_x = ((tile_data >> 9) & 1) != 0;

        // Fine vertical position inside the tile.
        int tile_line = line - ((7 - (y_scroll & 7)) + 1);
        // Original formula effectively uses (line + y_scroll) mod 8 for the pattern line.
        tile_line = (line + y_scroll) & 7;
        if (flip_y) tile_line = 7 - tile_line;

        const uint16_t pattern_base = uint16_t((tile_selected << 5) + (tile_line << 2));
        const uint8_t bp0 = vram_[(pattern_base + 0) & 0x3fff];
        const uint8_t bp1 = vram_[(pattern_base + 1) & 0x3fff];
        const uint8_t bp2 = vram_[(pattern_base + 2) & 0x3fff];
        const uint8_t bp3 = vram_[(pattern_base + 3) & 0x3fff];

        for (int px = 0; px < 8; ++px) {
            uint8_t pen = uint8_t(
                (((bp3 >> (7 - px)) & 1) << 3) | (((bp2 >> (7 - px)) & 1) << 2) |
                (((bp1 >> (7 - px)) & 1) << 1) | ((bp0 >> (7 - px)) & 1));
            if (palette_selected) pen |= 0x10;

            int plot = flip_x ? (7 - px) : px;
            plot = (tile_column << 3) + plot + scroll_x_fine;
            if (plot < 256) {
                line_buf_[plot + kPixelsLeftBorder] = current_pal_[pen & 0x1f];
                priority_[plot] = uint16_t(priority_select | (pen & 0x0f));
            }
        }
    }

    // Borders
    for (int x = 0; x < kPixelsLeftBorder; ++x) line_buf_[x] = border;
    for (int x = 0; x < kPixelsRightBorder; ++x) {
        line_buf_[kPixelsLeftBorder + kPixelsActive + x] = border;
    }
}

void SegaVdp::fill_line(uint32_t color) {
    line_buf_.fill(color);
}

// ---------------------------------------------------------------------------
// Scanline refresh
// ---------------------------------------------------------------------------
void SegaVdp::refresh(int line) {
    display_disabled_ = (regs_[1] & 0x40) == 0;

    // V-counter readback value (linea_back).
    if (is_pal_) {
        switch (video_mode_) {
            case 0:
                linea_back_ = (line > 0xf2) ? uint8_t(line - 0x39) : uint8_t(line);
                break;
            case 1:
                linea_back_ = (line > 0x102) ? uint8_t(line - 0x39) : uint8_t(line & 0xff);
                break;
            default:
                linea_back_ = (line > 0x10a) ? uint8_t(line - 0x39) : uint8_t(line & 0xff);
                break;
        }
    } else {
        switch (video_mode_) {
            case 0:
                linea_back_ = (line > 0xda) ? uint8_t(line - 6) : uint8_t(line);
                break;
            case 1:
                linea_back_ = (line > 0xeb) ? uint8_t(line - 6) : uint8_t(line);
                break;
            default:
                linea_back_ = uint8_t(line & 0xff);
                break;
        }
    }

    // Line interrupt counter.
    if (line <= y_pixels_) {
        if (line_counter_ == 0) {
            line_counter_ = regs_[0x0a];
            hint_ = true;
            if ((regs_[0] & 0x10) != 0) assert_irq(true);
        } else {
            --line_counter_;
        }
    } else {
        line_counter_ = regs_[0x0a];
        reg9_tmp_ = regs_[9];
    }

    if (vdp_mode_) {
        // Frame IRQ: status bit set slightly after the last active line.
        if (line == y_pixels_) {
            status_ |= kStatusFrame;
        } else if (line >= y_pixels_ + 1) {
            if ((regs_[1] & 0x20) != 0 && (status_ & kStatusFrame) != 0) {
                assert_irq(true);
            }
        }

        const uint32_t border = current_pal_[0x10 + (regs_[7] & 0x0f)];

        if (line < y_pixels_) {
            if (!display_disabled_) {
                priority_.fill(0);
                draw_mode_sms(line);
                select_sprites(line);
                draw_sprites();
                // Leftmost 8 pixels forced to border when reg0 bit5 set.
                if ((regs_[0] & 0x20) != 0) {
                    for (int x = 0; x < 8; ++x) {
                        line_buf_[kPixelsLeftBorder + x] = border;
                    }
                }
            } else {
                select_sprites(line);
                fill_line(border);
            }
        } else if (line < line_border_down_) {
            fill_line(border);
        } else if (line >= line_border_down_ + 19) {
            fill_line(border);
        } else {
            fill_line(border);
        }

        // Latch H-counter for this line (driver may also call set_hpos).
        hpos_ = hpos_temp_;
    } else {
        // TMS mode stub: solid backdrop.
        fill_line(tms_pal_[regs_[7] & 0x0f]);
    }
}

}  // namespace dsp
