#include "video/nes_ppu.h"

#include <algorithm>
#include <cmath>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#include <cstring>

namespace dsp {
namespace {

constexpr uint32_t kTransparent = 0;

uint32_t pack_rgb(int r, int g, int b) {
    r = std::clamp(r, 0, 255);
    g = std::clamp(g, 0, 255);
    b = std::clamp(b, 0, 255);
    return 0xff000000u | (uint32_t(r) << 16) | (uint32_t(g) << 8) | uint32_t(b);
}

}  // namespace

const uint8_t NesPpu::kMirrorTypes[9][4] = {
    {0, 0, 0, 0},  // unused (Pascal is 1-based)
    {0, 0, 1, 1},  // Horizontal
    {0, 1, 0, 1},  // Vertical
    {0, 0, 0, 0},  // Low
    {1, 1, 1, 1},  // High
    {0, 1, 2, 3},  // Four screen
    {1, 1, 0, 0},  // Map95
    {0, 0, 0, 1},  // Map243
    {0, 1, 1, 1},  // Map139
};

NesPpu::NesPpu() {
    // YUV palette from nesppu_chip.create. Only the first 64 entries (no
    // emphasis) are used; emphasis is applied at draw time like set_emphasis.
    constexpr float kBrightness[3][4] = {
        {0.50f, 0.75f, 1.00f, 1.00f},
        {0.29f, 0.45f, 0.73f, 0.90f},
        {0.00f, 0.24f, 0.47f, 0.77f},
    };
    constexpr float tint = 0.22f;
    constexpr float hue = 287.0f;
    constexpr float Kr = 0.2989f;
    constexpr float Kb = 0.1145f;
    constexpr float Ku = 2.029f;
    constexpr float Kv = 1.140f;
    int pos = 0;
    for (int intensity = 0; intensity < 4; ++intensity) {
        for (int color = 0; color < 16; ++color) {
            float sat = 0, rad = 0, y = 0;
            if (color == 0) {
                y = kBrightness[0][intensity];
            } else if (color == 13) {
                y = kBrightness[2][intensity];
            } else if (color == 14 || color == 15) {
                y = 0;
            } else {
                sat = tint;
                rad = float(M_PI) * ((float(color) * 30.0f + hue) / 180.0f);
                y = kBrightness[1][intensity];
            }
            const float u = sat * std::cos(rad);
            const float v = sat * std::sin(rad);
            const float r = (y + Kv * v) * 255.0f;
            const float g = (y - (Kb * Ku * u + Kr * Kv * v) / (1.0f - Kb - Kr)) * 255.0f;
            const float b = (y + Ku * u) * 255.0f;
            palette_[size_t(pos++)] =
                pack_rgb(int(std::floor(r + 0.5f)), int(std::floor(g + 0.5f)),
                         int(std::floor(b + 0.5f)));
        }
    }
    reset();
}

void NesPpu::reset() {
    control1 = 0;
    control2 = 0;
    pal_mask = 0x3f;
    status = 0;
    sprite_ram_pos = 0;
    address = 0;
    address_temp = 0;
    dir_first = true;  // next $2005/$2006 write is the first
    sprite0_hit = false;
    sprite_over_flow = false;
    sprite_size = 8;
    pos_bg = 0;
    pos_spt = 1;
    disable_chr = false;
    buffer_read_ = 0;
    tile_x_offset = 0;
    linea = 0;
    pal_ram_.fill(0);
    sprite_ram_.fill(0);
}

int NesPpu::nametable_index(uint16_t addr) const {
    const int m = (mirror >= 1 && mirror <= 8) ? mirror : int(Vertical);
    return kMirrorTypes[m][(addr >> 10) & 3];
}

uint8_t NesPpu::chr_read(uint16_t address) const {
    if (disable_chr) return uint8_t(address & 0xff);
    const int half = (address >> 12) & 1;
    const int bank = chr_map_ ? (chr_map_[half] & 3) : half;
    return chr_[size_t(bank)][address & 0xfff];
}

void NesPpu::chr_write(uint16_t address, uint8_t value) {
    if (disable_chr || !write_chr) return;
    const int half = (address >> 12) & 1;
    const int bank = chr_map_ ? (chr_map_[half] & 3) : half;
    chr_[size_t(bank)][address & 0xfff] = value;
}

uint8_t NesPpu::read_mem(uint16_t address) const {
    address &= 0x3fff;
    if (address <= 0x1fff) return chr_read(address);
    if (address <= 0x3eff) return name_table_[size_t(nametable_index(address))][address & 0x3ff];
    return pal_ram_[pal_index(address)];
}

uint32_t NesPpu::pal_color(uint8_t index) const {
    return palette_[size_t(index & pal_mask)];
}

uint32_t NesPpu::set_emphasis(uint32_t color) const {
    // Pascal masks 16-bit palette entries; here darken the channels that
    // emphasis would suppress (same visual intent as bits 5-7 of $2001).
    int r = int((color >> 16) & 0xff);
    int g = int((color >> 8) & 0xff);
    int b = int(color & 0xff);
    if (control2 & 0x80) {  // blue
        r = r * 3 / 4;
        g = g * 3 / 4;
    }
    if (control2 & 0x40) {  // green
        r = r * 3 / 4;
        b = b * 3 / 4;
    }
    if (control2 & 0x20) {  // red
        g = g * 3 / 4;
        b = b * 3 / 4;
    }
    return pack_rgb(r, g, b);
}

void NesPpu::advance_vram() {
    if (linea >= 240 || (control2 & 0x18) == 0) {
        if (control1 & 0x04) {
            address = uint16_t((address + 32) & 0x7fff);
        } else {
            address = uint16_t((address + 1) & 0x7fff);
        }
    } else {
        if ((address & 0x1f) == 0x1f) {
            address = uint16_t(address ^ 0x41f);
        } else {
            address = uint16_t(address + 1);
        }
        end_y_coarse();
    }
}

uint8_t NesPpu::read() {
    const uint16_t addr = uint16_t(address & 0x3fff);
    uint8_t ret;
    if (addr >= 0x3f00) {
        // Palette reads are not buffered; the buffer gets the nametable
        // byte "under" the palette.
        ret = uint8_t((read_mem(addr) & 0x3f) | (open_bus & 0xc0));
        buffer_read_ = read_mem(uint16_t(addr - 0x1000));
    } else {
        ret = disable_chr ? uint8_t(address & 0xff) : buffer_read_;
        buffer_read_ = read_mem(addr);
    }
    advance_vram();
    return ret;
}

void NesPpu::write(uint8_t value) {
    const uint16_t addr = uint16_t(address & 0x3fff);
    if (addr <= 0x1fff) {
        chr_write(addr, value);
    } else if (addr <= 0x3eff) {
        name_table_[size_t(nametable_index(addr))][addr & 0x3ff] = value;
    } else {
        // $3F10/$3F14/$3F18/$3F1C mirror $3F00/$3F04/$3F08/$3F0C.
        pal_ram_[pal_index(addr)] = uint8_t(value & 0x3f);
    }
    advance_vram();
}

void NesPpu::end_y_coarse() {
    if ((control2 & 0x18) == 0) return;
    address = uint16_t(address + 0x1000);
    if (line_ack_) line_ack_(false);
    if (address & 0x8000) {
        const uint16_t tmp = uint16_t((address & 0x03e0) + 0x20);
        address = uint16_t(address & 0x7c1f);
        if (tmp == 0x03c0) {
            address = uint16_t(address ^ 0x0800);
        } else {
            address = uint16_t(address | (tmp & 0x03e0));
        }
    }
    address = uint16_t((address & 0x7be0) | (address_temp & 0x41f));
}

void NesPpu::dma_spr(uint8_t page, const uint8_t* cpu_mem, std::function<void(int)> steal) {
    const int base = int(page) * 0x100;
    if (sprite_ram_pos != 0) {
        std::memcpy(&sprite_ram_[sprite_ram_pos], cpu_mem + base, size_t(0x100 - sprite_ram_pos));
        std::memcpy(&sprite_ram_[0], cpu_mem + base, sprite_ram_pos);
    } else {
        std::memcpy(&sprite_ram_[0], cpu_mem + base, 0x100);
    }
    // Pascal: contador := contador + 513 + (contador and 1). The caller
    // supplies the odd/even bit via steal's captured cycle count.
    if (steal) steal(513);
}

// Sprite evaluation and drawing for one line.  OAM Y is the line before the
// sprite's first line.  Up to eight sprites per line, the lowest OAM index
// winning where two overlap (whatever their priority bits: a low-index
// "behind" sprite masks higher-index sprites, the SMB mushroom trick).
// Fills spr[] with 0x10 | palette | colour (0 = transparent), behind[] with
// the priority bit and zero[] where sprite 0 is opaque.
void NesPpu::eval_sprites(int line, uint8_t* spr, bool* behind, bool* zero) {
    int found = 0;
    for (int f = 0; f < 64; ++f) {
        const uint8_t pos_y = sprite_ram_[size_t(f * 4)];
        const int row = line - (int(pos_y) + 1);
        if (row < 0 || row >= sprite_size) continue;
        if (++found > 8) {
            status |= 0x20;  // sprite overflow
            break;
        }
        const uint8_t tile = sprite_ram_[size_t(f * 4 + 1)];
        const uint8_t attr = sprite_ram_[size_t(f * 4 + 2)];
        const uint8_t pos_x = sprite_ram_[size_t(f * 4 + 3)];
        const bool flipx = (attr & 0x40) != 0;
        const bool flipy = (attr & 0x80) != 0;
        uint16_t pattern;
        if (sprite_size == 8) {
            const int y = flipy ? 7 - row : row;
            pattern = uint16_t(pos_spt * 0x1000 + tile * 16 + y);
        } else {
            // 8x16: bit 0 picks the pattern table, the pair of tiles is
            // swapped by vertical flip.
            const int y = flipy ? 15 - row : row;
            const int t = (tile & 0xfe) + (y >> 3);
            pattern = uint16_t((tile & 1) * 0x1000 + t * 16 + (y & 7));
        }
        const uint8_t lo = read_mem(pattern);
        if (ppu_read_) ppu_read_(pattern);
        const uint8_t hi = read_mem(uint16_t(pattern + 8));
        if (ppu_read_) ppu_read_(uint16_t(pattern + 8));
        const uint8_t palette = uint8_t(0x10 | ((attr & 0x03) << 2));
        for (int i = 0; i < 8; ++i) {
            // Pattern bit 7 is the leftmost pixel unless flipped.
            const int bit = flipx ? i : 7 - i;
            const uint8_t color = uint8_t(((lo >> bit) & 1) | (((hi >> bit) & 1) << 1));
            const int px = pos_x + i;
            if (color == 0 || px >= kScreenWidth) continue;
            if ((control2 & 0x04) == 0 && px < 8) continue;  // left 8 pixels clipped
            if (spr[px] != 0) continue;                      // lower index wins
            spr[px] = uint8_t(palette | color);
            behind[px] = (attr & 0x20) != 0;
            zero[px] = f == 0;
        }
    }
}

// Background for one line: 33 tiles from the loopy V address (coarse X
// advances as on hardware), then the fine X offset.  bg[] gets the palette
// address (attribute * 4 + colour, 0 = transparent).
void NesPpu::put_background(uint8_t* bg) {
    uint8_t scratch[33 * 8];
    uint16_t attrib_table = uint16_t(
        0x2000 + (address & 0xc00) + 0x3c0 +
        ((((address & 0x3e0) / 0x20) & 0xfffc) * 2) + ((address & 0x1f) / 4));
    int pos_x = 0;
    const int tile_y_offset = (address & 0x7000) >> 12;
    auto attribute = [&]() -> uint8_t {
        const uint8_t a = read_mem(attrib_table);
        const int shift = ((address & 0x40) ? 4 : 0) + ((address & 0x02) ? 2 : 0);
        return uint8_t(((a >> shift) & 3) << 2);
    };
    uint8_t attrib_val = attribute();
    for (int tiles = 0; tiles < 33; ++tiles) {
        const uint16_t pattern =
            uint16_t(pos_bg * 0x1000 + read_mem(uint16_t(0x2000 + (address & 0xfff))) * 16 +
                     tile_y_offset);
        if (ppu_read_) ppu_read_(pattern);
        const uint8_t lo = read_mem(pattern);
        const uint8_t hi = read_mem(uint16_t(pattern + 8));
        for (int x = 7; x >= 0; --x) {
            const uint8_t col = uint8_t(((lo >> x) & 1) | (((hi >> x) & 1) << 1));
            scratch[pos_x++] = col ? uint8_t(attrib_val | col) : 0;
        }
        if ((address & 0x1f) == 0x1f) {
            attrib_table = uint16_t((attrib_table ^ 0x400) - 8);
            address = uint16_t(address ^ 0x41f);
        } else {
            address = uint16_t(address + 1);
        }
        if ((address & 0x03) == 0) attrib_table = uint16_t(attrib_table + 1);
        if ((address & 0x01) == 0) attrib_val = attribute();
    }
    for (int x = 0; x < kScreenWidth; ++x) bg[x] = scratch[tile_x_offset + x];
    if ((control2 & 0x02) == 0) {
        for (int x = 0; x < 8; ++x) bg[x] = 0;  // left 8 pixels clipped
    }
}

void NesPpu::draw_linea(int line, uint32_t* out) {
    uint8_t bg[kScreenWidth] = {};
    uint8_t spr[kScreenWidth] = {};
    bool behind[kScreenWidth] = {};
    bool zero[kScreenWidth] = {};
    const bool show_bg = (control2 & 0x08) != 0;
    const bool show_spr = (control2 & 0x10) != 0;
    if (show_bg) put_background(bg);
    if (show_spr) eval_sprites(line, spr, behind, zero);

    // With rendering off the PPU outputs the backdrop, or the palette entry
    // V points at when V is in palette space.
    uint8_t backdrop = pal_ram_[0];
    if (!show_bg && !show_spr && (address & 0x3f00) == 0x3f00) backdrop = pal_ram_[pal_index(address)];

    for (int x = 0; x < kScreenWidth; ++x) {
        // Sprite 0 hit: opaque sprite 0 over opaque background, not at x=255.
        if (zero[x] && bg[x] != 0 && show_bg && show_spr && x != 255) status |= 0x40;
        uint8_t value;
        if (spr[x] != 0 && (!behind[x] || bg[x] == 0)) value = pal_ram_[spr[x] & 0x1f];
        else if (bg[x] != 0) value = pal_ram_[bg[x]];
        else value = backdrop;
        out[x] = set_emphasis(pal_color(value));
    }
}

}  // namespace dsp
