#include "video/k053246.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <algorithm>

namespace dsp {

K053246::K053246(SpriteCallback cb, std::vector<uint8_t> rom)
    : callback_(std::move(cb)), rom_(std::move(rom)) {
    // Pascal k053247_start: gfx_set_desc_data(4,0,8*128,0,1,2,3)
    // ps_x: 2*4,3*4,0*4,1*4,6*4,7*4,4*4,5*4, 10*4..13*4
    // ps_y: 0*64 .. 15*64
    GfxLayout layout;
    layout.width = 16;
    layout.height = 16;
    layout.total = int(rom_.size() / 128);
    layout.planes = 4;
    layout.char_increment = 8 * 128;
    layout.plane_offsets = {0, 1, 2, 3};
    layout.x_offsets = {
        2*4, 3*4, 0*4, 1*4, 6*4, 7*4, 4*4, 5*4,
        10*4, 11*4, 8*4, 9*4, 14*4, 15*4, 12*4, 13*4};
    layout.y_offsets = {
        0*64, 1*64, 2*64, 3*64, 4*64, 5*64, 6*64, 7*64,
        8*64, 9*64, 10*64, 11*64, 12*64, 13*64, 14*64, 15*64};
    if (!rom_.empty()) gfx_.decode(layout, rom_);
    sprite_mask_ = uint32_t(layout.total ? layout.total - 1 : 0);
}

void K053246::start(int dx, int dy) {
    dx_ = dx;
    dy_ = dy;
}

void K053246::reset() {
    kx46_regs_.fill(0);
    kx47_regs_.fill(0);
    objcha_ = false;
    sprite_count_ = 0;
}

uint8_t K053246::read(uint8_t offset) {
    if (objcha_) {
        // ROM read via OBJCHA
        const uint32_t addr =
            (uint32_t(kx46_regs_[6]) << 17) | (uint32_t(kx46_regs_[7]) << 9) |
            (uint32_t(kx46_regs_[4]) << 1) | (offset & 1);
        if (addr < rom_.size()) return rom_[addr];
        return 0;
    }
    return 0;
}

void K053246::write(uint8_t offset, uint8_t value) {
    offset &= 7;
    kx46_regs_[offset] = value;
}

void K053246::update_sprites() {
    int count = 0;
    for (int f = 0; f < 256; f++) {
        if ((ram_[size_t(f * 8)] & 0x8000) != 0) {
            sorted_list_[size_t(count++)] = uint16_t(f * 8);
        }
    }
    sprite_count_ = count;
}

void K053246::draw_sprites(uint16_t* dest, int dest_w, int dest_h, int crop_x, int crop_y,
                           uint8_t prio) {
    if (sprite_count_ <= 0) return;

    // Simple sort by Z (descending when PRI clear)
    const bool ascending = (kx47_regs_[(0x0c / 2)] & 0x10) != 0;
    for (int y = 0; y < sprite_count_ - 1; y++) {
        for (int x = y + 1; x < sprite_count_; x++) {
            const int za = ram_[sorted_list_[size_t(y)]] & 0xff;
            const int zb = ram_[sorted_list_[size_t(x)]] & 0xff;
            const bool swap = ascending ? (za >= zb) : (za <= zb);
            if (swap) {
                const uint16_t t = sorted_list_[size_t(y)];
                sorted_list_[size_t(y)] = sorted_list_[size_t(x)];
                sorted_list_[size_t(x)] = t;
            }
        }
    }

    for (int f = 0; f < sprite_count_; f++) {
        const int w = sorted_list_[size_t(f)];
        uint32_t code = ram_[size_t(w + 1)] & sprite_mask_;
        uint16_t color = ram_[size_t(w + 6)];
        uint16_t primask = 0;
        if (callback_) callback_(code, color, primask);
        if (primask != prio) continue;
        draw_single(code, w, color, dest, dest_w, dest_h, crop_x, crop_y);
    }
}

void K053246::draw_single(uint32_t code, int offs, uint16_t color, uint16_t* dest, int dw, int dh,
                          int crop_x, int crop_y) {
    const uint16_t temp4 = ram_[size_t(offs)];
    int oy = int(ram_[size_t(offs + 2)] & 0x3ff);
    int ox = int(ram_[size_t(offs + 3)] & 0x3ff);

    const uint8_t scaley = uint8_t(ram_[size_t(offs + 4)]);
    float zoomy = (scaley != 0) ? (64.0f / float(scaley)) : 0.f;
    float zoomx;
    uint8_t scalex;
    if ((temp4 & 0x4000) == 0) {
        scalex = uint8_t(ram_[size_t(offs + 5)]);
        zoomx = (scalex != 0) ? (64.0f / float(scalex)) : 0.f;
    } else {
        zoomx = zoomy;
        scalex = scaley;
    }
    if (zoomx <= 0.f || zoomy <= 0.f) return;

    const bool flipx = (temp4 & 0x1000) != 0;
    const bool flipy = (temp4 & 0x2000) != 0;

    const int offx = int16_t((uint16_t(kx46_regs_[0]) << 8) | kx46_regs_[1]);
    const int offy = int16_t((uint16_t(kx46_regs_[2]) << 8) | kx46_regs_[3]);

    const int wrapsize = ((kx47_regs_[(0x0c / 2)] & 0x40) != 0) ? 512 : 1024;
    const int xwraplim = wrapsize - (((kx47_regs_[(0x0c / 2)] & 0x40) != 0) ? 64 : 384);
    const int ywraplim = wrapsize - (((kx47_regs_[(0x0c / 2)] & 0x40) != 0) ? 128 : 512);
    const int temp = wrapsize - 1;
    ox = (ox - offx) & temp;
    oy = (-oy - offy) & temp;
    if (ox >= xwraplim) ox -= wrapsize;
    if (oy >= ywraplim) oy -= wrapsize;

    const int size_bits = (temp4 >> 8) & 0x0f;
    const int width = 1 << (size_bits & 3);
    const int height = 1 << ((size_bits >> 2) & 3);

    ox = ox - int(zoomx * float(width) * 8.f);
    oy = oy - int(zoomy * float(height) * 8.f);
    ox += 53 - dx_;
    oy -= 6 + dy_;

    // Keep original code; for multi-tile sprites low 6 bits encode sub-tile start
    const uint32_t code_raw = code;
    int xa = 0, ya = 0;
    if (code & 0x01) xa += 1;
    if (code & 0x02) ya += 1;
    if (code & 0x04) xa += 2;
    if (code & 0x08) ya += 2;
    if (code & 0x10) xa += 4;
    if (code & 0x20) ya += 4;
    const uint32_t code_base = code & ~uint32_t(0x3f);

    const uint16_t color_base = uint16_t(color << 4);
    static const int xoffset[8] = {0, 1, 4, 5, 16, 17, 20, 21};
    static const int yoffset[8] = {0, 2, 8, 10, 32, 34, 40, 42};

    // Sub-tile positions: use consecutive rounded positions so tiles abut (no 1px gaps).
    // Pascal: sx := ox + round(zoomx * x * 16);  zw := next_sx - sx
    for (int y = 0; y < height; y++) {
        const int sy0 = oy + int(std::lround(double(zoomy) * double(y) * 16.0));
        const int sy1 = oy + int(std::lround(double(zoomy) * double(y + 1) * 16.0));
        const int dh16 = sy1 - sy0;
        if (dh16 <= 0) continue;

        for (int x = 0; x < width; x++) {
            const int sx0 = ox + int(std::lround(double(zoomx) * double(x) * 16.0));
            const int sx1 = ox + int(std::lround(double(zoomx) * double(x + 1) * 16.0));
            const int dw16 = sx1 - sx0;
            if (dw16 <= 0) continue;

            // Pascal mirror/flip tile code selection
            uint32_t tile;
            bool fx = flipx, fy = flipy;
            if (width == 1 && height == 1) {
                tile = code_raw;
            } else {
                tile = code_base;
                if (flipx)
                    tile += uint32_t(xoffset[(width - 1 - x + xa) & 7]);
                else
                    tile += uint32_t(xoffset[(x + xa) & 7]);
                if (flipy)
                    tile += uint32_t(yoffset[(height - 1 - y + ya) & 7]);
                else
                    tile += uint32_t(yoffset[(y + ya) & 7]);
            }

            const uint8_t* pix = gfx_.element(int(tile & sprite_mask_));
            if (!pix) continue;

            for (int py = 0; py < dh16; py++) {
                const int src_y = fy ? (15 - (py * 16 / dh16)) : (py * 16 / dh16);
                for (int px = 0; px < dw16; px++) {
                    const int src_x = fx ? (15 - (px * 16 / dw16)) : (px * 16 / dw16);
                    const uint8_t pen = pix[size_t(src_y * 16 + src_x)];
                    if (!pen) continue;
                    const int dx = sx0 + px - crop_x;
                    const int dy = sy0 + py - crop_y;
                    if (dx < 0 || dy < 0 || dx >= dw || dy >= dh) continue;
                    dest[size_t(dy * dw + dx)] = uint16_t(color_base + pen);
                }
            }
        }
    }
}

}  // namespace dsp
