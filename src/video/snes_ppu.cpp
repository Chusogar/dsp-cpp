#include "video/snes_ppu.h"

#include <algorithm>
#include <cstring>

namespace dsp {
namespace {
constexpr uint8_t kBgDepth[8][4] = {
    {2, 2, 2, 2}, {4, 4, 2, 0}, {4, 4, 0, 0}, {8, 4, 0, 0},
    {8, 2, 0, 0}, {4, 2, 0, 0}, {4, 0, 0, 0}, {8, 0, 0, 0},
};
}  // namespace

void SnesPpu::reset() {
    vram_.fill(0); cgram_.fill(0); oam_.fill(0);
    inidisp_ = 0x80; bgmode_ = 0; obsel_ = 0; bg_ = {};
    tm_ = ts_ = 0; tmw_ = tsw_ = 0; cgwsel_ = 0; cgadsub_ = 0; coldata_ = 0;
    vmain_ = 0; vmadd_ = 0; vram_latch_ = 0;
    cgadd_ = 0; cg_high_ = false; cg_low_ = 0;
    oamadd_ = 0; oam_high_ = false; oam_low_ = 0;
    m7a_ = 0x100; m7b_ = 0; m7c_ = 0; m7d_ = 0x100;
    m7x_ = m7y_ = m7hofs_ = m7vofs_ = 0; m7_prev_ = 0; m7sel_ = 0;
    scroll_prev_ = 0; scroll_prev_h_ = 0; ppu_open_bus_ = 0;
}

uint32_t SnesPpu::colour(uint16_t bgr15) const {
    const int b = (bgr15 >> 10) & 0x1f, g = (bgr15 >> 5) & 0x1f, r = bgr15 & 0x1f;
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
            if (oamadd_ >= 0x100) {
                oam_[size_t(0x200 + ((oamadd_ - 0x100) & 0x1f))] = value;
                oamadd_ = uint16_t((oamadd_ + 1) & 0x1ff);
            } else if (!oam_high_) { oam_low_ = value; oam_high_ = true; }
            else {
                const size_t a = size_t(oamadd_) * 2;
                if (a + 1 < oam_.size()) { oam_[a] = oam_low_; oam_[a + 1] = value; }
                oam_high_ = false; oamadd_ = uint16_t((oamadd_ + 1) & 0x1ff);
            }
            return;
        case 0x05:
            bgmode_ = value;
            for (int i = 0; i < 4; i++) bg_[size_t(i)].tile16 = (value & (0x10 << i)) != 0;
            return;
        case 0x07: case 0x08: case 0x09: case 0x0a: {
            Bg& b = bg_[size_t((reg & 0xff) - 0x07)];
            b.map_base = uint16_t((value & 0xfc) << 8); b.map_size = value & 0x03; return;
        }
        case 0x0b:
            bg_[0].chr_base = uint16_t((value & 0x0f) << 12);
            bg_[1].chr_base = uint16_t((value >> 4) << 12); return;
        case 0x0c:
            bg_[2].chr_base = uint16_t((value & 0x0f) << 12);
            bg_[3].chr_base = uint16_t((value >> 4) << 12); return;
        case 0x0d: {
            m7hofs_ = int16_t(clip13((value << 8) | m7_prev_)); m7_prev_ = value;
            bg_[0].hofs = uint16_t((value << 8) | (scroll_prev_ & ~7) | (scroll_prev_h_ & 7));
            scroll_prev_ = value; scroll_prev_h_ = value; return;
        }
        case 0x0e: {
            m7vofs_ = int16_t(clip13((value << 8) | m7_prev_)); m7_prev_ = value;
            bg_[0].vofs = uint16_t((value << 8) | scroll_prev_); scroll_prev_ = value; return;
        }
        case 0x0f: case 0x11: case 0x13: {
            Bg& b = bg_[size_t(((reg & 0xff) - 0x0d) / 2)];
            b.hofs = uint16_t((value << 8) | (scroll_prev_ & ~7) | (scroll_prev_h_ & 7));
            scroll_prev_ = value; scroll_prev_h_ = value; return;
        }
        case 0x10: case 0x12: case 0x14: {
            Bg& b = bg_[size_t(((reg & 0xff) - 0x0e) / 2)];
            b.vofs = uint16_t((value << 8) | scroll_prev_); scroll_prev_ = value; return;
        }
        case 0x15: vmain_ = value; return;
        case 0x16: vmadd_ = uint16_t((vmadd_ & 0xff00) | value); return;
        case 0x17: vmadd_ = uint16_t((vmadd_ & 0x00ff) | (value << 8)); return;
        case 0x18:
            vram_[vmadd_ & (kVramWords - 1)] =
                uint16_t((vram_[vmadd_ & (kVramWords - 1)] & 0xff00) | value);
            if (!(vmain_ & 0x80)) vmadd_ = uint16_t(vmadd_ + vram_step()); return;
        case 0x19:
            vram_[vmadd_ & (kVramWords - 1)] =
                uint16_t((vram_[vmadd_ & (kVramWords - 1)] & 0x00ff) | (value << 8));
            if (vmain_ & 0x80) vmadd_ = uint16_t(vmadd_ + vram_step()); return;
        case 0x1a: m7sel_ = value; return;
        case 0x1b: m7a_ = int16_t((value << 8) | m7_prev_); m7_prev_ = value; return;
        case 0x1c: m7b_ = int16_t((value << 8) | m7_prev_); m7_prev_ = value; return;
        case 0x1d: m7c_ = int16_t((value << 8) | m7_prev_); m7_prev_ = value; return;
        case 0x1e: m7d_ = int16_t((value << 8) | m7_prev_); m7_prev_ = value; return;
        case 0x1f: m7x_ = int16_t(clip13((value << 8) | m7_prev_)); m7_prev_ = value; return;
        case 0x20: m7y_ = int16_t(clip13((value << 8) | m7_prev_)); m7_prev_ = value; return;
        case 0x21: cgadd_ = value; cg_high_ = false; return;
        case 0x22:
            if (!cg_high_) { cg_low_ = value; cg_high_ = true; }
            else { cgram_[cgadd_] = uint16_t((value << 8) | cg_low_); cgadd_ = uint8_t(cgadd_ + 1); cg_high_ = false; }
            return;
        case 0x2c: tm_ = value; return;
        case 0x2d: ts_ = value; return;
        case 0x2e: tmw_ = value; return;
        case 0x2f: tsw_ = value; return;
        case 0x30: cgwsel_ = value; return;
        case 0x31: cgadsub_ = value; return;
        case 0x32: {
            const uint8_t intensity = value & 0x1f;
            if (value & 0x20) coldata_ = uint16_t((coldata_ & ~0x001f) | intensity);
            if (value & 0x40) coldata_ = uint16_t((coldata_ & ~0x03e0) | (intensity << 5));
            if (value & 0x80) coldata_ = uint16_t((coldata_ & ~0x7c00) | (intensity << 10));
            return;
        }
        default: return;
    }
}

