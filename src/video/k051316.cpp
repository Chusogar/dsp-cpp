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
    wrap_ = false;
    flipx_enabled_ = false;
    flipy_enabled_ = false;
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
    offset &= 0x0f;
    if (offset == 0x0e) {
        // MAME: bit0 ROM readout (active low), bit1 tile X flip enable,
        // bit2 tile Y flip enable when colour bits 6/7 are set.
        const bool flipx = (value & 0x02) != 0;
        const bool flipy = (value & 0x04) != 0;
        if (flipx != flipx_enabled_ || flipy != flipy_enabled_) {
            flipx_enabled_ = flipx;
            flipy_enabled_ = flipy;
            dirty_.fill(true);
            layer_dirty_ = true;
        }
    }
    control_[offset] = value;
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
        const uint8_t attr = ram_[size_t(f + 0x400)];
        uint16_t color = attr;
        uint16_t pri = 0;
        if (callback_) callback_(nchar, color, pri);
        const uint8_t* pixels = gfx_.element(int(nchar & tile_mask_));
        const uint16_t color_base = uint16_t(color << color_shift_);
        const bool flipx = flipx_enabled_ && (attr & 0x40) != 0;
        const bool flipy = flipy_enabled_ && (attr & 0x80) != 0;

        for (int y = 0; y < kTile; y++) {
            const int py = flipy ? (kTile - 1 - y) : y;
            for (int x = 0; x < kTile; x++) {
                const int px = flipx ? (kTile - 1 - x) : x;
                const uint8_t pen = pixels ? pixels[size_t(py * kTile + px)] : 0;
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

    // MAME k051316_device::zoom_draw — startx/starty are u32 so the later
    // <<5 and clip pre-advance wrap the same way as tilemap_t::draw_roz.
    uint32_t startx = uint32_t(256 * int(int16_t((uint16_t(control_[0]) << 8) | control_[1])));
    uint32_t starty = uint32_t(256 * int(int16_t((uint16_t(control_[6]) << 8) | control_[7])));
    int32_t incxx = int32_t(int16_t((uint16_t(control_[0x2]) << 8) | control_[0x3]));
    int32_t incyx = int32_t(int16_t((uint16_t(control_[0x4]) << 8) | control_[0x5]));
    int32_t incxy = int32_t(int16_t((uint16_t(control_[0x8]) << 8) | control_[0x9]));
    int32_t incyy = int32_t(int16_t((uint16_t(control_[0xa]) << 8) | control_[0xb]));

    startx -= uint32_t((16 + dy_) * incyx);
    starty -= uint32_t((16 + dy_) * incyy);
    startx -= uint32_t((89 + dx_) * incxx);
    starty -= uint32_t((89 + dx_) * incxy);

    startx <<= 5;
    starty <<= 5;
    incxx <<= 5;
    incyx <<= 5;
    incxy <<= 5;
    incyy <<= 5;

    // draw_roz_core: pre-advance by cliprect, then per pixel
    //   srcx += screenx*incxx + screeny*incyx
    //   srcy += screenx*incxy + screeny*incyy
    // In-range test is unsigned: cx < (layer_w << 16).
    constexpr uint32_t kWidthShifted = uint32_t(kLayerW) << 16;
    constexpr uint32_t kHeightShifted = uint32_t(kLayerH) << 16;
    startx += uint32_t(int32_t(crop_x) * incxx + int32_t(crop_y) * incyx);
    starty += uint32_t(int32_t(crop_x) * incxy + int32_t(crop_y) * incyy);

    for (int sy = 0; sy < dest_h; sy++) {
        uint32_t cx = startx;
        uint32_t cy = starty;
        for (int sx = 0; sx < dest_w; sx++) {
            if (wrap_) {
                const int src_x = int(cx >> 16) & (kLayerW - 1);
                const int src_y = int(cy >> 16) & (kLayerH - 1);
                const uint16_t pen = layer_[size_t(src_y * kLayerW + src_x)];
                if (pen) dest[size_t(sy * dest_w + sx)] = pen;
            } else if (cx < kWidthShifted && cy < kHeightShifted) {
                const uint16_t pen = layer_[size_t((cy >> 16) * uint32_t(kLayerW) + (cx >> 16))];
                if (pen) dest[size_t(sy * dest_w + sx)] = pen;
            }
            cx += uint32_t(incxx);
            cy += uint32_t(incxy);
        }
        startx += uint32_t(incyx);
        starty += uint32_t(incyy);
    }
}


}  // namespace dsp
