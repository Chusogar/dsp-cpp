#include "drivers/arcade/blktiger.h"

#include <algorithm>
#include <cstring>

#include "core/rom_loader.h"

namespace dsp {
namespace {

const std::vector<RomEntry> kMain = {
    {"bdu-01a.5e", 0x8000, 0x00000, 0xa8f98f22}, {"bdu-02a.6e", 0x10000, 0x08000, 0x7bef96e8},
    {"bdu-03a.8e", 0x10000, 0x18000, 0x4089e157}, {"bd-04.9e", 0x10000, 0x28000, 0xed6af6ec},
    {"bd-05.10e", 0x10000, 0x38000, 0xae59b72e},
};
const std::vector<RomEntry> kSound = {{"bd-06.1l", 0x8000, 0, 0x2cf54274}};
const std::vector<RomEntry> kMcu = {{"bd.6k", 0x1000, 0, 0xac7d14f1}};
const std::vector<RomEntry> kChars = {{"bd-15.2n", 0x8000, 0, 0x70175d78}};
const std::vector<RomEntry> kTiles = {
    {"bd-12.5b", 0x10000, 0x00000, 0xc4524993}, {"bd-11.4b", 0x10000, 0x10000, 0x7932c86f},
    {"bd-14.9b", 0x10000, 0x20000, 0xdc49593a}, {"bd-13.8b", 0x10000, 0x30000, 0x7ed7a122},
};
const std::vector<RomEntry> kSprites = {
    {"bd-08.5a", 0x10000, 0x00000, 0xe2f17438}, {"bd-07.4a", 0x10000, 0x10000, 0x5fccbd27},
    {"bd-10.9a", 0x10000, 0x20000, 0xfc33ccc6}, {"bd-09.8a", 0x10000, 0x30000, 0xf449de01},
};

// Background priority split by colour (MAME's guessed table): the group
// selects which pens of the tile are drawn in front of the sprites.
constexpr uint8_t kSplitTable[16] = {3, 3, 2, 2, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
constexpr uint16_t kFrontTransMask[4] = {0xffff, 0xfff0, 0xff00, 0xf000};
constexpr uint16_t kBackTransMask[4] = {0x8000, 0x800f, 0x80ff, 0x8fff};

uint32_t pal4(uint8_t r, uint8_t g, uint8_t b) {
    r &= 15, g &= 15, b &= 15;
    return 0xff000000u | uint32_t(r * 17) << 16 | uint32_t(g * 17) << 8 | uint32_t(b * 17);
}

GfxLayout layout16(int total, int half_bits) {
    GfxLayout l;
    l.width = l.height = 16;
    l.total = total;
    l.planes = 4;
    l.char_increment = 32 * 16;
    l.plane_offsets = {half_bits + 4, half_bits, 4, 0};
    l.x_offsets = {0, 1, 2, 3, 8, 9, 10, 11, 256, 257, 258, 259, 264, 265, 266, 267};
    for (int i = 0; i < 16; ++i) l.y_offsets.push_back(i * 16);
    return l;
}

}  // namespace

BlackTiger::BlackTiger()
    : screen_(256u * 256u, 0xff000000u),
      framebuffer_(size_t(kScreenWidth) * kScreenHeight, 0xff000000u) {
    main_cpu_.set_memory_handlers([this](uint16_t a) { return main_read(a); },
                                  [this](uint16_t a, uint8_t v) { main_write(a, v); });
    main_cpu_.set_io_handlers([this](uint16_t p) { return main_in(uint8_t(p)); },
                              [this](uint16_t p, uint8_t v) { main_out(uint8_t(p), v); });
    sound_cpu_.set_memory_handlers([this](uint16_t a) { return sound_read(a); },
                                   [this](uint16_t a, uint8_t v) { sound_write(a, v); });
    sound_cpu_.set_cycle_handler([this](int cycles) { on_sound_cycles(cycles); });
    ym0_.set_irq_handler([this](bool on) {
        if (on) ++sound_irqs_;
        sound_cpu_.set_irq(on ? IrqLine::Assert : IrqLine::Clear);
    });

    // Protection MCU: port 0 is the latch pair with the main CPU.
    mcu_.set_port_read_handler(0, [this] {
        mcu_.set_irq1_line(IrqLine::Clear);
        ++mcu_reads_;
        return z80_latch_;
    });
    mcu_.set_port_write_handler(0, [this](uint8_t v) { i8751_latch_ = v; });
}

bool BlackTiger::init(const std::string& rom_path, std::string* error) {
    RomLoader loader;
    if (!loader.open(rom_path, error)) return false;

    std::vector<uint8_t> main(0x48000, 0);
    if (!loader.load(kMain, main, error)) return false;
    std::copy(main.begin(), main.begin() + 0x8000, rom_.begin());
    for (size_t b = 0; b < banks_.size(); ++b)
        std::copy_n(main.begin() + 0x8000 + b * 0x4000, 0x4000, banks_[b].begin());

    std::vector<uint8_t> sound(0x8000, 0);
    if (!loader.load(kSound, sound, error)) return false;
    std::copy(sound.begin(), sound.end(), sound_rom_.begin());

    std::vector<uint8_t> mcu(0x1000, 0);
    if (!loader.load(kMcu, mcu, error)) return false;
    std::fill(mcu_.rom(), mcu_.rom() + Mcs51::kRomSize, 0);
    std::copy(mcu.begin(), mcu.end(), mcu_.rom());

    std::vector<uint8_t> chars(0x8000, 0);
    if (!loader.load(kChars, chars, error)) return false;
    {
        GfxLayout l;
        l.width = l.height = 8;
        l.total = 2048;
        l.planes = 2;
        l.char_increment = 16 * 8;
        l.plane_offsets = {4, 0};
        l.x_offsets = {0, 1, 2, 3, 8, 9, 10, 11};
        l.y_offsets = {0, 16, 32, 48, 64, 80, 96, 112};
        chars_.decode(l, chars);
    }

    std::vector<uint8_t> gfx(0x40000, 0);
    if (!loader.load(kTiles, gfx, error)) return false;
    tiles_.decode(layout16(2048, 0x20000 * 8), gfx);
    std::fill(gfx.begin(), gfx.end(), 0);
    if (!loader.load(kSprites, gfx, error)) return false;
    sprites_.decode(layout16(2048, 0x20000 * 8), gfx);

    warnings_ = loader.warnings();
    reset();
    return true;
}

void BlackTiger::reset() {
    scroll_ram_.fill(0);
    tx_ram_.fill(0);
    pal_ram_.fill(0);
    wram_.fill(0);
    sprite_buf_.fill(0);
    sound_ram_.fill(0);
    palette_.fill(0xff000000u);
    bank_ = soundlatch_ = i8751_latch_ = z80_latch_ = 0;
    scroll_x_ = scroll_y_ = scroll_bank_ = 0;
    screen_layout_ = false;
    ch_on_ = bg_on_ = obj_on_ = true;
    flip_screen_ = false;
    sound_reset_ = false;
    main_debt_ = sound_debt_ = mcu_debt_ = 0;
    audio_accum_ = 0;
    audio_.clear();
    mcu_reads_ = sound_irqs_ = 0;
    main_cpu_.reset();
    sound_cpu_.reset();
    mcu_.reset();
    mcu_.set_irq1_line(IrqLine::Clear);
    ym0_.reset();
    ym1_.reset();
}

// ---------------------------------------------------------------------------
// Main CPU

uint8_t BlackTiger::main_read(uint16_t a) {
    if (a < 0x8000) return rom_[a];
    if (a < 0xc000) return banks_[bank_][a & 0x3fff];
    if (a < 0xd000) return scroll_ram_[scroll_bank_ + (a & 0xfff)];
    if (a < 0xd800) return tx_ram_[a & 0x7ff];
    if (a < 0xe000) return pal_ram_[a & 0x7ff];
    return wram_[a & 0x1fff];
}

void BlackTiger::main_write(uint16_t a, uint8_t v) {
    if (a < 0xc000) return;
    if (a < 0xd000) {
        scroll_ram_[scroll_bank_ + (a & 0xfff)] = v;
    } else if (a < 0xd800) {
        tx_ram_[a & 0x7ff] = v;
    } else if (a < 0xe000) {
        pal_ram_[a & 0x7ff] = v;
        set_color(a & 0x3ff);
    } else {
        wram_[a & 0x1fff] = v;
    }
}

void BlackTiger::set_color(int i) {
    // xBRG_444: low byte RRRRGGGG at d800, high byte xxxxBBBB at dc00.
    const uint8_t rg = pal_ram_[size_t(i)];
    const uint8_t b = pal_ram_[size_t(i) + 0x400];
    palette_[size_t(i)] = pal4(rg >> 4, rg, b);
}

uint8_t BlackTiger::main_in(uint8_t port) {
    switch (port) {
        case 0: return in0_;
        case 1: return in1_;
        case 2: return in2_;
        case 3: return dsw_a_;
        case 4: return dsw_b_;
        case 5: return 0x01;  // "freeze" switch off
        case 7: return i8751_latch_;
        default: return 0xff;
    }
}

void BlackTiger::main_out(uint8_t port, uint8_t v) {
    switch (port) {
        case 0x00: soundlatch_ = v; break;
        case 0x01: bank_ = v & 0x0f; break;
        case 0x04: {
            // bits 0-1 coin counters, 5 sound CPU reset, 6 flip, 7 text off
            ch_on_ = (v & 0x80) == 0;
            flip_screen_ = (v & 0x40) != 0;
            const bool held = (v & 0x20) != 0;
            if (held && !sound_reset_) sound_cpu_.reset();
            sound_reset_ = held;
            break;
        }
        case 0x07:
            z80_latch_ = v;
            mcu_.set_irq1_line(IrqLine::Assert);
            break;
        case 0x08: scroll_x_ = uint16_t((scroll_x_ & 0xff00) | v); break;
        case 0x09: scroll_x_ = uint16_t((scroll_x_ & 0x00ff) | (v << 8)); break;
        case 0x0a: scroll_y_ = uint16_t((scroll_y_ & 0xff00) | v); break;
        case 0x0b: scroll_y_ = uint16_t((scroll_y_ & 0x00ff) | (v << 8)); break;
        case 0x0c:
            bg_on_ = (v & 0x02) == 0;
            obj_on_ = (v & 0x04) == 0;
            break;
        case 0x0d: scroll_bank_ = uint16_t((v & 3) << 12); break;
        case 0x0e: screen_layout_ = (v & 1) != 0; break;
        default: break;  // 03 coin lockout, 06 watchdog
    }
}

// ---------------------------------------------------------------------------
// Sound CPU

uint8_t BlackTiger::sound_read(uint16_t a) {
    if (a < 0x8000) return sound_rom_[a];
    if (a >= 0xc000 && a < 0xc800) return sound_ram_[a & 0x7ff];
    if (a == 0xc800) return soundlatch_;
    if (a == 0xe000) return ym0_.status();
    if (a == 0xe001) return ym0_.read();
    if (a == 0xe002) return ym1_.status();
    if (a == 0xe003) return ym1_.read();
    return 0xff;
}

void BlackTiger::sound_write(uint16_t a, uint8_t v) {
    if (a >= 0xc000 && a < 0xc800) {
        sound_ram_[a & 0x7ff] = v;
        return;
    }
    switch (a) {
        case 0xe000: ym0_.control(v); break;
        case 0xe001: ym0_.write(v); break;
        case 0xe002: ym1_.control(v); break;
        case 0xe003: ym1_.write(v); break;
        default: break;
    }
}

void BlackTiger::on_sound_cycles(int cycles) {
    // The YM2203 timers advance per generated sample, so the chips are clocked
    // in step with the sound CPU (the timer IRQ paces the sound driver).
    audio_accum_ += int64_t(cycles) * YM2203::kSampleRate;
    while (audio_accum_ >= int64_t(kSoundClock)) {
        audio_accum_ -= int64_t(kSoundClock);
        const int32_t s = (ym0_.update() + ym1_.update()) * 3 / 4;
        audio_.push_back(int16_t(std::clamp(s, int32_t(-32768), int32_t(32767))));
    }
}

// ---------------------------------------------------------------------------
// Video

void BlackTiger::update_video() {
    std::fill(screen_.begin(), screen_.end(), palette_[0x3ff]);

    // Background: 8x4 pages of 16x16 tiles (2048x1024) or 4x8 pages (1024x2048).
    const int bg_w = screen_layout_ ? 2048 : 1024;
    const int bg_h = screen_layout_ ? 1024 : 2048;
    auto bg_pixel = [&](int x, int y, int& pen, int& group, int& color) {
        const int px = (x + scroll_x_) & (bg_w - 1);
        const int py = (y + scroll_y_) & (bg_h - 1);
        const int col = px >> 4, row = py >> 4;
        const int index = screen_layout_
            ? (col & 0x0f) + ((row & 0x0f) << 4) + ((col & 0x70) << 4) + ((row & 0x30) << 7)
            : (col & 0x0f) + ((row & 0x0f) << 4) + ((col & 0x30) << 4) + ((row & 0x70) << 6);
        const uint8_t attr = scroll_ram_[size_t(index) * 2 + 1];
        const int code = scroll_ram_[size_t(index) * 2] | ((attr & 0x07) << 8);
        color = (attr & 0x78) >> 3;
        group = kSplitTable[color];
        int tx = px & 15;
        if (attr & 0x80) tx = 15 - tx;
        pen = tiles_.element(code)[(py & 15) * 16 + tx];
    };

    if (bg_on_) {  // back half
        for (int y = 16; y < 240; ++y)
            for (int x = 0; x < 256; ++x) {
                int pen, group, color;
                bg_pixel(x, y, pen, group, color);
                if (!(kBackTransMask[group] >> pen & 1))
                    screen_[size_t(y) * 256 + x] = palette_[size_t(color) * 16 + pen];
            }
    }

    if (obj_on_) {
        for (int offs = 0x200 - 4; offs >= 0; offs -= 4) {
            const uint8_t attr = sprite_buf_[size_t(offs) + 1];
            const int sx = sprite_buf_[size_t(offs) + 3] - ((attr & 0x10) << 4);
            const int sy = sprite_buf_[size_t(offs) + 2];
            const int code = sprite_buf_[size_t(offs)] | ((attr & 0xe0) << 3);
            const int base = 0x200 + (attr & 0x07) * 16;
            const bool flip_x = (attr & 0x08) != 0;
            const uint8_t* g = sprites_.element(code);
            for (int r = 0; r < 16; ++r) {
                const int y = sy + r;
                if (y < 16 || y >= 240) continue;
                for (int c = 0; c < 16; ++c) {
                    const int x = sx + c;
                    if (x < 0 || x >= 256) continue;
                    const int pen = g[r * 16 + (flip_x ? 15 - c : c)];
                    if (pen != 15) screen_[size_t(y) * 256 + x] = palette_[size_t(base + pen)];
                }
            }
        }
    }

    if (bg_on_) {  // front half
        for (int y = 16; y < 240; ++y)
            for (int x = 0; x < 256; ++x) {
                int pen, group, color;
                bg_pixel(x, y, pen, group, color);
                if (!(kFrontTransMask[group] >> pen & 1))
                    screen_[size_t(y) * 256 + x] = palette_[size_t(color) * 16 + pen];
            }
    }

    if (ch_on_) {
        for (int i = 0; i < 0x400; ++i) {
            const int ty = i >> 5, tx = i & 31;
            if (ty < 2 || ty >= 30) continue;
            const uint8_t attr = tx_ram_[size_t(i) + 0x400];
            const int code = tx_ram_[size_t(i)] | ((attr & 0xe0) << 3);
            const int base = 0x300 + (attr & 0x1f) * 4;
            const uint8_t* g = chars_.element(code);
            for (int r = 0; r < 8; ++r)
                for (int c = 0; c < 8; ++c) {
                    const int pen = g[r * 8 + c];
                    if (pen != 3)
                        screen_[size_t(ty * 8 + r) * 256 + tx * 8 + c] = palette_[size_t(base + pen)];
                }
        }
    }

    // Flip screen turns the whole picture by 180 degrees (MAME flips every
    // layer and mirrors the sprites with sx = 240 - sx, sy = 240 - sy).
    for (int y = 0; y < kScreenHeight; ++y) {
        uint32_t* dst = &framebuffer_[size_t(y) * kScreenWidth];
        if (!flip_screen_) {
            std::memcpy(dst, &screen_[size_t(y + 16) * 256], 256 * sizeof(uint32_t));
        } else {
            const uint32_t* src = &screen_[size_t(239 - y) * 256];
            for (int x = 0; x < 256; ++x) dst[x] = src[255 - x];
        }
    }
}

// ---------------------------------------------------------------------------

void BlackTiger::run_frame() {
    constexpr int kSlices = 2;  // per scanline, for the MCU latch handshake
    const double main_cycles = double(kMainClock) / kFramesPerSecond / kScanlines / kSlices;
    const double sound_cycles = double(kSoundClock) / kFramesPerSecond / kScanlines / kSlices;
    const double mcu_cycles = double(mcu_.clock()) / kFramesPerSecond / kScanlines / kSlices;
    for (int line = 0; line < kScanlines; ++line) {
        if (line == 246) {  // vblank: IRQ, sprite buffer copy and frame render
            main_cpu_.set_irq(IrqLine::Hold);
            std::copy_n(wram_.begin() + 0x1e00, 0x200, sprite_buf_.begin());
            update_video();
        }
        for (int s = 0; s < kSlices; ++s) {
            main_debt_ += main_cycles;
            main_debt_ -= main_cpu_.run(int(main_debt_));
            sound_debt_ += sound_cycles;
            if (sound_reset_) {
                const int c = int(sound_debt_);
                on_sound_cycles(c);
                sound_debt_ -= c;
            } else {
                sound_debt_ -= sound_cpu_.run(int(sound_debt_));
            }
            mcu_debt_ += mcu_cycles;
            mcu_debt_ -= mcu_.run(int(mcu_debt_));
        }
    }
}

void BlackTiger::set_inputs(const MachineInputs& in) {
    auto pad = [](const InputState& p) {
        uint8_t v = 0xff;
        if (p.right) v &= ~0x01;
        if (p.left) v &= ~0x02;
        if (p.down) v &= ~0x04;
        if (p.up) v &= ~0x08;
        if (p.button1) v &= ~0x10;
        if (p.button2) v &= ~0x20;
        return v;
    };
    in1_ = pad(in.player1);
    in2_ = pad(in.player2);
    uint8_t sys = 0xff;
    if (in.player1.start) sys &= ~0x01;
    if (in.player2.start) sys &= ~0x02;
    if (in.service) sys &= ~0x20;
    if (in.coin1) sys &= ~0x40;
    if (in.coin2) sys &= ~0x80;
    in0_ = sys;
}

void BlackTiger::set_dip_switch(int bank, uint8_t value) {
    if (bank == 0) dsw_a_ = value;
    else if (bank == 1) dsw_b_ = value;
}

void BlackTiger::drain_audio(std::vector<int16_t>& out) {
    out.insert(out.end(), audio_.begin(), audio_.end());
    audio_.clear();
}

}  // namespace dsp
