#include "video/neogeo_video.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace dsp {
namespace {

constexpr int kFixWidth = 8;
constexpr int kFixHeight = 8;
constexpr int kSprWidth = 16;
constexpr int kSprHeight = 16;

void decode_fix(const uint8_t* src, size_t size, std::vector<uint8_t>& dest, int* tiles_out) {
    // NeoGeoDev Fix graphics format (8x8, 4bpp, 32 bytes/tile):
    //   ROM bit layout within tile: ... n H C LLL
    //     LLL = line 0..7, C = column in half, H = half (0=right, 1=left)
    //   Byte offsets for screen columns L→R on line y:
    //     [16+y], [24+y], [0+y], [8+y]  (left half then right half)
    //   Each byte: bits 0-3 = LEFT pixel, bits 4-7 = RIGHT pixel (no nibble swap).
    const int tiles = int(size / 32);
    dest.assign(size_t(tiles) * kFixWidth * kFixHeight, 0);
    static constexpr int kColBase[4] = {0x10, 0x18, 0x00, 0x08};
    for (int t = 0; t < tiles; t++) {
        const uint8_t* tile = src + size_t(t) * 32;
        uint8_t* out = dest.data() + size_t(t) * kFixWidth * kFixHeight;
        for (int y = 0; y < 8; y++) {
            for (int c = 0; c < 4; c++) {
                const uint8_t data = tile[kColBase[c] + y];
                out[y * 8 + c * 2 + 0] = uint8_t(data & 0x0f);        // left
                out[y * 8 + c * 2 + 1] = uint8_t((data >> 4) & 0x0f);  // right
            }
        }
    }
    if (tiles_out) *tiles_out = tiles;
}

void decode_sprites(const uint8_t* src, size_t size, std::vector<uint8_t>& dest, int* tiles_out) {
    // MAME neosprite_optimized_device::optimize_helper: left 8px from +0x40,
    // right 8px from +0, planes 0/1/2/3 at +0/+2/+1/+3 of each 4-byte row.
    const int tiles = int(size / 128);
    dest.assign(size_t(tiles) * kSprWidth * kSprHeight, 0);
    for (int t = 0; t < tiles; t++) {
        const uint8_t* tile = src + size_t(t) * 128;
        uint8_t* out = dest.data() + size_t(t) * kSprWidth * kSprHeight;
        for (int y = 0; y < 16; y++) {
            const int row = y << 2;
            for (int x = 0; x < 8; x++) {
                uint8_t left = 0;
                left |= uint8_t(((tile[0x40 + row + 0] >> x) & 1) << 0);
                left |= uint8_t(((tile[0x40 + row + 2] >> x) & 1) << 1);
                left |= uint8_t(((tile[0x40 + row + 1] >> x) & 1) << 2);
                left |= uint8_t(((tile[0x40 + row + 3] >> x) & 1) << 3);
                uint8_t right = 0;
                right |= uint8_t(((tile[row + 0] >> x) & 1) << 0);
                right |= uint8_t(((tile[row + 2] >> x) & 1) << 1);
                right |= uint8_t(((tile[row + 1] >> x) & 1) << 2);
                right |= uint8_t(((tile[row + 3] >> x) & 1) << 3);
                out[y * 16 + x] = left;
                out[y * 16 + 8 + x] = right;
            }
        }
    }
    if (tiles_out) *tiles_out = tiles;
}

// Horizontal shrink bitmasks verified on hardware (MAME neogeo_spr.cpp).
constexpr uint16_t kZoomXTables[16] = {
    0x0080, 0x0880, 0x0888, 0x2888, 0x288a, 0x2a8a, 0x2aaa, 0xaaaa,
    0xaaea, 0xbaea, 0xbaeb, 0xbbeb, 0xbbef, 0xfbef, 0xfbff, 0xffff,
};

}  // namespace

NeoGeoVideo::NeoGeoVideo() = default;

