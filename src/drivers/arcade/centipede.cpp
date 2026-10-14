#include "drivers/arcade/centipede.h"

#include <algorithm>
#include <cstring>

#include "core/rom_loader.h"

namespace dsp {
namespace {

const std::vector<RomEntry> kCentipedeRom = {
    {"136001-407.d1", 0x800, 0x2000, 0xc4d995eb},
    {"136001-408.e1", 0x800, 0x2800, 0xbcdebe1b},
    {"136001-409.fh1", 0x800, 0x3000, 0x66d7b04a},
    {"136001-410.j1", 0x800, 0x3800, 0x33ce4640},
};
const std::vector<RomEntry> kCentipedeChars = {
    {"136001-211.f7", 0x800, 0x0000, 0x880acfb9},
    {"136001-212.hj7", 0x800, 0x0800, 0xb1397029},
};

const std::vector<RomEntry> kMillipedeRom = {
    {"136013-104.mn1", 0x1000, 0x4000, 0x40711675},
    {"136013-103.l1", 0x1000, 0x5000, 0xfb01baf2},
    {"136013-102.jk1", 0x1000, 0x6000, 0x62e137e0},
    {"136013-101.h1", 0x1000, 0x7000, 0x46752c7d},
};
const std::vector<RomEntry> kMillipedeChars = {
    {"136013-107.r5", 0x800, 0x0000, 0x68c3437a},
    {"136013-106.p5", 0x800, 0x0800, 0xf4468045},
};

// convert_gfx(..., flipx=false, flipy=true): reverse Y.
GfxLayout char_layout() {
    GfxLayout l;
    l.width = l.height = 8;
    l.total = 0x100;
    l.planes = 2;
    l.char_increment = 8 * 8;
    l.plane_offsets = {0x100 * 8 * 8, 0};
    for (int i = 0; i < 8; ++i) l.x_offsets.push_back(i);
    for (int i = 7; i >= 0; --i) l.y_offsets.push_back(i * 8);
    return l;
}

GfxLayout sprite_layout() {
    GfxLayout l;
    l.width = 8;
    l.height = 16;
    l.total = 0x80;
    l.planes = 2;
    l.char_increment = 16 * 8;
    l.plane_offsets = {0x80 * 16 * 8, 0};
    for (int i = 0; i < 8; ++i) l.x_offsets.push_back(i);
    for (int i = 15; i >= 0; --i) l.y_offsets.push_back(i * 8);
    return l;
}

}  // namespace

Centipede::Centipede(Game game)
    : game_(game),
      screen_(256u * 256u, 0xff000000u),
      framebuffer_(size_t(kWidth) * kHeight, 0xff000000u) {
    if (game_ == Game::Millipede) {
        dsw_a_ = 0x14;
        dsw_b_ = 0x02;
        dsw_c_ = 0x04;
        in0_ = in1_ = 0x30;
        in2_ = in3_ = 0xff;
    }

    cpu_.set_memory_handlers([this](uint16_t a) { return main_read(a); },
                             [this](uint16_t a, uint8_t v) { main_write(a, v); });
    cpu_.set_cycle_handler([this](int cycles) { on_cycles(cycles); });

    if (game_ == Game::Millipede) {
        for (int i = 0; i < 8; ++i) {
            pokey0_.set_pot_handler(i, [this](uint8_t) { return dsw_a_; });
            pokey1_.set_pot_handler(i, [this](uint8_t) { return dsw_b_; });
        }
    }
}

bool Centipede::init(const std::string& rom_path, std::string* error) {
    RomLoader loader;
    if (!loader.open(rom_path, error)) return false;

    memory_.fill(0);
    {
        std::vector<uint8_t> rom(0x8000, 0xff);
        if (game_ == Game::Millipede) {
            if (!loader.load(kMillipedeRom, rom, error)) return false;
        } else {
            if (!loader.load(kCentipedeRom, rom, error)) return false;
        }
        std::copy_n(rom.begin(), memory_.size(), memory_.begin());
    }

    std::vector<uint8_t> gfx(0x1000, 0);
    if (game_ == Game::Millipede) {
        if (!loader.load(kMillipedeChars, gfx, error)) return false;
    } else {
        if (!loader.load(kCentipedeChars, gfx, error)) return false;
    }
    chars_.decode(char_layout(), gfx);
    sprites_.decode(sprite_layout(), gfx);

    warnings_ = loader.warnings();
    reset();
    return true;
}

void Centipede::reset() {
    cpu_.reset();
    pokey0_.reset();
    if (game_ == Game::Millipede) pokey1_.reset();
    nvram_.fill(0);
    sprite_pen_.fill(0);
    for (size_t i = 0; i < sprite_pen_.size(); ++i) sprite_pen_[i] = uint8_t(i);
    palette_.fill(0xff000000u);
    flip_screen_ = false;
    audio_accum_ = 0;
    audio_.clear();

    if (game_ == Game::Millipede) {
        std::fill(memory_.begin() + 0x2480, memory_.begin() + 0x24a0, 0xff);
        in0_ = in1_ = 0x30;
        in2_ = in3_ = 0xff;
    } else {
        std::fill(memory_.begin() + 0x1400, memory_.begin() + 0x1410, 0xff);
        in0_ = in1_ = 0xff;
    }
}

// ---------------------------------------------------------------------------
// Memory

uint8_t Centipede::main_read(uint16_t address) {
    if (game_ == Game::Millipede) {
        address &= 0x7fff;
        if (address <= 0x3ff || (address >= 0x1000 && address <= 0x13ff) || address >= 0x4000) {
            return memory_[address];
        }
        if (address >= 0x400 && address <= 0x40f) return pokey0_.read(address & 0xf);
        if (address >= 0x800 && address <= 0x80f) return pokey1_.read(address & 0xf);
        if (address == 0x2000) return uint8_t(dsw_c_ | in0_);
        if (address == 0x2001) return in1_;
        if (address == 0x2010) return in2_;
        if (address == 0x2011) return in3_;
        if (address >= 0x2030 && address <= 0x206f) return nvram_[address & 0x3f];
        return 0xff;
    }

    address &= 0x3fff;
    if (address <= 0x7ff || address >= 0x2000) return memory_[address];
    if (address == 0x800) return dsw_a_;
    if (address == 0x801) return dsw_b_;
    if (address == 0xc00) return dsw_c_;  // vsync + trackball / cabinet
    if (address == 0xc01) return in0_;
    if (address == 0xc02) return 0;  // trackball 2
    if (address == 0xc03) return in1_;
    if (address >= 0x1000 && address <= 0x100f) return pokey0_.read(address & 0xf);
    if (address >= 0x1700 && address <= 0x173f) return nvram_[address & 0x3f];
    return 0xff;
}

void Centipede::main_write(uint16_t address, uint8_t value) {
    if (game_ == Game::Millipede) {
        address &= 0x7fff;
        if (address <= 0x3ff) {
            memory_[address] = value;
            return;
        }
        if (address >= 0x400 && address <= 0x40f) {
            pokey0_.write(address & 0xf, value);
            return;
        }
        if (address >= 0x800 && address <= 0x80f) {
            pokey1_.write(address & 0xf, value);
            return;
        }
        if (address >= 0x1000 && address <= 0x13ff) {
            memory_[address] = value;
            return;
        }
        if (address >= 0x2480 && address <= 0x249f) {
            if (memory_[address] != value) {
                memory_[address] = value;
                update_palette_millipede(uint8_t(address & 0x1f), value);
            }
            return;
        }
        if (address == 0x2506) {
            flip_screen_ = (value & 0x80) != 0;
            return;
        }
        if (address == 0x2600) {
            cpu_.set_irq(IrqLine::Clear);
            return;
        }
        if (address >= 0x2780 && address <= 0x27bf) {
            nvram_[address & 0x3f] = value;
            return;
        }
        return;
    }

    address &= 0x3fff;
    if (address <= 0x3ff) {
        memory_[address] = value;
        return;
    }
    if (address >= 0x400 && address <= 0x7ff) {
        memory_[address] = value;
        return;
    }
    if (address >= 0x1000 && address <= 0x100f) {
        pokey0_.write(address & 0xf, value);
        return;
    }
    if (address >= 0x1400 && address <= 0x140f) {
        if (memory_[address] != value) {
            memory_[address] = value;
            update_palette_centipede(uint8_t(address & 0xf), value);
        }
        return;
    }
    if (address >= 0x1600 && address <= 0x163f) {
        nvram_[address & 0x3f] = value;
        return;
    }
    if (address == 0x1800) {
        cpu_.set_irq(IrqLine::Clear);
        return;
    }
    if (address == 0x1c07) flip_screen_ = (value & 0x80) != 0;
}

void Centipede::update_palette_centipede(uint8_t pos, uint8_t value) {
    // Bit 2 of the palette address must be set or the write is ignored.
    if ((pos & 4) == 0) return;
    uint8_t r = uint8_t(0xff * ((~value >> 0) & 1));
    uint8_t g = uint8_t(0xff * ((~value >> 1) & 1));
    uint8_t b = uint8_t(0xff * ((~value >> 2) & 1));
    if ((~value & 0x08) != 0) {
        if (b) b = 0xc0;
        else if (g) g = 0xc0;
    }
    const uint32_t rgb = 0xff000000u | uint32_t(r) << 16 | uint32_t(g) << 8 | b;
    if (pos & 0x08) {
        palette_[pos] = rgb;
        for (int f = 0; f < 0x40; ++f) {
            const int base = f * 4;
            if ((pos & 3) == (((base) >> 2) & 3)) sprite_pen_[size_t(base + 1)] = pos;
            if ((pos & 3) == (((base) >> 4) & 3)) sprite_pen_[size_t(base + 2)] = pos;
            if ((pos & 3) == (((base) >> 6) & 3)) sprite_pen_[size_t(base + 3)] = pos;
        }
    } else {
        palette_[pos & 3] = rgb;
    }
}

void Centipede::update_palette_millipede(uint8_t pos, uint8_t value) {
    const uint8_t r =
        uint8_t(0x21 * ((~value >> 5) & 1) + 0x47 * ((~value >> 6) & 1) + 0x97 * ((~value >> 7) & 1));
    const uint8_t g = uint8_t(0x47 * ((~value >> 3) & 1) + 0x97 * ((~value >> 4) & 1));
    const uint8_t b =
        uint8_t(0x21 * ((~value >> 0) & 1) + 0x47 * ((~value >> 1) & 1) + 0x97 * ((~value >> 2) & 1));
    palette_[pos] = 0xff000000u | uint32_t(r) << 16 | uint32_t(g) << 8 | b;
    if (pos >= 0x10) {
        const int base = (pos & 0xc) << 6;
        for (int f = 0; f < 0x40; ++f) {
            const int res = base + f * 4;
            if ((pos & 3) == ((res >> 2) & 3)) sprite_pen_[size_t(res + 1)] = pos;
            if ((pos & 3) == ((res >> 4) & 3)) sprite_pen_[size_t(res + 2)] = pos;
            if ((pos & 3) == ((res >> 6) & 3)) sprite_pen_[size_t(res + 3)] = pos;
        }
    }
}

void Centipede::on_cycles(int cycles) {
    pokey0_.run(cycles);
    if (game_ == Game::Millipede) pokey1_.run(cycles);
    audio_accum_ += int64_t(cycles) * kSampleRate;
    while (audio_accum_ >= int64_t(kCpuClock)) {
        audio_accum_ -= int64_t(kCpuClock);
        int32_t s = pokey0_.update();
        if (game_ == Game::Millipede) s += pokey1_.update();
        audio_.push_back(int16_t(std::clamp(s, -32768, 32767)));
    }
}

void Centipede::drain_audio(std::vector<int16_t>& out) {
    out.insert(out.end(), audio_.begin(), audio_.end());
    audio_.clear();
}

// ---------------------------------------------------------------------------
// Video

void Centipede::draw_sprites() {
    const int spr_base = game_ == Game::Millipede ? 0x13c0 : 0x7c0;
    for (int f = 0; f < 0x10; ++f) {
        const int y = 255 - memory_[size_t(spr_base + 0x20 + f)];
        if (y <= 7) continue;
        const uint8_t atrib = memory_[size_t(spr_base + f)];
        const int nchar = ((atrib & 0x3e) >> 1) | ((atrib & 1) << 6);
        const int color = memory_[size_t(spr_base + 0x30 + f)] << 2;
        const int x = 240 - memory_[size_t(spr_base + 0x10 + f)];
        const bool flip_x = (atrib & 0x80) != 0;
        const bool flip_y = game_ == Game::Centipede && (atrib & 0x40) != 0;
        const uint8_t* g = sprites_.element(nchar);
        for (int r = 0; r < 16; ++r) {
            const int py = (y - 7) + r;
            if (py < 0 || py >= 256) continue;
            const uint8_t* row = g + (flip_y ? 15 - r : r) * 8;
            uint32_t* out = &screen_[size_t(py) * 256];
            for (int c = 0; c < 8; ++c) {
                const int px = x + c;
                if (px < 0 || px >= 256) continue;
                const int pen = row[flip_x ? 7 - c : c];
                if (pen == 0) continue;
                const uint8_t pal = sprite_pen_[size_t(color + pen) & 0x3ff];
                out[px] = palette_[pal & 0x1f];
            }
        }
    }
}

void Centipede::update_video() {
    std::fill(screen_.begin(), screen_.end(), palette_[0]);
    const int tile_base = game_ == Game::Millipede ? 0x1000 : 0x400;
    for (int f = 0; f < 0x400; ++f) {
        const int tx = f / 32;
        const int ty = 31 - (f % 32);
        const uint8_t atrib = memory_[size_t(tile_base + f)];
        int nchar;
        int color_base;
        bool flip_x = false, flip_y = false;
        if (game_ == Game::Millipede) {
            nchar = (atrib & 0x3f) | 0x40 | ((atrib & 0x40) << 1);
            color_base = ((atrib >> 6) & 3) << 2;
        } else {
            nchar = (atrib & 0x3f) | 0x40;
            color_base = 0;
            flip_x = (atrib & 0x40) != 0;
            flip_y = (atrib & 0x80) != 0;
        }
        const uint8_t* g = chars_.element(nchar);
        for (int r = 0; r < 8; ++r) {
            const int py = ty * 8 + r;
            const uint8_t* row = g + (flip_y ? 7 - r : r) * 8;
            uint32_t* out = &screen_[size_t(py) * 256];
            for (int c = 0; c < 8; ++c) {
                const int px = tx * 8 + c;
                const int pen = row[flip_x ? 7 - c : c];
                out[px] = palette_[size_t(color_base + pen) & 0x1f];
            }
        }
    }
    draw_sprites();

    // Crop left 240 columns; cocktail mode flips both axes.
    for (int y = 0; y < kHeight; ++y) {
        const int sy = flip_screen_ ? (kHeight - 1 - y) : y;
        const uint32_t* src = &screen_[size_t(sy) * 256];
        uint32_t* dst = &framebuffer_[size_t(y) * kWidth];
        if (flip_screen_) {
            for (int x = 0; x < kWidth; ++x) dst[x] = src[kWidth - 1 - x];
        } else {
            std::memcpy(dst, src, size_t(kWidth) * sizeof(uint32_t));
        }
    }
}

void Centipede::run_frame() {
    const int cycles = int(double(kCpuClock) / kFramesPerSecond / kScanlines + 0.5);
    for (int line = 0; line < kScanlines; ++line) {
        switch (line) {
            case 0:
                // VBLANK clear. Centipede reads it from DSWC; Millipede ORs
                // DSWC into $2000 (same bit 6 as MAME's IN0 vblank).
                dsw_c_ = uint8_t(dsw_c_ & ~0x40);
                break;
            case 16:
            case 80:
            case 144:
            case 208:
                cpu_.set_irq(IrqLine::Clear);
                break;
            case 48:
            case 112:
            case 176:
                cpu_.set_irq(IrqLine::Assert);
                break;
            case 240:
                update_video();
                cpu_.set_irq(IrqLine::Assert);
                dsw_c_ = uint8_t(dsw_c_ | 0x40);
                break;
            default:
                break;
        }
        cpu_.run(cycles);
    }
}

void Centipede::set_inputs(const MachineInputs& in) {
    if (game_ == Game::Millipede) {
        in0_ = 0x30;
        in1_ = 0x30;
        in2_ = 0xff;
        in3_ = 0xff;
        if (in.player1.button1) in0_ &= ~0x10;
        if (in.player1.start) in0_ &= ~0x20;
        if (in.player2.button1) in1_ &= ~0x10;
        if (in.player2.start) in1_ &= ~0x20;
        if (in.player1.right) in2_ &= ~0x01;
        if (in.player1.left) in2_ &= ~0x02;
        if (in.player1.down) in2_ &= ~0x04;
        if (in.player1.up) in2_ &= ~0x08;
        if (in.coin1) in2_ &= ~0x20;
        if (in.coin2) in2_ &= ~0x40;
        if (in.player2.right) in3_ &= ~0x01;
        if (in.player2.left) in3_ &= ~0x02;
        if (in.player2.down) in3_ &= ~0x04;
        if (in.player2.up) in3_ &= ~0x08;
        return;
    }

    in0_ = 0xff;
    in1_ = 0xff;
    if (in.player1.start) in0_ &= ~0x01;
    if (in.player2.start) in0_ &= ~0x02;
    if (in.player1.button1) in0_ &= ~0x04;
    if (in.player2.button1) in0_ &= ~0x08;
    if (in.coin1) in0_ &= ~0x20;
    if (in.coin2) in0_ &= ~0x40;
    if (in.player2.up) in1_ &= ~0x01;
    if (in.player2.down) in1_ &= ~0x02;
    if (in.player2.left) in1_ &= ~0x04;
    if (in.player2.right) in1_ &= ~0x08;
    if (in.player1.up) in1_ &= ~0x10;
    if (in.player1.down) in1_ &= ~0x20;
    if (in.player1.left) in1_ &= ~0x40;
    if (in.player1.right) in1_ &= ~0x80;
}

void Centipede::set_dip_switch(int bank, uint8_t value) {
    if (bank == 0) dsw_a_ = value;
    else if (bank == 1) dsw_b_ = value;
    else if (bank == 2) {
        // Preserve the live VBLANK bit (both games use dsw_c bit 6).
        dsw_c_ = uint8_t((dsw_c_ & 0x40) | (value & ~0x40));
    }
}

}  // namespace dsp
