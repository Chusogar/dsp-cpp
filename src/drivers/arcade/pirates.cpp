#include "drivers/arcade/pirates.h"

#include <algorithm>
#include <cstring>
#include <initializer_list>

#include "core/rom_loader.h"

namespace dsp {
namespace {

uint32_t pal5bit(uint16_t n) {
    n &= 0x1f;
    return uint32_t(n) * 255 / 31;
}

uint32_t argb(uint8_t r, uint8_t g, uint8_t b) {
    return 0xff000000u | (uint32_t(r) << 16) | (uint32_t(g) << 8) | b;
}

// Generic N->N bit permutation, source bit positions listed MSB first for the
// destination, mirroring the BITSWAPxx() macros in pirates_hw.pas.
uint32_t bitswap(uint32_t value, std::initializer_list<int> bits) {
    uint32_t out = 0;
    int shift = int(bits.size()) - 1;
    for (int b : bits) {
        out |= ((value >> b) & 1u) << shift;
        --shift;
    }
    return out;
}

uint8_t bitswap8(uint8_t value, std::initializer_list<int> bits) {
    return uint8_t(bitswap(value, bits));
}

uint32_t bitswap24(uint32_t value, std::initializer_list<int> bits) {
    return bitswap(value, bits);
}

// 68000 program ROMs: word wide, low/high byte halves use different address
// scrambles and different data bit swaps (decr_and_load_rom).
uint32_t program_addr_low(uint32_t f) {
    return bitswap24(f, {23, 22, 21, 20, 19, 18, 4, 8, 3, 14, 2, 15, 17, 0, 9, 13, 10, 5, 16, 7, 12, 6, 1, 11});
}
uint32_t program_addr_high(uint32_t f) {
    return bitswap24(f, {23, 22, 21, 20, 19, 18, 4, 10, 1, 11, 12, 5, 9, 17, 14, 0, 13, 6, 15, 8, 3, 16, 7, 2});
}
uint8_t program_data_low(uint8_t v) { return bitswap8(v, {4, 2, 7, 1, 6, 5, 0, 3}); }
uint8_t program_data_high(uint8_t v) { return bitswap8(v, {1, 4, 7, 0, 3, 5, 6, 2}); }

// OKI M6295 sample ROM (decr_and_load_oki). Tiles use a different swap.
uint32_t oki_addr(uint32_t f) {
    return bitswap24(f, {23, 22, 21, 20, 19, 10, 16, 13, 8, 4, 7, 11, 14, 17, 12, 6, 2, 0, 5, 18, 15, 3, 1, 9});
}
uint8_t oki_data(uint8_t v) { return bitswap8(v, {2, 3, 4, 0, 7, 5, 1, 6}); }

// Tile ROMs (decr_and_load_gfx): one 27C040-style plane per chip, always at
// $80000 spacing even when Genix only fills the first 256 KB of each plane.
uint32_t tile_addr(uint32_t f) {
    return bitswap24(f, {23, 22, 21, 20, 19, 18, 10, 2, 5, 9, 7, 13, 16, 14, 11, 4, 1, 6, 12, 17, 3, 0, 15, 8});
}
uint8_t tile_data_plane0(uint8_t v) { return bitswap8(v, {2, 3, 4, 0, 7, 5, 1, 6}); }
uint8_t tile_data_plane1(uint8_t v) { return bitswap8(v, {4, 2, 7, 1, 6, 5, 0, 3}); }
uint8_t tile_data_plane2(uint8_t v) { return bitswap8(v, {1, 4, 7, 0, 3, 5, 6, 2}); }
uint8_t tile_data_plane3(uint8_t v) { return bitswap8(v, {2, 3, 4, 0, 7, 5, 1, 6}); }

uint32_t sprite_addr(uint32_t f) {
    return bitswap24(f, {23, 22, 21, 20, 19, 18, 17, 5, 12, 14, 8, 3, 0, 7, 9, 16, 4, 2, 6, 11, 13, 1, 10, 15});
}
uint8_t sprite_data_plane0(uint8_t v) { return bitswap8(v, {4, 2, 7, 1, 6, 5, 0, 3}); }
uint8_t sprite_data_plane1(uint8_t v) { return bitswap8(v, {1, 4, 7, 0, 3, 5, 6, 2}); }
uint8_t sprite_data_plane2(uint8_t v) { return bitswap8(v, {2, 3, 4, 0, 7, 5, 1, 6}); }
uint8_t sprite_data_plane3(uint8_t v) { return bitswap8(v, {4, 2, 7, 1, 6, 5, 0, 3}); }

constexpr int kPlaneSize = 0x80000;

const std::vector<RomEntry> kPiratesProgramLow = {{"r_449b.bin", 0x80000, 0, 0x224aeeda}};
const std::vector<RomEntry> kPiratesProgramHigh = {{"l_5c1e.bin", 0x80000, 0, 0x46740204}};
const std::vector<RomEntry> kPiratesGfx = {
    {"p4_4d48.bin", 0x80000, 0 * kPlaneSize, 0x89fda216},
    {"p2_5d74.bin", 0x80000, 1 * kPlaneSize, 0x40e069b4},
    {"p1_7b30.bin", 0x80000, 2 * kPlaneSize, 0x26d78518},
    {"p8_9f4f.bin", 0x80000, 3 * kPlaneSize, 0xf31696ea},
};
const std::vector<RomEntry> kPiratesSprites = {
    {"s1_6e89.bin", 0x80000, 0 * kPlaneSize, 0xc78a276f},
    {"s2_6df3.bin", 0x80000, 1 * kPlaneSize, 0x9f0bad96},
    {"s4_fdcc.bin", 0x80000, 2 * kPlaneSize, 0x8916ddb5},
    {"s8_4b7c.bin", 0x80000, 3 * kPlaneSize, 0x1c41bd2c},
};
const std::vector<RomEntry> kPiratesOki = {{"s89_49d4.bin", 0x80000, 0, 0x63a739ec}};

const std::vector<RomEntry> kGenixProgramLow = {{"11.u15.15c", 0x80000, 0, 0xd26abfb0}};
const std::vector<RomEntry> kGenixProgramHigh = {{"12.u16.16c", 0x80000, 0, 0xa14a25b4}};
const std::vector<RomEntry> kGenixGfx = {
    {"17.u34.12g", 0x40000, 0 * kPlaneSize, 0x58da8aac},
    {"19.u35.12h", 0x40000, 1 * kPlaneSize, 0x96bad9a8},
    {"18.u48.13g", 0x40000, 2 * kPlaneSize, 0x0ddc58b6},
    {"20.u49.13h", 0x40000, 3 * kPlaneSize, 0x2be308c5},
};
const std::vector<RomEntry> kGenixSprites = {
    {"16.u69.6g", 0x40000, 0 * kPlaneSize, 0xb8422af7},
    {"15.u70.4g", 0x40000, 1 * kPlaneSize, 0xe46125c5},
    {"14.u71.3g", 0x40000, 2 * kPlaneSize, 0x7a8ed21b},
    {"13.u72.1g", 0x40000, 3 * kPlaneSize, 0xf78bd6ca},
};
const std::vector<RomEntry> kGenixOki = {{"10.u31.1b", 0x80000, 0, 0x80d087bc}};

using PlaneFn = uint8_t (*)(uint8_t);

void decrypt_planes(std::vector<uint8_t>& dest, const std::vector<uint8_t>& raw,
                    uint32_t (*addr_fn)(uint32_t), const PlaneFn plane_fn[4]) {
    dest.assign(size_t(kPlaneSize) * 4, 0);
    for (uint32_t f = 0; f < uint32_t(kPlaneSize); f++) {
        const uint32_t addr = addr_fn(f);
        for (int plane = 0; plane < 4; plane++) {
            const size_t src = size_t(plane) * size_t(kPlaneSize) + f;
            uint8_t value = src < raw.size() ? raw[src] : 0;
            dest[size_t(plane) * size_t(kPlaneSize) + addr] = plane_fn[plane](value);
        }
    }
}

}  // namespace

Pirates::Pirates(Game game)
    : game_(game),
      main_cpu_(kMainClock),
      oki_(kOkiClock, /*pin7_high=*/false),
      eeprom_(16) {
    framebuffer_.assign(size_t(kScreenWidth) * size_t(kScreenHeight), 0xff000000u);
    canvas_.assign(512u * 256u, 0xff000000u);

    main_cpu_.set_memory_handlers([this](uint32_t a) { return main_read(a); },
                                  [this](uint32_t a, uint16_t v) { main_write(a, v); });
    main_cpu_.set_cycle_handler([this](int cycles) { on_main_cycles(cycles); });

    main_cycles_per_line_ = int(kMainClock / uint32_t(kScanlines * kFramesPerSecond));
}

const char* Pirates::title() const {
    return game_ == Game::Pirates ? "Pirates" : "Genix Family";
}

bool Pirates::load_program(const std::string& rom_path, std::string* error) {
    RomLoader loader;
    if (!loader.open(rom_path, error)) return false;

    std::vector<uint8_t> even(0x80000, 0), odd(0x80000, 0);
    const auto& even_entries = game_ == Game::Pirates ? kPiratesProgramLow : kGenixProgramLow;
    const auto& odd_entries = game_ == Game::Pirates ? kPiratesProgramHigh : kGenixProgramHigh;
    if (!loader.load(even_entries, even, error)) return false;
    if (!loader.load(odd_entries, odd, error)) return false;

    // roms_load16w: p:0 (even) is the high byte of each word, p:1 (odd) the low byte.
    std::vector<uint16_t> raw(0x80000);
    for (size_t i = 0; i < raw.size(); i++) raw[i] = uint16_t((uint16_t(even[i]) << 8) | odd[i]);

    for (uint32_t f = 0; f < 0x80000; f++) {
        const uint8_t vl = program_data_low(uint8_t(raw[program_addr_low(f)] & 0xff));
        const uint8_t vr = program_data_high(uint8_t(raw[program_addr_high(f)] >> 8));
        rom_[f] = uint16_t((uint16_t(vr) << 8) | vl);
    }
    return true;
}

bool Pirates::load_sound(const std::string& rom_path, std::string* error) {
    RomLoader loader;
    if (!loader.open(rom_path, error)) return false;

    std::vector<uint8_t> raw(0x80000, 0);
    if (!loader.load(game_ == Game::Pirates ? kPiratesOki : kGenixOki, raw, error)) return false;

    std::vector<uint8_t> decoded(0x80000, 0);
    for (uint32_t f = 0; f < 0x80000; f++) decoded[oki_addr(f)] = oki_data(raw[f]);

    std::copy(decoded.begin(), decoded.begin() + 0x40000, sound_banks_[0].begin());
    std::copy(decoded.begin() + 0x40000, decoded.end(), sound_banks_[1].begin());
    return true;
}

bool Pirates::load_graphics(const std::string& rom_path, std::string* error) {
    RomLoader loader;
    if (!loader.open(rom_path, error)) return false;

    {
        const auto& entries = game_ == Game::Pirates ? kPiratesGfx : kGenixGfx;
        std::vector<uint8_t> raw(size_t(kPlaneSize) * 4, 0);
        if (!loader.load(entries, raw, error)) return false;

        std::vector<uint8_t> decoded;
        const PlaneFn plane_fn[4] = {tile_data_plane0, tile_data_plane1, tile_data_plane2,
                                      tile_data_plane3};
        decrypt_planes(decoded, raw, tile_addr, plane_fn);

        GfxLayout layout;
        layout.width = 8;
        layout.height = 8;
        layout.total = 0x10000;
        layout.planes = 4;
        layout.char_increment = 8 * 8;
        // gfx_set_desc_data(4, 0, 8*8, $180000*8, $100000*8, $80000*8, 0)
        layout.plane_offsets = {0x180000 * 8, 0x100000 * 8, 0x80000 * 8, 0};
        layout.x_offsets = {7, 6, 5, 4, 3, 2, 1, 0};
        layout.y_offsets = {0, 8, 16, 24, 32, 40, 48, 56};
        tiles_.decode(layout, decoded);
    }

    {
        const auto& entries = game_ == Game::Pirates ? kPiratesSprites : kGenixSprites;
        std::vector<uint8_t> raw(size_t(kPlaneSize) * 4, 0);
        if (!loader.load(entries, raw, error)) return false;

        std::vector<uint8_t> decoded;
        const PlaneFn plane_fn[4] = {sprite_data_plane0, sprite_data_plane1, sprite_data_plane2,
                                      sprite_data_plane3};
        decrypt_planes(decoded, raw, sprite_addr, plane_fn);

        GfxLayout layout;
        layout.width = 16;
        layout.height = 16;
        layout.total = 0x4000;
        layout.planes = 4;
        layout.char_increment = 16 * 16;
        layout.plane_offsets = {0x180000 * 8, 0x100000 * 8, 0x80000 * 8, 0};
        layout.x_offsets = {7, 6, 5, 4, 3, 2, 1, 0, 15, 14, 13, 12, 11, 10, 9, 8};
        layout.y_offsets = {0, 16, 32, 48, 64, 80, 96, 112, 128, 144, 160, 176, 192, 208, 224, 240};
        sprites_.decode(layout, decoded);
    }
    return true;
}

void Pirates::select_oki_bank(int bank) {
    bank &= 1;
    if (bank == last_oki_bank_) return;
    last_oki_bank_ = bank;
    oki_.set_rom(std::vector<uint8_t>(sound_banks_[size_t(bank)].begin(),
                                      sound_banks_[size_t(bank)].end()));
}

bool Pirates::init(const std::string& rom_path, std::string* error) {
    if (!load_program(rom_path, error)) return false;
    if (!load_sound(rom_path, error)) return false;
    if (!load_graphics(rom_path, error)) return false;

    // Pirates only: two-byte protection patch from pirates_hw.pas
    // (`rom[$62c0 shr 1] := $6006`).
    if (game_ == Game::Pirates) rom_[0x62c0 / 2] = 0x6006;

    eeprom_.reset();
    last_oki_bank_ = -1;
    select_oki_bank(0);
    reset();
    return true;
}

void Pirates::reset() {
    main_cpu_.reset();
    oki_.reset();
    in0_ = 0x0f;
    in1_ = 0xffff;
    scroll_x_ = 0;
}

uint16_t Pirates::main_read(uint32_t addr) {
    addr &= 0xffffff;
    if (addr <= 0xfffff) return rom_[addr >> 1];
    if (addr >= 0x100000 && addr <= 0x10ffff) {
        if (game_ == Game::Genix) {
            const uint32_t byte_off = addr & 0xffff;
            if (byte_off == 0x9e98) return 4;
            if (byte_off >= 0x9e99 && byte_off <= 0x9e9b) return 0;
        }
        return work_ram_[(addr & 0xffff) >> 1];
    }
    if (addr == 0x300000) return in1_;
    if (addr == 0x400000) {
        if (game_ == Game::Genix) return in0_;
        return uint16_t(in0_ | (uint16_t(eeprom_.do_read()) << 4));
    }
    if (addr >= 0x500000 && addr <= 0x500fff) return sprite_ram_[(addr & 0xfff) >> 1];
    if (addr >= 0x800000 && addr <= 0x803fff) return palette_ram_[(addr & 0x3fff) >> 1];
    if (addr >= 0x900000 && addr <= 0x907fff) return tile_ram_[(addr & 0x7fff) >> 1];
    if (addr == 0xa00000) return oki_.read();
    return 0xffff;
}

void Pirates::main_write(uint32_t addr, uint16_t value) {
    addr &= 0xffffff;
    if (addr <= 0xfffff) return;
    if (addr >= 0x100000 && addr <= 0x10ffff) {
        work_ram_[(addr & 0xffff) >> 1] = value;
        return;
    }
    if (addr >= 0x500000 && addr <= 0x500fff) {
        sprite_ram_[(addr & 0xfff) >> 1] = value;
        return;
    }
    if (addr == 0x600000) {
        eeprom_.di_write(uint8_t((value >> 2) & 1));
        eeprom_.cs_write(uint8_t(value & 1));
        eeprom_.clk_write(uint8_t((value >> 1) & 1));
        select_oki_bank((value >> 6) & 1);
        return;
    }
    if (addr == 0x700000) {
        scroll_x_ = value & 0x1ff;
        return;
    }
    if (addr >= 0x800000 && addr <= 0x803fff) {
        write_palette(int((addr & 0x3fff) >> 1), value);
        return;
    }
    if (addr >= 0x900000 && addr <= 0x907fff) {
        tile_ram_[(addr & 0x7fff) >> 1] = value;
        return;
    }
    if (addr == 0xa00000) {
        oki_.write(uint8_t(value & 0xff));
        return;
    }
}

void Pirates::write_palette(int index, uint16_t value) {
    if (index < 0 || size_t(index) >= palette_ram_.size()) return;
    palette_ram_[size_t(index)] = value;
    const uint8_t r = uint8_t(pal5bit(value >> 10));
    const uint8_t g = uint8_t(pal5bit(value >> 5));
    const uint8_t b = uint8_t(pal5bit(value));
    palette_[size_t(index)] = argb(r, g, b);
}

uint32_t Pirates::pal_color(int index) const {
    if (index < 0 || size_t(index) >= palette_.size()) return 0xff000000u;
    return palette_[size_t(index)];
}

void Pirates::on_main_cycles(int cycles) {
    oki_accumulator_ += int64_t(cycles) * int64_t(oki_.sample_frequency());
    while (oki_accumulator_ >= int64_t(kMainClock)) {
        oki_accumulator_ -= int64_t(kMainClock);
        last_oki_ = oki_.update();
    }
    audio_accumulator_ += int64_t(cycles) * int64_t(sample_rate());
    while (audio_accumulator_ >= int64_t(kMainClock)) {
        audio_accumulator_ -= int64_t(kMainClock);
        audio_.push_back(int16_t(std::clamp(last_oki_, -32768, 32767)));
    }
}

void Pirates::set_inputs(const MachineInputs& inputs) {
    uint16_t in1 = 0xffff;
    if (inputs.player1.up) in1 &= ~0x0001;
    if (inputs.player1.down) in1 &= ~0x0002;
    if (inputs.player1.left) in1 &= ~0x0004;
    if (inputs.player1.right) in1 &= ~0x0008;
    if (inputs.player1.button1) in1 &= ~0x0010;
    if (inputs.player1.button2) in1 &= ~0x0020;
    if (inputs.player1.button3) in1 &= ~0x0040;
    if (inputs.player1.start) in1 &= ~0x0080;
    if (inputs.player2.right) in1 &= ~0x0100;
    if (inputs.player2.left) in1 &= ~0x0200;
    if (inputs.player2.up) in1 &= ~0x0400;
    if (inputs.player2.down) in1 &= ~0x0800;
    // Pascal P2 uses but1/but2/but3 (not but0), so button2/3/4 here.
    if (inputs.player2.button2) in1 &= ~0x1000;
    if (inputs.player2.button3) in1 &= ~0x2000;
    if (inputs.player2.button4) in1 &= ~0x4000;
    if (inputs.player2.start) in1 &= ~0x8000;
    in1_ = in1;

    uint8_t in0 = 0x0f;
    if (inputs.coin1) in0 &= ~0x01;
    if (inputs.coin2) in0 &= ~0x02;
    in0_ = in0;
}

void Pirates::set_dip_switch(int, uint8_t) {
    // This board has no physical DIP bank; every setting lives in the 93C46
    // EEPROM and is configured from the game's own service menu.
}

void Pirates::draw_layer(int base_word, int width_tiles, int height_tiles, int color_add,
                         bool transparent, int scroll_x, int dest_width) {
    const int layer_w = width_tiles * 8;
    for (int f = 0; f < width_tiles * height_tiles; f++) {
        const int tx = f / height_tiles;
        const int ty = f % height_tiles;
        const size_t entry = size_t(base_word) + size_t(f) * 2;
        if (entry + 1 >= tile_ram_.size()) continue;
        const uint16_t nchar = tile_ram_[entry];
        const int pal_base = (int(tile_ram_[entry + 1] & 0x1ff) + color_add) << 4;
        const uint8_t* px = tiles_.element(nchar);

        const int base_x = ((tx * 8 - scroll_x) % layer_w + layer_w) % layer_w;
        const int base_y = ty * 8;
        for (int y = 0; y < 8; y++) {
            const int fy = base_y + y;
            if (fy < 0 || fy >= 256) continue;
            for (int x = 0; x < 8; x++) {
                const uint8_t p = px[y * 8 + x];
                if (transparent && p == 0) continue;
                int fx = base_x + x;
                if (layer_w != 512) {
                    if (fx < 0 || fx >= dest_width) continue;
                } else {
                    fx %= 512;
                }
                if (fx < 0 || fx >= 512) continue;
                canvas_[size_t(fy) * 512 + size_t(fx)] = pal_color(pal_base + p);
            }
        }
    }
}

void Pirates::draw_sprites() {
    for (int f = 0; f <= 0x1fd; f++) {
        const size_t base = size_t(f) * 4;
        if (base + 6 >= sprite_ram_.size()) break;
        const uint16_t sy_raw = sprite_ram_[base + 3];
        if ((sy_raw & 0x8000) != 0) break;  // end-of-list marker

        const int sx = int(sprite_ram_[base + 5]) - 32;
        const uint16_t atrib = sprite_ram_[base + 6];
        const int nchar = atrib >> 2;
        const bool flip_x = (atrib & 2) != 0;
        const bool flip_y = (atrib & 1) != 0;
        const int pal_base = (int(sprite_ram_[base + 4] & 0xff) << 4) + 0x1800;
        const int sy = int(uint16_t(0x00f2 - sy_raw));

        const uint8_t* px = sprites_.element(nchar);
        for (int y = 0; y < 16; y++) {
            const int fy = sy + y;
            if (fy < 0 || fy >= 256) continue;
            const int sy_src = flip_y ? 15 - y : y;
            for (int x = 0; x < 16; x++) {
                const uint8_t p = px[sy_src * 16 + (flip_x ? 15 - x : x)];
                if (p == 0) continue;
                const int fx = sx + x;
                if (fx < 0 || fx >= 512) continue;
                canvas_[size_t(fy) * 512 + size_t(fx)] = pal_color(pal_base + p);
            }
        }
    }
}

void Pirates::render_frame() {
    // Pascal compose: BG (opaque) then FG onto the 512-wide playfield, sprites
    // on top of that, then the 288-wide text layer last.
    draw_layer(0x1540, 64, 32, 0x100, /*transparent=*/false, scroll_x_, 512);
    draw_layer(0x9c0, 64, 32, 0x080, /*transparent=*/true, scroll_x_, 512);
    draw_sprites();
    draw_layer(0xc0, 36, 32, 0x000, /*transparent=*/true, 0, 288);

    for (int y = 0; y < kScreenHeight; y++) {
        const uint32_t* src = &canvas_[size_t(y + 16) * 512];
        uint32_t* dst = &framebuffer_[size_t(y) * size_t(kScreenWidth)];
        std::memcpy(dst, src, size_t(kScreenWidth) * sizeof(uint32_t));
    }
}

void Pirates::run_frame() {
    for (int line = 0; line < kScanlines; line++) {
        if (line == kVBlankLine) {
            main_cpu_.set_irq(1, IrqLine::Hold);
            render_frame();
        }
        main_cpu_.run(main_cycles_per_line_);
    }
}

void Pirates::drain_audio(std::vector<int16_t>& out) {
    out.insert(out.end(), audio_.begin(), audio_.end());
    audio_.clear();
}

}  // namespace dsp
