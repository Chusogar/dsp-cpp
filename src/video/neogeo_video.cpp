#include "video/neogeo_video.h"

#include <algorithm>
#include <cstring>

namespace dsp {
namespace {

constexpr int kFixWidth = 8;
constexpr int kFixHeight = 8;
constexpr int kSprWidth = 16;
constexpr int kSprHeight = 16;

void decode_fix(const uint8_t* src, size_t size, std::vector<uint8_t>& dest, int* tiles_out) {
    const int tiles = int(size / 32);
    dest.assign(size_t(tiles) * kFixWidth * kFixHeight, 0);
    for (int t = 0; t < tiles; t++) {
        const uint8_t* tile = src + size_t(t) * 32;
        uint8_t* out = dest.data() + size_t(t) * kFixWidth * kFixHeight;
        for (int y = 0; y < 8; y++) {
            const uint8_t p0 = tile[16 + y];
            const uint8_t p1 = tile[24 + y];
            const uint8_t p2 = tile[0 + y];
            const uint8_t p3 = tile[8 + y];
            for (int x = 0; x < 8; x++) {
                const uint8_t bit = uint8_t(0x80 >> x);
                uint8_t pixel = 0;
                if (p0 & bit) pixel |= 1;
                if (p1 & bit) pixel |= 2;
                if (p2 & bit) pixel |= 4;
                if (p3 & bit) pixel |= 8;
                out[y * 8 + x] = pixel;
            }
        }
    }
    if (tiles_out) *tiles_out = tiles;
}

void decode_sprites(const uint8_t* src, size_t size, std::vector<uint8_t>& dest, int* tiles_out) {
    const int tiles = int(size / 128);
    dest.assign(size_t(tiles) * kSprWidth * kSprHeight, 0);
    for (int t = 0; t < tiles; t++) {
        const uint8_t* tile = src + size_t(t) * 128;
        uint8_t* out = dest.data() + size_t(t) * kSprWidth * kSprHeight;
        for (int y = 0; y < 16; y++) {
            const uint8_t* row01 = tile + y * 4;
            const uint8_t* row23 = tile + 0x40 + y * 4;
            for (int x = 0; x < 8; x++) {
                const uint8_t bit_l = uint8_t(0x80 >> x);
                const uint8_t bit_r = uint8_t(0x80 >> x);
                uint8_t left = 0;
                uint8_t right = 0;
                if (row01[2] & bit_l) left |= 1;
                if (row01[3] & bit_l) left |= 2;
                if (row23[2] & bit_l) left |= 4;
                if (row23[3] & bit_l) left |= 8;
                if (row01[0] & bit_r) right |= 1;
                if (row01[1] & bit_r) right |= 2;
                if (row23[0] & bit_r) right |= 4;
                if (row23[1] & bit_r) right |= 8;
                out[y * 16 + x] = left;
                out[y * 16 + 8 + x] = right;
            }
        }
    }
    if (tiles_out) *tiles_out = tiles;
}

}  // namespace

NeoGeoVideo::NeoGeoVideo() { build_zoom_table(); }

void NeoGeoVideo::reset() {
    vram_.fill(0);
    palette_[0].fill(0);
    palette_[1].fill(0);
    palette_rgb_[0].fill(0xff000000u);
    palette_rgb_[1].fill(0xff000000u);
    vram_addr_ = 0;
    vram_mod_ = 1;
    lspc_mode_ = 0;
    timer_reload_ = 0;
    timer_counter_ = 0;
    timer_enable_ = false;
    timer_mode_ = 0;
    irq_vblank_ = false;
    irq_timer_ = false;
    irq_reset_ = true;
    palette_bank_ = 0;
    use_bios_fix_ = true;
    auto_anim_ = 0;
    auto_anim_speed_ = 0;
    auto_anim_frame_ = 0;
    scanline_ = 0;
}

uint32_t NeoGeoVideo::colour(uint16_t packed) {
    const int dark = (packed >> 15) & 1;
    int r = (packed >> 10) & 0x1f;
    int g = (packed >> 5) & 0x1f;
    int b = packed & 0x1f;
    r = (r << 3) | (r >> 2);
    g = (g << 3) | (g >> 2);
    b = (b << 3) | (b >> 2);
    if (dark) {
        r = (r * 5) / 8;
        g = (g * 5) / 8;
        b = (b * 5) / 8;
    }
    return 0xff000000u | (uint32_t(r) << 16) | (uint32_t(g) << 8) | uint32_t(b);
}

void NeoGeoVideo::rebuild_palette_entry(int bank, int index) {
    palette_rgb_[size_t(bank)][size_t(index) & 0xfff] =
        colour(palette_[size_t(bank)][size_t(index) & 0xfff]);
}

void NeoGeoVideo::set_fix_roms(const uint8_t* cart, size_t cart_size, const uint8_t* bios,
                               size_t bios_size) {
    cart_fix_.assign(cart, cart + cart_size);
    bios_fix_.assign(bios, bios + bios_size);
}

void NeoGeoVideo::set_sprite_rom(const uint8_t* data, size_t size) {
    sprite_rom_.assign(data, data + size);
}

void NeoGeoVideo::set_lo_rom(const uint8_t* data, size_t size) {
    lo_rom_.assign(data, data + size);
    build_zoom_table();
}

void NeoGeoVideo::decode_graphics() {
    decode_fix(cart_fix_.data(), cart_fix_.size(), fix_pixels_, &cart_fix_tiles_);
    decode_fix(bios_fix_.data(), bios_fix_.size(), bios_fix_pixels_, &bios_fix_tiles_);
    decode_sprites(sprite_rom_.data(), sprite_rom_.size(), sprite_pixels_, &sprite_tiles_);
}

void NeoGeoVideo::build_zoom_table() {
    for (int z = 0; z < 256; z++) {
        ZoomRow& row = zoom_[size_t(z)];
        row.draw.fill(0);
        int pixels = ((z + 1) * 16) >> 8;
        if (z == 0xff) pixels = 16;
        row.pixels = pixels;
        if (pixels <= 0) continue;
        int acc = 0;
        int drawn = 0;
        for (int i = 0; i < 16 && drawn < pixels; i++) {
            acc += pixels;
            if (acc >= 16) {
                acc -= 16;
                row.draw[size_t(i)] = 1;
                drawn++;
            }
        }
        if (drawn == 0 && z > 0) {
            row.draw[0] = 1;
            row.pixels = 1;
        }
    }
    if (lo_rom_.size() >= 0x10000) {
        for (int z = 0; z < 256; z++) {
            ZoomRow& row = zoom_[size_t(z)];
            row.draw.fill(0);
            int pixels = 0;
            for (int i = 0; i < 16; i++) {
                const uint8_t v = lo_rom_[size_t(z) * 256 + size_t(i)];
                if (v != 0) {
                    row.draw[size_t(i)] = 1;
                    pixels++;
                }
            }
            row.pixels = pixels;
        }
    }
}

void NeoGeoVideo::ack_irq(uint8_t mask) {
    if (mask & 0x04) irq_vblank_ = false;
    if (mask & 0x02) irq_timer_ = false;
    if (mask & 0x01) irq_reset_ = false;
}

uint16_t NeoGeoVideo::read_vram() const { return vram_[vram_addr_ & 0xffff]; }

void NeoGeoVideo::write_vram(uint16_t value) {
    vram_[vram_addr_ & 0xffff] = value;
    vram_addr_ = uint16_t(vram_addr_ + vram_mod_);
}

uint16_t NeoGeoVideo::read_register(uint32_t address) const {
    switch (address & 0x0e) {
        case 0x00:
        case 0x08:
            return read_vram();
        case 0x02:
        case 0x0a:
            return read_vram();
        case 0x04:
        case 0x0c:
            return vram_mod_;
        case 0x06:
        case 0x0e: {
            // Bits 15-7: raster counter (line + 0xF8), bits 2-0: auto-anim.
            const int raster = ((scanline_ + 0xf8) & 0x1ff) << 7;
            return uint16_t(raster | (auto_anim_ & 7));
        }
        default:
            return 0;
    }
}

void NeoGeoVideo::write_register(uint32_t address, uint16_t value) {
    switch (address & 0x0e) {
        case 0x00:
            vram_addr_ = value;
            break;
        case 0x02:
            write_vram(value);
            break;
        case 0x04:
            vram_mod_ = value;
            break;
        case 0x06:
            lspc_mode_ = value;
            auto_anim_speed_ = (value >> 8) & 0xff;
            timer_mode_ = (value >> 4) & 7;
            timer_enable_ = (value & 0x10) != 0;
            break;
        case 0x08:
            timer_reload_ = (timer_reload_ & 0xffff) | (uint32_t(value) << 16);
            break;
        case 0x0a:
            timer_reload_ = (timer_reload_ & 0xffff0000u) | value;
            if ((timer_mode_ & 0x01) == 0) timer_counter_ = timer_reload_;
            break;
        case 0x0c:
            ack_irq(uint8_t(value));
            break;
        default:
            break;
    }
}

uint16_t NeoGeoVideo::read_palette(uint32_t address) const {
    return palette_[size_t(palette_bank_)][(address >> 1) & 0xfff];
}

void NeoGeoVideo::write_palette(uint32_t address, uint16_t value) {
    const int index = int((address >> 1) & 0xfff);
    palette_[size_t(palette_bank_)][size_t(index)] = value;
    rebuild_palette_entry(palette_bank_, index);
}

void NeoGeoVideo::begin_frame() {
    scanline_ = 0;
    if ((lspc_mode_ & 0x08) == 0) {
        auto_anim_frame_++;
        const int period = std::max(1, auto_anim_speed_ + 1);
        if (auto_anim_frame_ >= period) {
            auto_anim_frame_ = 0;
            auto_anim_ = (auto_anim_ + 1) & 7;
        }
    }
    if (timer_enable_ && (timer_mode_ & 0x02) != 0) timer_counter_ = timer_reload_;
}

void NeoGeoVideo::tick_timer() {
    if (!timer_enable_) return;
    const uint32_t pixels = 384;
    if (timer_counter_ <= pixels) {
        irq_timer_ = true;
        timer_counter_ = timer_reload_;
    } else {
        timer_counter_ -= pixels;
    }
}

void NeoGeoVideo::end_scanline(int line) {
    scanline_ = line;
    if (timer_enable_ && (timer_mode_ & 0x01) != 0) timer_counter_ = timer_reload_;
    tick_timer();
    if (line == kVblankLine) irq_vblank_ = true;
}

const uint8_t* NeoGeoVideo::fix_tile(int code) const {
    if (use_bios_fix_ && bios_fix_tiles_ > 0) {
        return bios_fix_pixels_.data() + size_t(code % bios_fix_tiles_) * 64;
    }
    if (cart_fix_tiles_ <= 0) return nullptr;
    return fix_pixels_.data() + size_t(code % cart_fix_tiles_) * 64;
}

const uint8_t* NeoGeoVideo::sprite_tile(int code) const {
    if (sprite_tiles_ <= 0) return nullptr;
    return sprite_pixels_.data() + size_t(code % sprite_tiles_) * 256;
}

void NeoGeoVideo::draw_fix(uint32_t* framebuffer) {
    const auto& pal = palette_rgb_[size_t(palette_bank_)];
    for (int col = 0; col < 40; col++) {
        for (int row = 0; row < 32; row++) {
            const uint16_t word = vram_[0x7000 + col * 32 + row];
            int code = word & 0xfff;
            const int pal_index = (word >> 12) & 0x0f;
            if ((word & 0x4000) != 0) {
                // 3-frame / 4-frame auto animation lives in bits of the tile number.
            }
            if ((lspc_mode_ & 0x08) == 0) {
                if (word & 0x0800) {
                    code = (code & ~7) | ((code + auto_anim_) & 7);
                } else if (word & 0x0400) {
                    code = (code & ~3) | ((code + auto_anim_) & 3);
                }
            }
            const uint8_t* tile = fix_tile(code);
            if (tile == nullptr) continue;
            const int dest_x = col * 8;
            const int dest_y = row * 8;
            for (int y = 0; y < 8; y++) {
                const int py = dest_y + y;
                if (py < 0 || py >= kScreenHeight) continue;
                uint32_t* line = framebuffer + py * kScreenWidth + dest_x;
                const uint8_t* src = tile + y * 8;
                for (int x = 0; x < 8; x++) {
                    const uint8_t pen = src[x];
                    if (pen == 0) continue;
                    line[x] = pal[size_t((pal_index << 4) | pen)];
                }
            }
        }
    }
}

void NeoGeoVideo::draw_sprite_line(uint32_t* framebuffer, int /*sprite*/, int /*tile*/, int dest_x,
                                   int dest_y, int y_in_tile, int xzoom, bool flip_x, bool flip_y,
                                   int palette, int code) {
    const uint8_t* tile = sprite_tile(code);
    if (tile == nullptr) return;
    int src_y = flip_y ? (15 - y_in_tile) : y_in_tile;
    if (src_y < 0 || src_y > 15) return;
    if (dest_y < 0 || dest_y >= kScreenHeight) return;
    const auto& pal = palette_rgb_[size_t(palette_bank_)];
    const ZoomRow& zoom = zoom_[size_t(xzoom & 0xff)];
    uint32_t* line = framebuffer + dest_y * kScreenWidth;
    int x = dest_x;
    for (int px = 0; px < 16; px++) {
        if (!zoom.draw[size_t(px)]) continue;
        const int src_x = flip_x ? (15 - px) : px;
        const uint8_t pen = tile[src_y * 16 + src_x];
        if (pen != 0 && x >= 0 && x < kScreenWidth) {
            line[x] = pal[size_t(((palette & 0xff) << 4) | pen)];
        }
        x++;
    }
}

void NeoGeoVideo::draw_sprites(uint32_t* framebuffer) {
    int chain_x = 0;
    int chain_xzoom = 0xff;
    for (int sprite = 1; sprite < kSpriteCount; sprite++) {
        const uint16_t scb3 = vram_[0x8200 + sprite];
        const int size = scb3 & 0x3f;
        const bool sticky = (scb3 & 0x40) != 0;
        const uint16_t scb2 = vram_[0x8000 + sprite];
        const int yzoom = scb2 & 0xff;
        int xzoom = (scb2 >> 8) & 0xff;
        int sx;
        if (sticky) {
            sx = chain_x;
            xzoom = chain_xzoom;
        } else {
            sx = (vram_[0x8400 + sprite] >> 7) & 0x1ff;
            if (sx > 0x1f0) sx -= 0x200;
            chain_x = sx;
            chain_xzoom = xzoom;
        }
        if (size == 0) continue;

        int sy = 0x200 - ((scb3 >> 7) & 0x1ff);
        const int lines_per_tile = std::max(1, zoom_[size_t(yzoom)].pixels);
        const uint16_t* scb1 = &vram_[sprite * 64];

        int dest_y = sy;
        for (int t = 0; t < size; t++) {
            const uint16_t tile_lo = scb1[t * 2];
            const uint16_t attr = scb1[t * 2 + 1];
            int code = int(tile_lo) | (int(attr & 0x00f0) << 12);
            if ((lspc_mode_ & 0x08) == 0) {
                if (attr & 0x0008) {
                    // y flip shares this bit on some docs; auto-anim is bits 6-5.
                }
                if (attr & 0x0040) {
                    code = (code & ~7) | ((code + auto_anim_) & 7);
                } else if (attr & 0x0020) {
                    code = (code & ~3) | ((code + auto_anim_) & 3);
                }
            }
            const int palette = (attr >> 8) & 0xff;
            const bool flip_x = (attr & 0x0001) != 0;
            const bool flip_y = (attr & 0x0002) != 0;

            for (int row = 0; row < 16; row++) {
                if (!zoom_[size_t(yzoom)].draw[size_t(row)]) continue;
                draw_sprite_line(framebuffer, sprite, t, sx, dest_y, row, xzoom, flip_x, flip_y,
                                 palette, code);
                dest_y++;
                if (dest_y >= kScreenHeight && sy < 0x180) break;
            }
            (void)lines_per_tile;
        }
        chain_x += zoom_[size_t(xzoom)].pixels;
    }
}

void NeoGeoVideo::render_frame(uint32_t* framebuffer) {
    const uint32_t backdrop = palette_rgb_[size_t(palette_bank_)][0xfff];
    std::fill(framebuffer, framebuffer + kScreenWidth * kScreenHeight, backdrop);
    draw_sprites(framebuffer);
    draw_fix(framebuffer);
}

}  // namespace dsp
