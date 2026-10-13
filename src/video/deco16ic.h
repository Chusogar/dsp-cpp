#pragma once

#include <array>
#include <cstdint>
#include <functional>

#include "video/gfx.h"

namespace dsp {

// Data East DECO 55 / 16IC tilemap generator, ported from dsp-emulator
// deco_16ic.pas (MAME deco16ic.cpp).
//
// Two playfields of 64x32 8x8 tiles (512x256) or 64x32 16x16 tiles (1024x512,
// stored as four 32x32 pages), each with its own scroll, per-row or
// per-column scroll and a tile bank picked through a game callback from the
// bank register. Tile words: bits 15-12 colour, 11-0 tile number; bit 15 also
// enables the per-layer flip bits of the control register.
class Deco16ic {
public:
    struct Layer {
        const GfxSet* gfx8 = nullptr;   // 8x8 tiles
        const GfxSet* gfx16 = nullptr;  // 16x16 tiles
        int color_base8 = 0;
        int color_base16 = 0;
        int granularity16 = 16;  // colours per code of the 16x16 set (32 for 5bpp)
        int color_bank = 0;      // added to the colour code
        int color_mask8 = 0x0f;
        int color_mask16 = 0x0f;
        std::function<int(int)> bank;  // bank register byte -> tile number offset
    };

    Deco16ic();

    void configure(int playfield, Layer layer) { layers_[size_t(playfield)] = std::move(layer); }
    void reset();

    void control_w(int offset, uint16_t value) { control_[size_t(offset & 7)] = value; }
    uint16_t control_r(int offset) const { return control_[size_t(offset & 7)]; }

    std::array<uint16_t, 0x1000> data[2];
    std::array<uint16_t, 0x800> rowscroll[2];

    bool enabled(int playfield) const {
        return (control_[5] & (playfield == 0 ? 0x0080 : 0x8000)) != 0;
    }
    bool is_8x8(int playfield) const {
        return (control_[6] & (playfield == 0 ? 0x0080 : 0x8000)) != 0;
    }

    // Draws a playfield over `dest` (`width` x `height` pixels, whose first row
    // is screen line `first_line`). Pen 0 is transparent unless `opaque`.
    void draw(int playfield, uint32_t* dest, int width, int height, int first_line,
              const uint32_t* palette, bool opaque) const;

private:
    std::array<Layer, 2> layers_;
    std::array<uint16_t, 8> control_{};
};

}  // namespace dsp
