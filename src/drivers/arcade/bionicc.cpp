#include "drivers/arcade/bionicc.h"

#include <algorithm>
#include <cstring>

#include "core/rom_loader.h"
#include "cpu/irq_line.h"

namespace dsp {
namespace {

const std::vector<RomEntry> kMain = {
    {"tse_02.1a", 0x10000, 0x00000, 0xe4aeefaa},
    {"tse_04.1b", 0x10000, 0x00001, 0xd0c8ec75},
    {"tse_03.2a", 0x10000, 0x20000, 0xb2ac0a45},
    {"tse_05.2b", 0x10000, 0x20001, 0xa79cb406},
};
const std::vector<RomEntry> kSound = {{"ts_01b.4e", 0x8000, 0x0000, 0xa9a6cafa}};
const std::vector<RomEntry> kMcu = {{"ts.2f", 0x1000, 0x0000, 0x3ed7f0be}};
const std::vector<RomEntry> kChars = {{"tsu_08.8l", 0x8000, 0x0000, 0x9bf0b7a2}};
const std::vector<RomEntry> kBg = {
    {"tsu_07.5l", 0x8000, 0x0000, 0x9469efa4},
    {"tsu_06.4l", 0x8000, 0x8000, 0x40bf0eb4},
};
const std::vector<RomEntry> kFg = {
    {"ts_12.17f", 0x8000, 0x00000, 0xe4b4619e}, {"ts_11.15f", 0x8000, 0x08000, 0xab30237a},
    {"ts_17.17g", 0x8000, 0x10000, 0xdeb657e4}, {"ts_16.15g", 0x8000, 0x18000, 0xd363b5f9},
    {"ts_13.18f", 0x8000, 0x20000, 0xa8f5a004}, {"ts_18.18g", 0x8000, 0x28000, 0x3b36948c},
    {"ts_23.18j", 0x8000, 0x30000, 0xbbfbe58a}, {"ts_24.18k", 0x8000, 0x38000, 0xf156e564},
};
const std::vector<RomEntry> kSprites = {
    {"tse_10.13f", 0x8000, 0x00000, 0xd28eeacc}, {"tsu_09.11f", 0x8000, 0x08000, 0x6a049292},
    {"tse_15.13g", 0x8000, 0x10000, 0x9b5593c0}, {"tsu_14.11g", 0x8000, 0x18000, 0x46b2ad83},
    {"tse_20.13j", 0x8000, 0x20000, 0xb03db778}, {"tsu_19.11j", 0x8000, 0x28000, 0xb5c82722},
    {"tse_22.17j", 0x8000, 0x30000, 0xd4dedeb3}, {"tsu_21.15j", 0x8000, 0x38000, 0x98777006},
};

// Draws one tile into a 256x256 buffer with wrap-around clipping to the
// buffer; `mask` bit n set = pen n transparent.
void draw_tile(uint32_t* dst, const uint8_t* src, int size, int x, int y, bool flipx, bool flipy,
               const uint32_t* pal, uint32_t mask) {
    for (int ty = 0; ty < size; ++ty) {
        const int py = y + ty;
        if (py < 0 || py >= 256) continue;
        const uint8_t* row = src + (flipy ? size - 1 - ty : ty) * size;
        for (int tx = 0; tx < size; ++tx) {
            const int px = x + tx;
            if (px < 0 || px >= 256) continue;
            const uint8_t pen = row[flipx ? size - 1 - tx : tx];
            if (mask & (1u << pen)) continue;
            dst[py * 256 + px] = pal[pen];
        }
    }
}

}  // namespace

BionicCommando::BionicCommando()
    : main_cpu_(kMainClock), sound_cpu_(kSoundClock), mcu_(kMcuClock), ym_(kSoundClock) {
    rom_.assign(0x40000, 0xff);
    framebuffer_.assign(size_t(kScreenWidth) * kScreenHeight, 0xff000000u);
    screen_.assign(256 * 256, 0xff000000u);

    main_cpu_.set_memory_handlers([this](uint32_t a) { return read16(a); },
                                  [this](uint32_t a, uint16_t v) { write16(a, v); });
    main_cpu_.set_byte_handlers([this](uint32_t a) { return read8(a); },
                                [this](uint32_t a, uint8_t v) { write8(a, v); });
    main_cpu_.set_address_mask(0xfffff);

    sound_cpu_.set_memory_handlers([this](uint16_t a) { return sound_read(a); },
                                   [this](uint16_t a, uint8_t v) { sound_write(a, v); });
    sound_cpu_.set_cycle_handler([this](int cycles) { on_sound_cycles(cycles); });

    mcu_.set_port_read_handler(1, [this] { return audiocpu_to_mcu_; });
    mcu_.set_port_write_handler(1, [this](uint8_t v) { mcu_p1_ = v; });
    mcu_.set_port_write_handler(3, [this](uint8_t v) { mcu_p3_write(v); });
    // MOVX: the MCU reaches the 68000 bus (odd bytes) while it holds the CPU.
    mcu_.set_external_handlers(
        [this](uint16_t offset) -> uint8_t {
            if (mcu_p3_ & 0x20) return 0xff;
            const uint32_t a = 0xe3e01u | ((offset & 0x700u) << 6) | ((offset & 0xffu) << 1);
            return read8(a);
        },
        [this](uint16_t offset, uint8_t v) {
            if (mcu_p3_ & 0x20) return;
            const uint32_t a = 0xe3e01u | ((offset & 0x700u) << 6) | ((offset & 0xffu) << 1);
            write8(a, v);
        });
}

BionicCommando::~BionicCommando() = default;

bool BionicCommando::init(const std::string& rom_path, std::string* error) {
    RomLoader loader;
    if (!loader.open(rom_path, error)) return false;

    // 68000 program: tse_02/03 even bytes, tse_04/05 odd bytes.
    for (const auto& e : kMain) {
        std::vector<uint8_t> data;
        if (!loader.try_read(e.name, data) || data.size() != e.length) {
            if (error) *error = std::string("missing or bad ROM ") + e.name;
            return false;
        }
        if (crc32_of(data.data(), data.size()) != e.crc) warnings_.push_back(std::string("CRC mismatch: ") + e.name);
        const uint32_t base = e.offset & ~1u;
        for (uint32_t i = 0; i < data.size(); ++i) rom_[base + i * 2 + (e.offset & 1)] = data[i];
    }

    std::vector<uint8_t> sound(0x8000, 0);
    if (!loader.load(kSound, sound, error)) return false;
    std::copy(sound.begin(), sound.end(), sound_rom_.begin());

    std::vector<uint8_t> mcu(0x1000, 0);
    if (!loader.load(kMcu, mcu, error)) return false;
    std::fill(mcu_.rom(), mcu_.rom() + Mcs51::kRomSize, 0);
    std::copy(mcu.begin(), mcu.end(), mcu_.rom());

    // Text: 8x8, 2bpp (MAME vramlayout).
    std::vector<uint8_t> chars(0x8000, 0);
    if (!loader.load(kChars, chars, error)) return false;
    {
        GfxLayout l;
        l.width = l.height = 8;
        l.total = 1024;
        l.planes = 2;
        l.char_increment = 128;
        l.plane_offsets = {4, 0};
        l.x_offsets = {0, 1, 2, 3, 8, 9, 10, 11};
        l.y_offsets = {0, 16, 32, 48, 64, 80, 96, 112};
        chars_.decode(l, chars);
    }
    // Background: 8x8, 4bpp (scroll2layout).
    std::vector<uint8_t> bg(0x10000, 0);
    if (!loader.load(kBg, bg, error)) return false;
    {
        GfxLayout l;
        l.width = l.height = 8;
        l.total = 2048;
        l.planes = 4;
        l.char_increment = 128;
        l.plane_offsets = {0x8000 * 8 + 4, 0x8000 * 8, 4, 0};
        l.x_offsets = {0, 1, 2, 3, 8, 9, 10, 11};
        l.y_offsets = {0, 16, 32, 48, 64, 80, 96, 112};
        bg_tiles_.decode(l, bg);
    }
    // Foreground: 16x16, 4bpp (scroll1layout).
    std::vector<uint8_t> fg(0x40000, 0);
    if (!loader.load(kFg, fg, error)) return false;
    {
        GfxLayout l;
        l.width = l.height = 16;
        l.total = 2048;
        l.planes = 4;
        l.char_increment = 512;
        l.plane_offsets = {0x20000 * 8 + 4, 0x20000 * 8, 4, 0};
        l.x_offsets = {0, 1, 2, 3, 8, 9, 10, 11, 256, 257, 258, 259, 264, 265, 266, 267};
        for (int i = 0; i < 16; ++i) l.y_offsets.push_back(i * 16);
        fg_tiles_.decode(l, fg);
    }
    // Sprites: 16x16, 4bpp, one plane per quarter of the ROMs.
    std::vector<uint8_t> spr(0x40000, 0);
    if (!loader.load(kSprites, spr, error)) return false;
    {
        GfxLayout l;
        l.width = l.height = 16;
        l.total = 2048;
        l.planes = 4;
        l.char_increment = 256;
        l.plane_offsets = {0x30000 * 8, 0x20000 * 8, 0x10000 * 8, 0};
        l.x_offsets = {0, 1, 2, 3, 4, 5, 6, 7, 128, 129, 130, 131, 132, 133, 134, 135};
        for (int i = 0; i < 16; ++i) l.y_offsets.push_back(i * 8);
        sprites_.decode(l, spr);
    }

    for (const auto& w : loader.warnings()) warnings_.push_back(w);
    reset();
    return true;
}

void BionicCommando::reset() {
    ram_.fill(0);
    wram_.fill(0);
    tx_ram_.fill(0);
    fg_ram_.fill(0);
    bg_ram_.fill(0);
    pal_ram_.fill(0);
    sound_ram_.fill(0);
    sprite_buf_.fill(0);
    palette_.fill(0xff000000u);
    scroll_.fill(0);
    flip_screen_ = false;
    audiocpu_to_mcu_ = mcu_to_audiocpu_ = mcu_p1_ = mcu_p3_ = 0;
    inputs_ = 0xffff;
    main_debt_ = sound_debt_ = mcu_debt_ = 0;
    audio_accum_ = 0;
    audio_.clear();
    sound_nmis_ = dma_count_ = 0;
    main_cpu_.reset();
    main_cpu_.set_halt_line(IrqLine::Clear);
    sound_cpu_.reset();
    mcu_.reset();
    ym_.reset();
}

// ---------------------------------------------------------------------------
// Main CPU bus (byte level; words are two bytes, big-endian)

uint8_t BionicCommando::read8(uint32_t a) {
    a &= 0xfffff;
    if (a < 0x40000) return rom_[a];
    if (a >= 0xe0000 && a < 0xe4000) return ram_[a & 0xfff];
    if (a >= 0xe4000 && a < 0xe8000) {
        const uint16_t w = (a & 2) ? dsw_ : inputs_;
        return (a & 1) ? uint8_t(w) : uint8_t(w >> 8);
    }
    if (a >= 0xec000 && a < 0xf0000) return tx_ram_[a & 0xfff];
    if (a >= 0xf0000 && a < 0xf4000) return fg_ram_[a & 0x3fff];
    if (a >= 0xf4000 && a < 0xf8000) return bg_ram_[a & 0x3fff];
    if (a >= 0xf8000 && a < 0xf8800) return pal_ram_[a & 0x7ff];
    if (a >= 0xfc000) return wram_[a & 0x3fff];
    return 0xff;
}

uint16_t BionicCommando::read16(uint32_t a) {
    a &= 0xffffe;
    return uint16_t(read8(a) << 8 | read8(a + 1));
}

void BionicCommando::set_palette(int i) {
    // RRRRGGGGBBBBIIII: when I bit 3 is clear the colour is dimmed.
    const uint16_t raw = uint16_t(pal_ram_[size_t(i) * 2] << 8 | pal_ram_[size_t(i) * 2 + 1]);
    const int bright = raw & 0x0f;
    int r = ((raw >> 12) & 0x0f) * 0x11;
    int g = ((raw >> 8) & 0x0f) * 0x11;
    int b = ((raw >> 4) & 0x0f) * 0x11;
    if ((bright & 0x08) == 0) {
        r = int(r / 2 + bright * 18.15);
        g = int(g / 2 + bright * 18.15);
        b = int(b / 2 + bright * 18.15);
    }
    palette_[size_t(i)] = 0xff000000u | uint32_t(std::min(r, 255)) << 16 | uint32_t(std::min(g, 255)) << 8 |
                          uint32_t(std::min(b, 255));
}

void BionicCommando::write8(uint32_t a, uint8_t v) {
    a &= 0xfffff;
    if (a < 0x40000) return;
    if (a >= 0xe0000 && a < 0xe4000) {
        ram_[a & 0xfff] = v;
    } else if (a >= 0xe4000 && a < 0xe8000) {
        if ((a & 3) == 0) {
            flip_screen_ = (v & 1) != 0;  // also coin counters / lockouts
        } else if ((a & 3) == 2) {
            sound_cpu_.set_nmi(IrqLine::Pulse);
            ++sound_nmis_;
        }
    } else if (a >= 0xe8010 && a < 0xe8018) {
        uint16_t& s = scroll_[(a - 0xe8010) / 2];
        s = (a & 1) ? uint16_t((s & 0xff00) | v) : uint16_t((s & 0x00ff) | (v << 8));
    } else if (a == 0xe8018 || a == 0xe8019) {
        // Sprite RAM buffering (e0800-e0cff).
        for (size_t i = 0; i < sprite_buf_.size(); ++i)
            sprite_buf_[i] = uint16_t(ram_[0x800 + i * 2] << 8 | ram_[0x800 + i * 2 + 1]);
    } else if (a == 0xe801a || a == 0xe801b) {
        // DMA on: the MCU's INT0 copies data while the 68000 is halted.
        mcu_.set_irq0_line(IrqLine::Assert);
        main_cpu_.set_halt_line(IrqLine::Assert);
        ++dma_count_;
    } else if (a >= 0xec000 && a < 0xf0000) {
        tx_ram_[a & 0xfff] = v;
    } else if (a >= 0xf0000 && a < 0xf4000) {
        fg_ram_[a & 0x3fff] = v;
    } else if (a >= 0xf4000 && a < 0xf8000) {
        bg_ram_[a & 0x3fff] = v;
    } else if (a >= 0xf8000 && a < 0xf8800) {
        pal_ram_[a & 0x7ff] = v;
        set_palette(int((a & 0x7ff) >> 1));
    } else if (a >= 0xfc000) {
        wram_[a & 0x3fff] = v;
    }
}

void BionicCommando::write16(uint32_t a, uint16_t v) {
    a &= 0xffffe;
    write8(a, uint8_t(v >> 8));
    if (a == 0xe8018 || a == 0xe801a) return;  // single strobe per word access
    write8(a + 1, uint8_t(v));
}

// ---------------------------------------------------------------------------
// Sound CPU and MCU

uint8_t BionicCommando::sound_read(uint16_t a) {
    if (a < 0x8000) return sound_rom_[a];
    if (a == 0x8000 || a == 0x8001) return ym_.status();
    if (a == 0xa000) return mcu_to_audiocpu_;
    if (a >= 0xc000 && a < 0xc800) return sound_ram_[a & 0x7ff];
    return 0xff;
}

void BionicCommando::sound_write(uint16_t a, uint8_t v) {
    if (a == 0x8000) ym_.select_register(v);
    else if (a == 0x8001) ym_.write(v);
    else if (a == 0xa000) audiocpu_to_mcu_ = v;
    else if (a >= 0xc000 && a < 0xc800) sound_ram_[a & 0x7ff] = v;
}

void BionicCommando::mcu_p3_write(uint8_t v) {
    // 7 read strobe, 6 write strobe, 5 dma, 4 int1 ack, 0 int0 ack
    if ((mcu_p3_ & 0x01) && !(v & 0x01)) {
        mcu_.set_irq0_line(IrqLine::Clear);
        main_cpu_.set_halt_line(IrqLine::Clear);
    }
    if ((mcu_p3_ & 0x10) && !(v & 0x10)) mcu_.set_irq1_line(IrqLine::Clear);
    if ((mcu_p3_ & 0x40) && !(v & 0x40)) mcu_to_audiocpu_ = mcu_p1_;
    mcu_p3_ = v;
}

void BionicCommando::on_sound_cycles(int cycles) {
    ym_.run_timers(cycles);
    audio_accum_ += int64_t(cycles) * YM2151::kSampleRate;
    while (audio_accum_ >= int64_t(kSoundClock)) {
        audio_accum_ -= int64_t(kSoundClock);
        const int32_t s = int32_t(ym_.update() * 6 / 10);
        audio_.push_back(int16_t(std::clamp(s, int32_t(-32768), int32_t(32767))));
    }
}

// ---------------------------------------------------------------------------
// Video

void BionicCommando::update_video() {
    std::fill(screen_.begin(), screen_.end(), 0xff000000u);
    const int fg_sx = scroll_[0] & 0x3ff, fg_sy = scroll_[1] & 0x3ff;
    const int bg_sx = scroll_[2] & 0x1ff, bg_sy = scroll_[3] & 0x1ff;

    auto word = [](const uint8_t* m, int i) { return uint16_t(m[i * 2] << 8 | m[i * 2 + 1]); };

    // Foreground 16x16 tilemap (64x64). pass: 0 = tiles behind the BG
    // (both flip bits set), 1 = back half, 2 = front half.
    auto draw_fg = [&](int pass) {
        for (int ty = 0; ty < 17; ++ty) {
            for (int tx = 0; tx < 17; ++tx) {
                const int col = ((fg_sx >> 4) + tx) & 63, row = ((fg_sy >> 4) + ty) & 63;
                const int idx = row * 64 + col;
                const int attr = word(fg_ram_.data(), idx * 2 + 1) & 0xff;
                const int code = ((attr & 7) << 8) | (word(fg_ram_.data(), idx * 2) & 0xff);
                const bool behind = (attr & 0xc0) == 0xc0;
                if ((pass == 0) != behind) continue;
                const int group = behind ? 0 : (attr & 0x20) >> 5;
                uint32_t mask;
                if (pass == 2) mask = group ? 0xffc1u : 0xffffu;   // front half
                else mask = group ? 0x803eu : 0x8000u;              // back half
                if (mask == 0xffff) continue;
                const bool fx = !behind && (attr & 0x80), fy = !behind && (attr & 0x40);
                draw_tile(screen_.data(), fg_tiles_.element(code), 16, tx * 16 - (fg_sx & 15),
                          ty * 16 - (fg_sy & 15), fx, fy, palette_.data() + 256 + ((attr & 0x18) >> 3) * 16, mask);
            }
        }
    };

    draw_fg(0);
    // Background 8x8 tilemap (64x64), pen 15 transparent.
    for (int ty = 0; ty < 33; ++ty) {
        for (int tx = 0; tx < 33; ++tx) {
            const int col = ((bg_sx >> 3) + tx) & 63, row = ((bg_sy >> 3) + ty) & 63;
            const int idx = row * 64 + col;
            const int attr = word(bg_ram_.data(), idx * 2 + 1) & 0xff;
            const int code = ((attr & 7) << 8) | (word(bg_ram_.data(), idx * 2) & 0xff);
            draw_tile(screen_.data(), bg_tiles_.element(code), 8, tx * 8 - (bg_sx & 7), ty * 8 - (bg_sy & 7),
                      (attr & 0x80) != 0, (attr & 0x40) != 0, palette_.data() + ((attr & 0x38) >> 3) * 16, 0x8000);
        }
    }
    draw_fg(1);

    // Sprites: last entry first, pen 15 transparent, colours 512-767.
    for (int i = 0x9f; i >= 0; --i) {
        const uint16_t* s = &sprite_buf_[size_t(i) * 4];
        const int code = s[0] & 0x7ff;
        if (code == 0x7ff) continue;
        const int attr = s[1];
        int sy = s[2] & 0x1ff;
        int sx = s[3] & 0x1ff;
        if (sy & 0x100) sy -= 0x200;
        if (sx & 0x100) sx -= 0x200;
        draw_tile(screen_.data(), sprites_.element(code), 16, sx, sy, (attr & 2) != 0, (attr & 1) != 0,
                  palette_.data() + 512 + ((attr >> 2) & 0x0f) * 16, 0x8000);
    }
    draw_fg(2);

    // Text 8x8 (32x32), pen 3 transparent, colours 768+.
    for (int idx = 0; idx < 0x400; ++idx) {
        const int attr = word(tx_ram_.data(), idx + 0x400) & 0xff;
        const int code = ((attr & 0xc0) << 2) | (word(tx_ram_.data(), idx) & 0xff);
        draw_tile(screen_.data(), chars_.element(code), 8, (idx & 31) * 8, (idx >> 5) * 8, (attr & 0x10) != 0,
                  (attr & 0x20) != 0, palette_.data() + 768 + (attr & 0x0f) * 4, 0x8);
    }

    // Visible area: rows 16..239.
    for (int y = 0; y < kScreenHeight; ++y) {
        for (int x = 0; x < kScreenWidth; ++x) {
            const int sx = flip_screen_ ? 255 - x : x;
            const int sy = flip_screen_ ? 255 - (y + 16) : y + 16;
            framebuffer_[size_t(y) * kScreenWidth + size_t(x)] = screen_[size_t(sy) * 256 + size_t(sx)];
        }
    }
}

// ---------------------------------------------------------------------------
// Frame

void BionicCommando::run_frame() {
    constexpr int kSlices = 4;  // per scanline, so the MCU "DMA" handshake keeps up
    const double main_cycles = double(kMainClock) / kFramesPerSecond / kScanlines / kSlices;
    const double sound_cycles = double(kSoundClock) / kFramesPerSecond / kScanlines / kSlices;
    const double mcu_cycles = double(mcu_.clock()) / kFramesPerSecond / kScanlines / kSlices;
    for (int line = 0; line < kScanlines; ++line) {
        if (line == 128) main_cpu_.set_irq(4, IrqLine::Hold);  // inputs
        if (line == 240) {                                     // vblank
            main_cpu_.set_irq(2, IrqLine::Hold);
            update_video();
        }
        for (int s = 0; s < kSlices; ++s) {
            main_debt_ += main_cycles;
            main_debt_ -= main_cpu_.run(int(main_debt_));
            sound_debt_ += sound_cycles;
            sound_debt_ -= sound_cpu_.run(int(sound_debt_));
            mcu_debt_ += mcu_cycles;
            mcu_debt_ -= mcu_.run(int(mcu_debt_));
        }
    }
}

void BionicCommando::set_inputs(const MachineInputs& in) {
    uint16_t v = 0xffff;
    const auto& p1 = in.player1;
    const auto& p2 = in.player2;
    if (p2.button2) v &= ~0x0001;
    if (p2.button1) v &= ~0x0002;
    if (p2.right) v &= ~0x0004;
    if (p2.left) v &= ~0x0008;
    if (p2.down) v &= ~0x0010;
    if (p2.up) v &= ~0x0020;
    if (p1.button2) v &= ~0x0040;
    if (p1.button1) v &= ~0x0080;
    if (p1.right) v &= ~0x0100;
    if (p1.left) v &= ~0x0200;
    if (p1.down) v &= ~0x0400;
    if (p1.up) v &= ~0x0800;
    if (p2.start) v &= ~0x1000;
    if (p1.start) v &= ~0x2000;
    if (in.coin2) v &= ~0x4000;
    if (in.coin1) v &= ~0x8000;
    inputs_ = v;
}

void BionicCommando::set_dip_switch(int bank, uint8_t value) {
    // Bank 0 = low byte (switch B: coinage, service, flip),
    // bank 1 = high byte (switch A: lives, cabinet, bonus, difficulty, freeze).
    if (bank == 0) dsw_ = uint16_t((dsw_ & 0xff00) | value);
    else if (bank == 1) dsw_ = uint16_t((dsw_ & 0x00ff) | (value << 8));
}

void BionicCommando::drain_audio(std::vector<int16_t>& out) {
    out.insert(out.end(), audio_.begin(), audio_.end());
    audio_.clear();
}

}  // namespace dsp
