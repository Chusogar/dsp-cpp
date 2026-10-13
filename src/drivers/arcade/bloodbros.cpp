#include "drivers/arcade/bloodbros.h"

#include <algorithm>

#include "core/rom_loader.h"

namespace dsp {
namespace {

struct GameRoms {
    std::vector<RomEntry> program;  // offset = byte position in the interleaved image
    std::vector<RomEntry> sound, chars, tiles, sprites, oki;
};

const GameRoms kBloodBros = {
    {{"2.u021.7n", 0x20000, 0x00001, 0x204dca6e}, {"1.u022.8n", 0x20000, 0x00000, 0xac6719e7},
     {"4.u023.7l", 0x20000, 0x40001, 0xfd951c2c}, {"3.u024.8l", 0x20000, 0x40000, 0x18d3c460}},
    {{"bb_07.u1016.6a", 0x10000, 0, 0x411b94e8}},
    {{"bb_05.u061.6f", 0x10000, 0x00000, 0x04ba6d19}, {"bb_06.u063.6d", 0x10000, 0x10000, 0x7092e35b}},
    {{"blood_bros_bk__=c=1990_tad_corp.u064.4d", 0x100000, 0, 0x1aa87ee6}},
    {{"blood_bros_obj__=c=1990_tad_corp.u078.2n", 0x100000, 0, 0xd27c3952}},
    {{"bb_08.u095.5a", 0x20000, 0, 0xdeb1b975}},
};

const GameRoms kSkySmasher = {
    {{"rom5", 0x20000, 0x00000, 0x867f9897}, {"rom6", 0x20000, 0x00001, 0xe9c1d308},
     {"rom7", 0x20000, 0x40000, 0xd209db4d}, {"rom8", 0x20000, 0x40001, 0xd3646728}},
    {{"rom2", 0x10000, 0, 0x75b194cf}},
    {{"rom3", 0x10000, 0x00000, 0xfbb241be}, {"rom4", 0x10000, 0x10000, 0xad3cde81}},
    {{"rom9", 0x100000, 0, 0xb0a5eecf}},
    {{"rom10", 0x80000, 0, 0x1bbcda5d}},
    {{"rom1", 0x20000, 0, 0xe69986f6}},
};

uint32_t pal4(uint16_t v) {
    const uint32_t r = v & 15, g = (v >> 4) & 15, b = (v >> 8) & 15;
    return 0xff000000u | (r * 17) << 16 | (g * 17) << 8 | (b * 17);
}

// 16x16 tiles and sprites: 128 bytes each, planes in the nibbles of 16-bit words.
GfxLayout tile_layout(int total) {
    GfxLayout l;
    l.width = l.height = 16;
    l.total = total;
    l.planes = 4;
    l.char_increment = 128 * 8;
    l.plane_offsets = {8, 12, 0, 4};
    l.x_offsets = {3, 2, 1, 0, 16 + 3, 16 + 2, 16 + 1, 16 + 0,
                   3 + 32 * 16, 2 + 32 * 16, 1 + 32 * 16, 0 + 32 * 16,
                   16 + 3 + 32 * 16, 16 + 2 + 32 * 16, 16 + 1 + 32 * 16, 16 + 0 + 32 * 16};
    for (int i = 0; i < 16; ++i) l.y_offsets.push_back(i * 32);
    return l;
}

}  // namespace

BloodBros::BloodBros(Game game)
    : game_(game),
      screen_(256u * 256u, 0xff000000u),
      framebuffer_(size_t(kWidth) * kHeight, 0xff000000u) {
    main_cpu_.set_memory_handlers([this](uint32_t a) { return main_read(a); },
                                  [this](uint32_t a, uint16_t v) { main_write(a, v); });
    irq_level_ = game == Game::SkySmasher ? 2 : 4;
}

bool BloodBros::init(const std::string& rom_path, std::string* error) {
    const GameRoms& roms = game_ == Game::SkySmasher ? kSkySmasher : kBloodBros;
    RomLoader loader;
    if (!loader.open(rom_path, error)) return false;

    // 68000 program: each file holds the even or odd bytes of its half.
    std::vector<uint8_t> image(0x80000, 0xff);
    for (const RomEntry& e : roms.program) {
        std::vector<uint8_t> half(e.length, 0xff);
        if (!loader.load({{e.name, e.length, 0, e.crc}}, half, error)) return false;
        for (uint32_t i = 0; i < e.length; ++i) image[e.offset + 2 * i] = half[i];
    }
    for (size_t i = 0; i < rom_.size(); ++i) rom_[i] = uint16_t(image[2 * i] << 8 | image[2 * i + 1]);

    std::vector<uint8_t> sound(0x10000, 0xff);
    if (!loader.load(roms.sound, sound, error)) return false;
    sound_.load_program(sound, false);
    std::vector<uint8_t> oki(0x40000, 0);
    if (!loader.load(roms.oki, oki, error)) return false;
    sound_.load_samples(std::move(oki));

    std::vector<uint8_t> chars(0x20000, 0);
    if (!loader.load(roms.chars, chars, error)) return false;
    {
        GfxLayout l;
        l.width = l.height = 8;
        l.total = 0x1000;
        l.planes = 4;
        l.char_increment = 16 * 8;
        l.plane_offsets = {0, 4, 0x1000 * 16 * 8, 0x1000 * 16 * 8 + 4};
        l.x_offsets = {3, 2, 1, 0, 8 + 3, 8 + 2, 8 + 1, 8 + 0};
        for (int i = 0; i < 8; ++i) l.y_offsets.push_back(i * 16);
        chars_.decode(l, chars);
    }
    std::vector<uint8_t> gfx(0x100000, 0);
    if (!loader.load(roms.tiles, gfx, error)) return false;
    tiles_.decode(tile_layout(0x2000), gfx);
    std::fill(gfx.begin(), gfx.end(), 0);
    if (!loader.load(roms.sprites, gfx, error)) return false;
    sprites_.decode(tile_layout(0x2000), gfx);

    warnings_ = loader.warnings();
    reset();
    return true;
}

void BloodBros::reset() {
    ram_.fill(0);
    pal_ram_.fill(0);
    crt_.fill(0);
    palette_.fill(0xff000000u);
    in0_ = in1_ = 0xffff;
    main_debt_ = sound_debt_ = 0;
    main_cpu_.reset();
    sound_.reset();
    sound_.set_coins(0);
}

// ---------------------------------------------------------------------------
// Main CPU

int BloodBros::crt_index(uint32_t a) const {
    int i = int((a & 0x7f) >> 1);
    // Sky Smasher swaps address bits 3 and 4 of the CRT registers.
    if (game_ == Game::SkySmasher) i = (i & 0xffe7) | ((i & 0x10) >> 1) | ((i & 0x08) << 1);
    return i < int(crt_.size()) ? i : -1;
}

uint16_t BloodBros::main_read(uint32_t a) {
    a &= 0xfffff;
    if (a < 0x80000) return rom_[a >> 1];
    if (a < 0x90000) {
        if (a >= 0x8e800 && a < 0x8f800) return pal_ram_[(a - 0x8e800) >> 1];
        return ram_[(a & 0xffff) >> 1];
    }
    if (a >= 0xa0000 && a < 0xa000e) return sound_.main_read(int((a & 0xf) >> 1));
    if (a >= 0xc0000 && a < 0xc0050) {
        const int i = crt_index(a);
        return i >= 0 ? crt_[size_t(i)] : 0xffff;
    }
    switch (a) {
        case 0xe0000: return dsw_;
        case 0xe0002: return in0_;
        case 0xe0004: return in1_;
        default: return 0xffff;
    }
}

void BloodBros::main_write(uint32_t a, uint16_t v) {
    a &= 0xfffff;
    if (a < 0x80000) return;
    if (a < 0x90000) {
        if (a >= 0x8e800 && a < 0x8f800) {
            const int i = int((a - 0x8e800) >> 1);
            pal_ram_[size_t(i)] = v;
            set_color(i, v);
            return;
        }
        ram_[(a & 0xffff) >> 1] = v;
        return;
    }
    if (a >= 0xa0000 && a < 0xa000e) {
        sound_.main_write(int((a & 0xf) >> 1), uint8_t(v));
    } else if (a >= 0xc0000 && a < 0xc0050) {
        const int i = crt_index(a);
        if (i >= 0) crt_[size_t(i)] = v;
    }
}

void BloodBros::set_color(int index, uint16_t value) { palette_[size_t(index)] = pal4(value); }

// ---------------------------------------------------------------------------
// Video

void BloodBros::draw_sprites(int priority) {
    for (int f = 0x1ff; f >= 0; --f) {
        const uint16_t* s = &ram_[0x5800 + size_t(f) * 4];
        const uint16_t attr = s[0];
        if (attr & 0x8000) continue;
        if (((attr >> 11) & 1) != priority) continue;
        const int width = (attr >> 7) & 7, height = (attr >> 4) & 7;
        const bool flip_x = (attr & 0x2000) != 0, flip_y = (attr & 0x4000) != 0;
        const int base = (attr & 0x0f) << 4;
        int code = s[1] & 0x1fff;
        int x = s[2] & 0x1ff, y = s[3] & 0x1ff;
        if (x >= 256) x -= 512;  // 9-bit positions wrap around
        if (y >= 256) y -= 512;
        for (int w = 0; w <= width; ++w) {
            const int tx = x + 16 * (flip_x ? width - w : w);
            for (int h = 0; h <= height; ++h, ++code) {
                const int ty = y + 16 * (flip_y ? height - h : h);
                const uint8_t* g = sprites_.element(code & 0x1fff);
                for (int r = 0; r < 16; ++r) {
                    const int py = ty + r;
                    if (py < 16 || py >= 240) continue;
                    const uint8_t* row = g + (flip_y ? 15 - r : r) * 16;
                    for (int c = 0; c < 16; ++c) {
                        const int px = tx + c;
                        if (px < 0 || px >= 256) continue;
                        const int pen = row[flip_x ? 15 - c : c];
                        if (pen != 15) screen_[size_t(py) * 256 + size_t(px)] = palette_[size_t(base + pen)];
                    }
                }
            }
        }
    }
}

void BloodBros::update_video() {
    const uint16_t control = crt_[0x0e];
    // A 512x256 layer of 16x16 tiles scrolled by (sx, sy); `opaque` false skips pen 15.
    auto tile_layer = [&](size_t ram_base, int code_add, int color_base, uint16_t sx, uint16_t sy,
                          bool opaque) {
        for (int y = 16; y < 240; ++y) {
            const int py = (y + sy) & 255;
            for (int x = 0; x < 256; ++x) {
                const int px = (x + sx) & 511;
                const uint16_t attr = ram_[ram_base + size_t((py >> 4) * 32 + (px >> 4))];
                const int pen = tiles_.element((attr & 0xfff) + code_add)[(py & 15) * 16 + (px & 15)];
                if (opaque || pen != 15)
                    screen_[size_t(y) * 256 + size_t(x)] = palette_[size_t(color_base + (attr >> 12) * 16 + pen)];
            }
        }
    };

    if (!(control & 0x01)) tile_layer(0x6000, 0, 0x400, crt_[0x10], crt_[0x11], true);
    else std::fill(screen_.begin(), screen_.end(), palette_[0x800]);
    if (!(control & 0x10)) draw_sprites(1);
    if (!(control & 0x02)) tile_layer(0x6800, 0x1000, 0x500, crt_[0x12], crt_[0x13], false);
    if (!(control & 0x10)) draw_sprites(0);
    if (!(control & 0x08)) {  // text: 32x32 characters, no scroll
        for (int i = 0; i < 0x400; ++i) {
            const int cy = i >> 5, cx = i & 31;
            if (cy < 2 || cy >= 30) continue;
            const uint16_t attr = ram_[0x6c00 + size_t(i)];
            const uint8_t* g = chars_.element(attr & 0xfff);
            const int base = 0x700 + (attr >> 12) * 16;
            for (int r = 0; r < 8; ++r)
                for (int c = 0; c < 8; ++c) {
                    const int pen = g[r * 8 + c];
                    if (pen != 15)
                        screen_[size_t(cy * 8 + r) * 256 + size_t(cx * 8 + c)] = palette_[size_t(base + pen)];
                }
        }
    }

    if (!rotated()) {
        std::copy_n(screen_.begin() + 16 * 256, size_t(kWidth) * kHeight, framebuffer_.begin());
    } else {
        // Sky Smasher's monitor is turned 270 degrees (MAME ROT270): the
        // picture's right edge becomes the top.
        for (int y = 0; y < kWidth; ++y)
            for (int x = 0; x < kHeight; ++x)
                framebuffer_[size_t(y) * kHeight + size_t(x)] = screen_[size_t(16 + x) * 256 + size_t(255 - y)];
    }
}

// ---------------------------------------------------------------------------

void BloodBros::run_frame() {
    const double main_cycles = double(kMainClock) / kFramesPerSecond / kScanlines;
    const double sound_cycles = double(kSoundClock) / kFramesPerSecond / kScanlines;
    for (int line = 0; line < kScanlines; ++line) {
        if (line == 240) {
            main_cpu_.set_irq(irq_level_, IrqLine::Hold);
            update_video();
        }
        main_debt_ += main_cycles;
        main_debt_ -= main_cpu_.run(int(main_debt_));
        sound_debt_ += sound_cycles;
        sound_debt_ -= sound_.run(int(sound_debt_));
    }
}

void BloodBros::set_inputs(const MachineInputs& in) {
    auto pad = [](const InputState& p) {
        uint16_t v = 0xff;
        if (p.up) v &= ~0x01;
        if (p.down) v &= ~0x02;
        if (p.left) v &= ~0x04;
        if (p.right) v &= ~0x08;
        if (p.button1) v &= ~0x10;
        if (p.button2) v &= ~0x20;
        if (p.button3) v &= ~0x40;
        return v;
    };
    in0_ = uint16_t(pad(in.player1) | pad(in.player2) << 8);
    uint16_t sys = 0xffff;
    if (in.player1.start) sys &= ~0x0001;
    if (in.player2.start) sys &= ~0x0010;
    in1_ = sys;
    // The coins go to the sound CPU, which reports them to the main program.
    sound_.set_coins(uint8_t((in.coin1 ? 1 : 0) | (in.coin2 ? 2 : 0)));
}

void BloodBros::set_dip_switch(int bank, uint8_t value) {
    if (bank == 0) dsw_ = uint16_t((dsw_ & 0xff00) | value);
    else if (bank == 1) dsw_ = uint16_t((dsw_ & 0x00ff) | (value << 8));
}

}  // namespace dsp