uint8_t SnesPpu::read(uint16_t reg) {
    switch (reg & 0xff) {
        case 0x38: {
            const uint8_t v = oam_[size_t(oamadd_ * 2) % oam_.size()];
            oamadd_ = uint16_t((oamadd_ + 1) & 0x1ff); return v;
        }
        case 0x39: {
            const uint8_t v = uint8_t(vram_latch_);
            vram_latch_ = vram_[vmadd_ & (kVramWords - 1)];
            if (!(vmain_ & 0x80)) vmadd_ = uint16_t(vmadd_ + vram_step()); return v;
        }
        case 0x3a: {
            const uint8_t v = uint8_t(vram_latch_ >> 8);
            vram_latch_ = vram_[vmadd_ & (kVramWords - 1)];
            if (vmain_ & 0x80) vmadd_ = uint16_t(vmadd_ + vram_step()); return v;
        }
        default: return ppu_open_bus_;
    }
}

uint16_t SnesPpu::tilemap_entry(const Bg& bg, int tx, int ty) const {
    const int map_w = (bg.map_size & 1) ? 64 : 32;
    const int map_h = (bg.map_size & 2) ? 64 : 32;
    tx &= (map_w - 1); ty &= (map_h - 1);
    int map_off = 0;
    if (bg.map_size & 1) { map_off += (tx & 32) ? 0x400 : 0; tx &= 31; }
    if (bg.map_size & 2) { map_off += (ty & 32) ? ((bg.map_size & 1) ? 0x800 : 0x400) : 0; ty &= 31; }
    return vram_[(bg.map_base + map_off + ty * 32 + tx) & (kVramWords - 1)];
}

uint8_t SnesPpu::bg_priority(int layer, bool tile_prio) const {
    const int mode = bgmode_ & 7;
    if (mode == 1) {
        switch (layer) {
            case 0: return tile_prio ? 12 : 5;
            case 1: return tile_prio ? 11 : 4;
            case 2: return tile_prio ? ((bgmode_ & 0x08) ? 15 : 2) : 1;
            default: return 0;
        }
    }
    static const uint8_t kBase[4] = {3, 2, 1, 0};
    return uint8_t(kBase[layer & 3] + (tile_prio ? 8 : 0));
}

uint8_t SnesPpu::obj_priority(int sprite_prio) const {
    static const uint8_t kP[4] = {3, 6, 9, 12};
    return kP[sprite_prio & 3];
}

