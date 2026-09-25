#include "video/snes_ppu.h"

#include <algorithm>
#include <cstring>

namespace dsp {
namespace {

constexpr uint8_t kBgDepth[8][4] = {
    {2, 2, 2, 2}, {4, 4, 2, 0}, {4, 4, 0, 0}, {8, 4, 0, 0},
    {8, 2, 0, 0}, {4, 2, 0, 0}, {4, 0, 0, 0}, {8, 0, 0, 0},
};

// OBJ sizes selected by OBSEL bits 5-7: {small w, small h, large w, large h}.
constexpr int kObjSize[8][4] = {
    {8, 8, 16, 16},   {8, 8, 32, 32},   {8, 8, 64, 64},   {16, 16, 32, 32},
    {16, 16, 64, 64}, {32, 32, 64, 64}, {16, 32, 32, 64}, {16, 32, 32, 32},
};

// Place of every layer in the priority order of each mode (bigger is in
// front). BG entries are {tile priority 0, tile priority 1}.
struct Priorities {
    uint8_t bg[4][2];
    uint8_t obj[4];
};

Priorities priorities_for(int mode, bool bg3_high) {
    Priorities p{};
    const uint8_t obj[4] = {3, 6, 9, 12};
    std::memcpy(p.obj, obj, sizeof(obj));
    switch (mode) {
        case 0:
            p.bg[0][0] = 8; p.bg[0][1] = 11;
            p.bg[1][0] = 7; p.bg[1][1] = 10;
            p.bg[2][0] = 2; p.bg[2][1] = 5;
            p.bg[3][0] = 1; p.bg[3][1] = 4;
            break;
        case 1:
            p.bg[0][0] = 8; p.bg[0][1] = 11;
            p.bg[1][0] = 7; p.bg[1][1] = 10;
            p.bg[2][0] = 2; p.bg[2][1] = bg3_high ? 13 : 5;
            break;
        case 7:
            p.bg[0][0] = 5; p.bg[0][1] = 5;
            p.bg[1][0] = 2; p.bg[1][1] = 8;   // EXTBG
            break;
        default:  // modes 2-6
            p.bg[0][0] = 5; p.bg[0][1] = 11;
            p.bg[1][0] = 2; p.bg[1][1] = 8;
            break;
    }
    return p;
}

int sext10(int n) { return (n & 0x2000) ? (n | ~1023) : (n & 1023); }

}  // namespace

void SnesPpu::reset() {
    vram_.fill(0);
    cgram_.fill(0);
    oam_.fill(0);
    inidisp_ = 0x80;
    bgmode_ = 0;
    obsel_ = 0;
    mosaic_ = 0;
    bg_ = {};
    tm_ = ts_ = 0;
    tmw_ = tsw_ = 0;
    std::memset(wsel_, 0, sizeof(wsel_));
    std::memset(wh_, 0, sizeof(wh_));
    wbglog_ = wobjlog_ = 0;
    cgwsel_ = 0;
    cgadsub_ = 0;
    coldata_ = 0;
    setini_ = 0;
    vmain_ = 0;
    vmadd_ = 0;
    vram_latch_ = 0;
    vram_open_ = true;
    cgadd_ = 0;
    cg_high_ = false;
    cg_low_ = 0;
    cg_read_high_ = false;
    oam_reload_ = 0;
    oam_addr_ = 0;
    oam_priority_ = false;
    oam_low_ = 0;
    range_over_ = time_over_ = false;
    m7a_ = 0x100; m7b_ = 0; m7c_ = 0; m7d_ = 0x100;
    m7x_ = m7y_ = m7hofs_ = m7vofs_ = 0;
    m7_prev_ = 0;
    m7sel_ = 0;
    scroll_prev_ = 0;
    scroll_prev_h_ = 0;
    ppu1_open_bus_ = ppu2_open_bus_ = 0;
    mosaic_start_ = 1;
}

void SnesPpu::start_vblank() {
    // The OAM address is reloaded from OAMADD at the start of every
    // vertical blank (unless the screen is forced blank).
    if (!forced_blank()) oam_addr_ = uint16_t(oam_reload_ << 1);
    range_over_ = time_over_ = false;
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

uint16_t SnesPpu::direct_color(uint8_t pixel, int pal) const {
    // Direct colour: BBGGGRRR from the pixel, one extra bit per component
    // from the tile's palette number.
    const int r = ((pixel & 7) << 2) | ((pal & 1) << 1);
    const int g = (((pixel >> 3) & 7) << 2) | (pal & 2);
    const int b = (((pixel >> 6) & 3) << 3) | (pal & 4);
    return uint16_t(r | (g << 5) | (b << 10));
}

uint16_t SnesPpu::vram_address() const {
    // VMAIN bits 2-3 rotate the low bits of the address (bitmap uploads).
    const uint16_t a = vmadd_;
    switch ((vmain_ >> 2) & 3) {
        case 1: return uint16_t((a & 0xff00) | ((a & 0x001f) << 3) | ((a >> 5) & 7));
        case 2: return uint16_t((a & 0xfe00) | ((a & 0x003f) << 3) | ((a >> 6) & 7));
        case 3: return uint16_t((a & 0xfc00) | ((a & 0x007f) << 3) | ((a >> 7) & 7));
        default: return a;
    }
}

void SnesPpu::write(uint16_t reg, uint8_t value) {
    ppu1_open_bus_ = value;
    switch (reg & 0xff) {
        case 0x00:
            // Leaving forced blank at the start of vblank also reloads OAM.
            inidisp_ = value;
            return;
        case 0x01: obsel_ = value; return;
        case 0x02:
            oam_reload_ = uint16_t((oam_reload_ & 0x100) | value);
            oam_addr_ = uint16_t(oam_reload_ << 1);
            return;
        case 0x03:
            oam_reload_ = uint16_t((oam_reload_ & 0x0ff) | ((value & 1) << 8));
            oam_priority_ = (value & 0x80) != 0;
            oam_addr_ = uint16_t(oam_reload_ << 1);
            return;
        case 0x04:
            // The low table is written a word at a time: even bytes are
            // latched and land together with the following odd byte. The
            // 32-byte high table is written directly.
            if (oam_addr_ >= 0x200) {
                oam_[size_t(0x200 + (oam_addr_ & 0x1f))] = value;
            } else if (!(oam_addr_ & 1)) {
                oam_low_ = value;
            } else {
                oam_[size_t(oam_addr_ - 1)] = oam_low_;
                oam_[size_t(oam_addr_)] = value;
            }
            oam_addr_ = uint16_t((oam_addr_ + 1) & 0x3ff);
            return;
        case 0x05:
            bgmode_ = value;
            for (int i = 0; i < 4; i++) bg_[size_t(i)].tile16 = (value & (0x10 << i)) != 0;
            return;
        case 0x06: mosaic_ = value; return;
        case 0x07: case 0x08: case 0x09: case 0x0a: {
            Bg& b = bg_[size_t((reg & 0xff) - 0x07)];
            b.map_base = uint16_t((value & 0xfc) << 8);
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
        case 0x0d:
            m7hofs_ = int16_t(sext13((value << 8) | m7_prev_));
            m7_prev_ = value;
            bg_[0].hofs = uint16_t(((value << 8) | (scroll_prev_ & ~7) | (scroll_prev_h_ & 7)) & 0x3ff);
            scroll_prev_ = value;
            scroll_prev_h_ = value;
            return;
        case 0x0e:
            m7vofs_ = int16_t(sext13((value << 8) | m7_prev_));
            m7_prev_ = value;
            bg_[0].vofs = uint16_t(((value << 8) | scroll_prev_) & 0x3ff);
            scroll_prev_ = value;
            return;
        case 0x0f: case 0x11: case 0x13: {
            Bg& b = bg_[size_t(((reg & 0xff) - 0x0d) / 2)];
            b.hofs = uint16_t(((value << 8) | (scroll_prev_ & ~7) | (scroll_prev_h_ & 7)) & 0x3ff);
            scroll_prev_ = value;
            scroll_prev_h_ = value;
            return;
        }
        case 0x10: case 0x12: case 0x14: {
            Bg& b = bg_[size_t(((reg & 0xff) - 0x0e) / 2)];
            b.vofs = uint16_t(((value << 8) | scroll_prev_) & 0x3ff);
            scroll_prev_ = value;
            return;
        }
        case 0x15: vmain_ = value; return;
        case 0x16:
            vmadd_ = uint16_t((vmadd_ & 0xff00) | value);
            vram_latch_ = vram_[vram_address() & (kVramWords - 1)];
            return;
        case 0x17:
            vmadd_ = uint16_t((vmadd_ & 0x00ff) | (value << 8));
            vram_latch_ = vram_[vram_address() & (kVramWords - 1)];
            return;
        case 0x18: {
            uint16_t& w = vram_[vram_address() & (kVramWords - 1)];
            if (vram_open_ || forced_blank()) w = uint16_t((w & 0xff00) | value);
            if (!(vmain_ & 0x80)) vmadd_ = uint16_t(vmadd_ + vram_step());
            return;
        }
        case 0x19: {
            uint16_t& w = vram_[vram_address() & (kVramWords - 1)];
            if (vram_open_ || forced_blank()) w = uint16_t((w & 0x00ff) | (value << 8));
            if (vmain_ & 0x80) vmadd_ = uint16_t(vmadd_ + vram_step());
            return;
        }
        case 0x1a: m7sel_ = value; return;
        case 0x1b: m7a_ = int16_t((value << 8) | m7_prev_); m7_prev_ = value; return;
        case 0x1c: m7b_ = int16_t((value << 8) | m7_prev_); m7_prev_ = value; return;
        case 0x1d: m7c_ = int16_t((value << 8) | m7_prev_); m7_prev_ = value; return;
        case 0x1e: m7d_ = int16_t((value << 8) | m7_prev_); m7_prev_ = value; return;
        case 0x1f: m7x_ = int16_t(sext13((value << 8) | m7_prev_)); m7_prev_ = value; return;
        case 0x20: m7y_ = int16_t(sext13((value << 8) | m7_prev_)); m7_prev_ = value; return;
        case 0x21:
            cgadd_ = value;
            cg_high_ = false;
            cg_read_high_ = false;
            return;
        case 0x22:
            if (!cg_high_) {
                cg_low_ = value;
                cg_high_ = true;
            } else {
                cgram_[cgadd_] = uint16_t(((value & 0x7f) << 8) | cg_low_);
                cgadd_ = uint8_t(cgadd_ + 1);
                cg_high_ = false;
            }
            return;
        case 0x23: wsel_[0] = value; return;
        case 0x24: wsel_[1] = value; return;
        case 0x25: wsel_[2] = value; return;
        case 0x26: case 0x27: case 0x28: case 0x29: wh_[(reg & 0xff) - 0x26] = value; return;
        case 0x2a: wbglog_ = value; return;
        case 0x2b: wobjlog_ = value; return;
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
        case 0x33: setini_ = value; return;
        default: return;
    }
}

uint8_t SnesPpu::read(uint16_t reg) {
    switch (reg & 0xff) {
        case 0x34: case 0x35: case 0x36: {
            // MPYL/M/H: M7A times the signed high byte of the last M7B write.
            const int32_t r = int32_t(m7a_) * int32_t(int8_t(m7b_ >> 8));
            ppu1_open_bus_ = uint8_t(r >> (8 * ((reg & 0xff) - 0x34)));
            return ppu1_open_bus_;
        }
        case 0x38: {
            const uint8_t v = oam_addr_ >= 0x200 ? oam_[size_t(0x200 + (oam_addr_ & 0x1f))]
                                                 : oam_[oam_addr_];
            oam_addr_ = uint16_t((oam_addr_ + 1) & 0x3ff);
            ppu1_open_bus_ = v;
            return v;
        }
        case 0x39: {
            const uint8_t v = uint8_t(vram_latch_);
            if (!(vmain_ & 0x80)) {
                vram_latch_ = vram_[vram_address() & (kVramWords - 1)];
                vmadd_ = uint16_t(vmadd_ + vram_step());
            }
            ppu1_open_bus_ = v;
            return v;
        }
        case 0x3a: {
            const uint8_t v = uint8_t(vram_latch_ >> 8);
            if (vmain_ & 0x80) {
                vram_latch_ = vram_[vram_address() & (kVramWords - 1)];
                vmadd_ = uint16_t(vmadd_ + vram_step());
            }
            ppu1_open_bus_ = v;
            return v;
        }
        case 0x3b: {
            const uint16_t c = cgram_[cgadd_];
            uint8_t v;
            if (!cg_read_high_) {
                v = uint8_t(c);
            } else {
                v = uint8_t(((c >> 8) & 0x7f) | (ppu2_open_bus_ & 0x80));
                cgadd_ = uint8_t(cgadd_ + 1);
            }
            cg_read_high_ = !cg_read_high_;
            ppu2_open_bus_ = v;
            return v;
        }
        case 0x3e:   // STAT77: time/range over, PPU1 version 1
            ppu1_open_bus_ = uint8_t((time_over_ ? 0x80 : 0) | (range_over_ ? 0x40 : 0) | 0x01);
            return ppu1_open_bus_;
        case 0x3f:   // STAT78: NTSC, PPU2 version 3
            ppu2_open_bus_ = 0x03;
            return ppu2_open_bus_;
        default:
            return ppu1_open_bus_;
    }
}

uint16_t SnesPpu::tilemap_entry(const Bg& bg, int tx, int ty) const {
    const int map_w = (bg.map_size & 1) ? 64 : 32;
    const int map_h = (bg.map_size & 2) ? 64 : 32;
    tx &= (map_w - 1);
    ty &= (map_h - 1);
    int map_off = 0;
    if (tx & 32) map_off += 0x400;
    if (ty & 32) map_off += (bg.map_size & 1) ? 0x800 : 0x400;
    return vram_[(bg.map_base + map_off + (ty & 31) * 32 + (tx & 31)) & (kVramWords - 1)];
}

bool SnesPpu::window(int layer, int x) const {
    uint8_t sel;
    uint8_t logic;
    switch (layer) {
        case 0: sel = wsel_[0] & 0x0f; logic = wbglog_ & 3; break;
        case 1: sel = wsel_[0] >> 4; logic = (wbglog_ >> 2) & 3; break;
        case 2: sel = wsel_[1] & 0x0f; logic = (wbglog_ >> 4) & 3; break;
        case 3: sel = wsel_[1] >> 4; logic = (wbglog_ >> 6) & 3; break;
        case 4: sel = wsel_[2] & 0x0f; logic = wobjlog_ & 3; break;
        default: sel = wsel_[2] >> 4; logic = (wobjlog_ >> 2) & 3; break;
    }
    const bool en1 = (sel & 0x02) != 0, en2 = (sel & 0x08) != 0;
    if (!en1 && !en2) return false;
    const bool w1 = (x >= wh_[0] && x <= wh_[1]) != ((sel & 0x01) != 0);
    const bool w2 = (x >= wh_[2] && x <= wh_[3]) != ((sel & 0x04) != 0);
    if (en1 && !en2) return w1;
    if (en2 && !en1) return w2;
    switch (logic) {
        case 0: return w1 || w2;
        case 1: return w1 && w2;
        case 2: return w1 != w2;
        default: return w1 == w2;
    }
}

void SnesPpu::render_bg(int bgi, int depth, int line, std::array<Pixel, kWidth>& out,
                        const uint8_t (&z)[2]) {
    const Bg& bg = bg_[size_t(bgi)];
    const int mode = bgmode_ & 7;
    const bool hires = mode == 5 || mode == 6;
    const int tile_w = (bg.tile16 || hires) ? 16 : 8;
    const int tile_h = bg.tile16 ? 16 : 8;
    const bool mosaic = (mosaic_ & (1 << bgi)) != 0;
    const int msize = (mosaic_ >> 4) + 1;
    int y_line = line;
    if (mosaic && msize > 1) y_line = line - ((line - mosaic_start_) % msize + msize) % msize;
    const bool opt = (mode == 2 || mode == 4 || mode == 6) && bgi < 2;
    const uint16_t opt_bit = uint16_t(bgi == 0 ? 0x2000 : 0x4000);

    uint16_t cached_entry = 0;
    int cached_tx = -1, cached_ty = -1;
    for (int px = 0; px < kWidth; px++) {
        int sx = px;
        if (mosaic && msize > 1) sx = px - px % msize;
        int hofs = bg.hofs, vofs = bg.vofs;
        if (opt) {
            // Offset-per-tile: BG3's tilemap row holds a horizontal (and in
            // modes 2/6 a vertical) offset for every 8-pixel column but the
            // first one.
            const int col = (sx + (hofs & 7)) >> 3;
            if (col > 0) {
                const Bg& b3 = bg_[2];
                const int tx = (col - 1) + (b3.hofs >> 3);
                const int ty = b3.vofs >> 3;
                if (mode == 4) {
                    const uint16_t v = tilemap_entry(b3, tx, ty);
                    if (v & opt_bit) {
                        if (v & 0x8000) vofs = v & 0x3ff;
                        else hofs = (hofs & 7) | (v & 0x3f8);
                    }
                } else {
                    const uint16_t h = tilemap_entry(b3, tx, ty);
                    const uint16_t v = tilemap_entry(b3, tx, ty + 1);
                    if (h & opt_bit) hofs = (hofs & 7) | (h & 0x3f8);
                    if (v & opt_bit) vofs = v & 0x3ff;
                }
            }
        }
        const int y = (y_line + vofs) & 0x3ff;
        const int xw = hires ? ((sx * 2 + hofs * 2) & 0x7ff) : ((sx + hofs) & 0x3ff);
        const int tx = xw / tile_w, ty = y / tile_h;
        if (tx != cached_tx || ty != cached_ty) {
            cached_entry = tilemap_entry(bg, tx, ty);
            cached_tx = tx;
            cached_ty = ty;
        }
        const uint16_t entry = cached_entry;
        const bool hflip = (entry & 0x4000) != 0, vflip = (entry & 0x8000) != 0;
        const int pal = (entry >> 10) & 7;
        int fx = xw % tile_w, fy = y % tile_h;
        if (hflip) fx = tile_w - 1 - fx;
        if (vflip) fy = tile_h - 1 - fy;
        int tile_no = entry & 0x3ff;
        if (tile_w == 16 && fx >= 8) tile_no += 1;
        if (tile_h == 16 && fy >= 8) tile_no += 16;
        fx &= 7;
        fy &= 7;
        const uint16_t base = uint16_t(bg.chr_base + (tile_no & 0x3ff) * (depth * 4));
        uint8_t pixel = 0;
        const int bit = 7 - fx;
        for (int plane = 0; plane < depth; plane += 2) {
            const uint16_t w = vram_[(base + (plane / 2) * 8 + fy) & (kVramWords - 1)];
            pixel = uint8_t(pixel | (((w >> bit) & 1) << plane));
            pixel = uint8_t(pixel | (((w >> (8 + bit)) & 1) << (plane + 1)));
        }
        if (pixel == 0) continue;
        uint16_t color;
        if (depth == 8) {
            color = (cgwsel_ & 0x01) ? direct_color(pixel, pal) : cgram_[pixel];
        } else {
            int pal_base = pal * (1 << depth);
            if (mode == 0) pal_base += bgi * 32;
            color = cgram_[(pal_base + pixel) & 0xff];
        }
        out[size_t(px)] = {color, z[(entry & 0x2000) ? 1 : 0], uint8_t(bgi), true};
    }
}

void SnesPpu::render_mode7(int line, std::array<Pixel, kWidth>& bg1,
                           std::array<Pixel, kWidth>& bg2, const uint8_t (&z1)[2],
                           const uint8_t (&z2)[2]) {
    const int a = m7a_, b = m7b_, c = m7c_, d = m7d_;
    const int cx = sext13(m7x_), cy = sext13(m7y_);
    const int hofs = sext13(m7hofs_), vofs = sext13(m7vofs_);
    const int y = (m7sel_ & 0x02) ? 255 - line : line;
    const int dx = sext10(hofs - cx), dy = sext10(vofs - cy);
    const int psx = ((a * dx) & ~63) + ((b * dy) & ~63) + ((b * y) & ~63) + (cx << 8);
    const int psy = ((c * dx) & ~63) + ((d * dy) & ~63) + ((d * y) & ~63) + (cy << 8);
    const int repeat = (m7sel_ >> 6) & 3;
    const bool extbg = (setini_ & 0x40) != 0;
    for (int px = 0; px < kWidth; px++) {
        const int x = (m7sel_ & 0x01) ? 255 - px : px;
        const int vx = (psx + a * x) >> 8;
        const int vy = (psy + c * x) >> 8;
        const bool outside = ((vx | vy) & ~1023) != 0;
        uint8_t tile;
        if (outside && repeat == 2) continue;
        if (outside && repeat == 3) tile = 0;
        else tile = uint8_t(vram_[(((vy & 1023) >> 3) * 128 + ((vx & 1023) >> 3)) & (kVramWords - 1)]);
        const uint8_t pixel =
            uint8_t(vram_[(tile * 64 + (vy & 7) * 8 + (vx & 7)) & (kVramWords - 1)] >> 8);
        if (pixel != 0) {
            const uint16_t color = (cgwsel_ & 0x01) ? direct_color(pixel, 0) : cgram_[pixel];
            bg1[size_t(px)] = {color, z1[0], 0, true};
        }
        if (extbg && (pixel & 0x7f) != 0) {
            bg2[size_t(px)] = {cgram_[pixel & 0x7f], z2[(pixel & 0x80) ? 1 : 0], 1, true};
        }
    }
}

void SnesPpu::render_sprites(int line, std::array<Pixel, kWidth>& out, const uint8_t (&z)[4]) {
    const int sel = (obsel_ >> 5) & 7;
    const uint16_t chr_base = uint16_t((obsel_ & 0x07) << 13);
    const uint16_t name_gap = uint16_t((((obsel_ >> 3) & 3) + 1) << 12);
    const int row = line - 1;   // OBJ Y=0 shows on the first visible line
    const int first = oam_priority_ ? ((oam_reload_ >> 1) & 0x7f) : 0;

    // Range pass: the first 32 sprites (from the priority start) on this line.
    int list[32];
    int count = 0;
    for (int k = 0; k < 128; k++) {
        const int i = (first + k) & 127;
        const uint8_t hi = oam_[size_t(0x200 + (i >> 2))];
        const int shift = (i & 3) * 2;
        const bool large = ((hi >> shift) & 2) != 0;
        const int h = kObjSize[sel][large ? 3 : 1];
        const int w = kObjSize[sel][large ? 2 : 0];
        int x = oam_[size_t(i * 4)] | (((hi >> shift) & 1) << 8);
        if (x >= 256) x -= 512;
        if (x <= -w) continue;   // entirely off the left edge
        const int rel = (row - oam_[size_t(i * 4 + 1)]) & 0xff;
        if (rel >= h) continue;
        if (count == 32) {
            range_over_ = true;
            break;
        }
        list[count++] = i;
    }

    // Time pass: at most 34 8-pixel slivers, fetched from the last sprite
    // in range backwards; draw in the same order so the first sprite wins.
    std::array<uint16_t, kWidth> color{};
    std::array<int8_t, kWidth> prio;
    std::array<bool, kWidth> math{};
    prio.fill(-1);
    int tiles = 0;
    for (int n = count - 1; n >= 0; n--) {
        const int i = list[n];
        const uint8_t* e = &oam_[size_t(i * 4)];
        const uint8_t hi = oam_[size_t(0x200 + (i >> 2))];
        const int shift = (i & 3) * 2;
        const bool large = ((hi >> shift) & 2) != 0;
        const int w = kObjSize[sel][large ? 2 : 0];
        const int h = kObjSize[sel][large ? 3 : 1];
        int x = e[0] | (((hi >> shift) & 1) << 8);
        if (x >= 256) x -= 512;
        const int rel = (row - e[1]) & 0xff;
        const uint8_t attr = e[3];
        const bool hflip = (attr & 0x40) != 0, vflip = (attr & 0x80) != 0;
        const int pal = (attr >> 1) & 7;
        const int priority = (attr >> 4) & 3;
        const int fy = vflip ? (h - 1 - rel) : rel;
        const int tile = e[2] | ((attr & 1) << 8);
        for (int col = 0; col < w / 8; col++) {
            const int sx0 = x + col * 8;
            if (sx0 <= -8 || sx0 >= kWidth) continue;
            if (tiles >= 34) {
                time_over_ = true;
                break;
            }
            tiles++;
            const int fcol = hflip ? (w / 8 - 1 - col) : col;
            // Tiles form a 16x16 grid: rows add 16, columns wrap in the row.
            const int tno = (tile & 0x100) | ((((tile >> 4) + (fy >> 3)) & 0x0f) << 4) |
                            ((tile + fcol) & 0x0f);
            const uint16_t addr = uint16_t(chr_base + ((tno & 0x100) ? name_gap : 0) +
                                           (tno & 0xff) * 16 + (fy & 7));
            const uint16_t w0 = vram_[addr & (kVramWords - 1)];
            const uint16_t w1 = vram_[(addr + 8) & (kVramWords - 1)];
            for (int b = 0; b < 8; b++) {
                const int sx = sx0 + b;
                if (sx < 0 || sx >= kWidth) continue;
                const int bit = hflip ? b : 7 - b;
                const uint8_t pixel = uint8_t(((w0 >> bit) & 1) | (((w0 >> (8 + bit)) & 1) << 1) |
                                              (((w1 >> bit) & 1) << 2) |
                                              (((w1 >> (8 + bit)) & 1) << 3));
                if (pixel == 0) continue;
                color[size_t(sx)] = cgram_[size_t(128 + pal * 16 + pixel)];
                prio[size_t(sx)] = int8_t(priority);
                math[size_t(sx)] = pal >= 4;
            }
        }
    }
    for (int x = 0; x < kWidth; x++) {
        if (prio[size_t(x)] < 0) continue;
        out[size_t(x)] = {color[size_t(x)], z[prio[size_t(x)]], 4, math[size_t(x)]};
    }
}

void SnesPpu::render_line(int line, uint32_t* dst) {
    if (forced_blank()) {
        std::fill(dst, dst + kWidth, 0xff000000u);
        return;
    }
    if (line == 1) mosaic_start_ = 1;
    const int mode = bgmode_ & 7;
    const Priorities pr = priorities_for(mode, (bgmode_ & 0x08) != 0);

    // Every layer is rendered once; main and sub screen pick from them.
    std::array<std::array<Pixel, kWidth>, 5> layer{};
    const uint8_t used = uint8_t(tm_ | ts_);
    if (mode == 7) {
        if (used & 0x03) render_mode7(line, layer[0], layer[1], pr.bg[0], pr.bg[1]);
    } else {
        for (int i = 0; i < 4; i++) {
            const int depth = kBgDepth[mode][i];
            if (depth != 0 && (used & (1 << i))) render_bg(i, depth, line, layer[size_t(i)], pr.bg[i]);
        }
    }
    if (used & 0x10) render_sprites(line, layer[4], pr.obj);

    // Precompute the window masks that are in use.
    std::array<std::array<bool, kWidth>, 6> win{};
    const uint8_t win_layers = uint8_t(tmw_ | tsw_);
    for (int l = 0; l < 5; l++) {
        if (!(win_layers & (1 << l))) continue;
        for (int x = 0; x < kWidth; x++) win[size_t(l)][size_t(x)] = window(l, x);
    }
    for (int x = 0; x < kWidth; x++) win[5][size_t(x)] = window(5, x);

    const uint16_t backdrop = cgram_[0];
    for (int x = 0; x < kWidth; x++) {
        Pixel main{backdrop, 0, 5, true};
        Pixel sub{coldata_, 0, 5, true};
        for (int l = 0; l < 5; l++) {
            const Pixel& p = layer[size_t(l)][size_t(x)];
            if (p.z == 0) continue;
            const bool masked = win[size_t(l)][size_t(x)];
            if ((tm_ & (1 << l)) && !((tmw_ & (1 << l)) && masked) && p.z > main.z) main = p;
            if ((ts_ & (1 << l)) && !((tsw_ & (1 << l)) && masked) && p.z > sub.z) sub = p;
        }

        const bool cw = win[5][size_t(x)];
        auto region = [cw](int r) { return r == 3 || (r == 1 && !cw) || (r == 2 && cw); };
        uint16_t c = main.color;
        const bool clipped = region((cgwsel_ >> 6) & 3);
        if (clipped) c = 0;
        const bool prevent = region((cgwsel_ >> 4) & 3);
        const bool enabled = (cgadsub_ & (1 << main.layer)) != 0 && main.math;
        if (enabled && !prevent) {
            const bool use_sub = (cgwsel_ & 0x02) != 0;
            const bool sub_transparent = sub.z == 0;
            const uint16_t addend = (use_sub && !sub_transparent) ? sub.color : coldata_;
            const bool half = (cgadsub_ & 0x40) && !clipped && !(use_sub && sub_transparent);
            int r = c & 0x1f, g = (c >> 5) & 0x1f, b = (c >> 10) & 0x1f;
            const int sr = addend & 0x1f, sg = (addend >> 5) & 0x1f, sb = (addend >> 10) & 0x1f;
            if (cgadsub_ & 0x80) {
                r -= sr; g -= sg; b -= sb;
            } else {
                r += sr; g += sg; b += sb;
            }
            if (half) {
                r >>= 1; g >>= 1; b >>= 1;
            }
            r = std::clamp(r, 0, 31);
            g = std::clamp(g, 0, 31);
            b = std::clamp(b, 0, 31);
            c = uint16_t(r | (g << 5) | (b << 10));
        }
        dst[x] = colour(c);
    }
}

}  // namespace dsp
