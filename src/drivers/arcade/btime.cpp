#include "drivers/arcade/btime.h"

#include <algorithm>

#include "core/rom_loader.h"

namespace dsp {
namespace {

struct GameRoms {
    std::vector<RomEntry> program, sound, chars, tiles, tile_map, prom;
};

const GameRoms kBurgerTime = {
    {{"aa04.9b", 0x1000, 0xc000, 0x368a25b5}, {"aa06.13b", 0x1000, 0xd000, 0xb4ba400d},
     {"aa05.10b", 0x1000, 0xe000, 0x8005bffa}, {"aa07.15b", 0x1000, 0xf000, 0x086440ad}},
    {{"ab14.12h", 0x1000, 0, 0xf55e5211}},
    {{"aa12.7k", 0x1000, 0x0000, 0xc4617243}, {"ab13.9k", 0x1000, 0x1000, 0xac01042f},
     {"ab10.10k", 0x1000, 0x2000, 0x854a872a}, {"ab11.12k", 0x1000, 0x3000, 0xd4848014},
     {"aa8.13k", 0x1000, 0x4000, 0x8650c788}, {"ab9.15k", 0x1000, 0x5000, 0x8dec15e6}},
    {{"ab00.1b", 0x800, 0x0000, 0xc7a14485}, {"ab01.3b", 0x800, 0x0800, 0x25b49078},
     {"ab02.4b", 0x800, 0x1000, 0xb8ef56c3}},
    {{"ab03.6b", 0x800, 0, 0xd26bc1f3}},
    {},
};

const GameRoms kLockNChase = {
    {{"s3-3d", 0x1000, 0xc000, 0x1ab4f2c2}, {"s2-3c", 0x1000, 0xd000, 0x5e46b789},
     {"s1-3b", 0x1000, 0xe000, 0x1308a32e}, {"s0-3a", 0x1000, 0xf000, 0xbeb4b1fc}},
    {{"sa-1h", 0x1000, 0, 0x379387ec}},
    {{"s4-11l", 0x1000, 0x0000, 0xa2162a9e}, {"s5-11m", 0x1000, 0x1000, 0x12f1c2db},
     {"s6-13l", 0x1000, 0x2000, 0xd21e2a57}, {"s7-13m", 0x1000, 0x3000, 0xc4f247cd},
     {"s8-15l", 0x1000, 0x4000, 0x672a92d0}, {"s9-15m", 0x1000, 0x5000, 0x87c8ee9a}},
    {},
    {},
    {{"sc-5m", 0x20, 0, 0x2a976ebe}},
};

const GameRoms kMinkyMonkey = {
    {{"mmonkey.e4", 0x1000, 0xc000, 0x8d31bf6a}, {"mmonkey.d4", 0x1000, 0xd000, 0xe54f584a},
     {"mmonkey.b4", 0x1000, 0xe000, 0x399a161e}, {"mmonkey.a4", 0x1000, 0xf000, 0xf7d3d1e3}},
    {{"mmonkey.h1", 0x1000, 0, 0x5bcb2e81}},
    {{"mmonkey.l11", 0x1000, 0x0000, 0xb6aa8566}, {"mmonkey.m11", 0x1000, 0x1000, 0x6cc4d0c4},
     {"mmonkey.l13", 0x1000, 0x2000, 0x2a343b7e}, {"mmonkey.m13", 0x1000, 0x3000, 0x0230b50d},
     {"mmonkey.l14", 0x1000, 0x4000, 0x922bb3e1}, {"mmonkey.m14", 0x1000, 0x5000, 0xf943e28c}},
    {},
    {},
    {{"mmi6331.m5", 0x20, 0, 0x55e28b32}},
};

uint8_t bitswap8(uint8_t v, const int (&bits)[8]) {
    uint8_t r = 0;
    for (int i = 0; i < 8; ++i)
        if (v & (1 << bits[i])) r = uint8_t(r | (1 << (7 - i)));
    return r;
}

uint32_t rgb(int r, int g, int b) { return 0xff000000u | uint32_t(r) << 16 | uint32_t(g) << 8 | uint32_t(b); }

// 16x16 elements: right half first, rows of 8 bits; three planes `plane`
// bits apart. The graphics are stored for the turned monitor.
GfxLayout tile_layout(int total, int plane) {
    GfxLayout l;
    l.width = l.height = 16;
    l.total = total;
    l.planes = 3;
    l.char_increment = 32 * 8;
    l.plane_offsets = {2 * plane, plane, 0};
    for (int i = 0; i < 8; ++i) l.x_offsets.push_back(16 * 8 + i);
    for (int i = 0; i < 8; ++i) l.x_offsets.push_back(i);
    for (int i = 0; i < 16; ++i) l.y_offsets.push_back(i * 8);
    l.rotate_ccw = true;
    return l;
}

}  // namespace

BurgerTime::BurgerTime(Game game)
    : game_(game), screen_(256u * 256u, 0xff000000u), framebuffer_(size_t(kSize) * kSize, 0xff000000u) {
    main_cpu_.set_memory_handlers(
        [this](uint16_t a) { return btime() ? main_read(a) : lnc_read(a); },
        [this](uint16_t a, uint8_t v) {
            if (btime()) main_write(a, v);
            else lnc_write(a, v);
        });
    main_cpu_.set_opcode_read([this](uint16_t a) { return opcode_read(a); });
    sound_cpu_.set_memory_handlers([this](uint16_t a) { return sound_read(a); },
                                   [this](uint16_t a, uint8_t v) { sound_write(a, v); });
    sound_cpu_.set_cycle_handler([this](int cycles) { on_sound_cycles(cycles); });
    // AY 0 port A bit 0 low lets the 16-line NMI through to the sound CPU.
    ay0_.set_port_handlers(nullptr, nullptr, [this](uint8_t v) { sound_nmi_enabled_ = !(v & 1); },
                           nullptr);
    if (game_ == Game::LockNChase) dsw_b_ = 0xff;
    if (game_ == Game::MinkyMonkey) dsw_b_ = 0xe9;
}

const char* BurgerTime::title() const {
    switch (game_) {
        case Game::LockNChase: return "Lock'n'Chase";
        case Game::MinkyMonkey: return "Minky Monkey";
        default: return "BurgerTime";
    }
}

bool BurgerTime::init(const std::string& rom_path, std::string* error) {
    const GameRoms& roms =
        game_ == Game::LockNChase ? kLockNChase : game_ == Game::MinkyMonkey ? kMinkyMonkey : kBurgerTime;
    RomLoader loader;
    if (!loader.open(rom_path, error)) return false;

    std::vector<uint8_t> program(0x10000, 0);
    if (!loader.load(roms.program, program, error)) return false;
    std::copy(program.begin() + 0xc000, program.end(), memory_.begin() + 0xc000);
    std::vector<uint8_t> sound(0x1000, 0);
    if (!loader.load(roms.sound, sound, error)) return false;
    std::copy(sound.begin(), sound.end(), sound_rom_.begin());

    // Characters and sprites share the same 3 bpp ROMs.
    std::vector<uint8_t> gfx(0x6000, 0);
    if (!loader.load(roms.chars, gfx, error)) return false;
    {
        GfxLayout l;
        l.width = l.height = 8;
        l.total = 1024;
        l.planes = 3;
        l.char_increment = 8 * 8;
        l.plane_offsets = {2 * 1024 * 8 * 8, 1024 * 8 * 8, 0};
        for (int i = 0; i < 8; ++i) l.x_offsets.push_back(i);
        for (int i = 0; i < 8; ++i) l.y_offsets.push_back(i * 8);
        l.rotate_ccw = true;
        chars_.decode(l, gfx);
    }
    sprites_.decode(tile_layout(256, 256 * 16 * 16), gfx);

    if (btime()) {
        std::vector<uint8_t> tiles(0x1800, 0);
        if (!loader.load(roms.tiles, tiles, error)) return false;
        tiles_.decode(tile_layout(64, 64 * 16 * 16), tiles);
        std::vector<uint8_t> map(0x800, 0);
        if (!loader.load(roms.tile_map, map, error)) return false;
        std::copy(map.begin(), map.end(), bg_map_.begin());
    } else {
        std::vector<uint8_t> prom(0x20, 0);
        if (!loader.load(roms.prom, prom, error)) return false;
        for (int i = 0; i < 0x20; ++i) {
            const uint8_t v = prom[size_t(i)];
            const int r = 0x21 * ((v >> 7) & 1) + 0x47 * ((v >> 6) & 1) + 0x97 * ((v >> 5) & 1);
            const int g = 0x21 * ((v >> 4) & 1) + 0x47 * ((v >> 3) & 1) + 0x97 * ((v >> 2) & 1);
            const int b = 0x47 * ((v >> 1) & 1) + 0x97 * (v & 1);
            palette_[size_t(i)] = rgb(r, g, b);
        }
    }

    warnings_ = loader.warnings();
    reset();
    return true;
}

void BurgerTime::reset() {
    std::fill(memory_.begin(), memory_.begin() + 0xc000, 0);
    sound_ram_.fill(0);
    video_ram_.fill(0);
    color_ram_.fill(0);
    if (btime()) {
        palette_ram_.fill(0);
        for (int i = 0; i < 16; ++i) set_color(i, 0);
    }
    in0_ = in1_ = 0xff;
    in2_ = btime() ? 0x3f : 0xff;
    sound_latch_ = 0;
    sound_nmi_enabled_ = false;
    charbank_ = 1;
    bg_control_ = 0;
    had_written_ = false;
    prot_status_ = prot_ret_ = prot_command_ = prot_value_ = 0;
    main_debt_ = sound_debt_ = 0;
    audio_accum_ = 0;
    audio_.clear();
    main_cpu_.reset();
    sound_cpu_.reset();
    ay0_.reset();
    ay1_.reset();
}

uint16_t BurgerTime::mirror_index(uint16_t a) {
    const int x = (a & 0x3ff) >> 5, y = a & 0x1f;
    return uint16_t((y << 5) + x);
}

// ---------------------------------------------------------------------------
// Main CPU

uint8_t BurgerTime::opcode_read(uint16_t a) {
    if (btime()) {
        uint8_t v = main_read(a);
        if (had_written_) {
            had_written_ = false;
            static constexpr int kSwap[8] = {6, 5, 3, 4, 2, 7, 1, 0};
            if ((a & 0x104) == 0x104) v = bitswap8(v, kSwap);
        }
        return v;
    }
    const uint8_t v = lnc_read(a);
    if (a < 0x3c00 || (a >= 0xb000 && a < 0xb200) || a >= 0xc000) {
        static constexpr int kSwap[8] = {7, 5, 6, 4, 3, 2, 1, 0};
        return bitswap8(v, kSwap);
    }
    return v;
}

uint8_t BurgerTime::main_read(uint16_t a) {
    if (a < 0x800 || a >= 0xb000) return memory_[a];
    if (a >= 0x1000 && a < 0x1400) return video_ram_[a & 0x3ff];
    if (a >= 0x1400 && a < 0x1800) return color_ram_[a & 0x3ff];
    if (a >= 0x1800 && a < 0x1c00) return video_ram_[mirror_index(a)];
    if (a >= 0x1c00 && a < 0x2000) return color_ram_[mirror_index(a)];
    switch (a) {
        case 0x4000: return in0_;
        case 0x4001: return in1_;
        case 0x4002: return in2_;
        case 0x4003: return dsw_a_;
        case 0x4004: return dsw_b_;
        default: return 0;
    }
}

void BurgerTime::set_color(int index, uint8_t value) {
    const uint8_t v = uint8_t(~value);  // stored inverted
    const int r = 0x21 * (v & 1) + 0x47 * ((v >> 1) & 1) + 0x97 * ((v >> 2) & 1);
    const int g = 0x21 * ((v >> 3) & 1) + 0x47 * ((v >> 4) & 1) + 0x97 * ((v >> 5) & 1);
    const int b = 0x47 * ((v >> 6) & 1) + 0x97 * ((v >> 7) & 1);
    palette_[size_t(index)] = rgb(r, g, b);
}

void BurgerTime::main_write(uint16_t a, uint8_t v) {
    had_written_ = true;
    if (a < 0x800) {
        memory_[a] = v;
    } else if (a >= 0xc00 && a < 0xc10) {
        palette_ram_[a & 0xf] = v;
        set_color(a & 0xf, v);
    } else if (a >= 0x1000 && a < 0x1400) {
        video_ram_[a & 0x3ff] = v;
    } else if (a >= 0x1400 && a < 0x1800) {
        color_ram_[a & 0x3ff] = v;
    } else if (a >= 0x1800 && a < 0x1c00) {
        video_ram_[mirror_index(a)] = v;
    } else if (a >= 0x1c00 && a < 0x2000) {
        color_ram_[mirror_index(a)] = v;
    } else if (a == 0x4003) {
        sound_latch_ = v;
        ++sound_commands_;
        sound_cpu_.set_irq(IrqLine::Assert);
    } else if (a == 0x4004) {
        bg_control_ = v;
    }
}

uint8_t BurgerTime::lnc_read(uint16_t a) {
    if (game_ == Game::MinkyMonkey && a >= 0xb000 && a < 0xc000) return protection_read(a);
    if (a < 0x3c00 || (a >= 0xb000 && a < 0xb200) || a >= 0xc000) return memory_[a];
    if (a < 0x4000) return video_ram_[a & 0x3ff];
    if (a >= 0x7c00 && a < 0x8000) return video_ram_[mirror_index(a)];
    switch (a) {
        case 0x8000: return dsw_a_;
        case 0x8001: return dsw_b_;
        case 0x9000: return in0_;
        case 0x9001: return in1_;
        case 0x9002: return in2_;
        default: return 0;
    }
}

void BurgerTime::lnc_write(uint16_t a, uint8_t v) {
    if (game_ == Game::MinkyMonkey && a >= 0xb000 && a < 0xc000) {
        protection_write(a, v);
    } else if (a < 0x3c00 || (a >= 0xb000 && a < 0xb200)) {
        memory_[a] = v;
    } else if (a < 0x4000) {
        // The character bank latch is stored with every character written.
        video_ram_[a & 0x3ff] = v;
        color_ram_[a & 0x3ff] = charbank_;
    } else if (a >= 0x7c00 && a < 0x8000) {
        video_ram_[mirror_index(a)] = v;
    } else if (a == 0x8003) {
        charbank_ = v;
    } else if (a == 0x9002) {
        sound_latch_ = v;
        ++sound_commands_;
        sound_cpu_.set_irq(IrqLine::Assert);
    }
}

uint8_t BurgerTime::protection_read(uint16_t a) {
    a &= 0xfff;
    if (a == 0) return prot_status_;
    if (a >= 0xd00 && a <= 0xd02) return memory_[0xb000 + a];
    if (a == 0xe00) return prot_ret_;
    return 0;
}

void BurgerTime::protection_write(uint16_t a, uint8_t v) {
    a &= 0xfff;
    if (a == 0) {
        if (v != 0) return;
        uint8_t* m = &memory_[0xbd00];
        if (prot_command_ == 0) {  // BCD score addition
            auto bcd = [](const uint8_t* p) {
                int r = 0;
                for (int i = 2; i >= 0; --i) r = r * 100 + (p[i] >> 4) * 10 + (p[i] & 0xf);
                return r;
            };
            int r = bcd(m) + bcd(m + 3);
            for (int i = 0; i < 3; ++i) {
                const int lo = r % 10;
                r /= 10;
                const int hi = r % 10;
                r /= 10;
                m[i] = uint8_t(lo | hi << 4);
            }
        } else if (prot_command_ == 1) {  // look the value up in the decryption table
            for (int f = 0; f < 0x100; ++f) {
                if (memory_[0xbf00 + size_t(f)] == prot_value_) {
                    prot_ret_ = uint8_t(f);
                    break;
                }
            }
        }
        prot_status_ = 0;
    } else if (a == 0xc00) {
        prot_command_ = v;
    } else if ((a >= 0xd00 && a <= 0xd05) || a >= 0xf00) {
        memory_[0xb000 + a] = v;
    } else if (a == 0xe00) {
        prot_value_ = v;
    }
}

// ---------------------------------------------------------------------------
// Sound CPU

uint8_t BurgerTime::sound_read(uint16_t a) {
    if (a < 0x2000) return sound_ram_[a & 0x3ff];
    if (a >= 0xa000 && a < 0xc000) {
        sound_cpu_.set_irq(IrqLine::Clear);
        return sound_latch_;
    }
    if (a >= 0xe000) return sound_rom_[a & 0xfff];
    return 0;
}

void BurgerTime::sound_write(uint16_t a, uint8_t v) {
    if (a < 0x2000) sound_ram_[a & 0x3ff] = v;
    else if (a < 0x4000) ay0_.write(v);
    else if (a < 0x6000) ay0_.control(v);
    else if (a < 0x8000) ay1_.write(v);
    else if (a < 0xa000) ay1_.control(v);
    else if (a >= 0xc000 && a < 0xe000) sound_nmi_enabled_ = (v & 1) != 0;
}

void BurgerTime::on_sound_cycles(int cycles) {
    audio_accum_ += int64_t(cycles) * AY8910::kSampleRate;
    while (audio_accum_ >= int64_t(kSoundClock)) {
        audio_accum_ -= int64_t(kSoundClock);
        const int32_t s = (ay0_.update() + ay1_.update()) / 3;  // two AYs: keep headroom
        audio_.push_back(int16_t(std::clamp<int32_t>(s, -32768, 32767)));
    }
}

void BurgerTime::drain_audio(std::vector<int16_t>& out) {
    out.insert(out.end(), audio_.begin(), audio_.end());
    audio_.clear();
}

// ---------------------------------------------------------------------------
// Video

void BurgerTime::update_video() {
    if (btime()) {
        // Background: 16x16 tiles from one of eight 256-byte maps, colours 8-15.
        if (bg_control_ & 0x10) {
            static constexpr int kMapOrder[8] = {1, 2, 3, 0, 5, 6, 7, 4};
            const uint8_t* map = &bg_map_[size_t(kMapOrder[bg_control_ & 7]) * 0x100];
            for (int f = 0; f < 0x100; ++f) {
                const uint8_t* g = tiles_.element(map[f]);
                const int tx = (f & 0xf) * 16, ty = (f >> 4) * 16;
                for (int r = 0; r < 16; ++r)
                    for (int c = 0; c < 16; ++c)
                        screen_[size_t(ty + r) * 256 + size_t(tx + c)] = palette_[size_t(8 + g[r * 16 + c])];
            }
        } else {
            std::fill(screen_.begin(), screen_.end(), palette_[0]);
        }
    }
    // Characters (transparent on BurgerTime, where the background shows).
    for (int f = 0; f < 0x400; ++f) {
        const int code = video_ram_[size_t(f)] + ((color_ram_[size_t(f)] & 3) << 8);
        const uint8_t* g = chars_.element(code);
        const int tx = (f & 0x1f) * 8, ty = (f >> 5) * 8;
        for (int r = 0; r < 8; ++r)
            for (int c = 0; c < 8; ++c) {
                const int pen = g[r * 8 + c];
                if (pen || !btime()) screen_[size_t(ty + r) * 256 + size_t(tx + c)] = palette_[size_t(pen)];
            }
    }
    // Eight sprites in the character RAM: attribute, code, x, y every 0x80.
    const int y_adjust = game_ == Game::MinkyMonkey ? 0 : 1;
    for (int f = 0; f < 8; ++f) {
        const uint8_t attr = video_ram_[size_t(f) * 0x80];
        if (!(attr & 1)) continue;
        const int x = 240 - video_ram_[size_t(f) * 0x80 + 0x40];
        const int y = video_ram_[size_t(f) * 0x80 + 0x60] - y_adjust;
        const uint8_t* g = sprites_.element(video_ram_[size_t(f) * 0x80 + 0x20]);
        const bool flip_x = (attr & 2) != 0, flip_y = (attr & 4) != 0;
        for (int r = 0; r < 16; ++r) {
            const int py = y + r;
            if (py < 0 || py >= 256) continue;
            const uint8_t* row = g + (flip_y ? 15 - r : r) * 16;
            for (int c = 0; c < 16; ++c) {
                const int px = x + c;
                if (px < 0 || px >= 256) continue;
                const int pen = row[flip_x ? 15 - c : c];
                if (pen) screen_[size_t(py) * 256 + size_t(px)] = palette_[size_t(pen)];
            }
        }
    }
    for (int y = 0; y < kSize; ++y)
        std::copy_n(screen_.begin() + long(8 + y) * 256 + 8, kSize, framebuffer_.begin() + long(y) * kSize);
}

// ---------------------------------------------------------------------------

void BurgerTime::run_frame() {
    const double main_cycles = double(kMainClock) / kFramesPerSecond / kScanlines;
    const double sound_cycles = double(kSoundClock) / kFramesPerSecond / kScanlines;
    for (int line = 0; line < kScanlines; ++line) {
        if (line == 0) dsw_a_ &= 0x7f;
        if (line == 248) {
            dsw_a_ |= 0x80;  // VBLANK
            update_video();
        }
        main_debt_ += main_cycles;
        main_debt_ -= main_cpu_.run(int(main_debt_));
        sound_debt_ += sound_cycles;
        sound_debt_ -= sound_cpu_.run(int(sound_debt_));
        // 16-line square wave on the sound CPU NMI, gated by the AY port.
        sound_cpu_.set_nmi((line & 8) && sound_nmi_enabled_ ? IrqLine::Assert : IrqLine::Clear);
    }
}

void BurgerTime::set_inputs(const MachineInputs& in) {
    auto pad = [](const InputState& p) {
        uint8_t v = 0xff;
        if (p.right) v &= ~0x01;
        if (p.left) v &= ~0x02;
        if (p.up) v &= ~0x04;
        if (p.down) v &= ~0x08;
        if (p.button1) v &= ~0x10;
        return v;
    };
    in0_ = pad(in.player1);
    in1_ = pad(in.player2);
    // Coins: active high on BurgerTime, low on the others; both hold the NMI.
    if (btime()) {
        in2_ = 0x3f;
        if (in.coin1) in2_ |= 0x40;
        else if (in.coin2) in2_ |= 0x80;
    } else {
        in2_ = 0xff;
        if (in.coin1) in2_ &= ~0x40;
        else if (in.coin2) in2_ &= ~0x80;
    }
    main_cpu_.set_nmi(in.coin1 || in.coin2 ? IrqLine::Assert : IrqLine::Clear);
    if (in.player1.start) in2_ &= ~0x01;
    if (in.player2.start) in2_ &= ~0x02;
}

void BurgerTime::set_dip_switch(int bank, uint8_t value) {
    if (bank == 0) dsw_a_ = uint8_t((dsw_a_ & 0x80) | (value & 0x7f));
    else if (bank == 1) dsw_b_ = value;
}

}  // namespace dsp
