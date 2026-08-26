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
    // FBNeo NeoDecodeSprites layout: right half at +0, left at +0x40, LSB-first pixels.
    // Plane map per half: bit0=b0, bit1=b2, bit2=b1, bit3=b3.
    const int tiles = int(size / 128);
    dest.assign(size_t(tiles) * kSprWidth * kSprHeight, 0);
    for (int t = 0; t < tiles; t++) {
        const uint8_t* tile = src + size_t(t) * 128;
        uint8_t* out = dest.data() + size_t(t) * kSprWidth * kSprHeight;
        for (int y = 0; y < 16; y++) {
            const int row = y << 2;
            for (int x = 0; x < 8; x++) {
                uint8_t left = 0;
                left |= uint8_t(((tile[64 + row + 0] >> x) & 1) << 0);
                left |= uint8_t(((tile[64 + row + 2] >> x) & 1) << 1);
                left |= uint8_t(((tile[64 + row + 1] >> x) & 1) << 2);
                left |= uint8_t(((tile[64 + row + 3] >> x) & 1) << 3);
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
    build_zoom_table();
}

void NeoGeoVideo::decode_graphics() {
    decode_fix(cart_fix_.data(), cart_fix_.size(), fix_pixels_, &cart_fix_tiles_);
    decode_fix(bios_fix_.data(), bios_fix_.size(), bios_fix_pixels_, &bios_fix_tiles_);
    decode_sprites(sprite_rom_.data(), sprite_rom_.size(), sprite_pixels_, &sprite_tiles_);
}

void NeoGeoVideo::build_zoom_table() {
    // Vertical zoom: 000-lo.lo holds 256 tables of 256 bytes. For each zoom z the
    // first 16 entries map output slots -> source line (0..15) or $FF to skip.
    for (int z = 0; z < 256; z++) {
        ZoomRow& row = zoom_[size_t(z)];
        row.draw.fill(0);
        row.pixels = 0;
        if (lo_rom_.size() >= 0x10000) {
            int drawn = 0;
            for (int i = 0; i < 16; i++) {
                const uint8_t src = lo_rom_[size_t((z << 8) | i)];
                if (src < 16) {
                    row.draw[size_t(i)] = uint8_t(src + 1);  // store src_line+1; 0 = skip
                    drawn++;
                }
            }
            row.pixels = drawn > 0 ? drawn : 0;
        } else {
            int pixels = ((z + 1) * 16) >> 8;
            if (z == 0xff) pixels = 16;
            row.pixels = pixels;
            int acc = 0, drawn = 0;
            for (int i = 0; i < 16 && drawn < pixels; i++) {
                acc += pixels;
                if (acc >= 16) {
                    acc -= 16;
                    row.draw[size_t(i)] = uint8_t(i + 1);
                    drawn++;
                }
            }
            if (drawn == 0 && z > 0) {
                row.draw[0] = 1;
                row.pixels = 1;
            }
        }
    }
    // Horizontal shrink is 4-bit: $0 draws 1 pixel, $F draws the full 16.
    for (int z = 0; z < 16; z++) {
        ZoomRow& row = xzoom_[size_t(z)];
        row.draw.fill(0);
        const int pixels = z + 1;
        row.pixels = pixels;
        int acc = 0, drawn = 0;
        for (int i = 0; i < 16 && drawn < pixels; i++) {
            acc += pixels;
            if (acc >= 16) {
                acc -= 16;
                row.draw[size_t(i)] = 1;
                drawn++;
            }
        }
    }
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
            const int dest_y = row * 8 - 16;
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
    const ZoomRow& zoom = xzoom_[size_t(xzoom & 0x0f)];
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
    int chain_xzoom = 0x0f;
    int chain_y = 0;
    int chain_size = 0;
    int chain_yzoom = 0xff;
    for (int sprite = 1; sprite < kSpriteCount; sprite++) {
        const uint16_t scb3 = vram_[0x8200 + sprite];
        const int size = scb3 & 0x3f;
        const bool sticky = (scb3 & 0x40) != 0;
        const uint16_t scb2 = vram_[0x8000 + sprite];
        int yzoom = scb2 & 0xff;
        int xzoom = (scb2 >> 8) & 0x0f;
        int sx;
        int sy;
        int draw_size;
        if (sticky) {
            sx = chain_x;
            xzoom = chain_xzoom;
            sy = chain_y;
            yzoom = chain_yzoom;
            draw_size = chain_size;
        } else {
            // FBNeo: nBankYPos = (0x200 - (SCB3>>7)) & 0x1FF; screen row = nYPos - 0x10
            sx = (vram_[0x8400 + sprite] >> 7) & 0x1ff;
            if (sx >= 0x1e0) sx -= 0x200;
            sy = int((0x200 - ((scb3 >> 7) & 0x1ff)) & 0x1ff);
            draw_size = size;
            chain_x = sx;
            chain_xzoom = xzoom;
            chain_y = sy;
            chain_yzoom = yzoom;
            chain_size = size;
        }
        if (draw_size == 0) continue;

        const uint16_t* scb1 = &vram_[sprite * 64];
        const int strip_lines = draw_size * 16;
        // Y-zoom via LO-ROM (FBNeo NeoZoomROM). For yzoom==0xFF the table is
        // identity and we draw every source line of the strip. For smaller zoom
        // values, only (yzoom+1) output slots are produced per 16-tile period.
        const uint8_t* ytable = (lo_rom_.size() >= 0x10000)
                                    ? lo_rom_.data() + (size_t(yzoom) << 8)
                                    : nullptr;
        int fb_y = int(sy & 0x1ff) - 16;  // FBNeo: nYPos - 0x10

        auto plot_src_line = [&](int src_line, int dest_y) {
            if (src_line < 0 || src_line >= strip_lines) return;
            const int tile_index = src_line >> 4;
            if (tile_index >= draw_size) return;
            const int row = src_line & 15;
            const uint16_t tile_lo = scb1[tile_index * 2];
            const uint16_t attr = scb1[tile_index * 2 + 1];
            int code = int(tile_lo) | (int(attr & 0x00f0) << 12);
            // FBNeo auto-animation: bits 3 / 2, REPLACE
            // Fix-layer auto-anim disabled: static HUD tiles use full 12-bit codes.
            // (Sprite auto-anim is handled separately in draw_sprites.)
            const int palette = (attr >> 8) & 0xff;
            const bool flip_x = (attr & 0x0001) != 0;
            const bool flip_y = (attr & 0x0002) != 0;
            const int draw_row = flip_y ? (15 - row) : row;
            draw_sprite_line(framebuffer, sprite, tile_index, sx, dest_y, draw_row, xzoom,
                             flip_x, false, palette, code);
        };

        if (ytable == nullptr || yzoom == 0xff) {
            // Full height / no table: 1:1 source lines
            for (int src_line = 0; src_line < strip_lines; src_line++) {
                plot_src_line(src_line, fb_y);
                fb_y++;
            }
        } else {
            // Reduced zoom: for each group of the strip, emit (yzoom+1) lines
            // using LO table entries as (tile<<4)|row selectors.
            const int slots = yzoom + 1;
            for (int base = 0; base < strip_lines; base += 16) {
                for (int slot = 0; slot < slots; slot++) {
                    const int entry = int(ytable[slot]);
                    const int src_line = base + (entry & 0x0f) + ((entry >> 4) << 4);
                    // entry high nibble selects tile relative to base's tile group
                    const int rel_tile = (entry >> 4) & 0x0f;
                    const int rel_row = entry & 0x0f;
                    const int src = ((base >> 4) + rel_tile) * 16 + rel_row;
                    plot_src_line(src, fb_y);
                    fb_y++;
                }
                // Only one 16-tile zoom period is encoded in LO; for taller strips
                // hardware repeats the period — step by slots worth of "output"
                // matched to how many source tiles the table spans (typically 1).
                if (slots <= 16) {
                    // compressed single tile: advance one tile worth
                    // actually advance is implicit via base += 16
                }
            }
        }
        // Sticky chain: next column advances by drawn width (xzoom+1)
        chain_x += (xzoom & 0x0f) + 1;
    }
}

void NeoGeoVideo::render_frame(uint32_t* framebuffer) {
    const uint32_t backdrop = palette_rgb_[size_t(palette_bank_)][0xfff];
    std::fill(framebuffer, framebuffer + kScreenWidth * kScreenHeight, backdrop);
    draw_sprites(framebuffer);
    draw_fix(framebuffer);
}

}  // namespace dsp
