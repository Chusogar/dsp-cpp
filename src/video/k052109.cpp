#include "video/k052109.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace dsp {

K052109::K052109(Callback cb, std::vector<uint8_t> rom)
    : callback_(std::move(cb)), rom_(std::move(rom)) {
    char_mask_ = uint32_t(rom_.size() / 32) - 1;
    GfxLayout layout;
    layout.width = 8;
    layout.height = 8;
    layout.total = int(rom_.size() / 32);
    layout.planes = 4;
    layout.char_increment = 8 * 32;
    // Pascal: gfx_set_desc_data(4,0,8*32,24,16,8,0); pc_x=0..7; pc_y=0*32..7*32
    layout.plane_offsets = {24, 16, 8, 0};
    layout.x_offsets = {0, 1, 2, 3, 4, 5, 6, 7};
    layout.y_offsets = {0 * 32, 1 * 32, 2 * 32, 3 * 32, 4 * 32, 5 * 32, 6 * 32, 7 * 32};
    gfx_.decode(layout, rom_);

    for (auto& layer : layers_) {
        layer.assign(size_t(kLayerW) * kLayerH, 0);
    }
    clean_video_buffer();
}

void K052109::reset() {
    rmrd_ = false;
    irq_enabled_ = false;
    romsubbank_ = 0;
    scrollctrl_ = 0;
    tileflip_enable_ = 0;
    has_extra_video_ram_ = false;
    charrombank_.fill(0);
    charrombank2_.fill(0);
    ram_.fill(0);
    scroll_tipo_.fill(3);
    for (auto& a : scroll_x_) a.fill(0);
    for (auto& a : scroll_y_) a.fill(0);
    clean_video_buffer();
}

void K052109::clean_video_buffer() {
    for (auto& d : dirty_) d.fill(true);
    for (auto& layer : layers_) std::fill(layer.begin(), layer.end(), 0);
}

uint8_t K052109::read(uint16_t address) {
    address &= 0x3fff;
    if (rmrd_) {
        // ROM read mode
        uint32_t code = uint32_t(ram_[0x2000 + (address & 0x7ff)]) +
                        256u * uint32_t(ram_[0x4000 + (address & 0x7ff)]);
        uint16_t color = romsubbank_;
        uint16_t flags = 0, priority = 0;
        int bank = charrombank_[(color & 0x0c) >> 2] >> 2;
        bank |= charrombank2_[(color & 0x0c) >> 2] >> 2;
        if (has_extra_video_ram_) {
            code |= uint32_t(color) << 8;
        } else if (callback_) {
            callback_(0, bank, code, color, flags, priority);
        }
        uint32_t addr = (code << 5) + (address & 0x1f);
        addr &= uint32_t(rom_.size() - 1);
        return rom_[addr];
    }
    return ram_[address];
}

void K052109::write(uint16_t address, uint8_t value) {
    address &= 0x3fff;
    if ((address & 0x1fff) < 0x1800) {
        // tilemap RAM — do NOT auto-set has_extra_video_ram (X-Men only)
        if (ram_[address] != value) {
            ram_[address] = value;
            dirty_[(address & 0x1800) >> 11][address & 0x7ff] = true;
        }
        return;
    }

    ram_[address] = value;
    switch (address) {
        case 0x1c80:
            scrollctrl_ = value;
            break;
        case 0x1d00:
            irq_enabled_ = (value & 0x04) != 0;
            break;
        case 0x1d80:
            charrombank_[0] = value & 0x0f;
            charrombank_[1] = (value >> 4) & 0x0f;
            for (auto& d : dirty_) d.fill(true);
            break;
        case 0x1e00:
        case 0x3e00:
            romsubbank_ = value;
            break;
        case 0x1e80:
            // Pascal: tileflip_enable := (val and $06) shr 1
            if (tileflip_enable_ != ((value & 0x06) >> 1)) {
                tileflip_enable_ = uint8_t((value & 0x06) >> 1);
                clean_video_buffer();
            }
            break;
        case 0x1f00:
            charrombank_[2] = value & 0x0f;
            charrombank_[3] = (value >> 4) & 0x0f;
            for (auto& d : dirty_) d.fill(true);
            break;
        case 0x1f20:
        case 0x1f21:
        case 0x1f22:
        case 0x1f23:
            charrombank2_[address & 3] = value;
            break;
        default:
            break;
    }
}

