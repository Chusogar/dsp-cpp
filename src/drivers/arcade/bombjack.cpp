#include "drivers/arcade/bombjack.h"

#include <algorithm>

#include "core/rom_loader.h"
#include "machine/sega_decrypt.h"

namespace dsp {
namespace {

struct GameRoms {
    std::vector<RomEntry> main, sound, chars, tiles, map, sprites;
    int chars_total, tiles_total, sprites_total;
    uint8_t dsw_a, dsw_b;
};

const GameRoms kBombJack = {
    {{"09_j01b.bin", 0x2000, 0x0000, 0xc668dc30}, {"10_l01b.bin", 0x2000, 0x2000, 0x52a1e5fb},
     {"11_m01b.bin", 0x2000, 0x4000, 0xb68a062a}, {"12_n01b.bin", 0x2000, 0x6000, 0x1d3ecee5},
     {"13.1r", 0x2000, 0xc000, 0x70e0244d}},
    {{"01_h03t.bin", 0x2000, 0, 0x8407917d}},
    {{"03_e08t.bin", 0x1000, 0x0000, 0x9f0470d5}, {"04_h08t.bin", 0x1000, 0x1000, 0x81ec12e6},
     {"05_k08t.bin", 0x1000, 0x2000, 0xe87ec8b1}},
    {{"06_l08t.bin", 0x2000, 0x0000, 0x51eebd89}, {"07_n08t.bin", 0x2000, 0x2000, 0x9dd98e9d},
     {"08_r08t.bin", 0x2000, 0x4000, 0x3155ee7d}},
    {{"02_p04t.bin", 0x1000, 0, 0x398d4a02}},
    {{"16_m07b.bin", 0x2000, 0x0000, 0x94694097}, {"15_l07b.bin", 0x2000, 0x2000, 0x013f58f2},
     {"14_j07b.bin", 0x2000, 0x4000, 0x101c858d}},
    0x200, 0x100, 0x100, 0xc0, 0x50,
};

const GameRoms kCalorieKun = {
    {{"epr10072.1j", 0x4000, 0x0000, 0xade792c1}, {"epr10073.1k", 0x4000, 0x4000, 0xb53e109f},
     {"epr10074.1m", 0x4000, 0x8000, 0xa08da685}},
    {{"epr10075.4d", 0x4000, 0, 0xca547036}},
    {{"epr10082.5r", 0x2000, 0x0000, 0x5984ea44}, {"epr10081.4r", 0x2000, 0x2000, 0xe2d45dd8},
     {"epr10080.3r", 0x2000, 0x4000, 0x42edfcfe}},
    {{"epr10078.7d", 0x4000, 0x0000, 0x5b8eecce}, {"epr10077.6d", 0x4000, 0x4000, 0x01bcb609},
     {"epr10076.5d", 0x4000, 0x8000, 0xb1529782}},
    {{"epr10079.8d", 0x2000, 0, 0x3c61a42c}},
    {{"epr10071.7m", 0x4000, 0x0000, 0x5f55527a}, {"epr10070.7k", 0x4000, 0x4000, 0x97f35a23},
     {"epr10069.7j", 0x4000, 0x8000, 0xc0c3deaf}},
    0x400, 0x200, 0x200, 0x30, 0x00,
};

// Pixel offsets of the 16x16 and 32x32 elements: 8x8 quarters in the order
// top-left, top-right (+8 bytes)... (MAME bombjack.cpp layouts).
constexpr int kX[32] = {0, 1, 2, 3, 4, 5, 6, 7, 64, 65, 66, 67, 68, 69, 70, 71,
                        256, 257, 258, 259, 260, 261, 262, 263, 320, 321, 322, 323, 324, 325, 326, 327};
constexpr int kY[32] = {0, 8, 16, 24, 32, 40, 48, 56, 128, 136, 144, 152, 160, 168, 176, 184,
                        512, 520, 528, 536, 544, 552, 560, 568, 640, 648, 656, 664, 672, 680, 688, 696};

GfxLayout layout(int size, int total, int plane_bytes, int element_bytes) {
    GfxLayout l;
    l.width = l.height = size;
    l.total = total;
    l.planes = 3;
    l.char_increment = element_bytes * 8;
    l.plane_offsets = {0, plane_bytes * 8, 2 * plane_bytes * 8};
    l.x_offsets.assign(kX, kX + size);
    l.y_offsets.assign(kY, kY + size);
    return l;
}

uint32_t pal4(uint8_t r, uint8_t g, uint8_t b) {
    return 0xff000000u | uint32_t((r & 15) * 17) << 16 | uint32_t((g & 15) * 17) << 8 | uint32_t((b & 15) * 17);
}

}  // namespace

BombJack::BombJack(Game game)
    : game_(game), screen_(256u * 256u, 0xff000000u), framebuffer_(256u * 224u, 0xff000000u) {
    main_cpu_.set_memory_handlers([this](uint16_t a) { return main_read(a); },
                                  [this](uint16_t a, uint8_t v) { main_write(a, v); });
    sound_cpu_.set_memory_handlers([this](uint16_t a) { return sound_read(a); },
                                   [this](uint16_t a, uint8_t v) { sound_write(a, v); });
    sound_cpu_.set_io_handlers([this](uint16_t p) { return sound_in(p); },
                               [this](uint16_t p, uint8_t v) { sound_out(p, v); });
    sound_cpu_.set_cycle_handler([this](int c) { on_sound_cycles(c); });
}

bool BombJack::init(const std::string& rom_path, std::string* error) {
    const GameRoms& r = game_ == Game::CalorieKun ? kCalorieKun : kBombJack;
    RomLoader loader;
    if (!loader.open(rom_path, error)) return false;

    rom_.assign(0x10000, 0xff);
    if (!loader.load(r.main, rom_, error)) return false;
    if (game_ == Game::CalorieKun) {
        std::vector<uint8_t> encrypted(rom_.begin(), rom_.begin() + 0x8000), data;
        sega_decrypt_type2(encrypted, SegaDecrypt2Chip::S317_000X, 0, &data, &opcodes_);
        std::copy(data.begin(), data.end(), rom_.begin());
        main_cpu_.set_opcode_read([this](uint16_t a) { return a < 0x8000 ? opcodes_[a] : main_read(a); });
    }
    sound_rom_.assign(0x4000, 0xff);
    if (!loader.load(r.sound, sound_rom_, error)) return false;
    bg_map_.assign(0x2000, 0);
    if (!loader.load(r.map, bg_map_, error)) return false;

    std::vector<uint8_t> gfx(size_t(r.chars_total) * 8 * 3, 0);
    if (!loader.load(r.chars, gfx, error)) return false;
    chars_.decode(layout(8, r.chars_total, r.chars_total * 8, 8), gfx);
    gfx.assign(size_t(r.tiles_total) * 32 * 3, 0);
    if (!loader.load(r.tiles, gfx, error)) return false;
    tiles_.decode(layout(16, r.tiles_total, r.tiles_total * 32, 32), gfx);
    gfx.assign(size_t(r.sprites_total) * 32 * 3, 0);
    if (!loader.load(r.sprites, gfx, error)) return false;
    sprites_.decode(layout(16, r.sprites_total, r.sprites_total * 32, 32), gfx);
    // 32x32 sprites: the second half of each plane, (mask + 1) of them.
    const int big = r.sprites_total / 4;
    big_mask_ = game_ == Game::CalorieKun ? 0x3f : 0x1f;
    std::vector<uint8_t> upper(gfx.begin() + 0x40 * big, gfx.end());
    upper.resize(gfx.size(), 0);
    big_sprites_.decode(layout(32, big_mask_ + 1, big * 128, 128), upper);

    dsw_a_ = r.dsw_a;
    dsw_b_ = r.dsw_b;
    warnings_ = loader.warnings();
    reset();
    return true;
}

void BombJack::reset() {
    ram_.fill(0);
    screen_ram_.fill(0);
    sprite_ram_.fill(0);
    pal_ram_.fill(0);
    sound_ram_.fill(0);
    palette_.fill(0xff000000u);
    in0_ = in1_ = 0;
    in2_ = 0xf0;
    sound_latch_ = 0;
    background_ = 0xff;
    sprite_control_[0] = sprite_control_[1] = 0;
    background_on_ = nmi_enabled_ = flip_ = false;
    main_debt_ = sound_debt_ = 0;
    audio_accum_ = 0;
    sound_commands_ = 0;
    audio_.clear();
    main_cpu_.reset();
    sound_cpu_.reset();
    for (AY8910& ay : ay_) ay.reset();
}

// ---------------------------------------------------------------------------
// Main CPU. Both games share one block of hardware (`map`), at 8000 on Bomb
// Jack and at c000 on Calorie Kun.

uint8_t BombJack::map_read(uint16_t o) {
    if (o < 0x1000) return ram_[o];
    if (o < 0x1800) return screen_ram_[o & 0x7ff];
    if (o >= 0x1c00 && o < 0x1e00) return pal_ram_[o & 0xff];
    if (o >= 0x3000 && o < 0x3800) {
        switch (o & 7) {
            case 0: return in0_;
            case 1: return in1_;
            case 2: return in2_;
            case 4: return dsw_a_;
            case 5: return dsw_b_;
            default: return 0;
        }
    }
    return 0;
}

void BombJack::map_write(uint16_t o, uint8_t v) {
    if (o < 0x1000) {
        ram_[o] = v;
    } else if (o < 0x1800) {
        screen_ram_[o & 0x7ff] = v;
    } else if (o < 0x1a00) {
        sprite_ram_[o & 0x7f] = v;
    } else if (o >= 0x1c00 && o < 0x1e00) {
        pal_ram_[o & 0xff] = v;
        const int i = (o & 0xfe);
        palette_[size_t(i >> 1)] = pal4(pal_ram_[size_t(i)], pal_ram_[size_t(i)] >> 4, pal_ram_[size_t(i) + 1]);
    } else if (o >= 0x1e00 && o < 0x2000) {
        background_on_ = (v & 0x10) != 0;
        background_ = v & 0x0f;
    } else if (o >= 0x3800 && o < 0x4000) {
        sound_latch_ = v;
        ++sound_commands_;
    }
}

uint8_t BombJack::main_read(uint16_t a) {
    if (game_ == Game::CalorieKun) {
        if (a < 0xc000) return rom_[a];
        return map_read(uint16_t(a - 0xc000));
    }
    if (a < 0x8000 || (a >= 0xc000 && a < 0xe000)) return rom_[a];
    if ((a >= 0x8000 && a < 0x9800) || (a >= 0x9c00 && a < 0x9e00) || (a >= 0xb000 && a < 0xb800))
        return map_read(uint16_t(a - 0x8000));
    return 0xff;
}

void BombJack::main_write(uint16_t a, uint8_t v) {
    if (game_ == Game::CalorieKun) {
        if (a >= 0xc000) map_write(uint16_t(a - 0xc000), v);
        return;
    }
    if ((a >= 0x8000 && a < 0x9a00) || (a >= 0x9c00 && a < 0xa000) || (a >= 0xb800 && a < 0xc000)) {
        map_write(uint16_t(a - 0x8000), v);
    } else if (a >= 0x9a00 && a < 0x9c00) {
        sprite_control_[a & 1] = v & 0x0f;
    } else if (a >= 0xb000 && a < 0xb800) {
        switch (a & 7) {
            case 0:
                nmi_enabled_ = v != 0;
                if (!nmi_enabled_) main_cpu_.set_nmi(IrqLine::Clear);
                break;
            case 4: flip_ = (v & 1) != 0; break;
            default: break;  // 3: watchdog
        }
    }
}

// ---------------------------------------------------------------------------
// Sound CPU

uint8_t BombJack::sound_read(uint16_t a) {
    if (game_ == Game::CalorieKun) {
        if (a < 0x8000) return sound_rom_[a & 0x3fff];
        if (a < 0xc000) return sound_ram_[a & 0x7ff];
    } else {
        a &= 0x7fff;
        if (a < 0x2000) return sound_rom_[a];
        if (a < 0x4000) return 0xff;
        if (a < 0x6000) return sound_ram_[a & 0x7ff];
    }
    const uint8_t v = sound_latch_;  // the latch clears when read
    sound_latch_ = 0;
    return v;
}

void BombJack::sound_write(uint16_t a, uint8_t v) {
    if (game_ == Game::CalorieKun) {
        if (a >= 0x8000 && a < 0xc000) sound_ram_[a & 0x7ff] = v;
    } else {
        a &= 0x7fff;
        if (a >= 0x4000 && a < 0x6000) sound_ram_[a & 0x7ff] = v;
    }
}

uint8_t BombJack::sound_in(uint16_t port) {
    switch (port & 0x91) {
        case 0x01: return ay_[0].read();
        case 0x11: return ay_[1].read();
        case 0x81: return ay_[2].read();
        default: return 0xff;
    }
}

void BombJack::sound_out(uint16_t port, uint8_t v) {
    switch (port & 0x91) {
        case 0x00: ay_[0].control(v); break;
        case 0x01: ay_[0].write(v); break;
        case 0x10: ay_[1].control(v); break;
        case 0x11: ay_[1].write(v); break;
        case 0x80: ay_[2].control(v); break;
        case 0x81: ay_[2].write(v); break;
        default: break;
    }
}

void BombJack::on_sound_cycles(int cycles) {
    audio_accum_ += int64_t(cycles) * AY8910::kSampleRate;
    while (audio_accum_ >= int64_t(kSoundClock)) {
        audio_accum_ -= int64_t(kSoundClock);
        // Three PSGs at a quarter of full scale each: their sum stays in range.
        const int32_t s = (ay_[0].update() + ay_[1].update() + ay_[2].update()) / 4;
        audio_.push_back(int16_t(std::clamp<int32_t>(s, -32768, 32767)));
    }
}

// ---------------------------------------------------------------------------
// Video

void BombJack::update_video() {
    // Background: one of 16 pictures of 16x16 tiles from the map ROM.
    if (background_on_) {
        const size_t base = size_t(background_) * 0x200;
        for (int f = 0; f < 0x100; ++f) {
            const uint8_t attr = bg_map_[base + 0x100 + size_t(f)];
            const int code = bg_map_[base + size_t(f)] + ((attr & 0x30) << 4);
            const int color = (attr & 0x0f) * 8;
            const bool flip_x = (attr & 0x40) != 0;
            const uint8_t* g = tiles_.element(code);
            const int tx = (f & 15) * 16, ty = (f >> 4) * 16;
            for (int r = 0; r < 16; ++r)
                for (int c = 0; c < 16; ++c)
                    screen_[size_t(ty + r) * 256 + size_t(tx + c)] =
                        palette_[size_t(color + g[r * 16 + (flip_x ? 15 - c : c)])];
        }
    } else {
        std::fill(screen_.begin(), screen_.end(), palette_[0]);
    }

    // Text layer.
    for (int f = 0; f < 0x400; ++f) {
        const uint8_t attr = screen_ram_[0x400 + size_t(f)];
        const int code = screen_ram_[size_t(f)] + ((attr & 0x30) << 4);
        const int color = (attr & 0x0f) * 8;
        const bool flip_x = (attr & 0x40) != 0, flip_y = (attr & 0x80) != 0;
        const uint8_t* g = chars_.element(code);
        const int tx = (f & 31) * 8, ty = (f >> 5) * 8;
        for (int r = 0; r < 8; ++r)
            for (int c = 0; c < 8; ++c) {
                const int pen = g[(flip_y ? 7 - r : r) * 8 + (flip_x ? 7 - c : c)];
                if (pen) screen_[size_t(ty + r) * 256 + size_t(tx + c)] = palette_[size_t(color + pen)];
            }
    }

    // Sprites 31..7 (the first seven entries are not shown).
    auto draw = [&](const GfxSet& set, int code, int size, int x, int y, int color, bool flip_x, bool flip_y) {
        const uint8_t* g = set.element(code);
        for (int r = 0; r < size; ++r) {
            const int py = y + r;
            if (py < 0 || py >= 256) continue;
            for (int c = 0; c < size; ++c) {
                const int px = x + c;
                if (px < 0 || px >= 256) continue;
                const int pen = g[(flip_y ? size - 1 - r : r) * size + (flip_x ? size - 1 - c : c)];
                if (pen) screen_[size_t(py) * 256 + size_t(px)] = palette_[size_t(color + pen)];
            }
        }
    };
    for (int f = 31; f >= 7; --f) {
        const uint8_t* s = &sprite_ram_[size_t(f) * 4];
        const int code = s[0];
        const uint8_t attr = s[1];
        int y = 240 - s[2];
        const int x = s[3];
        const int color = (attr & 0x0f) * 8;
        const bool flip_x = (attr & 0x40) != 0, flip_y = (attr & 0x80) != 0;
        bool big;
        if (game_ == Game::BombJack) {
            // Big sprites are a range of sprite pairs set by two registers.
            const int pair = f >> 1;
            const int lo = sprite_control_[0], hi = sprite_control_[1];
            big = lo > hi ? (pair > hi && pair <= lo) : (pair > lo && pair <= hi);
            y += 1;
        } else {
            big = (attr & 0x10) != 0;
        }
        if (!big) {
            draw(sprites_, code, 16, x, y, color, flip_x, flip_y);
        } else if (!(f & 1)) {
            draw(big_sprites_, code & big_mask_, 32, x, y - 16, color, flip_x, flip_y);
        }
    }

    // Visible rows 16..239; Bomb Jack's monitor is turned 90 degrees.
    for (int y = 0; y < 224; ++y)
        for (int x = 0; x < 256; ++x) {
            int sy = 16 + y, sx = x;
            if (flip_) {
                sy = 255 - sy;
                sx = 255 - sx;
            }
            const uint32_t p = screen_[size_t(sy) * 256 + size_t(sx)];
            if (rotated()) framebuffer_[size_t(x) * 224 + size_t(223 - y)] = p;
            else framebuffer_[size_t(y) * 256 + size_t(x)] = p;
        }
}

// ---------------------------------------------------------------------------

void BombJack::run_frame() {
    const double main_cycles = double(kMainClock) / kFramesPerSecond / kScanlines;
    const double sound_cycles = double(kSoundClock) / kFramesPerSecond / kScanlines;
    for (int line = 0; line < kScanlines; ++line) {
        if (line == 240) {
            update_video();
            if (game_ == Game::BombJack) {
                if (nmi_enabled_) main_cpu_.set_nmi(IrqLine::Assert);
                sound_cpu_.set_nmi(IrqLine::Pulse);
            } else {
                main_cpu_.set_irq(IrqLine::Hold);
                sound_cpu_.set_irq(IrqLine::Hold);
            }
        }
        main_debt_ += main_cycles;
        main_debt_ -= main_cpu_.run(int(main_debt_));
        sound_debt_ += sound_cycles;
        sound_debt_ -= sound_cpu_.run(int(sound_debt_));
    }
}

void BombJack::set_inputs(const MachineInputs& in) {
    if (game_ == Game::BombJack) {
        auto pad = [](const InputState& p) {
            uint8_t v = 0;
            if (p.right) v |= 0x01;
            if (p.left) v |= 0x02;
            if (p.up) v |= 0x04;
            if (p.down) v |= 0x08;
            if (p.button1) v |= 0x10;
            return v;
        };
        in0_ = pad(in.player1);
        in1_ = pad(in.player2);
        in2_ = uint8_t(0xf0 | (in.coin1 ? 1 : 0) | (in.coin2 ? 2 : 0) | (in.player1.start ? 4 : 0) |
                       (in.player2.start ? 8 : 0));
    } else {
        auto pad = [](const InputState& p) {
            uint8_t v = 0;
            if (p.up) v |= 0x01;
            if (p.down) v |= 0x02;
            if (p.left) v |= 0x04;
            if (p.right) v |= 0x08;
            if (p.button1) v |= 0x10;
            if (p.button2) v |= 0x20;
            return v;
        };
        in0_ = pad(in.player1);
        in1_ = pad(in.player2);
        in2_ = uint8_t(0xf0 | (in.player1.start ? 1 : 0) | (in.player2.start ? 2 : 0) | (in.coin1 ? 4 : 0) |
                       (in.coin2 ? 8 : 0));
    }
}

void BombJack::set_dip_switch(int bank, uint8_t value) {
    if (bank == 0) dsw_a_ = value;
    else if (bank == 1) dsw_b_ = value;
}

void BombJack::drain_audio(std::vector<int16_t>& out) {
    out.insert(out.end(), audio_.begin(), audio_.end());
    audio_.clear();
}

}  // namespace dsp
