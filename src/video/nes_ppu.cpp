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
    dir_first = false;
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
    dot_line_trans_.fill(0);
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
    return pal_ram_[address & 0x1f];
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
    uint8_t ret = disable_chr ? uint8_t(address & 0xff) : buffer_read_;
    buffer_read_ = read_mem(address);
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
        switch (addr & 0x1f) {
            case 0x00:
            case 0x10:
                pal_ram_[0x00] = pal_ram_[0x04] = pal_ram_[0x08] = pal_ram_[0x0c] = value;
                pal_ram_[0x10] = pal_ram_[0x14] = pal_ram_[0x18] = pal_ram_[0x1c] = value;
                break;
            default:
                pal_ram_[addr & 0x1f] = value;
                break;
        }
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

void NesPpu::sprite_line_overflow(int line) {
    int nsprites = 0;
    for (int f = 0; f < 64; ++f) {
        const uint8_t pos_y = sprite_ram_[size_t(f * 4)];
        if (pos_y == 255 || pos_y > 239) continue;
        const unsigned pos_linea = unsigned(line) - pos_y;
        if (pos_linea < sprite_size) {
            ++nsprites;
            if (nsprites == 9) {
                status |= 0x20;
                return;
            }
        }
    }
}

void NesPpu::put_sprites(int line, uint8_t pri, uint32_t* out) {
    int nsprites = 0;
    for (int f = 0; f < 64; ++f) {
        const uint8_t pos_y = sprite_ram_[size_t(f * 4)];
        if (((sprite_ram_[size_t(f * 4 + 2)] & 0x20) != pri) || pos_y > 239) continue;
        const uint8_t pos_x = sprite_ram_[size_t(f * 4 + 3)];
        const unsigned pos_linea = unsigned(line) - pos_y;
        if (pos_linea >= sprite_size) continue;
        ++nsprites;
        if (nsprites == 9) return;

        const uint8_t attrib = uint8_t((sprite_ram_[size_t(f * 4 + 2)] & 0x03) << 2);
        const bool flipx = (sprite_ram_[size_t(f * 4 + 2)] & 0x40) != 0;
        const bool flipy = (sprite_ram_[size_t(f * 4 + 2)] & 0x80) != 0;
        uint8_t num_char = sprite_ram_[size_t(f * 4 + 1)];
        uint8_t temp = pos_spt;
        uint8_t def_y;
        if (sprite_size == 8) {
            def_y = flipy ? uint8_t(7 - (pos_linea & 7)) : uint8_t(pos_linea & 7);
        } else {
            temp = num_char & 1;
            if (flipy) {
                def_y = uint8_t(7 - (pos_linea & 7));
                num_char = uint8_t((num_char & 0xfe) + ((~unsigned(pos_linea >> 3)) & 1));
            } else {
                def_y = uint8_t(pos_linea & 7);
                num_char = uint8_t((num_char & 0xfe) + (pos_linea >> 3));
            }
        }
        const uint16_t pattern = uint16_t(temp * 0x1000 + num_char * 16 + def_y);
        const uint8_t tempb1 = read_mem(pattern);
        if (ppu_read_) ppu_read_(pattern);
        const uint8_t tempb2 = read_mem(uint16_t(pattern + 8));
        if (ppu_read_) ppu_read_(uint16_t(pattern + 8));

        for (int i = 0; i < 8; ++i) {
            const int x = flipx ? i : (7 - i);
            const int px = flipx ? (pos_x + i) : (pos_x + (7 - i));
            const uint8_t punto = uint8_t(((tempb1 >> x) & 1) + (((tempb2 >> x) & 1) << 1));
            if (punto == 0 || px < 0 || px >= kScreenWidth) continue;
            if (px != 255 && f == 0 && (dot_line_trans_[size_t(px)] & 0x3f) != 0) {
                status |= 0x40;
            }
            if ((control2 & 0x04) == 0 && px < 8) continue;
            if ((dot_line_trans_[size_t(px)] & 0x3f) >= uint8_t(f)) {
                out[px] = pal_color(read_mem(uint16_t(0x3f10 + punto + attrib)));
                dot_line_trans_[size_t(px)] =
                    uint8_t((dot_line_trans_[size_t(px)] & 0x80) | uint8_t(f));
            }
        }
    }
}

