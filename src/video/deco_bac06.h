#pragma once

#include <array>
#include <cstdint>

#include "video/gfx.h"

namespace dsp {

// Data East BAC06 tilemap generator and the MXC06 sprite chip that goes with it,
// ported from deco_bac06.pas. Each of the three playfields is a tilemap of 8x8 or
// 16x16 tiles with per row X scroll, per column Y scroll and selectable geometry;
// everything is composited into a 256x256 buffer of palette indices.
class Bac06Layer {
public:
    static constexpr int kScreenWidth = 256;
    static constexpr int kScreenHeight = 256;

    Bac06Layer(uint16_t color_add, int mult, uint8_t color_mask);

    void reset();

    std::array<uint16_t, 0x2000> data{};
    std::array<uint16_t, 4> control_0{};
    std::array<uint16_t, 4> control_1{};
    std::array<uint16_t, 0x40> colscroll{};
    std::array<uint16_t, 0x200> rowscroll{};

    void change_control0(int pos, uint16_t value);
    // `dec1` selects the Sly Spy/Boulder Dash row scroll block size encoding.
    void change_control1(int pos, uint16_t value, bool dec1 = false);

    // Composites the playfield over `dest`, skipping pen 0 when `trans` is set.
    void draw(const GfxSet& gfx, bool trans, uint16_t* dest) const;
    // Composites only the pixels of the priority tiles: tiles with attribute bit 15
    // set contribute the pens that have bit 3 set, like put_gfx_dec0().
    void draw_priority(const GfxSet& gfx, uint16_t* dest) const;

private:
    struct Geometry {
        int width;
        int height;
        int tile_size;
    };

    Geometry geometry() const;
    int tile_offset(int tile_x, int tile_y, int tile_size) const;
    void composite(const GfxSet& gfx, bool trans, bool priority, uint16_t* dest) const;

    uint16_t color_add_;
    int mult_;
    uint8_t color_mask_;
    uint16_t scroll_x_ = 0;
    uint16_t scroll_y_ = 0;
    uint16_t control_ = 0;
    int row_block_ = 1;  // height in scanlines of a row scroll block
    int col_block_ = 1;  // width in pixels of a column scroll block
};

class Bac06Chip {
public:
    Bac06Chip(uint16_t color_add1, uint16_t color_add2, uint16_t color_add3, int mult1, int mult2,
              int mult3, uint16_t sprite_color, uint8_t color_mask = 0x0f);

    void reset();

    Bac06Layer tile_1;
    Bac06Layer tile_2;
    Bac06Layer tile_3;

    // 0x400 words of sprite RAM, oversized to match the Pascal buffer.
    std::array<uint16_t, 0x800> sprite_ram{};
    void update_sprite_data(const uint16_t* source);

    // Draws the sprites whose colour bits match `pri_val` under `pri_mask`.
    // `odd_frame` reproduces the flicker of the sprites with bit 11 set.
    void draw_sprites(const GfxSet& gfx, uint8_t pri_mask, uint8_t pri_val, bool odd_frame,
                      uint16_t* dest) const;

private:
    uint16_t sprite_color_;
    uint8_t color_mask_;
};

}  // namespace dsp
