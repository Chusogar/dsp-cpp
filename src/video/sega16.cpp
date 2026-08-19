#include "video/sega16.h"

#include <algorithm>
#include <cstring>

namespace dsp {
namespace {

constexpr uint32_t kTransparent = 0;

uint32_t pack_rgb(uint8_t r, uint8_t g, uint8_t b) {
    return 0xff000000u | (uint32_t(r) << 16) | (uint32_t(g) << 8) | b;
}

void put_sprite_pixel(uint32_t* dest, int x, int y, uint32_t color, uint32_t shadow_color,
                      bool is_shadow) {
    if (x < 0 || x >= 320 || y < 0 || y >= 224) return;
    uint32_t& pixel = dest[size_t(y * 320 + x)];
    if (is_shadow) {
        pixel = s16_mix_shadow(pixel, shadow_color);
    } else {
        pixel = color;
    }
}

int tile_code_16a(uint16_t data) { return int(((data >> 1) & 0x1000) | (data & 0xfff)); }

}  // namespace

uint32_t s16_argb(uint8_t r, uint8_t g, uint8_t b) { return pack_rgb(r, g, b); }

uint32_t s16_mix_shadow(uint32_t dest, uint32_t shadow) {
    const uint32_t r = (((dest >> 16) & 0xff) + ((shadow >> 16) & 0xff)) / 2;
    const uint32_t g = (((dest >> 8) & 0xff) + ((shadow >> 8) & 0xff)) / 2;
    const uint32_t b = ((dest & 0xff) + (shadow & 0xff)) / 2;
    return pack_rgb(uint8_t(r), uint8_t(g), uint8_t(b));
}

void build_s16_palette_luts(uint8_t normal[32], uint8_t shadow[32], uint8_t hilight[32]) {
    const std::vector<ResistorNet> nets_normal = {{{3900, 2000, 1000, 500, 250, 0}}};
    const std::vector<ResistorNet> nets_sh = {{{3900, 2000, 1000, 500, 250, 470}}};
    auto w_n = compute_resistor_weights(0, 255, -1.0, nets_normal);
    auto w_s = compute_resistor_weights(0, 255, -1.0, nets_sh);
    for (int f = 0; f < 32; f++) {
        const std::vector<int> bits = {(f >> 0) & 1, (f >> 1) & 1, (f >> 2) & 1,
                                       (f >> 3) & 1, (f >> 4) & 1, 0};
        std::vector<int> bits_hi = bits;
        bits_hi[5] = 1;
        normal[f] = uint8_t(combine_weights(w_n[0], bits));
        shadow[f] = uint8_t(combine_weights(w_s[0], bits));
        hilight[f] = uint8_t(combine_weights(w_s[0], bits_hi));
    }
}

bool load_rom_bytes(RomLoader& loader, const std::vector<RomEntry>& entries,
                    std::vector<uint8_t>& dest, std::string* error) {
    uint32_t size = 0;
    for (const RomEntry& entry : entries) {
        if (!entry.name || entry.name[0] == 0) continue;
        size = std::max(size, entry.offset + entry.length);
    }
    dest.assign(size, 0);
    for (const RomEntry& entry : entries) {
        if (!entry.name || entry.name[0] == 0) continue;
        std::vector<uint8_t> data(entry.length, 0);
        RomEntry single{entry.name, entry.length, 0, entry.crc};
        if (!loader.load({single}, data, error)) return false;
        std::copy(data.begin(), data.end(), dest.begin() + entry.offset);
    }
    return true;
}

bool load_roms16w(RomLoader& loader, const std::vector<RomEntry>& entries,
                  std::vector<uint16_t>& dest, std::string* error) {
    uint32_t bytes = 0;
    for (const RomEntry& entry : entries) {
        if (!entry.name || entry.name[0] == 0) continue;
        bytes = std::max(bytes, (entry.offset & ~1u) + entry.length * 2);
    }
    dest.assign(bytes / 2, 0);
    for (const RomEntry& entry : entries) {
        if (!entry.name || entry.name[0] == 0) continue;
        std::vector<uint8_t> data(entry.length, 0);
        RomEntry single{entry.name, entry.length, 0, entry.crc};
        if (!loader.load({single}, data, error)) return false;
        const bool high = (entry.offset & 1) == 0;
        uint32_t index = entry.offset >> 1;
        for (uint32_t i = 0; i < entry.length; i++) {
            if (index >= dest.size()) dest.resize(index + 1, 0);
            if (high) {
                dest[index] = uint16_t((dest[index] & 0x00ff) | (uint16_t(data[i]) << 8));
            } else {
                dest[index] = uint16_t((dest[index] & 0xff00) | data[i]);
            }
            index++;
        }
    }
    return true;
}

bool load_roms16b(RomLoader& loader, const std::vector<RomEntry>& entries,
                  std::vector<uint16_t>& dest, std::string* error) {
    uint32_t bytes = 0;
    for (const RomEntry& entry : entries) {
        if (!entry.name || entry.name[0] == 0) continue;
        bytes = std::max(bytes, entry.offset + entry.length * 2);
    }
    std::vector<uint8_t> raw(bytes, 0);
    for (const RomEntry& entry : entries) {
        if (!entry.name || entry.name[0] == 0) continue;
        std::vector<uint8_t> data(entry.length, 0);
        RomEntry single{entry.name, entry.length, 0, entry.crc};
        if (!loader.load({single}, data, error)) return false;
        for (uint32_t i = 0; i < entry.length; i++) {
            raw[entry.offset + i * 2] = data[i];
        }
    }
    dest.resize(raw.size() / 2);
    // roms_load16b writes interleaved bytes into a word array. On little-endian
    // that is even|odd<<8, which System 16 sprite drawing expects.
    for (size_t i = 0; i < dest.size(); i++) {
        dest[i] = uint16_t(raw[i * 2] | (uint16_t(raw[i * 2 + 1]) << 8));
    }
    return true;
}

bool load_roms32dw(RomLoader& loader, const std::vector<RomEntry>& entries,
                   std::vector<uint32_t>& dest, std::string* error) {
    uint32_t dwords = 0;
    for (const RomEntry& entry : entries) {
        if (!entry.name || entry.name[0] == 0) continue;
        dwords = std::max(dwords, (entry.offset >> 2) + entry.length);
    }
    dest.assign(dwords, 0);
    for (const RomEntry& entry : entries) {
        if (!entry.name || entry.name[0] == 0) continue;
        std::vector<uint8_t> data(entry.length, 0);
        RomEntry single{entry.name, entry.length, 0, entry.crc};
        if (!loader.load({single}, data, error)) return false;
        uint32_t index = entry.offset >> 2;
        const int lane = int(entry.offset & 3);
        for (uint32_t i = 0; i < entry.length; i++) {
            if (index >= dest.size()) dest.resize(index + 1, 0);
            uint32_t value = dest[index];
            switch (lane) {
                case 0: dest[index] = (value & 0xffffff00u) | data[i]; break;
                case 1: dest[index] = (value & 0xffff00ffu) | (uint32_t(data[i]) << 8); break;
                case 2: dest[index] = (value & 0xff00ffffu) | (uint32_t(data[i]) << 16); break;
                case 3: dest[index] = (value & 0x00ffffffu) | (uint32_t(data[i]) << 24); break;
            }
            index++;
        }
    }
    return true;
}

void decode_s16_tiles(GfxSet& tiles, const std::vector<uint8_t>& rom, int n) {
    GfxLayout layout;
    layout.width = 8;
    layout.height = 8;
    layout.total = n * 0x1000;
    layout.planes = 3;
    layout.char_increment = 8 * 8;
    layout.plane_offsets = {n * 0x10000 * 8, n * 0x8000 * 8, 0};
    layout.x_offsets = {0, 1, 2, 3, 4, 5, 6, 7};
    layout.y_offsets = {0 * 8, 1 * 8, 2 * 8, 3 * 8, 4 * 8, 5 * 8, 6 * 8, 7 * 8};
    tiles.decode(layout, rom);
}

void Sega16Video::reset() {
    pal_ram.fill(0);
    tile_ram.fill(0);
    char_ram.fill(0);
    sprite_ram.fill(0);
    palette.fill(0);
    screens.fill(0);
    tile_bank = {0, 0};
    for (int i = 0; i < 16; i++) sprite_bank[size_t(i)] = uint8_t(i);
    for (auto& page : tile_dirty) page.fill(true);
    text_dirty.fill(true);
    screen_enabled = true;
}

void Sega16Video::init_palette_luts() {
    build_s16_palette_luts(normal.data(), shadow.data(), hilight.data());
}

void Sega16Video::set_palette_entry(int index, uint16_t value, bool split_shadow) {
    pal_ram[size_t(index) & 0xfff] = value;
    const int r = ((value >> 12) & 1) | ((value << 1) & 0x1e);
    const int g = ((value >> 13) & 1) | ((value >> 3) & 0x1e);
    const int b = ((value >> 14) & 1) | ((value >> 7) & 0x1e);
    palette[size_t(index)] = pack_rgb(normal[size_t(r)], normal[size_t(g)], normal[size_t(b)]);
    if (split_shadow) {
        if (value & 0x8000) {
            palette[size_t(index + 0x1000)] =
                pack_rgb(shadow[size_t(r)], shadow[size_t(g)], shadow[size_t(b)]);
        } else {
            palette[size_t(index + 0x1000)] =
                pack_rgb(hilight[size_t(r)], hilight[size_t(g)], hilight[size_t(b)]);
        }
        if (index + 0x800 < int(palette.size())) {
            if (value & 0x8000) {
                palette[size_t(index + 0x800)] =
                    pack_rgb(shadow[size_t(r)], shadow[size_t(g)], shadow[size_t(b)]);
            } else {
                palette[size_t(index + 0x800)] =
                    pack_rgb(hilight[size_t(r)], hilight[size_t(g)], hilight[size_t(b)]);
            }
        }
    } else {
        palette[size_t(index + 0x800)] =
            pack_rgb(shadow[size_t(r)], shadow[size_t(g)], shadow[size_t(b)]);
        palette[size_t(index + 0x1000)] =
            pack_rgb(hilight[size_t(r)], hilight[size_t(g)], hilight[size_t(b)]);
    }
}

void Sega16Video::mark_tile(uint16_t word_offset) {
    const int page = word_offset >> 11;
    const int pos = word_offset & 0x7ff;
    for (int s = 0; s < 8; s++) {
        if (screens[size_t(s)] == page) tile_dirty[size_t(s)][size_t(pos)] = true;
    }
}

void Sega16Video::apply_screen_select_16b(uint16_t char_offset) {
    auto apply = [&](uint16_t addr, int base) {
        if (char_offset != addr) return;
        const uint16_t value = char_ram[addr];
        for (int i = 0; i < 4; i++) {
            const uint8_t page = uint8_t((value >> (12 - i * 4)) & 0xf);
            if (screens[size_t(base + i)] != page) {
                screens[size_t(base + i)] = page;
                tile_dirty[size_t(base + i)].fill(true);
            }
        }
    };
    apply(0x740, 4);
    apply(0x741, 0);
}

void Sega16Video::apply_screen_select_16a(uint16_t char_offset) {
    auto apply = [&](uint16_t addr, int base) {
        if (char_offset != addr) return;
        const uint16_t value = char_ram[addr];
        for (int i = 0; i < 4; i++) {
            const uint8_t page = uint8_t((value >> (12 - i * 4)) & 7);
            if (screens[size_t(base + i)] != page) {
                screens[size_t(base + i)] = page;
                tile_dirty[size_t(base + i)].fill(true);
            }
        }
    };
    apply(0x74e, 0);
    apply(0x74f, 4);
}

void Sega16Video::apply_screen_select_hangon(uint16_t char_offset) {
    auto apply = [&](uint16_t addr, int base) {
        if (char_offset != addr) return;
        const uint16_t value = char_ram[addr];
        for (int i = 0; i < 4; i++) {
            const uint8_t page = uint8_t((value >> (12 - i * 4)) & 3);
            if (screens[size_t(base + i)] != page) {
                screens[size_t(base + i)] = page;
                tile_dirty[size_t(base + i)].fill(true);
            }
        }
    };
    apply(0x74e, 0);
    apply(0x74f, 4);
}

void Sega16Video::render_tile_pages(std::vector<uint32_t>& low, std::vector<uint32_t>& high,
                                    int first_page, bool transparent, int color_shift,
                                    int code_mask, int pri_mask, bool use_tile_bank,
                                    bool extra_code_bit) {
    low.assign(size_t(kMapWidth * kMapHeight), kTransparent);
    high.assign(size_t(kMapWidth * kMapHeight), kTransparent);
    const int page_x[4] = {0, 512, 0, 512};
    const int page_y[4] = {256, 256, 0, 0};
    for (int p = 0; p < 4; p++) {
        const int num = first_page + p;
        const uint16_t pos = uint16_t(screens[size_t(num)] * 0x800);
        for (int f = 0; f < 0x800; f++) {
            const uint16_t data = tile_ram[size_t((pos + f) & 0x7fff)];
            int nchar = extra_code_bit ? tile_code_16a(data) : int(data & uint16_t(code_mask));
            if (use_tile_bank) {
                nchar = int(tile_bank[size_t(nchar / 0x1000)] * 0x1000 + (nchar % 0x1000));
            }
            const int color = (data >> color_shift) & 0x7f;
            const bool pri = (data & uint16_t(pri_mask)) != 0;
            const int x = ((f & 0x3f) << 3) + page_x[p];
            const int y = ((f >> 6) << 3) + page_y[p];
            const uint8_t* pixels = tiles.element(nchar);
            for (int row = 0; row < 8; row++) {
                for (int col = 0; col < 8; col++) {
                    const uint8_t pen = pixels[row * 8 + col];
                    const int dx = x + col;
                    const int dy = y + row;
                    if (dx < 0 || dx >= kMapWidth || dy < 0 || dy >= kMapHeight) continue;
                    uint32_t pixel = kTransparent;
                    if (pen != 0 || !transparent) {
                        pixel = palette[size_t((color << 3) + pen)];
                    }
                    const size_t index = size_t(dy * kMapWidth + dx);
                    if (pen != 0 || !transparent) low[index] = pixel;
                    if (pri && pen != 0) high[index] = pixel;
                }
            }
        }
    }
}

void Sega16Video::render_text(std::vector<uint32_t>& low, std::vector<uint32_t>& high,
                              int color_shift, int code_mask, int pri_mask, bool use_tile_bank) {
    low.assign(size_t(kTextWidth * 256), kTransparent);
    high.assign(size_t(kTextWidth * 256), kTransparent);
    for (int f = 0; f <= 0x6ff; f++) {
        const uint16_t atrib = char_ram[size_t(f)];
        const int color = (atrib >> color_shift) & 7;
        int nchar = atrib & code_mask;
        if (use_tile_bank) nchar = int(tile_bank[0]) * 0x1000 + nchar;
        const int x = (f & 0x3f) << 3;
        const int y = (f >> 6) << 3;
        const uint8_t* pixels = tiles.element(nchar);
        const bool pri = (atrib & uint16_t(pri_mask)) != 0;
        for (int row = 0; row < 8; row++) {
            for (int col = 0; col < 8; col++) {
                const uint8_t pen = pixels[row * 8 + col];
                if (pen == 0) continue;
                const uint32_t pixel = palette[size_t((color << 3) + pen)];
                const size_t index = size_t((y + row) * kTextWidth + (x + col));
                if (index >= low.size()) continue;
                low[index] = pixel;
                if (pri) high[index] = pixel;
            }
        }
    }
}

void Sega16Video::blit_scrolled(uint32_t* dest, const std::vector<uint32_t>& source, int scroll_x,
                                int scroll_y, int src_width, int src_height) const {
    for (int y = 0; y < kHeight; y++) {
        const int sy = (y + scroll_y) & (src_height - 1);
        for (int x = 0; x < kWidth; x++) {
            const int sx = (x + scroll_x) & (src_width - 1);
            const uint32_t pixel = source[size_t(sy * src_width + sx)];
            if (pixel != kTransparent) dest[y * kWidth + x] = pixel;
        }
    }
}

void Sega16Video::blit_text(uint32_t* dest, const std::vector<uint32_t>& source) const {
    for (int y = 0; y < kHeight; y++) {
        for (int x = 0; x < kWidth; x++) {
            const uint32_t pixel = source[size_t(y * kTextWidth + (x + 192))];
            if (pixel != kTransparent) dest[y * kWidth + x] = pixel;
        }
    }
}

void draw_sprites_16a(Sega16Video& video, uint32_t* dest, const std::vector<uint16_t>& sprite_rom,
                      int banks, int pri, int pal_base, uint32_t shadow_index) {
    const uint32_t shadow = video.palette[shadow_index];
    for (int f = 0; f < 0x80; f++) {
        uint16_t* ram = &video.sprite_ram[size_t(f * 8)];
        const int bottom = (ram[0] >> 8) + 1;
        if (bottom > 0xf0) return;
        const int sprpri = ram[4] & 3;
        if (sprpri != pri) continue;
        uint16_t addr = ram[3];
        ram[7] = addr;
        const int bank = video.sprite_bank[(ram[4] >> 4) & 7];
        const int top = (ram[0] & 0xff) + 1;
        if (top >= bottom || bank == 255) continue;
        const int xpos = int(ram[1] & 0x1ff) - 0xbd;
        const int pitch = int16_t(ram[2]);
        const int color = ((ram[4] >> 8) & 0x3f) << 4;
        const uint32_t spritedata = uint32_t(0x8000 * (bank % std::max(banks, 1)));
        for (int y = top; y < bottom; y++) {
            addr = uint16_t(addr + pitch);
            if (y >= 256) continue;
            uint16_t data_7 = addr;
            int x = xpos;
            if ((addr & 0x8000) == 0) {
                while (x < 512) {
                    const uint16_t pixels =
                        sprite_rom[(spritedata + (data_7 & 0x7fff)) % sprite_rom.size()];
                    for (int g = 3; g >= 0; g--) {
                        const uint16_t pix = uint16_t(((pixels >> (g * 4)) & 0xf) | color);
                        const int pen = pix & 0xf;
                        if (x < 320 && y < 224 && pen != 0 && pen != 15) {
                            const bool is_shadow = (pix & 0x3f0) == 0x3f0;
                            put_sprite_pixel(dest, x, y,
                                             video.palette[size_t((pix & 0x3ff) + pal_base)], shadow,
                                             is_shadow);
                        }
                        x++;
                    }
                    if ((pixels & 0xf) == 15) {
                        ram[7] = data_7;
                        break;
                    }
                    data_7++;
                }
            } else {
                while (x < 512) {
                    const uint16_t pixels =
                        sprite_rom[(spritedata + (data_7 & 0x7fff)) % sprite_rom.size()];
                    uint16_t last = 0;
                    for (int g = 0; g < 4; g++) {
                        const uint16_t pix = uint16_t(((pixels >> (g * 4)) & 0xf) | color);
                        last = pix & 0xf;
                        if (x < 320 && y < 224 && last != 0 && last != 15) {
                            const bool is_shadow = (pix & 0x3f0) == 0x3f0;
                            put_sprite_pixel(dest, x, y,
                                             video.palette[size_t((pix & 0x3ff) + pal_base)], shadow,
                                             is_shadow);
                        }
                        x++;
                    }
                    if (last == 15) {
                        ram[7] = data_7;
                        break;
                    }
                    data_7--;
                }
            }
        }
    }
}

void draw_sprites_hangon(Sega16Video& video, uint32_t* dest, const std::vector<uint16_t>& sprite_rom,
                         const std::vector<uint8_t>& zoom, int banks, int pri) {
    for (int f = 0; f < 0x80; f++) {
        uint16_t* ram = &video.sprite_ram[size_t(f * 8)];
        const int sprpri = ram[4] & 3;
        if (sprpri != pri) continue;
        uint16_t addr = ram[3];
        ram[7] = addr;
        const int bottom = (ram[0] >> 8) + 1;
        if (bottom > 0xf0) break;
        const int top = (ram[0] & 0xff) + 1;
        const int bank = video.sprite_bank[(ram[1] >> 12) & 0xf];
        if (top >= bottom || bank == 255) continue;
        const int xpos = int(ram[1] & 0x1ff) - 0xbd;
        const int pitch = int16_t(ram[2]);
        const int color = ((ram[4] >> 8) & 0x3f) << 4;
        const uint16_t vzoom = (ram[4] >> 2) & 0x3f;
        const uint16_t hzoom = uint16_t(vzoom << 1);
        const uint32_t spritedata = uint32_t(0x8000 * (bank % std::max(banks, 1)));
        uint16_t zaddr = uint16_t((vzoom & 0x38) << 5);
        const uint16_t zmask = uint16_t(1 << (vzoom & 7));
        for (int y = top; y < bottom; y++) {
            addr = uint16_t(addr + pitch);
            if (!zoom.empty() && (zoom[zaddr % zoom.size()] & zmask) != 0) {
                addr = uint16_t(addr + pitch);
            }
            zaddr++;
            if (y >= 256) continue;
            uint16_t xacc = 0;
            uint16_t data_7 = addr;
            int x = xpos;
            if ((addr & 0x8000) == 0) {
                while (x < 512) {
                    const uint16_t pixels =
                        sprite_rom[(spritedata + (data_7 & 0x7fff)) % sprite_rom.size()];
                    for (int g = 3; g >= 0; g--) {
                        xacc = uint16_t((xacc & 0xff) + hzoom);
                        if (xacc < 0x100) {
                            const uint16_t pix = uint16_t(((pixels >> (g * 4)) & 0xf) | color);
                            const int pen = pix & 0xf;
                            if (x < 320 && y < 224 && pen != 0 && pen != 15) {
                                dest[size_t(y * 320 + x)] =
                                    video.palette[size_t((pix & 0x3ff) + 0x400)];
                            }
                            x++;
                        }
                    }
                    if ((pixels & 0xf) == 0xf) {
                        ram[7] = data_7;
                        break;
                    }
                    data_7++;
                }
            } else {
                while (x < 512) {
                    const uint16_t pixels =
                        sprite_rom[(spritedata + (data_7 & 0x7fff)) % sprite_rom.size()];
                    for (int g = 0; g < 4; g++) {
                        xacc = uint16_t((xacc & 0xff) + hzoom);
                        if (xacc < 0x100) {
                            const uint16_t pix = uint16_t(((pixels >> (g * 4)) & 0xf) | color);
                            const int pen = pix & 0xf;
                            if (x < 320 && y < 224 && pen != 0 && pen != 15) {
                                dest[size_t(y * 320 + x)] =
                                    video.palette[size_t((pix & 0x3ff) + 0x400)];
                            }
                            x++;
                        }
                    }
                    if (((pixels >> 12) & 0xf) == 0xf) {
                        ram[7] = data_7;
                        break;
                    }
                    data_7--;
                }
            }
        }
    }
}

void draw_sprites_sharrier(Sega16Video& video, uint32_t* dest, const std::vector<uint32_t>& sprite_rom,
                           const std::vector<uint8_t>& zoom, int banks, int pri) {
    const uint32_t shadow = video.palette[0x800];
    for (int f = 0; f < 0x100; f++) {
        uint16_t* ram = &video.sprite_ram[size_t(f * 8)];
        const int sprpri = (ram[2] >> 14) & 1;
        if (sprpri != pri) continue;
        uint16_t addr = ram[3];
        ram[7] = addr;
        const int bottom = ram[0] >> 8;
        if (bottom > 0xf0) break;
        const int top = ram[0] & 0xff;
        const int bank = video.sprite_bank[(ram[1] >> 12) & 0xf];
        if (top >= bottom || bank == 255) continue;
        const int xpos = int(ram[1] & 0x1ff) - 0xbd;
        int pitch = ram[2] & 0x7f;
        if (pitch > 0x3f) pitch = -(pitch & 0x3f);
        const int color = (ram[2] >> 8) << 4;
        const uint16_t vzoom = ram[4] & 0x3f;
        const uint16_t hzoom = uint16_t(((ram[4] >> 8) & 0x3f) << 1);
        const uint32_t spritedata = uint32_t(0x8000 * (bank % std::max(banks, 1)));
        uint16_t zaddr = uint16_t((vzoom & 0x38) << 5);
        const uint16_t zmask = uint16_t(1 << (vzoom & 7));
        for (int y = top; y < bottom; y++) {
            addr = uint16_t(int(addr) + pitch);
            if (!zoom.empty() && (zoom[zaddr % zoom.size()] & zmask) != 0) {
                addr = uint16_t(int(addr) + pitch);
            }
            zaddr++;
            if (y >= 256) continue;
            uint16_t xacc = 0;
            uint16_t data_7 = addr;
            int x = xpos;
            if ((addr & 0x8000) == 0) {
                while (x < 512) {
                    const uint32_t pixels =
                        sprite_rom[(spritedata + (data_7 & 0x7fff)) % sprite_rom.size()];
                    for (int g = 7; g >= 0; g--) {
                        xacc = uint16_t((xacc & 0xff) + hzoom);
                        if (xacc < 0x100) {
                            const uint16_t pix = uint16_t(((pixels >> (g * 4)) & 0xf) | color);
                            const int pen = pix & 0xf;
                            if (x >= 0 && x < 320 && y < 224 && pen != 0 && pen != 15) {
                                if ((pix & 0x80f) == 0xa) {
                                    dest[size_t(y * 320 + x)] =
                                        s16_mix_shadow(dest[size_t(y * 320 + x)], shadow);
                                } else {
                                    dest[size_t(y * 320 + x)] =
                                        video.palette[size_t((pix & 0x3ff) + 0x400)];
                                }
                            }
                            x++;
                        }
                    }
                    if ((pixels & 0xf) == 0xf) {
                        ram[7] = data_7;
                        break;
                    }
                    data_7++;
                }
            } else {
                while (x < 512) {
                    const uint32_t pixels =
                        sprite_rom[(spritedata + (data_7 & 0x7fff)) % sprite_rom.size()];
                    for (int g = 0; g < 8; g++) {
                        xacc = uint16_t((xacc & 0xff) + hzoom);
                        if (xacc < 0x100) {
                            const uint16_t pix = uint16_t(((pixels >> (g * 4)) & 0xf) | color);
                            const int pen = pix & 0xf;
                            if (x >= 0 && x < 320 && y < 224 && pen != 0 && pen != 15) {
                                if ((pix & 0x80f) == 0xa) {
                                    dest[size_t(y * 320 + x)] =
                                        s16_mix_shadow(dest[size_t(y * 320 + x)], shadow);
                                } else {
                                    dest[size_t(y * 320 + x)] =
                                        video.palette[size_t((pix & 0x3ff) + 0x400)];
                                }
                            }
                            x++;
                        }
                    }
                    if (((pixels >> 28) & 0xf) == 0xf) {
                        ram[7] = data_7;
                        break;
                    }
                    data_7--;
                }
            }
        }
    }
}

void draw_sprites_16b(Sega16Video& video, uint32_t* dest, const std::vector<uint16_t>& sprite_rom,
                      int banks, int pri, uint32_t shadow_index) {
    const uint32_t shadow = video.palette[shadow_index];
    for (int f = 0; f < 0x80; f++) {
        uint16_t* ram = &video.sprite_ram[size_t(f * 8)];
        if (ram[2] & 0x8000) return;
        const int sprpri = (ram[4] & 0xff) >> 6;
        if (sprpri != pri) continue;
        uint16_t addr = ram[3];
        ram[7] = addr;
        const int bottom = ram[0] >> 8;
        const int top = ram[0] & 0xff;
        const bool hide = (ram[2] & 0x4000) != 0;
        const int bank = video.sprite_bank[(ram[4] >> 8) & 0xf];
        if (hide || top >= bottom || bank == 255) continue;
        const int xpos = int(ram[1] & 0x1ff) - 0xb7;
        const int pitch = int8_t(ram[2] & 0xff);
        const int color = (ram[4] & 0x3f) << 4;
        const bool flip = (ram[2] & 0x100) != 0;
        const uint8_t vzoom = uint8_t((ram[5] >> 5) & 0x1f);
        const uint8_t hzoom = uint8_t(ram[5] & 0x1f);
        const uint32_t spritedata = uint32_t(0x10000 * (bank % std::max(banks, 1)));
        ram[5] &= 0x3ff;
        for (int y = top; y < bottom; y++) {
            addr = uint16_t(addr + pitch);
            ram[5] = uint16_t(ram[5] + (uint16_t(vzoom) << 10));
            if (ram[5] & 0x8000) {
                addr = uint16_t(addr + pitch);
                ram[5] &= 0x7fff;
            }
            if (y >= 256) continue;
            uint16_t xacc = uint16_t(4 * hzoom);
            uint16_t data_7 = addr;
            int x = xpos;
            if (!flip) {
                while (x < 512) {
                    const uint16_t pixels =
                        sprite_rom[(spritedata + data_7) % sprite_rom.size()];
                    for (int g = 3; g >= 0; g--) {
                        xacc = uint16_t((xacc & 0x3f) + hzoom);
                        if (xacc < 0x40) {
                            const uint16_t pix = uint16_t(((pixels >> (g * 4)) & 0xf) | color);
                            const int pen = pix & 0xf;
                            if (pen != 0 && pen != 15) {
                                const bool is_shadow = (pix & 0x3f0) == 0x3f0;
                                put_sprite_pixel(dest, x, y, video.palette[size_t((pix & 0x3ff) + 0x400)],
                                                 shadow, is_shadow);
                            }
                            x++;
                        }
                    }
                    if ((pixels & 0xf) == 15) {
                        ram[7] = data_7;
                        break;
                    }
                    data_7++;
                }
            } else {
                while (x < 512) {
                    const uint16_t pixels =
                        sprite_rom[(spritedata + data_7) % sprite_rom.size()];
                    for (int g = 0; g < 4; g++) {
                        xacc = uint16_t((xacc & 0x3f) + hzoom);
                        if (xacc < 0x40) {
                            const uint16_t pix = uint16_t(((pixels >> (g * 4)) & 0xf) | color);
                            const int pen = pix & 0xf;
                            if (pen != 0 && pen != 15) {
                                const bool is_shadow = (pix & 0x3f0) == 0x3f0;
                                put_sprite_pixel(dest, x, y, video.palette[size_t((pix & 0x3ff) + 0x400)],
                                                 shadow, is_shadow);
                            }
                            x++;
                        }
                    }
                    if (((pixels >> 12) & 0xf) == 15) {
                        ram[7] = data_7;
                        break;
                    }
                    data_7--;
                }
            }
        }
    }
}

void draw_sprites_outrun(Sega16Video& video, uint32_t* dest, const std::vector<uint32_t>& sprite_rom,
                         int banks, int pri, uint32_t shadow_index) {
    const uint32_t shadow = video.palette[shadow_index];
    for (int f = 0; f < 0x100; f++) {
        uint16_t* ram = &video.sprite_ram[size_t(f * 8)];
        if (ram[0] & 0x8000) return;
        const int sprpri = (ram[3] >> 12) & 3;
        if (sprpri != pri) continue;
        uint16_t addr = ram[1];
        ram[7] = addr;
        if (ram[0] & 0x5000) continue;
        const int top = int(ram[0] & 0x1ff) - 0x100;
        const int bank = ((ram[0] >> 9) & 7) % std::max(banks, 1);
        int xpos = ram[2] & 0x1ff;
        const int xdelta = (ram[4] & 0x2000) ? 1 : -1;
        if (xpos < 0x80 && xdelta < 0) xpos += 0x149;
        else xpos -= 0xb7;
        uint16_t vzoom = ram[3] & 0x7ff;
        uint16_t hzoom = ram[4] & 0x7ff;
        if (vzoom < 0x40) vzoom = 0x40;
        if (hzoom < 0x40) hzoom = 0x40;
        const uint16_t color = uint16_t(((ram[5] & 0x7f) << 4) | (ram[3] & 0x4000));
        const uint32_t spritedata = uint32_t(0x10000 * bank);
        const bool flip = ((~ram[4] >> 14) & 1) != 0;
        const int pitch =
            int16_t((ram[2] >> 1) | ((ram[4] & 0x1000) << 3)) / 256;
        const int height = (ram[5] >> 8) + 1;
        const int ydelta = (ram[4] & 0x8000) ? 1 : -1;
        int yacc = 0;
        int y = top;
        const int ytarget = top + ydelta * height;
        while (y != ytarget) {
            if (y >= 0 && y < 256) {
                int xacc = 0;
                uint16_t data_7 = addr;
                int x = xpos;
                if (!flip) {
                    while ((xdelta > 0 && x < 512) || (xdelta < 0 && x >= 0)) {
                        const uint32_t pixels =
                            sprite_rom[(spritedata + data_7) % sprite_rom.size()];
                        for (int g = 7; g >= 0; g--) {
                            const uint16_t pix = uint16_t(((pixels >> (g * 4)) & 0xf) | color);
                            while (xacc < 0x200) {
                                const int pen = pix & 0xf;
                                if (pen != 0 && pen != 15) {
                                    const bool is_shadow = (pix & 0x400f) == 0x400a;
                                    put_sprite_pixel(dest, x, y,
                                                     video.palette[size_t((pix & 0x7ff) + 0x800)],
                                                     shadow, is_shadow);
                                }
                                x += xdelta;
                                xacc += hzoom;
                            }
                            xacc -= 0x200;
                        }
                        if ((pixels & 0xf0) == 0xf0) {
                            ram[7] = data_7;
                            break;
                        }
                        data_7++;
                    }
                } else {
                    while ((xdelta > 0 && x < 512) || (xdelta < 0 && x >= 0)) {
                        const uint32_t pixels =
                            sprite_rom[(spritedata + data_7) % sprite_rom.size()];
                        for (int g = 0; g < 8; g++) {
                            const uint16_t pix = uint16_t(((pixels >> (g * 4)) & 0xf) | color);
                            while (xacc < 0x200) {
                                const int pen = pix & 0xf;
                                if (pen != 0 && pen != 15) {
                                    const bool is_shadow = (pix & 0x400f) == 0x400a;
                                    put_sprite_pixel(dest, x, y,
                                                     video.palette[size_t((pix & 0x7ff) + 0x800)],
                                                     shadow, is_shadow);
                                }
                                x += xdelta;
                                xacc += hzoom;
                            }
                            xacc -= 0x200;
                        }
                        if ((pixels & 0x0f000000) == 0x0f000000) {
                            ram[7] = data_7;
                            break;
                        }
                        data_7--;
                    }
                }
                yacc += vzoom;
                addr = uint16_t(addr + pitch * (yacc >> 9));
                yacc &= 0x1ff;
            }
            y += ydelta;
        }
    }
}

void decode_outrun_road(std::vector<uint8_t>& road_gfx, const std::vector<uint8_t>& rom) {
    const uint32_t len = 0x8000 * 2;
    road_gfx.assign(256 * 2 * 512 + 512, 3);
    for (int y = 0; y < 512; y++) {
        const uint32_t src = uint32_t(((y & 0xff) * 0x40 + (y >> 8) * 0x8000) % len);
        const uint32_t dst = uint32_t(y * 512);
        for (int x = 0; x < 512; x++) {
            const uint8_t a = (rom[(src + uint32_t(x / 8)) % rom.size()] >> ((~x) & 7)) & 1;
            const uint8_t b =
                (rom[(src + uint32_t(x / 8) + 0x4000) % rom.size()] >> ((~x) & 7)) & 1;
            uint8_t pix = uint8_t(a | (b << 1));
            if (x >= 256 - 8 && x < 256 && pix == 3) pix |= 4;
            road_gfx[dst + uint32_t(x)] = pix;
        }
    }
    std::fill(road_gfx.begin() + 256 * 2 * 512, road_gfx.begin() + 256 * 2 * 512 + 512, uint8_t(3));
}

void decode_hangon_road(std::vector<uint8_t>& road_gfx, const std::vector<uint8_t>& rom) {
    road_gfx.assign(256 * 512, 0);
    for (int y = 0; y < 256; y++) {
        const uint32_t src = uint32_t((y * 0x40) % 0x8000);
        const uint32_t dst = uint32_t(y * 512);
        for (int x = 0; x < 512; x++) {
            const uint8_t a = (rom[(src + uint32_t(x >> 3)) % rom.size()] >> ((~x) & 7)) & 1;
            const uint8_t b =
                (rom[(src + uint32_t(x >> 3) + 0x4000) % rom.size()] >> ((~x) & 7)) & 1;
            road_gfx[dst + uint32_t(x)] = uint8_t(a | (b << 1));
        }
    }
}

void draw_outrun_road(uint32_t* dest, const uint32_t* palette, const uint16_t* buffer,
                      const uint8_t* road_gfx, uint8_t control, uint16_t colorbase1,
                      uint16_t colorbase2, uint16_t colorbase3, uint16_t xoff, int pri) {
    static const uint8_t kPriorityMap[2][8] = {
        {0x80, 0x81, 0x81, 0x87, 0, 0, 0, 0},
        {0x81, 0x81, 0x81, 0x8f, 0, 0, 0, 0x80},
    };
    for (int y = 0; y < 224; y++) {
        const uint16_t data0 = buffer[y];
        const uint16_t data1 = buffer[0x100 + y];
        if (pri == 0) {
            uint16_t color0 = 0xff;
            switch (control & 3) {
                case 0:
                    if (data0 & 0x800) color0 = data0 & 0x7f;
                    break;
                case 1:
                    if (data0 & 0x800) color0 = data0 & 0x7f;
                    else if (data1 & 0x800) color0 = data1 & 0x7f;
                    break;
                case 2:
                    if (data1 & 0x800) color0 = data1 & 0x7f;
                    else if (data0 & 0x800) color0 = data0 & 0x7f;
                    break;
                case 3:
                    if (data1 & 0x800) color0 = data1 & 0x7f;
                    break;
            }
            const uint32_t fill =
                (color0 != 0xff) ? palette[color0 | colorbase3] : palette[0x2000];
            for (int x = 0; x < 320; x++) dest[y * 320 + x] = fill;
            continue;
        }
        if ((data0 & 0x800) && (data1 & 0x800)) continue;
        uint32_t src0 = (data0 & 0x800) ? uint32_t(256 * 2 * 512)
                                        : uint32_t(((data0 >> 1) & 0xff) * 512);
        int hpos0 = int(((control & 4) ? buffer[0x200 + y] : buffer[0x200 + (data0 & 0x1ff)]) & 0xfff);
        uint16_t color0 = (control & 4) ? buffer[0x600 + y] : buffer[0x600 + (data0 & 0x1ff)];
        uint32_t src1 = (data1 & 0x800) ? uint32_t(256 * 2 * 512)
                                        : uint32_t((0x100 + ((data1 >> 1) & 0xff)) * 512);
        int hpos1 = int(((control & 4) ? buffer[0x400 + (0x100 + y)] : buffer[0x400 + (data1 & 0x1ff)]) &
                        0xfff);
        uint16_t color1 = (control & 4) ? buffer[0x600 + (0x100 + y)] : buffer[0x600 + (data1 & 0x1ff)];
        uint16_t table[32] = {};
        table[0x00] = uint16_t(colorbase1 ^ 0x00 ^ ((color0 >> 0) & 1));
        table[0x01] = uint16_t(colorbase1 ^ 0x02 ^ ((color0 >> 1) & 1));
        table[0x02] = uint16_t(colorbase1 ^ 0x04 ^ ((color0 >> 2) & 1));
        const uint8_t bg0 = uint8_t((color0 >> 8) & 0xf);
        table[0x03] = (data0 & 0x200) ? table[0x00] : uint16_t(colorbase2 ^ 0x00 ^ bg0);
        table[0x07] = uint16_t(colorbase1 ^ 0x06 ^ ((color0 >> 3) & 1));
        table[0x10] = uint16_t(colorbase1 ^ 0x08 ^ ((color1 >> 4) & 1));
        table[0x11] = uint16_t(colorbase1 ^ 0x0a ^ ((color1 >> 5) & 1));
        table[0x12] = uint16_t(colorbase1 ^ 0x0c ^ ((color1 >> 6) & 1));
        const uint8_t bg1 = uint8_t((color1 >> 8) & 0xf);
        table[0x13] = (data1 & 0x200) ? table[0x10] : uint16_t(colorbase2 ^ 0x10 ^ bg1);
        table[0x17] = uint16_t(colorbase1 ^ 0x0e ^ ((color1 >> 7) & 1));
        switch (control & 3) {
            case 0:
                if (data0 & 0x800) break;
                hpos0 = (hpos0 - (0x5f8 + xoff)) & 0xfff;
                for (int x = 0; x < 320; x++) {
                    const uint8_t pix0 = (hpos0 < 0x200) ? road_gfx[src0 + uint32_t(hpos0)] : 3;
                    dest[y * 320 + x] = palette[table[pix0]];
                    hpos0 = (hpos0 + 1) & 0xfff;
                }
                break;
            case 1:
            case 2: {
                hpos0 = (hpos0 - (0x5f8 + xoff)) & 0xfff;
                hpos1 = (hpos1 - (0x5f8 + xoff)) & 0xfff;
                const int map = (control & 3) == 1 ? 0 : 1;
                for (int x = 0; x < 320; x++) {
                    const uint8_t pix0 = (hpos0 < 0x200) ? road_gfx[src0 + uint32_t(hpos0)] : 3;
                    const uint8_t pix1 = (hpos1 < 0x200) ? road_gfx[src1 + uint32_t(hpos1)] : 3;
                    if ((kPriorityMap[map][pix0] >> pix1) & 1) {
                        dest[y * 320 + x] = palette[table[0x10 + pix1]];
                    } else {
                        dest[y * 320 + x] = palette[table[pix0]];
                    }
                    hpos0 = (hpos0 + 1) & 0xfff;
                    hpos1 = (hpos1 + 1) & 0xfff;
                }
                break;
            }
            case 3:
                if (data1 & 0x800) break;
                hpos1 = (hpos1 - (0x5f8 + xoff)) & 0xfff;
                for (int x = 0; x < 320; x++) {
                    const uint8_t pix1 = (hpos1 < 0x200) ? road_gfx[src1 + uint32_t(hpos1)] : 3;
                    dest[y * 320 + x] = palette[table[0x10 + pix1]];
                    hpos1 = (hpos1 + 1) & 0xfff;
                }
                break;
        }
    }
}

void draw_hangon_road(uint32_t* dest, const uint32_t* palette, const uint16_t* road_ram,
                      const uint8_t* road_gfx, uint16_t colorbase1, uint16_t colorbase2, int pri,
                      bool sharrier) {
    for (int y = 0; y < 224; y++) {
        const uint16_t control = road_ram[y];
        const bool plycont = ((control >> 10) & 3) != 0;
        if (!plycont && pri != 0) continue;
        if (plycont && pri == 0) continue;
        const uint16_t hpos = road_ram[0x100 + (control & 0xff)];
        const uint16_t color0 = road_ram[0x200 + (control & 0xff)];
        const uint16_t color1 = road_ram[0x300 + (control & 0xff)];
        const uint32_t src = uint32_t(control & 0xff) * 512;
        uint8_t ctr9m = uint8_t(hpos & 7);
        uint8_t ctr9n9p = uint8_t(hpos >> 3);
        bool ff9j1 = ((hpos >> 11) & 1) != 0;
        bool ff9j2 = true;
        uint8_t ss8j = 0;
        for (int x = -24; x <= 511; x++) {
            const bool ctr9n9p_ena = (ctr9m == 7);
            if (ctr9n9p == 0xff) ff9j1 = false;
            if ((control & 0x100) == 0) ff9j1 = true;
            if (!sharrier && (control & 0x200) == 0) ff9j2 = true;
            uint8_t md = 3;
            if ((!sharrier || (control & 0x200) == 0) && (ctr9n9p & 0xc0) == 0xc0) {
                const uint32_t index =
                    src + uint32_t(((ctr9n9p & 0x3f) << 3) | ((ss8j & 1) ? ctr9m : (ctr9m ^ 7)));
                md = road_gfx[index];
            }
            const uint8_t select = uint8_t((ss8j >> 3) & 1);
            uint16_t color;
            if (ff9j2 && md == 3) {
                color = select ? uint16_t((color0 & 0x3f) | colorbase2)
                               : uint16_t(((color0 >> 8) & 0x3f) | colorbase2);
            } else {
                if ((color1 & 0x80) && md == 3) md = 0;
                color = uint16_t((color1 >> ((md << 1) | select)) & 1);
                color = uint16_t(color | (select << 3) | (md << 1) | colorbase1);
            }
            if (x >= 0 && x < 320) dest[y * 320 + x] = palette[color];
            ctr9m = uint8_t((ctr9m + 1) & 7);
            if (ctr9n9p_ena) {
                if (ff9j1) ctr9n9p++;
                else ctr9n9p--;
            }
            ff9j2 = !((!ff9j1) && ((ss8j & 0x80) != 0));
            ss8j = uint8_t((ss8j << 1) | uint8_t(ff9j1));
        }
    }
}

}  // namespace dsp
