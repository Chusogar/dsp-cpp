#include "video/deco_bac06.h"

namespace dsp {

Bac06Layer::Bac06Layer(uint16_t color_add, int mult, uint8_t color_mask)
    : color_add_(color_add), mult_(mult < 1 ? 1 : mult), color_mask_(color_mask) {
    reset();
}

void Bac06Layer::reset() {
    data.fill(0);
    control_0.fill(0xeeee);
    control_1.fill(0xeeee);
    colscroll.fill(0);
    rowscroll.fill(0);
    scroll_x_ = 0;
    scroll_y_ = 0;
    control_ = 0;
    row_block_ = 1;
    col_block_ = 1;
}

void Bac06Layer::change_control0(int pos, uint16_t value) {
    if (control_0[size_t(pos)] == value) return;
    control_0[size_t(pos)] = value;
    if (pos == 0) control_ = uint16_t(value & 0x0c);
}

void Bac06Layer::change_control1(int pos, uint16_t value, bool dec1) {
    if (control_1[size_t(pos)] == value) return;
    control_1[size_t(pos)] = value;
    switch (pos) {
        case 0: scroll_x_ = value; break;
        case 1: scroll_y_ = value; break;
        case 2: row_block_ = dec1 ? (16 >> (value & 3)) : (1 << (value & 0x0f)); break;
        case 3: col_block_ = 16 << (value & 0x0f); break;
        default: break;
    }
    if (row_block_ < 1) row_block_ = 1;
    if (col_block_ < 1) col_block_ = 1;
}

Bac06Layer::Geometry Bac06Layer::geometry() const {
    const int tile_size = (control_0[0] & 1) != 0 ? 8 : 16;
    switch (control_0[3] & 3) {
        case 0: return {1024 * mult_, 256, tile_size};
        case 1: return {512 * mult_, 512, tile_size};
        default: return {256 * mult_, 1024, tile_size};
    }
}

int Bac06Layer::tile_offset(int tile_x, int tile_y, int tile_size) const {
    if (tile_size == 8) {
        switch (control_0[3] & 3) {
            case 0: return (tile_x & 0x1f) + ((tile_y & 0x1f) << 5) + ((tile_x & 0x60) << 5);
            case 1:
                return (tile_x & 0x1f) + ((tile_y & 0x1f) << 5) + ((tile_y & 0x20) << 5) +
                       ((tile_x & 0x20) << 6);
            default: return (tile_x & 0x1f) + ((tile_y & 0x7f) << 5);
        }
    }
    switch (control_0[3] & 3) {
        case 0: return (tile_x & 0x0f) + ((tile_y & 0x0f) << 4) + ((tile_x & 0x1f0) << 4);
        case 1: return (tile_x & 0x0f) + ((tile_y & 0x1f) << 4) + ((tile_x & 0xf0) << 5);
        default: return (tile_x & 0x0f) + ((tile_y & 0x3f) << 4) + ((tile_x & 0x70) << 6);
    }
}

void Bac06Layer::composite(const GfxSet& gfx, bool trans, bool priority, uint16_t* dest) const {
    const Geometry size = geometry();
    const int mask_x = size.width - 1;
    const int mask_y = size.height - 1;
    const int tile_size = size.tile_size;
    const bool row_scroll = (control_ & 0x04) != 0;
    const bool column_scroll = (control_ & 0x08) != 0;

    for (int y = 0; y < kScreenHeight; y++) {
        const int row_offset =
            row_scroll ? int(rowscroll[size_t((y / row_block_) & 0xff)]) : 0;
        for (int x = 0; x < kScreenWidth; x++) {
            const int column_offset =
                column_scroll ? int(colscroll[size_t((x / col_block_) & 0x0f)]) : 0;
            const int source_x = (x + int(scroll_x_) + row_offset) & mask_x;
            const int source_y = (y + int(scroll_y_) + column_offset) & mask_y;
            const uint16_t attributes =
                data[size_t(tile_offset(source_x / tile_size, source_y / tile_size, tile_size))];
            if (priority && (attributes & 0x8000) == 0) continue;
            const uint8_t pen = gfx.element(attributes & 0xfff)[size_t((source_y % tile_size) *
                                                                          tile_size +
                                                                      (source_x % tile_size))];
            if (priority) {
                if ((pen & 0x08) == 0) continue;
            } else if (trans && pen == 0) {
                continue;
            }
            const uint16_t color = uint16_t((attributes >> 12) & color_mask_);
            dest[size_t(y * kScreenWidth + x)] = uint16_t(uint16_t(color << 4) + color_add_ + pen);
        }
    }
}

void Bac06Layer::draw(const GfxSet& gfx, bool trans, uint16_t* dest) const {
    composite(gfx, trans, false, dest);
}

void Bac06Layer::draw_priority(const GfxSet& gfx, uint16_t* dest) const {
    composite(gfx, true, true, dest);
}

Bac06Chip::Bac06Chip(uint16_t color_add1, uint16_t color_add2, uint16_t color_add3, int mult1,
                     int mult2, int mult3, uint16_t sprite_color, uint8_t color_mask)
    : tile_1(color_add1, mult1, color_mask),
      tile_2(color_add2, mult2, color_mask),
      tile_3(color_add3, mult3, color_mask),
      sprite_color_(sprite_color),
      color_mask_(color_mask) {}

void Bac06Chip::reset() {
    tile_1.reset();
    tile_2.reset();
    tile_3.reset();
    sprite_ram.fill(0);
}

void Bac06Chip::update_sprite_data(const uint16_t* source) {
    for (size_t index = 0; index < 0x400; index++) sprite_ram[index] = source[index];
}

void Bac06Chip::draw_sprites(const GfxSet& gfx, uint8_t pri_mask, uint8_t pri_val, bool odd_frame,
                             uint16_t* dest) const {
    for (int entry = 0; entry < 0x100; entry++) {
        const uint16_t y_word = sprite_ram[size_t(entry * 4)];
        if ((y_word & 0x8000) == 0) continue;
        const uint16_t x_word = sprite_ram[size_t(entry * 4 + 2)];
        uint8_t color = uint8_t(x_word >> 12);
        if ((color & pri_mask) != pri_val) continue;
        if ((x_word & 0x800) != 0 && odd_frame) continue;
        color = uint8_t(color & color_mask_);

        const bool flip_x = (y_word & 0x2000) != 0;
        const bool flip_y = (y_word & 0x4000) != 0;
        int multi = (1 << ((y_word & 0x1800) >> 11)) - 1;  // 1x, 2x, 4x or 8x height
        int code = sprite_ram[size_t(entry * 4 + 1)] & 0xfff;
        const int pos_x = (240 - x_word) & 0x1ff;
        const int pos_y = (240 - y_word) & 0x1ff;
        code &= ~multi;
        if (code == 0) continue;

        int increment = -1;
        if (!flip_y) {
            code += multi;
            increment = 1;
        }
        const uint16_t base = uint16_t(uint16_t(color << 4) + sprite_color_);
        while (multi >= 0) {
            const uint8_t* pixels = gfx.element(code - multi * increment);
            const int top = (pos_y - 16 * multi) & 0x1ff;
            for (int row = 0; row < 16; row++) {
                const int screen_y = top + row;
                if (screen_y < 0 || screen_y >= Bac06Layer::kScreenHeight) continue;
                for (int column = 0; column < 16; column++) {
                    const int screen_x = pos_x + column;
                    if (screen_x < 0 || screen_x >= Bac06Layer::kScreenWidth) continue;
                    const int source_row = flip_y ? 15 - row : row;
                    const int source_column = flip_x ? 15 - column : column;
                    const uint8_t pen = pixels[size_t(source_row * 16 + source_column)];
                    if (pen == 0) continue;
                    dest[size_t(screen_y * Bac06Layer::kScreenWidth + screen_x)] =
                        uint16_t(base + pen);
                }
            }
            multi--;
        }
    }
}

}  // namespace dsp