void K052109::update_tile(int layer, int index) {
    const int pos_x = index % 64;
    const int pos_y = index / 64;
    uint32_t nchar =
        uint32_t(ram_[0x2000 + index + 0x800 * layer]) +
        256u * uint32_t(ram_[0x4000 + index + 0x800 * layer]);
    uint16_t color = ram_[index + 0x800 * layer];
    uint16_t flags = 0, priority = 0;
    int bank = charrombank_[(color & 0x0c) >> 2];
    if (has_extra_video_ram_) bank = (color & 0x0c) >> 2;
    color = uint16_t((color & 0xf3) | ((bank & 0x03) << 2));
    bank >>= 2;
    const bool attr_flip_y = (color & 0x02) != 0;
    if (callback_) callback_(layer, bank, nchar, color, flags, priority);

    bool flip_x = (tileflip_enable_ & 1) != 0 && (flags & 1) != 0;
    bool flip_y = (attr_flip_y && (tileflip_enable_ & 2) != 0) || (flags & 2) != 0;

    const uint8_t* pixels = gfx_.element(int(nchar & char_mask_));
    const uint16_t color_base = uint16_t(color << 4);
    for (int y = 0; y < 8; y++) {
        const int sy = flip_y ? 7 - y : y;
        for (int x = 0; x < 8; x++) {
            const int sx = flip_x ? 7 - x : x;
            const uint8_t pen = pixels ? pixels[size_t(sy * 8 + sx)] : 0;
            layers_[size_t(layer)][size_t((pos_y * 8 + y) * kLayerW + (pos_x * 8 + x))] =
                pen ? uint16_t(color_base + pen) : 0;
        }
    }
}

void K052109::calc_scroll_1() {
    if ((scrollctrl_ & 0x03) == 0x02) {
        scroll_y_[1][0] = ram_[0x180c];
        for (int offs = 0; offs < 0x20; offs++) {
            const int idx = 0x1a00 + 2 * (offs & 0xfff8);
            scroll_x_[1][size_t(offs)] =
                uint16_t(int(ram_[size_t(idx)] + 256 * ram_[size_t(idx + 1)]) - 6);
        }
        scroll_tipo_[1] = 0;
    } else if ((scrollctrl_ & 0x03) == 0x03) {
        scroll_y_[1][0] = ram_[0x180c];
        for (int offs = 0; offs < 0x100; offs++) {
            const int idx = 0x1a00 + 2 * offs;
            scroll_x_[1][size_t(offs)] =
                uint16_t(int(ram_[size_t(idx)] + 256 * ram_[size_t(idx + 1)]) - 6);
        }
        scroll_tipo_[1] = 1;
    } else if ((scrollctrl_ & 0x04) == 0x04) {
        scroll_x_[1][0] = uint16_t(int(ram_[0x1a00] + 256 * ram_[0x1a01]) - 6);
        for (int offs = 0; offs < 0x40; offs++) scroll_y_[1][size_t(offs)] = ram_[0x1800 + offs];
        scroll_tipo_[1] = 2;
    } else {
        scroll_x_[1][0] = uint16_t(int(ram_[0x1a00] + (uint16_t(ram_[0x1a01]) << 8)) - 6);
        scroll_y_[1][0] = ram_[0x180c];
        scroll_tipo_[1] = 3;
    }
}

