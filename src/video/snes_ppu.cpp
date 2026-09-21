#include "video/snes_ppu.h"

#include <algorithm>
#include <cstring>

namespace dsp {
namespace {
// Bits per pixel of each background in modes 0-7. Index [mode][layer];
// 0 means the layer does not exist in that mode.
constexpr uint8_t kBgDepth[8][4] = {
    {2, 2, 2, 2},  // 0
    {4, 4, 2, 0},  // 1
    {4, 4, 0, 0},  // 2
    {8, 4, 0, 0},  // 3
    {8, 2, 0, 0},  // 4
    {4, 2, 0, 0},  // 5
    {4, 0, 0, 0},  // 6
    {8, 0, 0, 0},  // 7 (mode 7 is not a tile layer; handled as blank)
};
}  // namespace

void SnesPpu::reset() {
    vram_.fill(0);
    cgram_.fill(0);
    oam_.fill(0);
    inidisp_ = 0x80;
    bgmode_ = 0;
    obsel_ = 0;
    bg_ = {};
    tm_ = ts_ = 0;
    vmain_ = 0;
    vmadd_ = 0;
    vram_latch_ = 0;
    cgadd_ = 0;
    cg_high_ = false;
    cg_low_ = 0;
    oamadd_ = 0;
    oam_high_ = false;
    oam_low_ = 0;
    scroll_prev_ = 0;
    ppu_open_bus_ = 0;
}

uint32_t SnesPpu::colour(uint16_t bgr15) const {
    // CGRAM is 5 bits per channel in BGR order; scale to 8 bits and apply
    // the master brightness from INIDISP.
    const int b = (bgr15 >> 10) & 0x1f;
    const int g = (bgr15 >> 5) & 0x1f;
    const int r = bgr15 & 0x1f;
    const int bright = forced_blank() ? 0 : brightness();
    auto scale = [&](int c) {
        const int v = (c * 255 + 15) / 31;
        return uint32_t((v * bright + 7) / 15);
    };
    return 0xff000000u | (scale(r) << 16) | (scale(g) << 8) | scale(b);
}

void SnesPpu::write(uint16_t reg, uint8_t value) {
    ppu_open_bus_ = value;
    switch (reg & 0xff) {
        case 0x00: inidisp_ = value; return;
        case 0x01: obsel_ = value; return;
        case 0x02: oamadd_ = uint16_t((oamadd_ & 0x100) | value); oam_high_ = false; return;
        case 0x03: oamadd_ = uint16_t((oamadd_ & 0x0ff) | ((value & 1) << 8)); oam_high_ = false; return;
        case 0x04:
            // OAM is written a word at a time below $200 and a byte at a time
            // in the high table.
            if (oamadd_ >= 0x100) {
                oam_[size_t(0x200 + ((oamadd_ - 0x100) & 0x1f))] = value;
                oamadd_ = uint16_t((oamadd_ + 1) & 0x1ff);
            } else if (!oam_high_) {
                oam_low_ = value;
                oam_high_ = true;
            } else {
                const size_t a = size_t(oamadd_) * 2;
                if (a + 1 < oam_.size()) { oam_[a] = oam_low_; oam_[a + 1] = value; }
                oam_high_ = false;
                oamadd_ = uint16_t((oamadd_ + 1) & 0x1ff);
            }
            return;
        case 0x05: bgmode_ = value; 
            for (int i = 0; i < 4; i++) bg_[size_t(i)].tile16 = (value & (0x10 << i)) != 0;
            return;
        case 0x07: case 0x08: case 0x09: case 0x0a: {
            Bg& b = bg_[size_t((reg & 0xff) - 0x07)];
            b.map_base = uint16_t((value & 0xfc) << 8);   // 1K-word steps
            b.map_size = value & 0x03;
            return;
        }
        case 0x0b:
            bg_[0].chr_base = uint16_t((value & 0x0f) << 12);
            bg_[1].chr_base = uint16_t((value >> 4) << 12);
            return;
        case 0x0c:
            bg_[2].chr_base = uint16_t((value & 0x0f) << 12);
            bg_[3].chr_base = uint16_t((value >> 4) << 12);
            return;
        case 0x0f: case 0x11: case 0x13: {
            // BGnHOFS: written twice, low byte then high bits, sharing a latch.
            Bg& b = bg_[size_t(((reg & 0xff) - 0x0d) / 2)];
            b.hofs = uint16_t((value << 8) | (scroll_prev_ & ~7) | (scroll_prev_h_ & 7));
            scroll_prev_ = value;
            scroll_prev_h_ = value;
            return;
        }
        case 0x0e: case 0x10: case 0x12: case 0x14: {
            Bg& b = bg_[size_t(((reg & 0xff) - 0x0e) / 2)];
            b.vofs = uint16_t((value << 8) | scroll_prev_);
            scroll_prev_ = value;
            return;
        }
        case 0x1a: m7sel_ = value; return;
        case 0x1b: m7a_ = int16_t((value << 8) | m7_prev_); m7_prev_ = value; return;
        case 0x1c: m7b_ = int16_t((value << 8) | m7_prev_); m7_prev_ = value; return;
        case 0x1d: m7c_ = int16_t((value << 8) | m7_prev_); m7_prev_ = value; return;
        case 0x1e: m7d_ = int16_t((value << 8) | m7_prev_); m7_prev_ = value; return;
        case 0x1f: m7x_ = int16_t(int16_t((value << 8) | m7_prev_) << 3) >> 3;
                   m7_prev_ = value; return;
        case 0x20: m7y_ = int16_t(int16_t((value << 8) | m7_prev_) << 3) >> 3;
                   m7_prev_ = value; return;
        case 0x0d: if ((bgmode_ & 7) == 7) {
                       m7hofs_ = int16_t(int16_t((value << 8) | m7_prev_) << 3) >> 3;
                       m7_prev_ = value;
                   }
                   [[fallthrough]];
        case 0x15: if ((reg & 0xff) == 0x15) { vmain_ = value; return; }
                   break;
        case 0x16: vmadd_ = uint16_t((vmadd_ & 0xff00) | value); return;
        case 0x17: vmadd_ = uint16_t((vmadd_ & 0x00ff) | (value << 8)); return;
        case 0x18:
            vram_[vmadd_ & (kVramWords - 1)] =
                uint16_t((vram_[vmadd_ & (kVramWords - 1)] & 0xff00) | value);
            if (!(vmain_ & 0x80)) vmadd_ = uint16_t(vmadd_ + vram_step());
            return;
        case 0x19:
            vram_[vmadd_ & (kVramWords - 1)] =
                uint16_t((vram_[vmadd_ & (kVramWords - 1)] & 0x00ff) | (value << 8));
            if (vmain_ & 0x80) vmadd_ = uint16_t(vmadd_ + vram_step());
            return;
        case 0x21: cgadd_ = value; cg_high_ = false; return;
        case 0x22:
            if (!cg_high_) { cg_low_ = value; cg_high_ = true; }
            else {
                cgram_[cgadd_] = uint16_t((value << 8) | cg_low_);
                cgadd_ = uint8_t(cgadd_ + 1);
                cg_high_ = false;
            }
            return;
        case 0x2c: tm_ = value; return;
        case 0x2d: ts_ = value; return;
        default: return;
    }
}

uint8_t SnesPpu::read(uint16_t reg) {
    switch (reg & 0xff) {
        case 0x38: {  // OAMDATAREAD
            const uint8_t v = oam_[size_t(oamadd_ * 2) % oam_.size()];
            oamadd_ = uint16_t((oamadd_ + 1) & 0x1ff);
            return v;
        }
        case 0x39: {  // VMDATALREAD: the port returns a latched word
            const uint8_t v = uint8_t(vram_latch_);
            vram_latch_ = vram_[vmadd_ & (kVramWords - 1)];
            if (!(vmain_ & 0x80)) vmadd_ = uint16_t(vmadd_ + vram_step());
            return v;
        }
        case 0x3a: {
            const uint8_t v = uint8_t(vram_latch_ >> 8);
            vram_latch_ = vram_[vmadd_ & (kVramWords - 1)];
            if (vmain_ & 0x80) vmadd_ = uint16_t(vmadd_ + vram_step());
            return v;
        }
        default: return ppu_open_bus_;
    }
}

uint8_t SnesPpu::bg_priority(int layer, bool tile_prio) const {
    const int mode = bgmode_ & 7;
    if (mode == 1) {
        // Mode 1, back to front: BG3.0, BG3.1, OBJ0, BG2.0, BG1.0, OBJ1,
        // BG2.1, BG1.1, OBJ2, OBJ3. Bit 3 of BGMODE lifts BG3's high-priority
        // tiles above everything.
        switch (layer) {
            case 0: return tile_prio ? 8 : 5;
            case 1: return tile_prio ? 7 : 4;
            case 2: return tile_prio ? ((bgmode_ & 0x08) ? 15 : 2) : 1;
            default: return 1;
        }
    }
    // Modes 0 and 2 to 5 share one order: BG4.0, BG3.0, OBJ0, BG4.1, BG3.1,
    // OBJ1, BG2.0, BG1.0, OBJ2, BG2.1, BG1.1, OBJ3.
    switch (layer) {
        case 0: return tile_prio ? 11 : 8;
        case 1: return tile_prio ? 10 : 7;
        case 2: return tile_prio ? 5 : 2;
        default: return tile_prio ? 4 : 1;
    }
}

uint8_t SnesPpu::obj_priority(int sprite_prio) const {
    const int mode = bgmode_ & 7;
    if (mode == 1) {
        static const uint8_t kP[4] = {3, 6, 9, 12};
        return kP[sprite_prio & 3];
    }
    static const uint8_t kP[4] = {3, 6, 9, 12};
    return kP[sprite_prio & 3];
}

uint16_t SnesPpu::tilemap_entry(const Bg& bg, int tx, int ty) const {
    // A screen is 32x32 entries; map_size selects how many are stitched.
    const int wide = (bg.map_size & 1) ? 1 : 0;
    const int tall = (bg.map_size & 2) ? 1 : 0;
    const int sx = (tx >> 5) & wide;
    const int sy = (ty >> 5) & tall;
    const int screen = sy * (wide + 1) + sx;
    const int idx = ((ty & 31) << 5) | (tx & 31);
    return vram_[(bg.map_base + screen * 0x400 + idx) & (kVramWords - 1)];
}

void SnesPpu::draw_bg_line(int bg_index, int depth, int line, uint16_t* out, uint8_t* prio) {
    const Bg& bg = bg_[size_t(bg_index)];
    const int tile_px = bg.tile16 ? 16 : 8;
    const int y = (line + bg.vofs) & (bg.tile16 ? 1023 : 511);
    const int ty = y / tile_px;
    const int palette_base = (depth == 2) ? 0 : 0;  // offsets handled per entry

    for (int px = 0; px < kWidth; px++) {
        const int xw = (px + bg.hofs) & (bg.tile16 ? 1023 : 511);
        const int tx = xw / tile_px;
        const uint16_t entry = tilemap_entry(bg, tx, ty);
        const int tile = entry & 0x3ff;
        const bool hflip = (entry & 0x4000) != 0;
        const bool vflip = (entry & 0x8000) != 0;
        const int pal = (entry >> 10) & 7;
        const uint8_t pr = bg_priority(bg_index, (entry & 0x2000) != 0);

        int fx = xw % tile_px, fy = y % tile_px;
        if (hflip) fx = tile_px - 1 - fx;
        if (vflip) fy = tile_px - 1 - fy;
        // 16x16 tiles are four 8x8 characters laid out 2x2.
        int tile_no = tile;
        if (bg.tile16) {
            tile_no += (fy >= 8 ? 16 : 0) + (fx >= 8 ? 1 : 0);
            fx &= 7; fy &= 7;
        }

        const int words_per_tile = depth * 4;   // 2bpp=8 words, 4bpp=16, 8bpp=32
        const uint16_t base = uint16_t(bg.chr_base + tile_no * words_per_tile);
        uint8_t pixel = 0;
        for (int plane = 0; plane < depth; plane += 2) {
            const uint16_t w = vram_[(base + (plane / 2) * 8 + fy) & (kVramWords - 1)];
            const int bit = 7 - fx;
            pixel = uint8_t(pixel | (((w >> bit) & 1) << plane));
            pixel = uint8_t(pixel | (((w >> (8 + bit)) & 1) << (plane + 1)));
        }
        if (pixel == 0) continue;               // colour 0 is transparent
        const int colours = 1 << depth;
        const int index = palette_base + pal * colours + pixel;
        if (pr > prio[px]) { out[px] = cgram_[index & 0xff]; prio[px] = pr; }
    }
}

void SnesPpu::draw_sprite_line(int line, uint16_t* out, uint8_t* prio) {
    // Object sizes selected by OBSEL bits 5-7; only the common pairs are
    // handled, which covers the great majority of software.
    static const int kSizes[8][2][2] = {
        {{8, 8}, {16, 16}}, {{8, 8}, {32, 32}}, {{8, 8}, {64, 64}},
        {{16, 16}, {32, 32}}, {{16, 16}, {64, 64}}, {{32, 32}, {64, 64}},
        {{16, 32}, {32, 64}}, {{16, 32}, {32, 32}},
    };
    const int sel = (obsel_ >> 5) & 7;
    const uint16_t chr_base = uint16_t((obsel_ & 0x07) << 13);
    const uint16_t chr_gap = uint16_t(((((obsel_ >> 3) & 3) + 1) << 12));

    for (int i = 127; i >= 0; i--) {
        const uint8_t* e = &oam_[size_t(i) * 4];
        const uint8_t hi = oam_[size_t(0x200 + i / 4)];
        const int shift = (i % 4) * 2;
        const bool large = ((hi >> (shift + 1)) & 1) != 0;
        const int w = kSizes[sel][large ? 1 : 0][0];
        const int h = kSizes[sel][large ? 1 : 0][1];
        int sx = e[0] | (((hi >> shift) & 1) << 8);
        if (sx >= 256) sx -= 512;   // the 9-bit X is signed
        // Sprites are drawn one scanline below the Y in OAM, and a sprite
        // near the bottom wraps around through zero rather than being cut.
        const int sy = (e[1] + 1) & 0xff;
        const int dy = line - sy;
        if (dy < 0 || dy >= h) continue;

        const uint8_t attr = e[3];
        const int character = e[2];
        const bool name_select = (attr & 1) != 0;
        const int pal = (attr >> 1) & 7;
        const uint8_t pr = obj_priority((attr >> 4) & 3);
        const bool hflip = (attr & 0x40) != 0;
        const bool vflip = (attr & 0x80) != 0;
        const int fy = vflip ? (h - 1 - dy) : dy;

        // The character number is an index into a 16x16 grid of tiles, and
        // both axes wrap inside it: a wide sprite starting near column 15
        // continues at column 0 of the same row, not on the next row.
        const uint16_t base = uint16_t(chr_base + (name_select ? chr_gap : 0));
        const int char_x = character & 15;
        const int char_y = (((character >> 4) + (fy >> 3)) & 15) << 4;
        const int tiles_wide = w / 8;

        for (int px = 0; px < w; px++) {
            const int sxp = sx + px;
            if (sxp < 0 || sxp >= kWidth) continue;
            const int fx = hflip ? (w - 1 - px) : px;
            const int tile_x = fx >> 3;
            const int mirror_x = hflip ? (tiles_wide - 1 - (px >> 3)) : (px >> 3);
            (void)tile_x;
            const int tno = char_y + ((char_x + mirror_x) & 15);
            const uint16_t addr = uint16_t(base + tno * 16 + (fy & 7));
            const int bx = 7 - (fx & 7);
            uint8_t pixel = 0;
            for (int plane = 0; plane < 4; plane += 2) {
                const uint16_t word = vram_[(addr + (plane / 2) * 8) & (kVramWords - 1)];
                pixel = uint8_t(pixel | (((word >> bx) & 1) << plane));
                pixel = uint8_t(pixel | (((word >> (8 + bx)) & 1) << (plane + 1)));
            }
            if (pixel == 0) continue;
            if (pr > prio[sxp]) { out[sxp] = cgram_[128 + pal * 16 + pixel]; prio[sxp] = pr; }
        }
    }
}

void SnesPpu::draw_mode7_line(int line, uint16_t* out, uint8_t* prio) {
    // Screen point (x, y) maps into the 1024x1024 plane through the matrix,
    // taken about the centre held in M7X/M7Y.
    const int cx = m7x_, cy = m7y_;
    const int sy = line + m7vofs_ - cy;
    for (int px = 0; px < kWidth; px++) {
        const int sx = px + m7hofs_ - cx;
        int vx = ((m7a_ * sx) >> 8) + ((m7b_ * sy) >> 8) + cx;
        int vy = ((m7c_ * sx) >> 8) + ((m7d_ * sy) >> 8) + cy;

        if ((m7sel_ & 0x80) && (m7sel_ & 0x40) == 0) {
            if (vx < 0 || vx >= 1024 || vy < 0 || vy >= 1024) continue;
        } else {
            vx &= 1023; vy &= 1023;
        }

        // The tilemap lives in the low byte of each VRAM word and the
        // character data in the high byte, interleaved across the same 32K.
        const int tile = vram_[((vy >> 3) * 128 + (vx >> 3)) & (kVramWords - 1)] & 0xff;
        const uint8_t pixel =
            uint8_t(vram_[(tile * 64 + (vy & 7) * 8 + (vx & 7)) & (kVramWords - 1)] >> 8);
        if (pixel == 0) continue;
        if (prio[px] == 0) { out[px] = cgram_[pixel]; prio[px] = 5; }
    }
}

void SnesPpu::render_line(int line, uint32_t* dst) {
    uint16_t row[kWidth];
    uint8_t prio[kWidth];
    const uint16_t backdrop = cgram_[0];
    for (int i = 0; i < kWidth; i++) { row[i] = backdrop; prio[i] = 0; }

    if (!forced_blank()) {
        const int mode = bgmode_ & 7;
        if (mode == 7) {
            if ((tm_ & 1) || (ts_ & 1)) draw_mode7_line(line, row, prio);
            if ((tm_ & 0x10) || (ts_ & 0x10)) draw_sprite_line(line, row, prio);
            for (int i = 0; i < kWidth; i++) dst[i] = colour(row[i]);
            return;
        }
        // Draw from the lowest-priority layer up, so later writes win.
        for (int layer = 3; layer >= 0; layer--) {
            const int depth = kBgDepth[mode][layer];
            if (depth == 0) continue;
            if (!(tm_ & (1 << layer)) && !(ts_ & (1 << layer))) continue;
            draw_bg_line(layer, depth, line, row, prio);
        }
        if ((tm_ & 0x10) || (ts_ & 0x10)) draw_sprite_line(line, row, prio);
    }

    for (int i = 0; i < kWidth; i++) dst[i] = colour(row[i]);
}

}  // namespace dsp