void SnesPpu::draw_bg_line(int bg_index, int depth, int line, Sample* out, uint8_t layer_mask) {
    const Bg& bg = bg_[size_t(bg_index)];
    const int tile_px = bg.tile16 ? 16 : 8;
    const int y = (line + bg.vofs) & (bg.tile16 ? 1023 : 511);
    for (int px = 0; px < kWidth; px++) {
        const int xw = (px + bg.hofs) & (bg.tile16 ? 1023 : 511);
        const int tx = xw / tile_px, ty = y / tile_px;
        const uint16_t entry = tilemap_entry(bg, tx, ty);
        const int tile = entry & 0x3ff;
        const bool hflip = (entry & 0x4000) != 0, vflip = (entry & 0x8000) != 0;
        const int pal = (entry >> 10) & 7;
        const uint8_t pr = bg_priority(bg_index, (entry & 0x2000) != 0);
        int fx = xw % tile_px, fy = y % tile_px;
        if (hflip) fx = tile_px - 1 - fx;
        if (vflip) fy = tile_px - 1 - fy;
        int tile_no = tile;
        if (bg.tile16) { tile_no += (fy >= 8 ? 16 : 0) + (fx >= 8 ? 1 : 0); fx &= 7; fy &= 7; }
        const uint16_t base = uint16_t(bg.chr_base + tile_no * (depth * 4));
        uint8_t pixel = 0;
        for (int plane = 0; plane < depth; plane += 2) {
            const uint16_t w = vram_[(base + (plane / 2) * 8 + fy) & (kVramWords - 1)];
            const int bit = 7 - fx;
            pixel = uint8_t(pixel | (((w >> bit) & 1) << plane));
            pixel = uint8_t(pixel | (((w >> (8 + bit)) & 1) << (plane + 1)));
        }
        if (pixel == 0) continue;
        uint16_t color;
        if (depth == 8) color = cgram_[pixel];
        else {
            int pal_base = pal * (1 << depth);
            if ((bgmode_ & 7) == 0) pal_base += bg_index * 32;
            color = cgram_[(pal_base + pixel) & 0xff];
        }
        if (pr > out[px].prio) { out[px].color = color; out[px].prio = pr; out[px].source = layer_mask; }
    }
}

void SnesPpu::draw_sprite_line(int line, Sample* out, uint8_t) {
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
        const uint8_t hi = oam_[0x200 + (i >> 2)];
        const int shift = (i & 3) * 2;
        const int size_bit = (hi >> shift) & 1;
        const int x_msb = (hi >> (shift + 1)) & 1;
        int x = int(e[0]) | (x_msb << 8); if (x >= 256) x -= 512;
        int y = int(e[1]);
        const int tile = e[2];
        const uint8_t attr = e[3];
        const int w = kSizes[sel][size_bit][0], h = kSizes[sel][size_bit][1];
        const uint8_t pr = obj_priority((attr >> 4) & 3);
        const int pal = attr & 7;
        const bool hflip = (attr & 0x40) != 0, vflip = (attr & 0x80) != 0;
        const int name_hi = (attr & 0x01) ? 0x100 : 0;
        int sy = line - ((y + 1) & 0xff);
        if (sy < 0 || sy >= h) continue;
        const int fy = vflip ? (h - 1 - sy) : sy;
        const int char_y = (fy / 8) * 16;
        for (int px = 0; px < w; px++) {
            const int sx = x + px;
            if (sx < 0 || sx >= kWidth) continue;
            const int fx = hflip ? (w - 1 - px) : px;
            const int char_x = fx / 8;
            const int tno = ((tile + name_hi + char_y + char_x) & 0x1ff);
            const uint16_t base = uint16_t(chr_base + ((tno & 0x100) ? chr_gap : 0) + (tno & 0xff) * 16);
            const uint16_t addr = uint16_t(base + (fy & 7));
            const int bx = 7 - (fx & 7);
            uint8_t pixel = 0;
            for (int plane = 0; plane < 4; plane += 2) {
                const uint16_t word = vram_[(addr + (plane / 2) * 8) & (kVramWords - 1)];
                pixel = uint8_t(pixel | (((word >> bx) & 1) << plane));
                pixel = uint8_t(pixel | (((word >> (8 + bx)) & 1) << (plane + 1)));
            }
            if (pixel == 0) continue;
            if (pr > out[sx].prio) {
                out[sx].color = cgram_[128 + pal * 16 + pixel];
                out[sx].prio = pr; out[sx].source = 0x10;
            }
        }
    }
}