void NesPpu::put_background(uint32_t* scratch) {
    uint16_t attrib_table = uint16_t(
        0x2000 + (address & 0xc00) + 0x3c0 +
        ((((address & 0x3e0) / 0x20) & 0xfffc) * 2) + ((address & 0x1f) / 4));
    int pos_x = 0;
    const int tile_y_offset = (address & 0x7000) >> 12;
    uint8_t attrib_val;
    if ((address & 0x40) == 0) {
        attrib_val = ((address & 0x02) == 0) ? uint8_t((read_mem(attrib_table) & 0x03) << 2)
                                             : uint8_t(read_mem(attrib_table) & 0x0c);
    } else {
        attrib_val = ((address & 0x02) == 0) ? uint8_t((read_mem(attrib_table) & 0x30) >> 2)
                                             : uint8_t((read_mem(attrib_table) & 0xc0) >> 4);
    }
    for (int tiles = 32; tiles >= 0; --tiles) {
        const uint16_t pattern =
            uint16_t(pos_bg * 0x1000 + read_mem(uint16_t(0x2000 + (address & 0xfff))) * 16 +
                     tile_y_offset);
        if (ppu_read_) ppu_read_(pattern);
        const uint8_t lo = read_mem(pattern);
        const uint8_t hi = read_mem(uint16_t(pattern + 8));
        for (int x = 7; x >= 0; --x) {
            const uint8_t col = uint8_t(((lo >> x) & 1) + (((hi >> x) & 1) * 2));
            if (col == 0) {
                scratch[pos_x] = kTransparent;
                dot_line_trans_[size_t(pos_x)] = uint8_t(dot_line_trans_[size_t(pos_x)] & 0x7f);
            } else {
                scratch[pos_x] = set_emphasis(pal_color(read_mem(uint16_t(0x3f00 + col + attrib_val))));
                dot_line_trans_[size_t(pos_x)] = uint8_t(dot_line_trans_[size_t(pos_x)] | 0x80);
            }
            ++pos_x;
        }
        if ((address & 0x1f) == 0x1f) {
            attrib_table = uint16_t((attrib_table ^ 0x400) - 8);
            address = uint16_t(address ^ 0x41f);
        } else {
            address = uint16_t(address + 1);
        }
        if ((address & 0x03) == 0) attrib_table = uint16_t(attrib_table + 1);
        if ((address & 0x01) == 0) {
            if ((address & 0x40) == 0) {
                attrib_val = ((address & 0x02) == 0) ? uint8_t((read_mem(attrib_table) & 0x03) << 2)
                                                     : uint8_t(read_mem(attrib_table) & 0x0c);
            } else {
                attrib_val = ((address & 0x02) == 0) ? uint8_t((read_mem(attrib_table) & 0x30) >> 2)
                                                     : uint8_t((read_mem(attrib_table) & 0xc0) >> 4);
            }
        }
    }
    if ((control2 & 0x02) == 0) {
        for (int x = 0; x < 8; ++x) {
            dot_line_trans_[size_t(x)] = uint8_t(dot_line_trans_[size_t(x)] & 0x7f);
            scratch[tile_x_offset + x] = set_emphasis(pal_color(read_mem(0x3f00)));
        }
    }
}

void NesPpu::draw_linea(int line, uint32_t* out) {
    const uint32_t backdrop = set_emphasis(pal_color(read_mem(0x3f00)));
    for (int x = 0; x < kScreenWidth; ++x) out[x] = backdrop;
    dot_line_trans_.fill(0x3f);
    if (control2 & 0x18) sprite_line_overflow(line + 1);
    if (control2 & 0x10) put_sprites(line, 0x20, out);
    if (control2 & 0x08) {
        uint32_t scratch[256 + 16]{};
        put_background(scratch);
        for (int x = 0; x < kScreenWidth; ++x) {
            const uint32_t pix = scratch[tile_x_offset + x];
            if (pix != kTransparent) out[x] = pix;
        }
    }
    if (control2 & 0x10) put_sprites(line, 0x00, out);
    if ((control2 & 0x18) != 0x18) status &= 0xbf;
}

}  // namespace dsp
