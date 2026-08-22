#include "drivers/galaxian.h"

#include "core/rom_loader.h"

#include <algorithm>
#include <cstring>

namespace dsp {
namespace {

// ROM sets from galaxian_hw.pas (Galaxian Midway)
const std::vector<RomEntry> kCpuRoms = {
    {"galmidw.u", 0x0800, 0x0000, 0x745e2d61},
    {"galmidw.v", 0x0800, 0x0800, 0x9c999a40},
    {"galmidw.w", 0x0800, 0x1000, 0xb5894925},
    {"galmidw.y", 0x0800, 0x1800, 0x6b3ca10b},
    {"7l", 0x0800, 0x2000, 0x1b933207},
};

const std::vector<RomEntry> kGfxRoms = {
    {"1h.bin", 0x0800, 0x0000, 0x39fb43a4},
    {"1k.bin", 0x0800, 0x0800, 0x7e3f56a2},
};

const std::vector<RomEntry> kPalRoms = {
    {"6l.bpr", 0x0020, 0x0000, 0xc3ac9467},
};

// 8x8, 2bpp, plane 0 then plane 1 at +n*8*8 bits (n=total chars).
// convert_gfx(..., true, false) → rotate 90° CW for vertical monitor.
GfxLayout char_layout(int total) {
    GfxLayout layout;
    layout.width = 8;
    layout.height = 8;
    layout.total = total;
    layout.planes = 2;
    layout.char_increment = 8 * 8;
    layout.rotate_cw = true;
    layout.plane_offsets = {0, total * 8 * 8};
    layout.x_offsets = {0, 1, 2, 3, 4, 5, 6, 7};
    layout.y_offsets = {0 * 8, 1 * 8, 2 * 8, 3 * 8, 4 * 8, 5 * 8, 6 * 8, 7 * 8};
    return layout;
}

GfxLayout sprite_layout(int total) {
    GfxLayout layout;
    layout.width = 16;
    layout.height = 16;
    layout.total = total;
    layout.planes = 2;
    layout.char_increment = 16 * 16;
    layout.rotate_cw = true;
    layout.plane_offsets = {0, total * 16 * 16};
    // Same bit order as chars, extended to 16×16 (ps_x / ps_y in Pascal).
    layout.x_offsets = {0, 1, 2, 3, 4, 5, 6, 7, 8 * 8 + 0, 8 * 8 + 1, 8 * 8 + 2, 8 * 8 + 3,
                        8 * 8 + 4, 8 * 8 + 5, 8 * 8 + 6, 8 * 8 + 7};
    layout.y_offsets = {0 * 8,  1 * 8,  2 * 8,  3 * 8,  4 * 8,  5 * 8,  6 * 8,  7 * 8,
                        16 * 8, 17 * 8, 18 * 8, 19 * 8, 20 * 8, 21 * 8, 22 * 8, 23 * 8};
    return layout;
}

}  // namespace

Galaxian::Galaxian()
    : cpu_(kCpuClock),
      framebuffer_(size_t(kScreenWidth * kScreenHeight), 0xff000000u) {
    cpu_.set_memory_handlers(
        [this](uint16_t a) { return read_byte(a); },
        [this](uint16_t a, uint8_t v) { write_byte(a, v); });
    cpu_.set_cycle_handler([this](int cycles) { on_cycles(cycles); });
}

bool Galaxian::load_roms(const std::string& rom_path, std::string* error) {
    RomLoader loader;
    if (!loader.open(rom_path, error)) return false;

    std::vector<uint8_t> cpu_rom(0x4000, 0);
    if (!loader.load(kCpuRoms, cpu_rom, error)) return false;
    std::copy(cpu_rom.begin(), cpu_rom.begin() + 0x2800, memory_.begin());

    std::vector<uint8_t> gfx_rom(0x1000, 0);
    if (!loader.load(kGfxRoms, gfx_rom, error)) return false;
    decode_graphics(gfx_rom);

    std::vector<uint8_t> pal(0x20, 0);
    if (!loader.load(kPalRoms, pal, error)) return false;
    build_palette(pal);

    warnings_ = loader.warnings();
    return true;
}

bool Galaxian::init(const std::string& rom_path, std::string* error) {
    if (!load_roms(rom_path, error)) return false;
    reset();
    return true;
}

void Galaxian::decode_graphics(const std::vector<uint8_t>& gfx_rom) {
    // Galaxian: $100 chars, $40 sprites (from same ROM plane layout as Pascal).
    chars_.decode(char_layout(0x100), gfx_rom);
    sprites_gfx_.decode(sprite_layout(0x40), gfx_rom);
}

void Galaxian::build_palette(const std::vector<uint8_t>& prom) {
    // PROM: bit0-2 R, 3-5 G, 6-7 B (same as MAME galaxian / Pascal).
    const std::vector<int> resistances_rg = {1000, 470, 220};
    const std::vector<int> resistances_b = {470, 220};
    auto weights = compute_resistor_weights(
        0, 255, -1.0,
        {{resistances_rg, 470, 0}, {resistances_rg, 470, 0}, {resistances_b, 470, 0}});

    palette_.fill(0xff000000u);
    for (size_t i = 0; i < 32 && i < prom.size(); ++i) {
        const uint8_t d = prom[i];
        const int r = combine_weights(weights[0], {(d >> 0) & 1, (d >> 1) & 1, (d >> 2) & 1});
        const int g = combine_weights(weights[1], {(d >> 3) & 1, (d >> 4) & 1, (d >> 5) & 1});
        const int b = combine_weights(weights[2], {(d >> 6) & 1, (d >> 7) & 1});
        palette_[i] = 0xff000000u | (uint32_t(r) << 16) | (uint32_t(g) << 8) | uint32_t(b);
    }
    // Bullet colours (indices 32/33 used by Pascal galaxian_draw_bullet).
    palette_[32] = 0xffffffffu;
    palette_[33] = 0xffffff00u;
    // Background fill colour ~ index 150 in full palette tables; approximate black-blue.
    palette_[35] = 0xff000010u;
}

void Galaxian::reset() {
    cpu_.reset();
    nmi_enable_ = false;
    stars_enable_ = false;
    stars_scroll_ = 0;
    in0_ = 0;
    in1_ = 0;
    videoram_.fill(0);
    attributes_.fill(0);
    sprites_.fill(0);
    bullets_.fill(0);
    dirty_.fill(true);
    tilemap_.fill(0xff000000u);
    composite_.fill(0xff000000u);
    audio_accumulator_ = 0;
    audio_.clear();
    cpu_.set_nmi(IrqLine::Clear);
}

void Galaxian::set_inputs(const MachineInputs& inputs) {
    // Active-high as in eventos_galaxian (OR into in0/in1).
    in0_ = 0;
    in1_ = 0;
    if (inputs.coin1) in0_ |= 0x01;
    if (inputs.coin2) in0_ |= 0x02;
    if (inputs.player1.left) in0_ |= 0x04;
    if (inputs.player1.right) in0_ |= 0x08;
    if (inputs.player1.button1) in0_ |= 0x10;
    if (inputs.player1.start) in1_ |= 0x01;
    if (inputs.player2.start) in1_ |= 0x02;
}

void Galaxian::set_dip_switch(int bank, uint8_t value) {
    if (bank == 0) dsw_a_ = value;
    else if (bank == 1) dsw_b_ = value;
    else if (bank == 2) dsw_c_ = value;
}

uint8_t Galaxian::read_byte(uint16_t address) {
    if (address <= 0x3fff) return memory_[address];
    if (address >= 0x4000 && address <= 0x47ff)
        return memory_[0x4000 + (address & 0x3ff)];
    if (address >= 0x5000 && address <= 0x57ff)
        return videoram_[address & 0x3ff];
    if (address >= 0x5800 && address <= 0x5fff) {
        const uint8_t off = uint8_t(address & 0xff);
        if (off <= 0x3f) return attributes_[off];
        if (off <= 0x5f) return sprites_[off & 0x1f];
        if (off <= 0x7f) return bullets_[off & 0x1f];
        return memory_[0x5800 + off];
    }
    if (address >= 0x6000 && address <= 0x67ff) return uint8_t(in0_ | dsw_a_);
    if (address >= 0x6800 && address <= 0x6fff) return uint8_t(in1_ | dsw_b_);
    if (address >= 0x7000 && address <= 0x77ff) return dsw_c_;
    return 0xff;
}

void Galaxian::write_byte(uint16_t address, uint8_t value) {
    if (address <= 0x3fff) return;  // ROM
    if (address >= 0x4000 && address <= 0x47ff) {
        memory_[0x4000 + (address & 0x3ff)] = value;
        return;
    }
    if (address >= 0x5000 && address <= 0x57ff) {
        const int off = address & 0x3ff;
        if (videoram_[size_t(off)] != value) {
            videoram_[size_t(off)] = value;
            dirty_[size_t(off)] = true;
        }
        return;
    }
    if (address >= 0x5800 && address <= 0x5fff) {
        const uint8_t off = uint8_t(address & 0xff);
        if (off <= 0x3f) {
            if (attributes_[off] != value) {
                attributes_[off] = value;
                const int col = off >> 1;
                for (int row = 0; row < 32; ++row)
                    dirty_[size_t(col + (row << 5))] = true;
            }
            return;
        }
        if (off <= 0x5f) {
            sprites_[off & 0x1f] = value;
            return;
        }
        if (off <= 0x7f) {
            bullets_[off & 0x1f] = value;
            return;
        }
        memory_[0x5800 + off] = value;
        return;
    }
    // $6800..$6fff: discrete sample triggers (stub)
    if (address >= 0x6800 && address <= 0x6fff) {
        return;
    }
    if (address >= 0x7000 && address <= 0x77ff) {
        switch (address & 7) {
            case 1:
                nmi_enable_ = (value & 1) != 0;
                if (!nmi_enable_) cpu_.set_nmi(IrqLine::Clear);
                break;
            case 4:
                stars_enable_ = (value & 1) != 0;
                break;
            default:
                break;
        }
        return;
    }
}

void Galaxian::on_cycles(int cycles) {
    audio_accumulator_ += int64_t(cycles) * 44100;
    while (audio_accumulator_ >= kCpuClock) {
        audio_accumulator_ -= kCpuClock;
        audio_.push_back(0);
    }
}

void Galaxian::draw_tile(int offset) {
    // f div 32 → column (rotated), f mod 32 → row
    const int tile_x = 31 - (offset / 32);
    const int tile_y = offset % 32;
    const int color = (attributes_[1 + (tile_y << 1)] & 7) << 2;
    const int scroll = (tile_x * 8) + attributes_[tile_y << 1];
    const int code = videoram_[size_t(offset)];

    const uint8_t* pixels = chars_.element(code);
    for (int y = 0; y < 8; ++y) {
        const int sy = tile_y * 8 + y;
        if (sy < 0 || sy >= 256) continue;
        for (int x = 0; x < 8; ++x) {
            const int sx = (scroll + x) & 0xff;
            const uint8_t pix = pixels[y * 8 + x];
            // Transparent colour 0
            if (pix == 0) {
                tilemap_[size_t(sy * 256 + sx)] = 0;  // mark empty for stars under
            } else {
                tilemap_[size_t(sy * 256 + sx)] = palette_[size_t(pix + color)];
            }
        }
    }
}

void Galaxian::draw_sprite(int index) {
    const uint8_t* e = &sprites_[index * 4];
    const int y = int(e[3]) + 1;
    if (y < 16) return;
    const uint8_t attr = e[1];
    const int code = attr & 0x3f;
    const bool flipx = (attr & 0x80) != 0;
    const bool flipy = (attr & 0x40) != 0;
    const int color = (e[2] & 7) << 2;
    const int x = e[0];

    const uint8_t* pixels = sprites_gfx_.element(code);
    for (int py = 0; py < 16; ++py) {
        const int sy = y + py;
        if (sy < 0 || sy >= 256) continue;
        const int src_y = flipy ? (15 - py) : py;
        for (int px = 0; px < 16; ++px) {
            const int src_x = flipx ? (15 - px) : px;
            const uint8_t pix = pixels[src_y * 16 + src_x];
            if (pix == 0) continue;
            const int sx = (x + px) & 0xff;
            composite_[size_t(sy * 256 + sx)] = palette_[size_t(pix + color)];
        }
    }
}

void Galaxian::draw_bullets() {
    for (int f = 0; f < 8; ++f) {
        int y = 250 - int(bullets_[3 + f * 4]);
        if (f > 2) y += 1;
        const uint32_t color = palette_[f == 7 ? 33 : 32];
        const int x = bullets_[1 + f * 4];
        for (int dy = 0; dy < 4; ++dy) {
            const int sy = y + dy;
            if (sy < 0 || sy >= 256) continue;
            composite_[size_t(sy * 256 + (x & 0xff))] = color;
        }
    }
}

void Galaxian::draw_stars() {
    if (!stars_enable_) return;
    // Simplified star field (full galaxian_stars is a separate unit in Pascal).
    uint32_t rng = 0x12345678u ^ stars_scroll_;
    for (int n = 0; n < 64; ++n) {
        rng = rng * 1664525u + 1013904223u;
        const int x = int((rng >> 8) + stars_scroll_) & 0xff;
        const int y = int(rng >> 16) & 0xff;
        if (composite_[size_t(y * 256 + x)] == 0 ||
            (composite_[size_t(y * 256 + x)] & 0x00ffffffu) == 0) {
            const int bright = (rng >> 24) & 3;
            const uint8_t c = uint8_t(0x40 + bright * 0x3f);
            composite_[size_t(y * 256 + x)] = 0xff000000u | (uint32_t(c) << 16) |
                                              (uint32_t(c) << 8) | uint32_t(c);
        }
    }
}

void Galaxian::update_video() {
    // Clear to black (background_type 0 → colour 150 ≈ black in full system).
    tilemap_.fill(0xff000000u);

    for (int offset = 0; offset < 0x400; ++offset) {
        if (!dirty_[size_t(offset)]) continue;
        draw_tile(offset);
        dirty_[size_t(offset)] = false;
    }

    // Full redraw each frame for column scroll (attributes change scroll only).
    for (int offset = 0; offset < 0x400; ++offset) draw_tile(offset);

    composite_ = tilemap_;
    draw_stars();
    draw_bullets();
    for (int i = 7; i >= 0; --i) draw_sprite(i);

    // Visible 224×256 from x=16 of the 256-wide work surface (Pascal final blit).
    for (int y = 0; y < kScreenHeight; ++y) {
        const uint32_t* src = &composite_[size_t(y * 256 + 16)];
        uint32_t* dst = &framebuffer_[size_t(y * kScreenWidth)];
        std::copy(src, src + kScreenWidth, dst);
    }

    ++stars_scroll_;
}

void Galaxian::run_frame() {
    const int cycles_per_line =
        int(double(kCpuClock) / (kFramesPerSecond * kScanlines) + 0.5);
    for (int line = 0; line < kScanlines; ++line) {
        if (line == 248 && nmi_enable_) {
            cpu_.set_nmi(IrqLine::Assert);
        }
        cpu_.run(cycles_per_line);
        if (line == 248) {
            cpu_.set_nmi(IrqLine::Clear);
        }
    }
    update_video();
}

void Galaxian::drain_audio(std::vector<int16_t>& out) {
    out.insert(out.end(), audio_.begin(), audio_.end());
    audio_.clear();
}

}  // namespace dsp
