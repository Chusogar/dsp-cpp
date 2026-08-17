#include "video/tms9918.h"

#include <cstring>

namespace dsp {

// Standard TMS9918A 16 colour palette (approximate RGB values, as commonly
// published in TMS9918A technical references).
const std::array<uint32_t, 16> TMS9918::kPalette = {
    0xff000000u,  // 0 transparent (never sampled directly, backdrop is used instead)
    0xff000000u,  // 1 black
    0xff21c842u,  // 2 medium green
    0xff5edc78u,  // 3 light green
    0xff5455edu,  // 4 dark blue
    0xff7d76fcu,  // 5 light blue
    0xffd4524du,  // 6 dark red
    0xff42ebf5u,  // 7 cyan
    0xfffc5554u,  // 8 medium red
    0xffff7978u,  // 9 light red
    0xffd4c154u,  // 10 dark yellow
    0xffe6ce80u,  // 11 light yellow
    0xff21b03bu,  // 12 dark green
    0xffc95bbau,  // 13 magenta
    0xffccccccu,  // 14 gray
    0xffffffffu,  // 15 white
};

TMS9918::TMS9918(int /*player*/, InterruptHandler on_interrupt)
    : on_interrupt_(std::move(on_interrupt)) {
    reset();
}

void TMS9918::reset() {
    vram_.fill(0);
    registers_.fill(0);
    framebuffer_.fill(0xff000000u);
    address_ = 0;
    read_buffer_ = 0;
    status_ = 0;
    latch_byte_ = 0;
    latch_pending_ = false;
    last_int_line_ = false;
}

uint8_t TMS9918::vram_read() {
    uint8_t value = read_buffer_;
    read_buffer_ = vram_[address_ & 0x3fff];
    address_ = uint16_t((address_ + 1) & 0x3fff);
    latch_pending_ = false;
    return value;
}

void TMS9918::vram_write(uint8_t value) {
    vram_[address_ & 0x3fff] = value;
    address_ = uint16_t((address_ + 1) & 0x3fff);
    latch_pending_ = false;
}

uint8_t TMS9918::register_read() {
    uint8_t value = status_;
    status_ &= 0x1f;  // clear F, 5S and C, keep the latched 5th sprite index
    latch_pending_ = false;
    update_interrupt_line();
    return value;
}

void TMS9918::register_write(uint8_t value) {
    if (!latch_pending_) {
        latch_byte_ = value;
        latch_pending_ = true;
        return;
    }
    latch_pending_ = false;
    if ((value & 0x80) != 0) {
        registers_[value & 0x07] = latch_byte_;
        update_interrupt_line();
    } else {
        address_ = uint16_t(((value & 0x3f) << 8) | latch_byte_);
        if ((value & 0x40) == 0) {  // set up for a read
            read_buffer_ = vram_[address_ & 0x3fff];
            address_ = uint16_t((address_ + 1) & 0x3fff);
        }
    }
}

void TMS9918::update_interrupt_line() {
    bool level = ((status_ & 0x80) != 0) && interrupt_enabled();
    if (level != last_int_line_) {
        last_int_line_ = level;
        if (on_interrupt_) on_interrupt_(level);
    }
}

void TMS9918::render_graphics1(int line, std::array<uint8_t, kScreenWidth>& pen) {
    int tile_row = line / 8;
    int y_in_char = line % 8;
    uint16_t nt = uint16_t(name_table_base() + tile_row * 32);
    for (int col = 0; col < 32; col++) {
        uint8_t name = vram_[nt + col];
        uint8_t pattern = vram_[pattern_table_base() + name * 8 + y_in_char];
        uint8_t colors = vram_[color_table_base() + (name >> 3)];
        uint8_t fg = uint8_t(colors >> 4);
        uint8_t bg = uint8_t(colors & 0x0f);
        for (int bit = 0; bit < 8; bit++) {
            bool set = ((pattern >> (7 - bit)) & 1) != 0;
            uint8_t color = set ? fg : bg;
            pen[size_t(col * 8 + bit)] = color == 0 ? backdrop_color() : color;
        }
    }
}

void TMS9918::render_graphics2(int line, std::array<uint8_t, kScreenWidth>& pen) {
    int tile_row = line / 8;
    int y_in_char = line % 8;
    int third = tile_row / 8;
    uint16_t pattern_base = uint16_t(((registers_[4] & 0x04) != 0 ? 0x2000 : 0x0000) + third * 0x800);
    uint16_t color_base = uint16_t(((registers_[3] & 0x80) != 0 ? 0x2000 : 0x0000) + third * 0x800);
    uint16_t nt = uint16_t(name_table_base() + tile_row * 32);
    for (int col = 0; col < 32; col++) {
        uint8_t name = vram_[nt + col];
        uint16_t offset = uint16_t(name * 8 + y_in_char);
        uint8_t pattern = vram_[pattern_base + offset];
        uint8_t colors = vram_[color_base + offset];
        uint8_t fg = uint8_t(colors >> 4);
        uint8_t bg = uint8_t(colors & 0x0f);
        for (int bit = 0; bit < 8; bit++) {
            bool set = ((pattern >> (7 - bit)) & 1) != 0;
            uint8_t color = set ? fg : bg;
            pen[size_t(col * 8 + bit)] = color == 0 ? backdrop_color() : color;
        }
    }
}

void TMS9918::render_multicolor(int line, std::array<uint8_t, kScreenWidth>& pen) {
    int tile_row = line / 8;
    int row_in_char = line % 8;
    uint16_t block_offset = uint16_t((row_in_char / 4) * 2);
    uint16_t nt = uint16_t(name_table_base() + tile_row * 32);
    for (int col = 0; col < 32; col++) {
        uint8_t name = vram_[nt + col];
        uint8_t colors = vram_[pattern_table_base() + name * 8 + block_offset];
        uint8_t left = uint8_t(colors >> 4);
        uint8_t right = uint8_t(colors & 0x0f);
        for (int bit = 0; bit < 8; bit++) {
            uint8_t color = bit < 4 ? left : right;
            pen[size_t(col * 8 + bit)] = color == 0 ? backdrop_color() : color;
        }
    }
}

void TMS9918::render_text(int line, std::array<uint8_t, kScreenWidth>& pen) {
    int tile_row = line / 8;
    int y_in_char = line % 8;
    uint8_t fg = text_color();
    uint8_t bg = backdrop_color();
    for (int x = 0; x < 8; x++) pen[size_t(x)] = bg;
    for (int x = 248; x < kScreenWidth; x++) pen[size_t(x)] = bg;
    uint16_t nt = uint16_t(name_table_base() + tile_row * 40);
    for (int col = 0; col < 40; col++) {
        uint8_t name = vram_[nt + col];
        uint8_t pattern = vram_[pattern_table_base() + name * 8 + y_in_char];
        for (int bit = 0; bit < 6; bit++) {
            bool set = ((pattern >> (7 - bit)) & 1) != 0;
            pen[size_t(8 + col * 6 + bit)] = set ? fg : bg;
        }
    }
}

void TMS9918::render_sprites(int line, std::array<uint8_t, kScreenWidth>& pen) {
    std::array<bool, kScreenWidth> occupied{};
    uint16_t base = sprite_attr_base();
    int height = sprites_large() ? 16 : 8;
    int mag = sprites_magnified() ? 2 : 1;
    int draw_size = height * mag;
    int drawn = 0;

    for (int index = 0; index < 32; index++) {
        uint8_t y_byte = vram_[base + index * 4 + 0];
        if (y_byte == 0xd0) break;  // sprite list terminator
        // Unsigned 8-bit Y: sprite starts at (Y+1) mod 256. int8_t Y hid
        // every sprite with Y>=128 (the bottom half of the 192-line screen).
        int top = (int(y_byte) + 1) & 0xff;
        int row = (line - top) & 0xff;
        if (row >= draw_size) continue;

        if (drawn >= 4) {
            status_ |= 0x40;  // 5th sprite flag
            if ((status_ & 0x1f) == 0 && index != 0) status_ = uint8_t((status_ & 0xe0) | index);
            break;
        }
        drawn++;

        uint8_t x_byte = vram_[base + index * 4 + 1];
        uint8_t name = vram_[base + index * 4 + 2];
        uint8_t flags = vram_[base + index * 4 + 3];
        int actual_x = int(x_byte) - ((flags & 0x80) != 0 ? 32 : 0);
        uint8_t color = uint8_t(flags & 0x0f);
        if (color == 0) continue;  // fully transparent

        int analog_row = row / mag;
        uint16_t sprite_base =
            uint16_t(sprite_pattern_base() + (sprites_large() ? (name & 0xfc) : name) * 8);
        uint8_t left_byte, right_byte = 0;
        if (sprites_large()) {
            int half = analog_row / 8;
            int sub_row = analog_row % 8;
            left_byte = vram_[sprite_base + half * 8 + sub_row];
            right_byte = vram_[sprite_base + 16 + half * 8 + sub_row];
        } else {
            left_byte = vram_[sprite_base + analog_row];
        }

        int width = (sprites_large() ? 16 : 8) * mag;
        for (int px = 0; px < width; px++) {
            int src_col = px / mag;
            uint8_t byte_value = (sprites_large() && src_col >= 8) ? right_byte : left_byte;
            int bit_index = src_col % 8;
            bool set = ((byte_value >> (7 - bit_index)) & 1) != 0;
            if (!set) continue;
            int screen_x = actual_x + px;
            if (screen_x < 0 || screen_x >= kScreenWidth) continue;
            if (occupied[size_t(screen_x)]) {
                status_ |= 0x20;  // sprite collision
                continue;
            }
            occupied[size_t(screen_x)] = true;
            pen[size_t(screen_x)] = color;
        }
    }
}

void TMS9918::render_scanline(int line) {
    std::array<uint8_t, kScreenWidth> pen{};
    pen.fill(backdrop_color());

    if (display_enabled()) {
        if (text_mode()) {
            render_text(line, pen);
        } else if (multicolor_mode()) {
            render_multicolor(line, pen);
        } else if (graphics2_mode()) {
            render_graphics2(line, pen);
        } else {
            render_graphics1(line, pen);
        }
        if (!text_mode()) render_sprites(line, pen);
    }

    plot_row(line, pen);
}

void TMS9918::plot_row(int line, const std::array<uint8_t, kScreenWidth>& pen) {
    uint32_t* row = &framebuffer_[size_t(line) * kScreenWidth];
    for (int x = 0; x < kScreenWidth; x++) row[x] = kPalette[pen[size_t(x)]];
}

void TMS9918::refresh_ntsc(int line) {
    if (line >= 0 && line < kScreenHeight) {
        render_scanline(line);
    } else if (line == kScreenHeight) {
        status_ |= 0x80;  // frame (vblank) flag
        update_interrupt_line();
    }
}

}  // namespace dsp