void K052109::calc_scroll_2() {
    if ((scrollctrl_ & 0x18) == 0x10) {
        scroll_y_[2][0] = ram_[0x380c];
        for (int offs = 0; offs < 0x20; offs++) {
            const int idx = 0x3a00 + 2 * (offs & 0xfff8);
            scroll_x_[2][size_t(offs)] =
                uint16_t(int(ram_[size_t(idx)] + 256 * ram_[size_t(idx + 1)]) - 6);
        }
        scroll_tipo_[2] = 0;
    } else if ((scrollctrl_ & 0x18) == 0x18) {
        scroll_y_[2][0] = ram_[0x380c];
        for (int offs = 0; offs < 0x100; offs++) {
            const int idx = 0x3a00 + 2 * offs;
            scroll_x_[2][size_t(offs)] =
                uint16_t(int(ram_[size_t(idx)] + 256 * ram_[size_t(idx + 1)]) - 6);
        }
        scroll_tipo_[2] = 1;
    } else if ((scrollctrl_ & 0x20) == 0x20) {
        scroll_x_[2][0] = uint16_t(int(ram_[0x3a00] + 256 * ram_[0x3a01]) - 6);
        for (int offs = 0; offs < 0x40; offs++) scroll_y_[2][size_t(offs)] = ram_[0x3800 + offs];
        scroll_tipo_[2] = 2;
    } else {
        scroll_x_[2][0] = uint16_t(int(ram_[0x3a00] + (uint16_t(ram_[0x3a01]) << 8)) - 6);
        scroll_y_[2][0] = ram_[0x380c];
        scroll_tipo_[2] = 3;
    }
}

void K052109::draw_tiles() {
    calc_scroll_1();
    calc_scroll_2();
    for (int layer = 0; layer < 3; layer++) {
        for (int i = 0; i < 0x800; i++) {
            if (dirty_[size_t(layer)][size_t(i)]) {
                update_tile(layer, i);
                dirty_[size_t(layer)][size_t(i)] = false;
            }
        }
    }
}

void K052109::draw_layer(int layer, uint16_t* dest, int dest_w, int dest_h, int crop_x,
                         int crop_y) const {
    if (layer < 0 || layer > 2 || !dest) return;
    const auto& src = layers_[size_t(layer)];

    // Layer 0 is fixed (no scroll)
    if (layer == 0) {
        for (int y = 0; y < dest_h; y++) {
            const int sy = (y + crop_y) & (kLayerH - 1);
            for (int x = 0; x < dest_w; x++) {
                const int sx = (x + crop_x) & (kLayerW - 1);
                dest[size_t(y * dest_w + x)] = src[size_t(sy * kLayerW + sx)];
            }
        }
        return;
    }

    const uint8_t tipo = scroll_tipo_[size_t(layer)];
    if (tipo == 3) {
        // Global scroll
        const int sx0 = int(scroll_x_[size_t(layer)][0]);
        const int sy0 = int(scroll_y_[size_t(layer)][0]);
        for (int y = 0; y < dest_h; y++) {
            const int sy = (y + crop_y + sy0) & (kLayerH - 1);
            for (int x = 0; x < dest_w; x++) {
                const int sx = (x + crop_x + sx0) & (kLayerW - 1);
                dest[size_t(y * dest_w + x)] = src[size_t(sy * kLayerW + sx)];
            }
        }
    } else if (tipo == 0 || tipo == 1) {
        // Row scroll: block height 8 (tipo0) or 1 (tipo1)
        const int block = (tipo == 0) ? 8 : 1;
        const int sy0 = int(scroll_y_[size_t(layer)][0]);
        for (int y = 0; y < dest_h; y++) {
            const int src_y = (y + crop_y + sy0) & (kLayerH - 1);
            const int row = src_y / block;
            const int sx0 = int(scroll_x_[size_t(layer)][size_t(row & 0xff)]);
            for (int x = 0; x < dest_w; x++) {
                const int sx = (x + crop_x + sx0) & (kLayerW - 1);
                dest[size_t(y * dest_w + x)] = src[size_t(src_y * kLayerW + sx)];
            }
        }
    } else if (tipo == 2) {
        // Column scroll
        const int sx0 = int(scroll_x_[size_t(layer)][0]);
        for (int x = 0; x < dest_w; x++) {
            const int src_x = (x + crop_x + sx0) & (kLayerW - 1);
            const int col = src_x / 8;
            const int sy0 = int(scroll_y_[size_t(layer)][size_t(col & 0x3f)]);
            for (int y = 0; y < dest_h; y++) {
                const int sy = (y + crop_y + sy0) & (kLayerH - 1);
                dest[size_t(y * dest_w + x)] = src[size_t(sy * kLayerW + src_x)];
            }
        }
    }
}

}  // namespace dsp