void SnesPpu::draw_mode7_line(int line, Sample* out, uint8_t layer_mask) {
    const int cx = clip13(m7x_), cy = clip13(m7y_);
    const int hofs = clip13(m7hofs_), vofs = clip13(m7vofs_);
    const int sy = line + vofs - cy;
    const int32_t b_sy = int32_t(m7b_) * sy, d_sy = int32_t(m7d_) * sy;
    const bool clip_outside = (m7sel_ & 0x80) != 0;
    const bool fill_transparent = (m7sel_ & 0x40) == 0;
    const bool hflip = (m7sel_ & 0x01) != 0;
    for (int px = 0; px < kWidth; px++) {
        int screen_x = hflip ? (255 - px) : px;
        const int sx = screen_x + hofs - cx;
        int32_t vx = ((int32_t(m7a_) * sx + b_sy) >> 8) + cx;
        int32_t vy = ((int32_t(m7c_) * sx + d_sy) >> 8) + cy;
        bool oob = (vx < 0 || vx >= 1024 || vy < 0 || vy >= 1024);
        if (clip_outside && oob) { if (fill_transparent) continue; continue; }
        if (!clip_outside) { vx &= 1023; vy &= 1023; }
        const int tile = vram_[((vy >> 3) * 128 + (vx >> 3)) & (kVramWords - 1)] & 0xff;
        const uint8_t pixel = uint8_t(vram_[(tile * 64 + (vy & 7) * 8 + (vx & 7)) & (kVramWords - 1)] >> 8);
        if (pixel == 0) continue;
        uint8_t pr = 5, color_idx = pixel;
        if (bgmode_ & 0x40) { pr = (pixel & 0x80) ? 11 : 5; color_idx = pixel & 0x7f; }
        if (pr > out[px].prio) {
            out[px].color = cgram_[color_idx]; out[px].prio = pr; out[px].source = layer_mask;
        }
    }
}

uint16_t SnesPpu::apply_color_math(const Sample& main, const Sample& sub) const {
    const uint8_t enable_mask = cgadsub_ & 0x3f;
    const bool do_math = (main.source == 0) ? ((enable_mask & 0x20) != 0)
                                            : ((enable_mask & main.source) != 0);
    const int sub_mode = (cgwsel_ >> 4) & 3;
    uint16_t sub_color = coldata_;
    if (sub_mode >= 2) {
        if (sub.source != 0 || sub.prio > 0) sub_color = sub.color;
        else if (sub_mode == 2) sub_color = cgram_[0];
    }
    if (!do_math) return main.color;
    int mr = main.color & 0x1f, mg = (main.color >> 5) & 0x1f, mb = (main.color >> 10) & 0x1f;
    int sr = sub_color & 0x1f, sg = (sub_color >> 5) & 0x1f, sb = (sub_color >> 10) & 0x1f;
    int r, g, b;
    if (cgadsub_ & 0x80) { r = mr - sr; g = mg - sg; b = mb - sb; }
    else { r = mr + sr; g = mg + sg; b = mb + sb; }
    if (cgadsub_ & 0x40) { r >>= 1; g >>= 1; b >>= 1; }
    r = std::clamp(r, 0, 31); g = std::clamp(g, 0, 31); b = std::clamp(b, 0, 31);
    return uint16_t(r | (g << 5) | (b << 10));
}

void SnesPpu::render_line(int line, uint32_t* dst) {
    Sample main_row[kWidth], sub_row[kWidth];
    const uint16_t backdrop = cgram_[0];
    for (int i = 0; i < kWidth; i++) {
        main_row[i] = {backdrop, 0, 0}; sub_row[i] = {backdrop, 0, 0};
    }
    if (!forced_blank()) {
        const int mode = bgmode_ & 7;
        auto draw_layers = [&](Sample* buf, uint8_t enable) {
            if (mode == 7) {
                if (enable & 1) draw_mode7_line(line, buf, 0x01);
                if (enable & 0x10) draw_sprite_line(line, buf, 0x10);
                return;
            }
            for (int layer = 3; layer >= 0; layer--) {
                const int depth = kBgDepth[mode][layer];
                if (depth == 0 || !(enable & (1 << layer))) continue;
                draw_bg_line(layer, depth, line, buf, uint8_t(1 << layer));
            }
            if (enable & 0x10) draw_sprite_line(line, buf, 0x10);
        };
        draw_layers(main_row, tm_);
        const int sub_mode = (cgwsel_ >> 4) & 3;
        if ((sub_mode >= 2 && ts_ != 0) || (ts_ != 0 && (cgadsub_ & 0x3f) != 0))
            draw_layers(sub_row, ts_);
    }
    for (int i = 0; i < kWidth; i++) {
        uint16_t c = main_row[i].color;
        if ((cgadsub_ & 0x3f) != 0) c = apply_color_math(main_row[i], sub_row[i]);
        dst[i] = colour(c);
    }
}

}  // namespace dsp
