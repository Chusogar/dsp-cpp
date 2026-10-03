// ZX Spectrum Next video: ULA / LoRes, tilemap, Layer 2 and sprites drawn
// into per-line buffers at 640 pixels across, then mixed with the layer
// priority (nr_15) and blend modes, the global transparency (nr_14) and
// the fallback colour (nr_4A).
#include <algorithm>
#include <cstring>

#include "drivers/computers/specnext.h"

namespace dsp {
namespace {

constexpr uint16_t kTransparent = 0x8000;
constexpr uint16_t kFlag = 0x4000;  // tilemap: below the ULA; Layer 2: priority colour

inline bool opaque(uint16_t v) { return !(v & kTransparent); }

inline uint16_t ula_row_offset(int y) { return uint16_t(((y & 7) << 8) | ((y & 0x38) << 2) | ((y & 0xc0) << 5)); }

inline uint16_t mix_add(uint16_t a, uint16_t b, int bias) {
    auto ch = [&](int shift) {
        const int v = ((a >> shift) & 7) + ((b >> shift) & 7) - bias;
        return uint16_t(std::clamp(v, 0, 7) << shift);
    };
    return uint16_t(ch(6) | ch(3) | ch(0));
}

}  // namespace

void SpecNext::render_ula(int row, uint16_t* out) {
    const bool ulanext = nr_43_ & 0x01;
    const bool ulap = ulap_en_ && !ulanext;
    const auto& pal = palette_[(nr_43_ & 0x02) ? 4 : 0];
    const bool shadow = port_7ffd_ & 0x08;
    const int mode = shadow ? 0 : (port_ff_ & 7);
    const uint16_t fallback = uint16_t((nr_4a_fallback_ << 1) | ((nr_4a_fallback_ & 3) ? 1 : 0));

    int ink_mask = 0xff, pap_shift = 8;
    switch (nr_42_ulanext_format_) {
        case 0x01: ink_mask = 0x01; pap_shift = 1; break;
        case 0x03: ink_mask = 0x03; pap_shift = 2; break;
        case 0x07: ink_mask = 0x07; pap_shift = 3; break;
        case 0x0f: ink_mask = 0x0f; pap_shift = 4; break;
        case 0x1f: ink_mask = 0x1f; pap_shift = 5; break;
        case 0x3f: ink_mask = 0x3f; pap_shift = 6; break;
        case 0x7f: ink_mask = 0x7f; pap_shift = 7; break;
        default: break;
    }
    auto colour = [&](int idx) -> uint16_t {
        const uint16_t c = idx < 0 ? fallback : uint16_t(pal[size_t(idx)] & 0x1ff);
        return global_transparent(c) ? kTransparent : c;
    };
    // Returns {paper, ink} palette indices (-1 = fallback colour).
    auto parse = [&](uint8_t attr, int& paper, int& ink) {
        if (ulanext) {
            ink = attr & ink_mask;
            paper = nr_42_ulanext_format_ == 0xff ? -1 : ((attr >> pap_shift) | 0x80) & 0xff;
        } else if (ulap) {
            ink = 0xc0 | ((attr & 0xc0) >> 2) | (attr & 7);
            paper = 0xc8 | ((attr & 0xc0) >> 2) | ((attr >> 3) & 7);
        } else {
            ink = ((attr & 0x40) >> 3) | (attr & 7);
            paper = (((attr & 0x40) >> 3) | ((attr >> 3) & 7)) | 0x10;
        }
    };

    // Border
    const int border = port_fe_ & 7;
    int bpaper, bink;
    if (ulanext)
        parse(uint8_t(border << pap_shift), bpaper, bink);
    else if (mode == 6)
        parse(uint8_t(0x40 | (~port_ff_ & 0x38)), bpaper, bink);
    else
        parse(uint8_t(border << 3), bpaper, bink);
    const uint16_t border_c = colour(bpaper);

    const int y = row - 32;
    const bool paper_line = y >= 0 && y < 192;
    for (int sx = 0; sx < kWidth; ++sx)
        out[sx] = (paper_line && sx >= 64 && sx < 576) ? kTransparent : border_c;
    if (!paper_line) return;

    const uint8_t clip_y2 = (clip_ula_[3] & 0xc0) == 0xc0 ? 0xbf : clip_ula_[3];
    if (y < clip_ula_[2] || y > clip_y2) return;
    const int cx1 = clip_ula_[0] * 2, cx2 = clip_ula_[1] * 2 + 1;

    if (nr_15_ & 0x80) {  // LoRes replaces the ULA paper
        render_lores(row, out);
        for (int sx = 64; sx < 576; ++sx)
            if (sx - 64 < cx1 || sx - 64 > cx2) out[sx] = kTransparent;
        return;
    }

    const uint8_t* scr = shadow ? bank7() : bank5();
    if (mode == 1) scr += 0x2000;
    const int ys = (y + nr_27_ula_scrolly_) % 192;
    const uint16_t roff = ula_row_offset(ys);
    const int fine = (nr_68_ & 0x04) ? 1 : 0;
    const bool flash_on = !ulanext && !ulap && flash_counter_ >= 16;

    if (mode == 6) {  // Timex 512x192
        int paper, ink;
        parse(uint8_t(0x40 | (~port_ff_ & 0x38) | ((port_ff_ >> 3) & 7)), paper, ink);
        const uint16_t cp = colour(paper), ci = colour(ink);
        const uint8_t* base = shadow ? bank7() : bank5();
        for (int sx = 64; sx < 576; ++sx) {
            const int rel = sx - 64;
            if (rel < cx1 || rel > cx2) continue;
            const int x = (rel + nr_26_ula_scrollx_ * 2 + fine) & 511;
            const int column = x >> 3;
            const uint8_t byte = base[roff + (column >> 1) + ((column & 1) ? 0x2000 : 0)];
            out[sx] = ((byte >> (7 - (x & 7))) & 1) ? ci : cp;
        }
        return;
    }

    for (int sx = 64; sx < 576; ++sx) {
        const int rel = sx - 64;
        if (rel < cx1 || rel > cx2) continue;
        const int x = ((rel + nr_26_ula_scrollx_ * 2 + fine) & 511) >> 1;
        const uint8_t byte = scr[roff + (x >> 3)];
        const uint8_t attr = mode == 2 ? scr[0x2000 + roff + (x >> 3)] : scr[0x1800 + ((ys >> 3) << 5) + (x >> 3)];
        bool bit = (byte >> (7 - (x & 7))) & 1;
        if (flash_on && (attr & 0x80)) bit = !bit;
        int paper, ink;
        parse(attr, paper, ink);
        out[sx] = colour(bit ? ink : paper);
    }
}

void SpecNext::render_lores(int row, uint16_t* out) {
    const auto& pal = palette_[(nr_43_ & 0x02) ? 4 : 0];
    const int y = row - 32;
    const int ys = ((y + nr_33_lores_scrolly_) % 192) >> 1;
    const bool radastan = nr_6a_ & 0x20;
    const bool dfile = ((port_ff_ & 1) != 0) != ((nr_6a_ & 0x10) != 0);
    const int pal_off = ((ulap_en_ && !(nr_43_ & 1)) ? 0xc : 0) | (nr_6a_ & 0x0f);
    const uint8_t* scr = bank5();
    for (int sx = 64; sx < 576; ++sx) {
        const int x4 = (sx - 64 + nr_32_lores_scrollx_ * 2 + 512) & 511;
        const int px = x4 >> 2;
        int idx;
        if (radastan) {
            const uint8_t b = scr[(px >> 1) + 64 * ys + (dfile ? 0x2000 : 0)];
            idx = (pal_off << 4) | ((px & 1) ? (b & 0x0f) : (b >> 4));
        } else {
            idx = scr[ys < 48 ? ys * 128 + px : 0x2000 + (ys - 48) * 128 + px];
        }
        const uint16_t c = uint16_t(pal[size_t(idx & 0xff)] & 0x1ff);
        out[sx] = global_transparent(c) ? kTransparent : c;
    }
}

void SpecNext::render_tilemap(int row, uint16_t* out) {
    for (int sx = 0; sx < kWidth; ++sx) out[sx] = kTransparent;
    const uint8_t ctrl = nr_6b_;
    if (!(ctrl & 0x80)) return;
    if (row < clip_tm_[2] || row > clip_tm_[3]) return;
    const bool cols80 = ctrl & 0x40;
    const bool noattr = ctrl & 0x20;
    const auto& pal = palette_[(ctrl & 0x10) ? 7 : 3];
    const bool text = ctrl & 0x08;
    const bool t512 = ctrl & 0x02;
    const bool on_top = ctrl & 0x01;
    const uint8_t* map = (nr_6e_ & 0x80) ? bank7() : bank5();
    const int map_mask = (nr_6e_ & 0x80) ? 0x1fff : 0x3fff;
    const int map_off = (nr_6e_ & 0x3f) << 8;
    const uint8_t* tiles = (nr_6f_ & 0x80) ? bank7() : bank5();
    const int tile_mask = (nr_6f_ & 0x80) ? 0x1fff : 0x3fff;
    const int tile_off = (nr_6f_ & 0x3f) << 8;
    const int ys = (row + nr_31_tm_scrolly_) & 255;
    const int trow = ys >> 3, py = ys & 7;
    const int cols = cols80 ? 80 : 40;
    const int x1 = clip_tm_[0] * 2, x2 = std::min(clip_tm_[1] * 2 + 1, 319);
    const uint8_t tm_transp = nr_4c_tm_transparent_ & 0x0f;

    int cached_col = -1;
    uint8_t tile_lo = 0, attr = 0;
    for (int sx = 0; sx < kWidth; ++sx) {
        const int x320 = sx >> 1;
        if (x320 < x1 || x320 > x2) continue;
        const int tx = cols80 ? (sx + nr_30_tm_scrollx_) % 640 : (x320 + nr_30_tm_scrollx_) % 320;
        const int tcol = tx >> 3, pxx = tx & 7;
        if (tcol != cached_col) {
            cached_col = tcol;
            const int index = trow * cols + tcol;
            const int addr = map_off + index * (noattr ? 1 : 2);
            tile_lo = map[addr & map_mask];
            attr = noattr ? nr_6c_ : map[(addr + 1) & map_mask];
        }
        int tile = tile_lo;
        if (t512) tile |= (attr & 1) << 8;
        const bool below = !t512 && (attr & 1) && !on_top;
        uint16_t v;
        if (text) {
            const uint8_t def = tiles[(tile_off + tile * 8 + py) & tile_mask];
            const int idx = (attr & 0xfe) | ((def >> (7 - pxx)) & 1);
            const uint16_t c = uint16_t(pal[size_t(idx)] & 0x1ff);
            v = global_transparent(c) ? kTransparent : c;
        } else {
            const int fx = (attr & 0x08) ? 7 - pxx : pxx;
            const int fy = (attr & 0x04) ? 7 - py : py;
            int col = fx, r = fy;
            if (attr & 0x02) {
                col = fy;
                r = 7 - fx;
            }
            const uint8_t b = tiles[(tile_off + tile * 32 + r * 4 + (col >> 1)) & tile_mask];
            const int nib = (col & 1) ? (b & 0x0f) : (b >> 4);
            if (nib == tm_transp) {
                v = kTransparent;
            } else {
                v = uint16_t(pal[size_t((attr & 0xf0) | nib)] & 0x1ff);
            }
        }
        if (opaque(v) && below) v |= kFlag;
        out[sx] = v;
    }
}

void SpecNext::render_layer2(int row, uint16_t* out) {
    for (int sx = 0; sx < kWidth; ++sx) out[sx] = kTransparent;
    if (!layer2_en_) return;
    const int res = (nr_70_ >> 4) & 3;
    const int pal_off = nr_70_ & 0x0f;
    const auto& pal = palette_[(nr_43_ & 0x04) ? 5 : 1];
    const size_t mask = sram_.size() - 1;
    const size_t base = 0x40000 + size_t(nr_12_layer2_bank_) * 0x4000;
    const int scroll_x = ((nr_71_ & 1) << 8) | nr_16_l2_scrollx_;
    const int scroll_y = nr_17_l2_scrolly_;
    auto put = [&](int sx, int idx) {
        const uint16_t e = pal[size_t(idx & 0xff)];
        const uint16_t c = uint16_t(e & 0x1ff);
        if (global_transparent(c)) return;
        out[sx] = uint16_t(c | ((e & 0x8000) ? kFlag : 0));
    };

    if (res == 0) {  // 256x192, row major
        const int y = row - 32;
        if (y < 0 || y >= 192) return;
        if (y < clip_l2_[2] || y > std::min<int>(clip_l2_[3], 191)) return;
        int ys = y + scroll_y;
        if (scroll_y >= 192) ys -= 192;
        else ys %= 192;
        for (int x = clip_l2_[0]; x <= clip_l2_[1]; ++x) {
            const int xs = (x + scroll_x) & 255;
            const uint8_t pix = sram_[(base + size_t(ys) * 256 + size_t(xs)) & mask];
            const int idx = (pix + (pal_off << 4)) & 0xff;
            const int sx = (x + 32) * 2;
            put(sx, idx);
            out[sx + 1] = out[sx];
        }
        return;
    }

    // 320x256 (8 bpp) and 640x256 (4 bpp), column major
    if (row < clip_l2_[2] || row > std::min<int>(clip_l2_[3], 255)) return;
    const int ys = (row + scroll_y) & 255;
    const int x1 = clip_l2_[0] * 2;
    const int x2 = std::min(clip_l2_[1] + 1, 160) * 2 - 1;
    const bool scrollover = scroll_x >= 320;
    for (int x = x1; x <= x2; ++x) {
        int xs = x + scroll_x;
        if (scrollover) xs -= 320;
        else xs %= 320;
        const uint8_t pix = sram_[(base + size_t(xs) * 256 + size_t(ys)) & mask];
        if (res == 1) {
            put(x * 2, (pix + (pal_off << 4)) & 0xff);
            out[x * 2 + 1] = out[x * 2];
        } else {
            put(x * 2, (pal_off << 4) | (pix >> 4));
            put(x * 2 + 1, (pal_off << 4) | (pix & 0x0f));
        }
    }
}

// Visible sprites with anchors and relatives resolved (specnext_sprites).
void SpecNext::build_sprite_cache() {
    sprite_cache_.clear();
    sprite_cache_valid_ = true;
    int anchor = -1;
    bool anchor_vis = false;
    for (int i = 0; i < 128; ++i) {
        const uint8_t* a = &sprite_attr_[size_t(i) * 8];
        const bool relative = (a[3] & 0x40) && ((a[4] >> 6) == 1);
        bool visible = a[3] & 0x80;
        if (relative)
            visible = visible && anchor_vis;
        else
            anchor_vis = visible;
        if (!visible) continue;

        uint8_t cur[5] = {};
        uint8_t anchor_pattern = 0;
        if (!relative) {
            std::memcpy(cur, a, 5);
        } else {
            const SpriteData& an = sprite_cache_[size_t(anchor)];
            const bool ar = an.rel_type && an.rotate;
            const bool axm = an.rel_type && an.xmirror;
            const bool aym = an.rel_type && an.ymirror;
            const int axs = an.rel_type ? an.xscale : 0;
            const int ays = an.rel_type ? an.yscale : 0;
            const uint8_t x0 = ar ? a[1] : a[0];
            const uint8_t y0 = ar ? a[0] : a[1];
            const uint8_t x1 = (ar != axm) ? uint8_t(~x0 + 1) : x0;
            const uint8_t y1 = aym ? uint8_t(~y0 + 1) : y0;
            const int x2 = ((((x1 & 0x80) << 1) | x1) << axs) & 0x1ff;
            const int y2 = ((((y1 & 0x80) << 1) | y1) << ays) & 0x1ff;
            const int x3 = (an.x + x2) & 0x1ff;
            const int y3 = (an.y + y2) & 0x1ff;
            const uint8_t paloff = (a[2] & 1) ? uint8_t((an.paloff + (a[2] >> 4)) & 0x0f) : uint8_t(a[2] >> 4);
            const bool xm = ar ? (((a[2] >> 2) & 1) != ((a[2] >> 1) & 1)) : ((a[2] >> 3) & 1);
            const bool ym = ar ? (((a[2] >> 3) & 1) != ((a[2] >> 1) & 1)) : ((a[2] >> 2) & 1);
            cur[0] = uint8_t(x3);
            cur[1] = uint8_t(y3);
            cur[2] = an.rel_type
                         ? uint8_t((paloff << 4) | (int(axm != xm) << 3) | (int(aym != ym) << 2) |
                                   (int(ar != (((a[2] >> 1) & 1) != 0)) << 1) | ((x3 >> 8) & 1))
                         : uint8_t((paloff << 4) | (a[2] & 0x0e) | ((x3 >> 8) & 1));
            cur[3] = uint8_t(0x80 | 0x40 | (a[3] & 0x3f));
            cur[4] = an.rel_type
                         ? uint8_t((int(an.h) << 7) | (a[4] & 0x20 ? 0x40 : 0) | (axs << 3) | (ays << 1) | ((y3 >> 8) & 1))
                         : uint8_t((int(an.h) << 7) | (a[4] & 0x20 ? 0x40 : 0) | (a[4] & 0x1e) | ((y3 >> 8) & 1));
            anchor_pattern = an.pattern;
        }
        const uint8_t ext = (a[3] & 0x40) ? cur[4] : 0;
        SpriteData s{};
        s.y = ((ext & 1) << 8) | cur[1];
        s.x = ((cur[2] & 1) << 8) | cur[0];
        s.rotate = cur[2] & 0x02;
        s.ymirror = cur[2] & 0x04;
        s.xmirror = cur[2] & 0x08;
        s.paloff = uint8_t(cur[2] >> 4);
        s.h = (ext & 0x80) && (a[3] & 0x40);
        const bool n6 = (ext & 0x40) && s.h;
        s.pattern = uint8_t(((cur[3] & 0x3f) << 1) | int(n6));
        if (relative && (a[4] & 1)) s.pattern = uint8_t((s.pattern + anchor_pattern) & 0x7f);
        s.yscale = uint8_t((ext >> 1) & 3);
        s.xscale = uint8_t((ext >> 3) & 3);
        s.rel_type = (a[4] & 0x20) && (a[3] & 0x40);
        sprite_cache_.push_back(s);
        if (!relative) anchor = int(sprite_cache_.size()) - 1;
    }
}

void SpecNext::render_sprites(int row, uint16_t* out) {
    for (int sx = 0; sx < kWidth; ++sx) out[sx] = kTransparent;
    if (!(nr_15_ & 0x01)) return;
    if (!sprite_cache_valid_) build_sprite_cache();
    const auto& pal = palette_[(nr_43_ & 0x08) ? 6 : 2];
    int cx1 = 0, cx2 = 319, cy1 = 0, cy2 = 255;
    if (!(nr_15_ & 0x02)) {  // clipped to the paper area
        cx1 = clip_spr_[0] + 32;
        cx2 = clip_spr_[1] + 32;
        cy1 = clip_spr_[2] + 32;
        cy2 = clip_spr_[3] + 32;
    } else if (nr_15_ & 0x20) {
        cx1 = clip_spr_[0] * 2;
        cx2 = clip_spr_[1] * 2 + 1;
        cy1 = clip_spr_[2];
        cy2 = clip_spr_[3];
    }
    if (row < cy1 || row > cy2) return;
    cx2 = std::min(cx2, 319);

    const int n = int(sprite_cache_.size());
    const bool zero_on_top = nr_15_ & 0x40;
    for (int k = 0; k < n; ++k) {
        const SpriteData& s = sprite_cache_[size_t(zero_on_top ? n - 1 - k : k)];
        int dy0 = s.y & 0x1ff;
        if (dy0 > 255) dy0 -= 512;
        const int h = 16 << s.yscale;
        const int dy = row - dy0;
        if (dy < 0 || dy >= h) continue;
        int dx0 = s.x & 0x1ff;
        if (dx0 > 319) dx0 -= 512;
        const int w = 16 << s.xscale;
        const int sy = dy >> s.yscale;
        const uint8_t transp = s.h ? uint8_t(nr_4b_sprite_transparent_ & 0x0f) : nr_4b_sprite_transparent_;
        for (int dx = 0; dx < w; ++dx) {
            const int x = dx0 + dx;
            if (x < cx1 || x > cx2) continue;
            const int sxp = dx >> s.xscale;
            const int fx = s.xmirror ? 15 - sxp : sxp;
            const int fy = s.ymirror ? 15 - sy : sy;
            int col = fx, r = fy;
            if (s.rotate) {
                col = fy;
                r = 15 - fx;
            }
            int idx;
            if (s.h) {
                const uint8_t b = sprite_pattern_[(size_t(s.pattern) * 128 + size_t(r) * 8 + size_t(col >> 1)) & 0x3fff];
                const int nib = (col & 1) ? (b & 0x0f) : (b >> 4);
                if (nib == transp) continue;
                idx = (s.paloff << 4) | nib;
            } else {
                const uint8_t p = sprite_pattern_[(size_t(s.pattern >> 1) * 256 + size_t(r) * 16 + size_t(col)) & 0x3fff];
                if (p == transp) continue;
                idx = (p + (s.paloff << 4)) & 0xff;
            }
            const uint16_t c = uint16_t(pal[size_t(idx)] & 0x1ff);
            out[x * 2] = out[x * 2 + 1] = c;
        }
    }
}

void SpecNext::render_line(int out_row, int /*vc*/) {
    uint16_t ula[kWidth], tm[kWidth], l2[kWidth], spr[kWidth];
    const bool ula_en = !(nr_68_ & 0x80);
    if (ula_en)
        render_ula(out_row, ula);
    else
        std::fill(ula, ula + kWidth, kTransparent);
    render_tilemap(out_row, tm);
    render_layer2(out_row, l2);
    render_sprites(out_row, spr);

    const bool tm_en = nr_6b_ & 0x80;
    const bool stencil = (nr_68_ & 0x01) && ula_en && tm_en;
    const uint16_t fallback = uint16_t((nr_4a_fallback_ << 1) | ((nr_4a_fallback_ & 3) ? 1 : 0));
    const int priority = (nr_15_ >> 2) & 7;
    const int blend_src = (nr_68_ >> 5) & 3;
    uint32_t* dst = &framebuffer_[size_t(out_row) * kWidth];

    for (int sx = 0; sx < kWidth; ++sx) {
        const uint16_t U = ula[sx], T = tm[sx], L = l2[sx], S = spr[sx];
        uint16_t ut;
        if (!tm_en) {
            ut = U;
        } else if (stencil) {
            ut = (opaque(U) && opaque(T)) ? uint16_t((U & T) & 0x1ff) : kTransparent;
        } else if (opaque(T) && !(T & kFlag)) {
            ut = T;
        } else if (opaque(U)) {
            ut = U;
        } else {
            ut = T;
        }
        const bool l2_top = opaque(L) && (L & kFlag);
        uint16_t c = kTransparent;
        if (l2_top) {
            c = L;
        } else if (priority < 6) {
            static const uint8_t order[6][3] = {
                {0, 1, 2}, {1, 0, 2}, {0, 2, 1}, {1, 2, 0}, {2, 0, 1}, {2, 1, 0}};
            const uint16_t layers[3] = {S, L, ut};
            for (int i = 0; i < 3; ++i) {
                const uint16_t v = layers[order[priority][i]];
                if (opaque(v)) {
                    c = v;
                    break;
                }
            }
        } else if (blend_src == 1) {  // blending disabled: S, U|T, L
            c = opaque(S) ? S : opaque(ut) ? ut : L;
        } else {
            const uint16_t ub = blend_src == 0 ? U : blend_src == 3 ? T : ut;
            const uint16_t top = blend_src == 0 ? (tm_en ? T : kTransparent)
                                                : blend_src == 3 ? U : kTransparent;
            uint16_t mixed;
            if (opaque(ub) && opaque(L))
                mixed = mix_add(ub, L, priority == 7 ? 5 : 0);
            else if (opaque(L))
                mixed = L;
            else
                mixed = ub;
            c = opaque(S) ? S : opaque(top) ? top : mixed;
        }
        dst[sx] = rgb9_to_argb(opaque(c) ? uint16_t(c & 0x1ff) : fallback);
    }
}

}  // namespace dsp
