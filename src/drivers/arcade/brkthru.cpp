#include "drivers/arcade/brkthru.h"

#include <algorithm>

#include "core/rom_loader.h"

namespace dsp {
namespace {

struct GameRoms {
    std::vector<RomEntry> program, sound, chars, sprites, tiles, proms;
};

const GameRoms kBreakThru = {
    {{"brkthru.1", 0x4000, 0x00000, 0xcfb4265f}, {"brkthru.2", 0x8000, 0x04000, 0xfa8246d9},
     {"brkthru.4", 0x8000, 0x0c000, 0x8cabf252}, {"brkthru.3", 0x8000, 0x14000, 0x2f2c40c2}},
    {{"brkthru.5", 0x8000, 0x8000, 0xc309435f}},
    {{"brkthru.12", 0x2000, 0, 0x58c0b29b}},
    {{"brkthru.9", 0x8000, 0x00000, 0xf54e50a7}, {"brkthru.10", 0x8000, 0x08000, 0xfd156945},
     {"brkthru.11", 0x8000, 0x10000, 0xc152a99b}},
    {{"brkthru.7", 0x8000, 0x00000, 0x920cc56a}, {"brkthru.6", 0x8000, 0x08000, 0xfd3cee40},
     {"brkthru.8", 0x8000, 0x10000, 0xf67ee64e}},
    {{"brkthru.13", 0x100, 0x000, 0xaae44269}, {"brkthru.14", 0x100, 0x100, 0xf2d4822a}},
};

const GameRoms kDarwin = {
    {{"darw_04.rom", 0x4000, 0x00000, 0x0eabf21c}, {"darw_05.rom", 0x8000, 0x04000, 0xe771f864},
     {"darw_07.rom", 0x8000, 0x0c000, 0x97ac052c}, {"darw_06.rom", 0x8000, 0x14000, 0x2a9fb208}},
    {{"darw_08.rom", 0x8000, 0x8000, 0x6b580d58}},
    {{"darw_09.rom", 0x2000, 0, 0x067b4cf5}},
    {{"darw_10.rom", 0x8000, 0x00000, 0x487a014c}, {"darw_11.rom", 0x8000, 0x08000, 0x548ce2d1},
     {"darw_12.rom", 0x8000, 0x10000, 0xfaba5fef}},
    {{"darw_03.rom", 0x8000, 0x00000, 0x57d0350d}, {"darw_02.rom", 0x8000, 0x08000, 0x559a71ab},
     {"darw_01.rom", 0x8000, 0x10000, 0x15a16973}},
    {{"df.12", 0x100, 0x000, 0x89b952ef}, {"df.13", 0x100, 0x100, 0xd595e91d}},
};

uint8_t weight4(uint8_t v) {
    return uint8_t(0x0e * (v & 1) + 0x1f * ((v >> 1) & 1) + 0x43 * ((v >> 2) & 1) + 0x8f * ((v >> 3) & 1));
}

}  // namespace

BreakThru::BreakThru(Game game)
    : game_(game), screen_(256u * 256u, 0xff000000u), framebuffer_(size_t(kSize) * kSize, 0xff000000u) {
    main_cpu_.set_memory_handlers([this](uint16_t a) { return main_read(a); },
                                  [this](uint16_t a, uint8_t v) { main_write(a, v); });
    sound_cpu_.set_memory_handlers([this](uint16_t a) { return sound_read(a); },
                                   [this](uint16_t a, uint8_t v) { sound_write(a, v); });
    sound_cpu_.set_cycle_handler([this](int cycles) { on_sound_cycles(cycles); });
    ym3526_.set_irq_handler(
        [this](bool state) { sound_cpu_.set_irq(state ? IrqLine::Assert : IrqLine::Clear); });
    if (game_ == Game::Darwin) dsw_a_ = 0xdf;
}

bool BreakThru::init(const std::string& rom_path, std::string* error) {
    const GameRoms& roms = game_ == Game::Darwin ? kDarwin : kBreakThru;
    RomLoader loader;
    if (!loader.open(rom_path, error)) return false;

    std::vector<uint8_t> program(0x1c000, 0xff);
    if (!loader.load(roms.program, program, error)) return false;
    rom_.assign(program.begin(), program.begin() + 0xc000);
    banks_.assign(program.begin() + 0xc000, program.end());
    std::vector<uint8_t> sound(0x10000, 0xff);
    if (!loader.load(roms.sound, sound, error)) return false;
    std::copy(sound.begin() + 0x8000, sound.end(), sound_mem_.begin() + 0x8000);

    // 8x8 characters, 3 bpp: two planes in the nibbles, the third 4 KB on.
    {
        std::vector<uint8_t> gfx(0x2000, 0);
        if (!loader.load(roms.chars, gfx, error)) return false;
        GfxLayout l;
        l.width = l.height = 8;
        l.total = 0x100;
        l.planes = 3;
        l.char_increment = 8 * 8;
        l.plane_offsets = {512 * 8 * 8 + 4, 0, 4};
        l.x_offsets = {256 * 8 * 8 + 0, 256 * 8 * 8 + 1, 256 * 8 * 8 + 2, 256 * 8 * 8 + 3, 0, 1, 2, 3};
        for (int i = 0; i < 8; ++i) l.y_offsets.push_back(i * 8);
        chars_.decode(l, gfx);
    }
    // 16x16 tiles: eight groups of 128. Each pair of groups shares 16 KB of
    // planes 1-2 and 8 KB holding plane 3 in the low (even group) or high (odd
    // group) nibbles; rearrange the ROMs like MAME, then decode each group.
    {
        std::vector<uint8_t> src(0x18000, 0);
        if (!loader.load(roms.tiles, src, error)) return false;
        std::vector<uint8_t> gfx(0x20000, 0);
        for (int f = 0; f < 4; ++f) {
            std::copy_n(src.begin() + 0x4000 * f, 0x4000, gfx.begin() + 0x8000 * f);
            std::copy_n(src.begin() + 0x10000 + 0x2000 * f, 0x1000, gfx.begin() + 0x8000 * f + 0x4000);
            std::copy_n(src.begin() + 0x11000 + 0x2000 * f, 0x1000, gfx.begin() + 0x8000 * f + 0x6000);
        }
        tiles_.create(16, 16, 0x400);
        for (int group = 0; group < 8; ++group) {
            const int f = group / 2;
            const bool odd = group & 1;
            const size_t base = size_t(0x8000 * f + (odd ? 0x1000 : 0));
            std::vector<uint8_t> region(gfx.begin() + long(base), gfx.begin() + long(base) + 0x7000);
            GfxLayout l;
            l.width = l.height = 16;
            l.total = 0x80;
            l.planes = 3;
            l.char_increment = 32 * 8;
            l.plane_offsets = {odd ? 0x3000 * 8 : 0x4000 * 8 + 4, 0, 4};
            l.x_offsets = {0, 1, 2, 3, 1024 * 8 * 8 + 0, 1024 * 8 * 8 + 1, 1024 * 8 * 8 + 2, 1024 * 8 * 8 + 3,
                           16 * 8 + 0, 16 * 8 + 1, 16 * 8 + 2, 16 * 8 + 3,
                           16 * 8 + 1024 * 8 * 8 + 0, 16 * 8 + 1024 * 8 * 8 + 1,
                           16 * 8 + 1024 * 8 * 8 + 2, 16 * 8 + 1024 * 8 * 8 + 3};
            for (int i = 0; i < 16; ++i) l.y_offsets.push_back(i * 8);
            tiles_.decode_elements(l, region, group * 0x80);
        }
    }
    {
        std::vector<uint8_t> gfx(0x18000, 0);
        if (!loader.load(roms.sprites, gfx, error)) return false;
        GfxLayout l;
        l.width = l.height = 16;
        l.total = 0x400;
        l.planes = 3;
        l.char_increment = 32 * 8;
        l.plane_offsets = {2 * 1024 * 32 * 8, 1024 * 32 * 8, 0};
        for (int i = 0; i < 8; ++i) l.x_offsets.push_back(16 * 8 + i);
        for (int i = 0; i < 8; ++i) l.x_offsets.push_back(i);
        for (int i = 0; i < 16; ++i) l.y_offsets.push_back(i * 8);
        sprites_.decode(l, gfx);
    }
    std::vector<uint8_t> proms(0x200, 0);
    if (!loader.load(roms.proms, proms, error)) return false;
    for (int i = 0; i < 0x100; ++i) {
        const uint32_t r = weight4(proms[size_t(i)] & 0x0f), g = weight4(proms[size_t(i)] >> 4),
                       b = weight4(proms[0x100 + size_t(i)] & 0x0f);
        palette_[size_t(i)] = 0xff000000u | r << 16 | g << 8 | b;
    }

    warnings_ = loader.warnings();
    reset();
    return true;
}

void BreakThru::reset() {
    ram_.fill(0);
    bg_video_.fill(0);
    fg_video_.fill(0);
    sprite_ram_.fill(0);
    std::fill(sound_mem_.begin(), sound_mem_.begin() + 0x8000, 0);
    in0_ = in1_ = 0xff;
    in2_ = 0xe0;
    rom_bank_ = 0;
    scroll_x_ = 0;
    old_coin1_ = old_coin2_ = false;
    sound_latch_ = 0;
    bg_color_ = 0;
    nmi_enabled_ = true;
    main_debt_ = sound_debt_ = 0;
    audio_accum_ = 0;
    audio_.clear();
    main_cpu_.reset();
    sound_cpu_.reset();
    ym2203_.reset();
    ym3526_.reset();
}

// ---------------------------------------------------------------------------
// Main CPU. read_common/write_common use the Break Thru addresses of the
// shared blocks (video, sprites, I/O); Darwin maps them elsewhere.

uint8_t BreakThru::read_common(uint16_t a) {
    if (a < 0x400) return bg_video_[a];
    if (a < 0xc00) return ram_[a];
    if (a < 0x1000) return fg_video_[a & 0x3ff];
    if (a < 0x1100) return sprite_ram_[a & 0xff];
    switch (a) {
        case 0x1800: return in0_;
        case 0x1801: return in1_;
        case 0x1802: return dsw_a_;
        case 0x1803: return uint8_t(dsw_b_ | in2_);
        default: return 0xff;
    }
}

void BreakThru::write_common(uint16_t a, uint8_t v) {
    if (a < 0x400) {
        bg_video_[a] = v;
    } else if (a < 0xc00) {
        ram_[a] = v;
    } else if (a < 0x1000) {
        fg_video_[a & 0x3ff] = v;
    } else if (a < 0x1100) {
        sprite_ram_[a & 0xff] = v;
    } else if (a == 0x1800) {
        scroll_x_ = uint16_t((scroll_x_ & 0xff00) | v);
    } else if (a == 0x1801) {
        rom_bank_ = v & 7;
        bg_color_ = uint8_t((v & 0x38) >> 2);
        scroll_x_ = uint16_t((scroll_x_ & 0x00ff) | ((v >> 7) << 8));
    } else if (a == 0x1802) {
        sound_latch_ = v;
        ++sound_commands_;
        sound_cpu_.set_nmi(IrqLine::Pulse);
    }
}

void BreakThru::write_control(uint8_t v) {
    // Break Thru enables the VBLANK NMI with a 0 in bit 0, Darwin with a 1.
    nmi_enabled_ = game_ == Game::Darwin ? (v & 1) != 0 : (v & 1) == 0;
    if (!(v & 2)) main_cpu_.set_irq(IrqLine::Clear);  // coin IRQ acknowledge
}

uint8_t BreakThru::main_read(uint16_t a) {
    if (a >= 0x4000) return rom_[a - 0x4000];
    if (a >= 0x2000) return banks_[size_t(rom_bank_) * 0x2000 + (a & 0x1fff)];
    if (game_ == Game::Darwin) {
        if (a < 0x100) return read_common(uint16_t(a + 0x1000));
        if (a >= 0x800 && a < 0x804) return read_common(uint16_t(a + 0x1000));
        if (a >= 0x1000) return read_common(uint16_t(a - 0x1000));
        return 0xff;
    }
    if (a < 0x1100 || (a >= 0x1800 && a < 0x1804)) return read_common(a);
    if (a < 0x1800) return ram_[a];
    return 0xff;
}

void BreakThru::main_write(uint16_t a, uint8_t v) {
    if (a >= 0x2000) return;
    if (game_ == Game::Darwin) {
        if (a < 0x100 || (a >= 0x800 && a < 0x803)) write_common(uint16_t(a + 0x1000), v);
        else if (a == 0x803) write_control(v);
        else if (a >= 0x1000) write_common(uint16_t(a - 0x1000), v);
        return;
    }
    if (a < 0x1100 || (a >= 0x1800 && a < 0x1803)) write_common(a, v);
    else if (a < 0x1800) ram_[a] = v;
    else if (a == 0x1803) write_control(v);
}

// ---------------------------------------------------------------------------
// Sound CPU

uint8_t BreakThru::sound_read(uint16_t a) {
    if (a < 0x2000 || a >= 0x8000) return sound_mem_[a];
    switch (a) {
        case 0x2000: return ym3526_.status();
        case 0x4000: return sound_latch_;
        case 0x6000: return ym2203_.status();
        default: return 0xff;
    }
}

void BreakThru::sound_write(uint16_t a, uint8_t v) {
    if (a < 0x2000) {
        sound_mem_[a] = v;
        return;
    }
    switch (a) {
        case 0x2000: ym3526_.control(v); break;
        case 0x2001: ym3526_.write(v); break;
        case 0x6000: ym2203_.control(v); break;
        case 0x6001: ym2203_.write(v); break;
        default: break;
    }
}

void BreakThru::on_sound_cycles(int cycles) {
    audio_accum_ += int64_t(cycles) * YM2203::kSampleRate;
    while (audio_accum_ >= int64_t(kSoundClock)) {
        audio_accum_ -= int64_t(kSoundClock);
        // Scaled down so the loudest passages stay below full scale.
        const int32_t s = (ym2203_.update() + ym3526_.update()) * 2 / 3;
        audio_.push_back(int16_t(std::clamp<int32_t>(s, -32768, 32767)));
    }
}

void BreakThru::drain_audio(std::vector<int16_t>& out) {
    out.insert(out.end(), audio_.begin(), audio_.end());
    audio_.clear();
}

// ---------------------------------------------------------------------------
// Video

// Sprite bytes: 0 = ccc d p bb e (colour, double height, priority, bank,
// enable), 1 = code, 2 = y, 3 = x. `priority` is the value of the enable and
// priority bits (0x01 in front of the tile layer's back part, 0x09 on top).
void BreakThru::draw_sprites(int priority) {
    for (int f = 0; f < 0x40; ++f) {
        const uint8_t* s = &sprite_ram_[size_t(f) * 4];
        if ((s[0] & 0x09) != priority) continue;
        const int code = s[1] + ((s[0] & 0x06) << 7);
        const int color = 0x40 + ((s[0] & 0xe0) >> 2);
        const int x = (240 - s[3]) & 0xff;
        const int y = (224 - s[2]) & 0xff;
        auto tile = [&](int c, int ty) {
            const uint8_t* g = sprites_.element(c & 0x3ff);
            for (int r = 0; r < 16; ++r) {
                uint32_t* out = &screen_[size_t((ty + r) & 0xff) * 256];
                for (int col = 0; col < 16; ++col) {
                    const int px = x + col;
                    if (px >= 256) break;
                    const int pen = g[r * 16 + col];
                    if (pen) out[px] = palette_[size_t(color + pen)];
                }
            }
        };
        if (s[0] & 0x10) {  // 16x32
            tile(code & 0x3fe, y);
            tile(code | 1, y + 16);
        } else {
            tile(code, y + 16);
        }
    }
}

void BreakThru::update_video() {
    // 512x256 tile layer, 32x16 tiles stored column by column.
    auto tiles = [&](bool opaque) {
        for (int y = 0; y < 256; ++y) {
            uint32_t* out = &screen_[size_t(y) * 256];
            for (int x = 0; x < 256; ++x) {
                const int px = (x + scroll_x_) & 0x1ff;
                const int f = (px >> 4) * 16 + (y >> 4);
                const uint8_t attr = fg_video_[size_t(f) * 2 + 1];
                const int code = fg_video_[size_t(f) * 2] + ((attr & 3) << 8);
                const int color = 0x80 + ((bg_color_ + ((attr & 4) >> 2)) << 3);
                const int pen = tiles_.element(code)[(y & 15) * 16 + (px & 15)];
                if (opaque || pen) out[x] = palette_[size_t(color + pen)];
            }
        }
    };
    tiles(true);
    draw_sprites(0x01);
    tiles(false);
    draw_sprites(0x09);
    for (int i = 0; i < 0x400; ++i) {  // text layer, colours 0-7
        const uint8_t* g = chars_.element(bg_video_[size_t(i)]);
        const int cx = (i & 31) * 8, cy = (i >> 5) * 8;
        for (int r = 0; r < 8; ++r)
            for (int c = 0; c < 8; ++c) {
                const int pen = g[r * 8 + c];
                if (pen) screen_[size_t(cy + r) * 256 + size_t(cx + c)] = palette_[size_t(pen)];
            }
    }
    // Visible area: 8-247 both ways.
    for (int y = 0; y < kSize; ++y)
        for (int x = 0; x < kSize; ++x) {
            const uint32_t p = screen_[size_t(8 + y) * 256 + size_t(8 + x)];
            if (game_ == Game::Darwin) framebuffer_[size_t(kSize - 1 - x) * kSize + size_t(y)] = p;  // ROT270
            else framebuffer_[size_t(y) * kSize + size_t(x)] = p;
        }
}

// ---------------------------------------------------------------------------

void BreakThru::run_frame() {
    const double main_cycles = double(kMainClock) / kFramesPerSecond / kScanlines;
    const double sound_cycles = double(kSoundClock) / kFramesPerSecond / kScanlines;
    for (int line = 0; line < kScanlines; ++line) {
        if (line == 0) in1_ |= 0x80;
        if (line == 248) {
            if (nmi_enabled_) main_cpu_.set_nmi(IrqLine::Pulse);
            update_video();
            in1_ &= 0x7f;  // VBLANK
        }
        main_debt_ += main_cycles;
        main_debt_ -= main_cpu_.run(int(main_debt_));
        sound_debt_ += sound_cycles;
        sound_debt_ -= sound_cpu_.run(int(sound_debt_));
    }
}

void BreakThru::set_inputs(const MachineInputs& in) {
    auto pad = [](const InputState& p) {
        uint8_t v = 0xff;
        if (p.button1) v &= ~0x01;
        if (p.button2) v &= ~0x02;
        if (p.down) v &= ~0x04;
        if (p.up) v &= ~0x08;
        if (p.left) v &= ~0x10;
        if (p.right) v &= ~0x20;
        return v;
    };
    in0_ = pad(in.player1);
    if (in.player1.start) in0_ &= ~0x40;
    if (in.player2.start) in0_ &= ~0x80;
    in1_ = uint8_t((pad(in.player2) & 0x7f) | (in1_ & 0x80));
    // A new coin clears its bit for a frame and interrupts the main CPU.
    in2_ = 0xe0;
    if (in.coin1 && !old_coin1_) {
        in2_ &= ~0x20;
        main_cpu_.set_irq(IrqLine::Assert);
    }
    if (in.coin2 && !old_coin2_) {
        in2_ &= ~0x40;
        main_cpu_.set_irq(IrqLine::Assert);
    }
    old_coin1_ = in.coin1;
    old_coin2_ = in.coin2;
}

void BreakThru::set_dip_switch(int bank, uint8_t value) {
    if (bank == 0) dsw_a_ = value;
    else if (bank == 1) dsw_b_ = uint8_t(value & 0x1f);
}

}  // namespace dsp