void NeoGeoVideo::reset() {
    vram_.fill(0);
    palette_[0].fill(0);
    palette_[1].fill(0);
    palette_rgb_[0].fill(0xff000000u);
    palette_rgb_[1].fill(0xff000000u);
    vram_addr_ = 0;
    vram_mod_ = 1;
    vram_read_buffer_ = 0;
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
    // Dark bit in 15, then R0 G0 B0 in 14-12, then R4-1 G4-1 B4-1 in 11-0.
    int r = ((packed >> 7) & 0x1e) | ((packed >> 14) & 1);
    int g = ((packed >> 3) & 0x1e) | ((packed >> 13) & 1);
    int b = ((packed << 1) & 0x1e) | ((packed >> 12) & 1);
    r = (r << 3) | (r >> 2);
    g = (g << 3) | (g >> 2);
    b = (b << 3) | (b >> 2);
    if (packed & 0x8000) {
        r = (r * 192) / 256;
        g = (g * 192) / 256;
        b = (b * 192) / 256;
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
}

void NeoGeoVideo::decode_graphics() {
    decode_fix(cart_fix_.data(), cart_fix_.size(), fix_pixels_, &cart_fix_tiles_);
    decode_fix(bios_fix_.data(), bios_fix_.size(), bios_fix_pixels_, &bios_fix_tiles_);
    decode_sprites(sprite_rom_.data(), sprite_rom_.size(), sprite_pixels_, &sprite_tiles_);
}

void NeoGeoVideo::ack_irq(uint8_t mask) {
    // REG_IRQACK: bit 0 = IRQ3 (reset), bit 1 = IRQ2 (timer), bit 2 = IRQ1 (vblank).
    if (mask & 0x04) irq_vblank_ = false;
    if (mask & 0x02) irq_timer_ = false;
    if (mask & 0x01) irq_reset_ = false;
}

uint16_t NeoGeoVideo::vram_offset() const {
    // Bit 15 selects the 2 KiB sprite-control window at $8000; increment never
    // carries into that bit.
    return (vram_addr_ & 0x8000) ? uint16_t(vram_addr_ & 0x87ff) : uint16_t(vram_addr_ & 0x7fff);
}

void NeoGeoVideo::step_vram_address() {
    vram_addr_ = uint16_t((vram_addr_ & 0x8000) | ((vram_addr_ + vram_mod_) & 0x7fff));
    vram_read_buffer_ = vram_[vram_offset()];
}

uint16_t NeoGeoVideo::read_vram() const { return vram_read_buffer_; }

void NeoGeoVideo::write_vram(uint16_t value) {
    vram_[vram_offset()] = value;
    step_vram_address();
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
            vram_read_buffer_ = vram_[vram_offset()];
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
            // Fix auto-animation disabled: bits 10/11 of the tile number are part of
            // the index for static text; mis-applying REPLACE turns glyphs into boxes.
            // (Re-enable with care once S-ROM banking is verified.)
            (void)auto_anim_;
            const uint8_t* tile = fix_tile(code);
            if (tile == nullptr) continue;
            const int dest_x = col * 8;
            // Visible area is internal lines 16..239 (224 lines).
            const int dest_y = row * 8 - kVisibleTop;
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

bool NeoGeoVideo::sprite_on_scanline(int scanline, int y, int rows) {
    return rows == 0 || rows >= 0x20 || ((scanline - y) & 0x1ff) < (rows * 0x10);
}

void NeoGeoVideo::draw_sprite_pixels(uint32_t* framebuffer, int dest_y, int x, int src_y, int xzoom,
                                     bool flip_x, int palette, int code) {
    const uint8_t* tile = sprite_tile(code);
    if (tile == nullptr) return;
    if (src_y < 0 || src_y > 15) return;
    if (dest_y < 0 || dest_y >= kScreenHeight) return;
    const auto& pal = palette_rgb_[size_t(palette_bank_)];
    uint32_t* line = framebuffer + dest_y * kScreenWidth;
    uint16_t zoom_bits = kZoomXTables[xzoom & 0x0f];
    int src_x = flip_x ? 15 : 0;
    const int src_inc = flip_x ? -1 : 1;
    if (x <= 0x1f0) {
        int dest_x = x;
        for (int i = 0; i < 16; i++) {
            if (zoom_bits & 0x8000) {
                if (dest_x >= 0 && dest_x < kScreenWidth) {
                    const uint8_t pen = tile[src_y * 16 + src_x];
                    if (pen != 0) {
                        line[dest_x] = pal[size_t(((palette & 0xff) << 4) | pen)];
                    }
                }
                dest_x++;
            }
            zoom_bits = uint16_t(zoom_bits << 1);
            if (zoom_bits == 0) break;
            src_x += src_inc;
        }
        return;
    }
    // x in 0x1f1..0x1ff wraps onto the left edge of the visarea.
    int wrapped = x;
    int dest_x = 0;
    for (int i = 0; i < 16; i++) {
        if (zoom_bits & 0x8000) {
            if (wrapped >= 0x200 && dest_x < kScreenWidth) {
                const uint8_t pen = tile[src_y * 16 + src_x];
                if (pen != 0) {
                    line[dest_x] = pal[size_t(((palette & 0xff) << 4) | pen)];
                }
                dest_x++;
            }
            wrapped++;
        }
        zoom_bits = uint16_t(zoom_bits << 1);
        if (zoom_bits == 0) break;
        src_x += src_inc;
    }
}

void NeoGeoVideo::draw_sprites(uint32_t* framebuffer) {
    // MAME neosprite_base_device::parse_sprites + draw_sprites, but evaluated
    // per visible scanline instead of through the $8600/$8680 line lists.
    for (int fy = 0; fy < kScreenHeight; fy++) {
        const int scanline = fy + kVisibleTop;
        int y = 0;
        int x = 0;
        int rows = 0;
        int zoom_y = 0;
        int zoom_x = 0;
        int drawn = 0;
        for (int sprite = 0; sprite < kSpriteCount; sprite++) {
            const uint16_t y_control = vram_[0x8200 + sprite];
            const uint16_t zoom_control = vram_[0x8000 + sprite];
            if (y_control & 0x40) {
                x = (x + zoom_x + 1) & 0x1ff;
                zoom_x = (zoom_control >> 8) & 0x0f;
            } else {
                y = 0x200 - (y_control >> 7);
                x = vram_[0x8400 + sprite] >> 7;
                zoom_y = zoom_control & 0xff;
                zoom_x = (zoom_control >> 8) & 0x0f;
                rows = y_control & 0x3f;
            }
            if (rows == 0) continue;
            if (!sprite_on_scanline(scanline, y, rows)) continue;
            if (x >= 0x140 && x <= 0x1f0) {
                if (++drawn >= kSpritesPerLine) break;
                continue;
            }

            int sprite_line = (scanline - y) & 0x1ff;
            int zoom_line = sprite_line & 0xff;
            bool invert = (sprite_line & 0x100) != 0;
            if (invert) zoom_line ^= 0xff;
            if (rows > 0x20) {
                const int period = (zoom_y + 1) << 1;
                if (period > 0) zoom_line %= period;
                if (zoom_line > zoom_y) {
                    zoom_line = ((zoom_y + 1) << 1) - 1 - zoom_line;
                    invert = !invert;
                }
            }

            int src_y;
            int tile;
            if (lo_rom_.size() >= 0x10000) {
                const uint8_t entry = lo_rom_[size_t((zoom_y << 8) | zoom_line)];
                src_y = entry & 0x0f;
                tile = entry >> 4;
            } else {
                src_y = zoom_line & 0x0f;
                tile = (zoom_line >> 4) & 0x1f;
            }
            if (invert) {
                src_y ^= 0x0f;
                tile ^= 0x1f;
            }

            const uint16_t tile_lo = vram_[sprite * 64 + tile * 2];
            const uint16_t attr = vram_[sprite * 64 + tile * 2 + 1];
            int code = int(tile_lo) | (int(attr & 0x00f0) << 12);
            if ((lspc_mode_ & 0x08) == 0) {
                if (attr & 0x0008) code = (code & ~0x07) | (auto_anim_ & 7);
                else if (attr & 0x0004) code = (code & ~0x03) | (auto_anim_ & 3);
            }
            if (attr & 0x0002) src_y ^= 0x0f;
            const int palette = (attr >> 8) & 0xff;
            const bool flip_x = (attr & 0x0001) != 0;
            draw_sprite_pixels(framebuffer, fy, x, src_y, zoom_x, flip_x, palette, code);

            drawn++;
            if (drawn >= kSpritesPerLine) break;
        }
    }
}

void NeoGeoVideo::render_frame(uint32_t* framebuffer) {
    const uint32_t backdrop = palette_rgb_[size_t(palette_bank_)][0xfff];
    std::fill(framebuffer, framebuffer + kScreenWidth * kScreenHeight, backdrop);
    draw_sprites(framebuffer);
    draw_fix(framebuffer);
}

}  // namespace dsp
