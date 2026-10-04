#include "video/model3_tilegen.h"

#include <algorithm>

namespace dsp {

Model3TileGen::Model3TileGen() : vram_(kVramSize, 0) {
    for (auto& p : pal_) p.assign(32768, 0);
    for (auto& s : surface_) s.assign(size_t(kWidth) * kHeight, 0);
}

void Model3TileGen::reset() {
    std::fill(vram_.begin(), vram_.end(), 0);
    regs_.fill(0);
    for (auto& o : offset_) o.fill(0);
    for (auto& p : pal_) std::fill(p.begin(), p.end(), 0);
    begin_frame();
}

uint32_t Model3TileGen::colour(int bank, uint32_t data) const {
    // tbbbbbgggggrrrrr, t = transparent
    if (data & 0x8000) return 0;
    int r = int(data & 0x1f) * 255 / 31 + offset_[size_t(bank)][0];
    int g = int((data >> 5) & 0x1f) * 255 / 31 + offset_[size_t(bank)][1];
    int b = int((data >> 10) & 0x1f) * 255 / 31 + offset_[size_t(bank)][2];
    r = std::clamp(r, 0, 255);
    g = std::clamp(g, 0, 255);
    b = std::clamp(b, 0, 255);
    return 0xff000000u | uint32_t(r) << 16 | uint32_t(g) << 8 | uint32_t(b);
}

void Model3TileGen::recompute_palette(int bank) {
    auto& p = pal_[size_t(bank)];
    for (int i = 0; i < 32768; ++i) p[size_t(i)] = colour(bank, palette_raw(i));
}

void Model3TileGen::write8(uint32_t offset, uint8_t value) {
    offset %= kVramSize;
    vram_[offset] = value;
    if (offset >= kPaletteBase) {
        const int index = int((offset - kPaletteBase) / 4);
        const uint32_t raw = palette_raw(index);
        pal_[0][size_t(index)] = colour(0, raw);
        pal_[1][size_t(index)] = colour(1, raw);
    }
}

void Model3TileGen::write_register(uint32_t reg, uint32_t value,
                                   const std::function<void(uint8_t)>& irq_ack) {
    reg &= 0xff;
    const uint32_t old = regs_[reg / 4];
    regs_[reg / 4] = value;
    if (reg == 0x10) {
        if (irq_ack) irq_ack(uint8_t(value & 0xff));
    } else if ((reg == 0x40 || reg == 0x44) && old != value) {
        const int bank = reg == 0x40 ? 0 : 1;
        offset_[size_t(bank)][2] = int(int8_t((value >> 16) & 0xff)) * 2;
        offset_[size_t(bank)][1] = int(int8_t((value >> 8) & 0xff)) * 2;
        offset_[size_t(bank)][0] = int(int8_t(value & 0xff)) * 2;
        recompute_palette(bank);
    }
}

void Model3TileGen::begin_frame() {
    for (auto& s : surface_) std::fill(s.begin(), s.end(), 0);
}

void Model3TileGen::draw_line(int line) {
    if (line < 0 || line >= kHeight) return;
    const uint32_t layer_ctrl = regs_[0x20 / 4];
    // Pair B/B' (layers 2, 3) first, then A/A' (0, 1) on top of it.
    for (int pair = 1; pair >= 0; --pair) {
        const int primary = pair * 2;
        bool enabled[2];
        int sx[2], sy[2];
        bool four_bit[2];
        uint32_t* dst[2];
        for (int k = 0; k < 2; ++k) {
            const int layer = primary + k;
            const uint32_t r = regs_[0x60 / 4 + layer];
            enabled[k] = (r & 0x80000000u) != 0;
            sy[k] = int((r >> 16) & 0x1ff);
            if (r & 0x8000) {
                const uint32_t w = word(0xF6000 + uint32_t(layer) * 0x400 + uint32_t(line / 2) * 4);
                sx[k] = int((w >> ((1 - (line & 1)) * 16)) & 0xffff);
            } else {
                sx[k] = int(r & 0x3ff);
            }
            four_bit[k] = (layer_ctrl & (1u << (12 + layer))) != 0;
            const bool above = ((layer_ctrl >> (8 + layer)) & 1) != 0;
            dst[k] = surface_[above ? 1 : 0].data() + size_t(line) * kWidth;
        }
        if (!enabled[0] && !enabled[1]) continue;
        const uint32_t mask_word = word(0xF7000 + uint32_t(line) * 4);
        const uint32_t line_mask = (pair == 0) ? (mask_word >> 16) : (mask_word & 0xffff);
        const uint32_t* pal = pal_[size_t(pair)].data();

        for (int x = 0; x < kWidth;) {
            // Mask bit set: primary layer, clear: alternate layer.
            const int k = (line_mask & (1u << (15 - x / 32))) ? 0 : 1;
            const int px = (x + sx[k]) & 511;
            // Pixels until the next tile or mask boundary share all attributes.
            const int run = std::min({8 - (px & 7), 32 - (x & 31), kWidth - x});
            if (!enabled[k]) {
                x += run;
                continue;
            }
            const int layer = primary + k;
            const int py = (line + sy[k]) & 511;
            const uint32_t pair_no = uint32_t((py >> 3) & 63) * 32 + uint32_t((px >> 4) & 31);
            const uint32_t data = word(0xF8000 + uint32_t(layer) * 0x2000 + pair_no * 4);
            const uint32_t e = (px & 8) ? (data & 0xffff) : (data >> 16);
            const int vfine = py & 7;
            int hfine = px & 7;
            uint32_t* out = dst[k] + x;
            if (four_bit[k]) {
                const uint32_t pattern = (((e & 0x3fff) << 1) | (e >> 15)) * 32;
                const uint32_t row = word((pattern + uint32_t(vfine) * 4) % kPaletteBase);
                const uint32_t base = e & 0x7ff0;
                for (int i = 0; i < run; ++i, ++hfine) {
                    const uint32_t c = pal[(base | ((row >> ((7 - hfine) * 4)) & 0xf)) & 0x7fff];
                    if (c) out[i] = c;
                }
            } else {
                const uint32_t pattern = (e & 0x3fff) * 64 + uint32_t(vfine) * 8;
                const uint32_t rows[2] = {word(pattern % kPaletteBase), word((pattern + 4) % kPaletteBase)};
                const uint32_t base = e & 0x7f00;
                for (int i = 0; i < run; ++i, ++hfine) {
                    const uint32_t c = pal[(base | ((rows[hfine >> 2] >> ((3 - (hfine & 3)) * 8)) & 0xff)) & 0x7fff];
                    if (c) out[i] = c;
                }
            }
            x += run;
        }
    }
}

}  // namespace dsp
