#include "video/k051316.h"

#include <algorithm>
#include <cstring>

namespace dsp {

K051316::K051316(Callback cb, std::vector<uint8_t> rom, Bpp bpp)
    : callback_(std::move(cb)), rom_(std::move(rom)), bpp_(bpp) {
    GfxLayout layout;
    layout.width = 16;
    layout.height = 16;
    if (bpp_ == Bpp::Bpp4) {
        layout.total = int(rom_.size() / 128);
        layout.planes = 4;
        layout.char_increment = 8 * 128;
        layout.plane_offsets = {0, 1, 2, 3};
        for (int i = 0; i < 16; i++) layout.x_offsets.push_back(i * 4);
        for (int i = 0; i < 16; i++) layout.y_offsets.push_back(i * 64);
        color_shift_ = 4;
        pixels_per_byte_ = 2;
    } else {
        // BPP7: 7 bitplanes, 256 bytes/tile
        layout.total = int(rom_.size() / 256);
        layout.planes = 7;
        layout.char_increment = 8 * 256;
        layout.plane_offsets = {1, 2, 3, 4, 5, 6, 7};
        for (int i = 0; i < 16; i++) layout.x_offsets.push_back(i * 8);
        for (int i = 0; i < 16; i++) layout.y_offsets.push_back(i * 128);
        color_shift_ = 7;
        pixels_per_byte_ = 1;
    }
    tile_mask_ = layout.total > 0 ? uint32_t(layout.total - 1) : 0;
    if (!rom_.empty() && layout.total > 0) gfx_.decode(layout, rom_);
    layer_.assign(size_t(kLayerW) * kLayerH, 0);
    dirty_.fill(true);
    layer_dirty_ = true;
}

void K051316::reset() {
    control_.fill(0);
    ram_.fill(0);
    dirty_.fill(true);
    layer_dirty_ = true;
    std::fill(layer_.begin(), layer_.end(), 0);
}

void K051316::clean_video_buffer() {
    dirty_.fill(true);
    layer_dirty_ = true;
}

uint8_t K051316::read(uint16_t address) { return ram_[address & 0x7ff]; }

void K051316::write(uint16_t address, uint8_t value) {
    address &= 0x7ff;
    if (ram_[address] != value) {
        ram_[address] = value;
        dirty_[address & 0x3ff] = true;
        layer_dirty_ = true;
    }
}

void K051316::control_w(uint8_t offset, uint8_t value) {
    control_[offset & 0x0f] = value;
}

uint8_t K051316::rom_read(uint16_t address) const {
    if ((control_[0x0e] & 0x01) != 0) return 0;
    uint32_t addr = uint32_t(address) + (uint32_t(control_[0x0c]) << 11) +
                    (uint32_t(control_[0x0d]) << 19);
    addr = (addr / uint32_t(pixels_per_byte_)) & uint32_t(rom_.size() - 1);
    return rom_[addr];
}

void K051316::rebuild_layer() {
    for (int f = 0; f < 0x400; f++) {
        if (!dirty_[size_t(f)]) continue;
        dirty_[size_t(f)] = false;
        const int tx = f % kMapW;
        const int ty = f / kMapW;
        uint16_t nchar = ram_[size_t(f)];
        uint16_t color = ram_[size_t(f + 0x400)];
        uint16_t pri = 0;
        if (callback_) callback_(nchar, color, pri);
        const uint8_t* pixels = gfx_.element(int(nchar & tile_mask_));
        const uint16_t color_base = uint16_t(color << color_shift_);
        for (int y = 0; y < kTile; y++) {
            for (int x = 0; x < kTile; x++) {
                const uint8_t pen = pixels ? pixels[size_t(y * kTile + x)] : 0;
                layer_[size_t((ty * kTile + y) * kLayerW + (tx * kTile + x))] =
                    pen ? uint16_t(color_base + pen) : 0;
            }
        }
    }
    layer_dirty_ = false;
}

void K051316::draw(uint16_t* dest, int dest_w, int dest_h, int crop_x, int crop_y) {
    if (!dest) return;
    if (layer_dirty_) rebuild_layer();

    // Affine parameters (16.16 fixed point), matching k051316.pas
    int32_t startx = int32_t(int16_t((uint16_t(control_[0]) << 8) | control_[1])) << 8;
    int32_t starty = int32_t(int16_t((uint16_t(control_[6]) << 8) | control_[7])) << 8;
    const int32_t incxx = int32_t(int16_t((uint16_t(control_[0x2]) << 8) | control_[0x3]));
    const int32_t incyx = int32_t(int16_t((uint16_t(control_[0x4]) << 8) | control_[0x5]));
    const int32_t incxy = int32_t(int16_t((uint16_t(control_[0x8]) << 8) | control_[0x9]));
    const int32_t incyy = int32_t(int16_t((uint16_t(control_[0xa]) << 8) | control_[0xb]));

    // Pre-offset like Pascal (centres the transform)
    startx -= 16 * incyx;
    starty -= 16 * incyy;
    startx -= 89 * incxx;
    starty -= 89 * incxy;

    // True per-pixel affine sample over the visible window.
    // Screen pixel (sx,sy) maps to source: start + sx*incx + sy*incy
    for (int sy = 0; sy < dest_h; sy++) {
        int32_t cx = startx + (crop_y + sy) * incxy;
        int32_t cy = starty + (crop_y + sy) * incyy;
        // advance by crop_x
        cx += crop_x * incxx;
        cy += crop_x * incyx;
        for (int sx = 0; sx < dest_w; sx++) {
            const int src_x = (cx >> 16) & (kLayerW - 1);
            const int src_y = (cy >> 16) & (kLayerH - 1);
            const uint16_t pen = layer_[size_t(src_y * kLayerW + src_x)];
            if (pen) dest[size_t(sy * dest_w + sx)] = pen;
            cx += incxx;
            cy += incyx;
        }
    }
}

}  // namespace dsp
