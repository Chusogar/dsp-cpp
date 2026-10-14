#include "drivers/arcade/cninja.h"

#include <algorithm>
#include <cstring>

#include "core/rom_loader.h"

namespace dsp {
namespace {

// Alternate names cover both Pascal dumps (*.rom) and MAME 0.260 non-merged.
const std::vector<RomEntry> kProgram = {
    {"gn-02-3.1k|gn-02-3.rom", 0x20000, 0x00000, 0x39aea12a},
    {"gn-05-2.3k|gn-05-2.rom", 0x20000, 0x00001, 0x0f4360ef},
    {"gn-01-2.1j|gn-01-2.rom", 0x20000, 0x40000, 0xf740ef7e},
    {"gn-04-2.3j|gn-04-2.rom", 0x20000, 0x40001, 0xc98fcb62},
    {"gn-00.1h|gn-00.rom", 0x20000, 0x80000, 0x0b110b16},
    {"gn-03.1k|gn-03.rom", 0x20000, 0x80001, 0x1e28e697},
};
const std::vector<RomEntry> kSound = {{"gl-07.13k|gl-07.rom", 0x10000, 0, 0xca8bef96}};
const std::vector<RomEntry> kChars = {{"gl-09.6z|gl-09.rom", 0x10000, 0, 0x5a2d4752},
                                      {"gl-08.6y|gl-08.rom", 0x10000, 1, 0x33a2b400}};
const std::vector<RomEntry> kTiles1 = {{"mag-02.4z|mag-02.rom", 0x80000, 0, 0xde89c69a}};
const std::vector<RomEntry> kTiles2 = {{"mag-00.1y|mag-00.rom", 0x80000, 0x00000, 0xa8f05d33},
                                       {"mag-01.1z|mag-01.rom", 0x80000, 0x80000, 0x5b399eed}};
const std::vector<RomEntry> kSprites = {
    {"mag-03.9y|mag-03.rom", 0x80000, 0x000000, 0x2220eb9f},
    {"mag-05.12y|mag-05.rom", 0x80000, 0x000001, 0x56a53254},
    {"mag-04.9z|mag-04.rom", 0x80000, 0x100000, 0x144b94cc},
    {"mag-06.12z|mag-06.rom", 0x80000, 0x100001, 0x82d44749},
};
const std::vector<RomEntry> kOki1 = {{"gl-06.13j|gl-06.rom", 0x20000, 0, 0xd92e519d}};
const std::vector<RomEntry> kOki2 = {{"mag-07.13f|mag-07.rom", 0x80000, 0, 0x08eb5264}};

bool load_interleaved(RomLoader& loader, const std::vector<RomEntry>& entries,
                      std::vector<uint8_t>& dest, std::string* error) {
    for (const RomEntry& e : entries) {
        std::vector<uint8_t> part(e.length, 0xff);
        if (!loader.load({{e.name, e.length, 0, e.crc}}, part, error)) return false;
        for (uint32_t i = 0; i < e.length; ++i) dest[e.offset + 2 * i] = part[i];
    }
    return true;
}

GfxLayout char_layout() {
    GfxLayout l;
    l.width = l.height = 8;
    l.total = 0x1000;
    l.planes = 4;
    l.char_increment = 16 * 8;
    l.plane_offsets = {0x10000 * 8 + 8, 0x10000 * 8, 8, 0};
    for (int i = 0; i < 8; ++i) l.x_offsets.push_back(i);
    for (int i = 0; i < 8; ++i) l.y_offsets.push_back(i * 16);
    return l;
}

// 16x16 tiles: high bitplanes live in the second half of the ROM region.
GfxLayout tile16_layout(int total, int half_bytes) {
    GfxLayout l;
    l.width = l.height = 16;
    l.total = total;
    l.planes = 4;
    l.char_increment = 64 * 8;
    l.plane_offsets = {half_bytes * 8 + 8, half_bytes * 8, 8, 0};
    for (int i = 0; i < 8; ++i) l.x_offsets.push_back(32 * 8 + i);
    for (int i = 0; i < 8; ++i) l.x_offsets.push_back(i);
    for (int i = 0; i < 16; ++i) l.y_offsets.push_back(i * 16);
    return l;
}

// DECO 52 sprites: planes packed in 16-bit words, 128 bytes per tile.
GfxLayout sprite_layout(int total) {
    GfxLayout l;
    l.width = l.height = 16;
    l.total = total;
    l.planes = 4;
    l.char_increment = 128 * 8;
    l.plane_offsets = {16, 0, 24, 8};
    for (int i = 0; i < 8; ++i) l.x_offsets.push_back(64 * 8 + i);
    for (int i = 0; i < 8; ++i) l.x_offsets.push_back(i);
    for (int i = 0; i < 16; ++i) l.y_offsets.push_back(i * 32);
    return l;
}

// Only two 4 KB banks; when bits 7-4 of the bank register are set the low bank
// is selected (cninja_video_bank in cavemanninja_hw.pas).
int cninja_bank(int bank) { return ((bank >> 4) & 0xf) != 0 ? 0 : 0x1000; }

}  // namespace

CavemanNinja::CavemanNinja()
    : screen_(size_t(kWidth) * kHeight, 0xff000000u),
      framebuffer_(size_t(kWidth) * kHeight, 0xff000000u) {
    main_cpu_.set_memory_handlers([this](uint32_t a) { return main_read(a); },
                                  [this](uint32_t a, uint16_t v) { main_write(a, v); });
    // Byte writes to the protection chip must not read first (side effects).
    main_cpu_.set_byte_handlers(
        [this](uint32_t a) {
            const uint16_t w = main_read(a & ~1u);
            return uint8_t((a & 1) ? w : w >> 8);
        },
        [this](uint32_t a, uint8_t v) {
            const uint32_t masked = a & 0xffffff;
            if (masked >= 0x1bc000 && masked < 0x1c0000) {
                deco104_.write_data(prot_address(masked - 0x1bc000), uint16_t(v << 8 | v),
                                    (a & 1) ? 0x00ff : 0xff00);
                return;
            }
            const uint16_t w = main_read(a & ~1u);
            main_write(a & ~1u, (a & 1) ? uint16_t((w & 0xff00) | v)
                                        : uint16_t((w & 0x00ff) | (uint16_t(v) << 8)));
        });
    sound_cpu_.set_memory_handlers([this](uint32_t a) { return sound_read(a); },
                                   [this](uint32_t a, uint8_t v) { sound_write(a, v); });
    sound_cpu_.set_cycle_handler([this](int cycles) { on_sound_cycles(cycles); });
    ym2151_.set_irq_handler([this](bool state) {
        sound_cpu_.set_irq_line(1, state ? IrqLine::Assert : IrqLine::Clear);
    });
    // YM2151 port A selects one of the two 256 KB banks of OKI #2.
    ym2151_.set_port_handler([this](uint8_t value) {
        oki2_.set_rom_bank_offset((value & 1) ? 0x40000 : 0);
    });
    deco104_.set_port_handlers([this] { return in0_; }, [this] { return in1_; },
                               [this] { return dsw_; });
    deco104_.set_soundlatch_handler([this](uint8_t value) {
        sound_latch_ = value;
        sound_cpu_.set_irq_line(0, IrqLine::Hold);
    });

    // Chip 0: 8x8 chars + 16x16 tiles1 (colour bank 16 -> palette $100).
    {
        Deco16ic::Layer pf1;
        pf1.gfx8 = &chars_;
        pf1.gfx16 = &tiles1_;
        pf1.color_base8 = 0;
        pf1.color_base16 = 0;
        pf1.color_bank = 0;
        tilegen_[0].configure(0, std::move(pf1));
        Deco16ic::Layer pf2;
        pf2.gfx8 = &chars_;
        pf2.gfx16 = &tiles1_;
        pf2.color_base8 = 0;
        pf2.color_base16 = 0;
        pf2.color_bank = 16;
        tilegen_[0].configure(1, std::move(pf2));
    }
    // Chip 1: chars + tiles2, pf1 base $200, pf2 colour bank 48 -> $500.
    {
        Deco16ic::Layer pf1;
        pf1.gfx8 = &chars_;
        pf1.gfx16 = &tiles2_;
        pf1.color_base8 = 0;
        pf1.color_base16 = 0x200;
        pf1.color_bank = 0;
        pf1.bank = cninja_bank;
        tilegen_[1].configure(0, std::move(pf1));
        Deco16ic::Layer pf2;
        pf2.gfx8 = &chars_;
        pf2.gfx16 = &tiles2_;
        pf2.color_base8 = 0;
        pf2.color_base16 = 0x200;
        pf2.color_bank = 48;
        pf2.bank = cninja_bank;
        tilegen_[1].configure(1, std::move(pf2));
    }
}

bool CavemanNinja::init(const std::string& rom_path, std::string* error) {
    RomLoader loader;
    if (!loader.open(rom_path, error)) return false;

    std::vector<uint8_t> image(0xc0000, 0xff);
    if (!load_interleaved(loader, kProgram, image, error)) return false;
    rom_.resize(0x60000);
    for (size_t i = 0; i < rom_.size(); ++i) {
        rom_[i] = uint16_t(image[2 * i] << 8 | image[2 * i + 1]);
    }

    sound_rom_.assign(0x10000, 0xff);
    if (!loader.load(kSound, sound_rom_, error)) return false;

    std::vector<uint8_t> oki(0x20000, 0);
    if (!loader.load(kOki1, oki, error)) return false;
    oki1_.set_rom(std::move(oki));
    oki.assign(0x80000, 0);
    if (!loader.load(kOki2, oki, error)) return false;
    oki2_.set_rom(std::move(oki));

    {
        std::vector<uint8_t> chars(0x20000, 0);
        if (!load_interleaved(loader, kChars, chars, error)) return false;
        chars_.decode(char_layout(), chars);
    }
    {
        std::vector<uint8_t> gfx(0x80000, 0);
        if (!loader.load(kTiles1, gfx, error)) return false;
        tiles1_.decode(tile16_layout(0x1000, 0x40000), gfx);
    }
    {
        std::vector<uint8_t> raw(0x100000, 0);
        if (!loader.load(kTiles2, raw, error)) return false;
        // Split each ROM into low/high halves so the plane layout matches
        // cavemanninja_hw.pas (mag-00 / mag-01 rearrange).
        std::vector<uint8_t> gfx(0x100000, 0);
        std::memcpy(gfx.data() + 0x00000, raw.data() + 0x00000, 0x40000);
        std::memcpy(gfx.data() + 0x80000, raw.data() + 0x40000, 0x40000);
        std::memcpy(gfx.data() + 0x40000, raw.data() + 0x80000, 0x40000);
        std::memcpy(gfx.data() + 0xc0000, raw.data() + 0xc0000, 0x40000);
        tiles2_.decode(tile16_layout(0x2000, 0x80000), gfx);
    }
    {
        std::vector<uint8_t> gfx(0x200000, 0);
        if (!load_interleaved(loader, kSprites, gfx, error)) return false;
        sprites_.decode(sprite_layout(0x4000), gfx);
    }

    warnings_ = loader.warnings();
    reset();
    return true;
}

void CavemanNinja::reset() {
    ram_.fill(0);
    sprite_buf_.fill(0);
    sprite_ram_.fill(0);
    pal_ram_.fill(0);
    palette_.fill(0xff000000u);
    sound_ram_.fill(0);
    tilegen_[0].reset();
    tilegen_[1].reset();
    deco104_.reset();
    in0_ = 0xffff;
    in1_ = 0x00f7;
    irq_mask_ = 0;
    irq_line_ = 0;
    raster_irq_ = false;
    flip_screen_ = false;
    sound_latch_ = 0;
    scanline_ = 0;
    main_debt_ = sound_debt_ = 0;
    ym_accum_ = oki1_accum_ = oki2_accum_ = audio_accum_ = 0;
    oki1_level_ = oki2_level_ = 0;
    audio_.clear();
    main_cpu_.reset();
    sound_cpu_.reset();
    ym2203_.reset();
    ym2151_.reset();
    oki1_.reset();
    oki2_.reset();
    oki2_.set_rom_bank_offset(0);
}

// ---------------------------------------------------------------------------
// DECO 104 address scramble (cavemanninja_hw.pas)

uint16_t CavemanNinja::prot_address(uint32_t real_address) const {
    // BITSWAP32(addr, 31..18, 13,12,11, 17,16,15,14, 10..0) & 0x7fff
    const uint32_t a = real_address;
    uint32_t out = (a & 0xfffc0000u) | (a & 0x7ffu);
    if (a & (1u << 13)) out |= 1u << 17;
    if (a & (1u << 12)) out |= 1u << 16;
    if (a & (1u << 11)) out |= 1u << 15;
    if (a & (1u << 17)) out |= 1u << 14;
    if (a & (1u << 16)) out |= 1u << 13;
    if (a & (1u << 15)) out |= 1u << 12;
    if (a & (1u << 14)) out |= 1u << 11;
    return uint16_t(out & 0x7fff);
}

// ---------------------------------------------------------------------------
// Main CPU

uint16_t CavemanNinja::main_read(uint32_t a) {
    a &= 0xffffff;
    if (a < 0xc0000) return rom_[a >> 1];
    if (a >= 0x144000 && a < 0x145000) return tilegen_[0].data[0][(a & 0xfff) >> 1];
    if (a >= 0x146000 && a < 0x147000) return tilegen_[0].data[1][(a & 0xfff) >> 1];
    if (a >= 0x14c000 && a < 0x14c800) return tilegen_[0].rowscroll[0][(a & 0x7ff) >> 1];
    if (a >= 0x14e000 && a < 0x14e800) return tilegen_[0].rowscroll[1][(a & 0x7ff) >> 1];
    if (a >= 0x154000 && a < 0x155000) return tilegen_[1].data[0][(a & 0xfff) >> 1];
    if (a >= 0x156000 && a < 0x157000) return tilegen_[1].data[1][(a & 0xfff) >> 1];
    if (a >= 0x15c000 && a < 0x15c800) return tilegen_[1].rowscroll[0][(a & 0x7ff) >> 1];
    if (a >= 0x15e000 && a < 0x15e800) return tilegen_[1].rowscroll[1][(a & 0x7ff) >> 1];
    if (a >= 0x184000 && a < 0x188000) return ram_[(a & 0x3fff) >> 1];
    if (a >= 0x190000 && a < 0x190008) {
        switch ((a >> 1) & 7) {
            case 1: return uint16_t(scanline_);
            case 2:
                main_cpu_.set_irq(3, IrqLine::Clear);
                main_cpu_.set_irq(4, IrqLine::Clear);
                return 0;
            default: return 0;
        }
    }
    if (a >= 0x19c000 && a < 0x19e000) return pal_ram_[(a & 0x1fff) >> 1];
    if (a >= 0x1a4000 && a < 0x1a4800) return sprite_buf_[(a & 0x7ff) >> 1];
    if (a >= 0x1bc000 && a < 0x1c0000) return deco104_.read_data(prot_address(a - 0x1bc000));
    return 0xffff;
}

void CavemanNinja::main_write(uint32_t a, uint16_t v) {
    a &= 0xffffff;
    if (a < 0xc0000) return;
    if (a >= 0x140000 && a < 0x140010) {
        tilegen_[0].control_w(int((a & 0xf) >> 1), v);
        return;
    }
    if (a >= 0x144000 && a < 0x145000) {
        tilegen_[0].data[0][(a & 0xfff) >> 1] = v;
        return;
    }
    if (a >= 0x146000 && a < 0x147000) {
        tilegen_[0].data[1][(a & 0xfff) >> 1] = v;
        return;
    }
    if (a >= 0x14c000 && a < 0x14c800) {
        tilegen_[0].rowscroll[0][(a & 0x7ff) >> 1] = v;
        return;
    }
    if (a >= 0x14e000 && a < 0x14e800) {
        tilegen_[0].rowscroll[1][(a & 0x7ff) >> 1] = v;
        return;
    }
    if (a >= 0x150000 && a < 0x150010) {
        tilegen_[1].control_w(int((a & 0xf) >> 1), v);
        if ((a & 0xf) == 0) flip_screen_ = (v & 0x80) != 0;
        return;
    }
    if (a >= 0x154000 && a < 0x155000) {
        tilegen_[1].data[0][(a & 0xfff) >> 1] = v;
        return;
    }
    if (a >= 0x156000 && a < 0x157000) {
        tilegen_[1].data[1][(a & 0xfff) >> 1] = v;
        return;
    }
    if (a >= 0x15c000 && a < 0x15c800) {
        tilegen_[1].rowscroll[0][(a & 0x7ff) >> 1] = v;
        return;
    }
    if (a >= 0x15e000 && a < 0x15e800) {
        tilegen_[1].rowscroll[1][(a & 0x7ff) >> 1] = v;
        return;
    }
    if (a >= 0x184000 && a < 0x188000) {
        ram_[(a & 0x3fff) >> 1] = v;
        return;
    }
    if (a >= 0x190000 && a < 0x190008) {
        switch ((a >> 1) & 7) {
            case 0: irq_mask_ = uint8_t(v & 0xff); break;
            case 1:
                irq_line_ = uint8_t(v & 0xff);
                // Valid raster lines are 1..239; bit 1 of the mask disables them.
                raster_irq_ = irq_line_ > 0 && irq_line_ < 240 && (irq_mask_ & 2) == 0;
                break;
            default: break;
        }
        return;
    }
    if (a >= 0x19c000 && a < 0x19e000) {
        const size_t idx = (a & 0x1fff) >> 1;
        if (pal_ram_[idx] != v) {
            pal_ram_[idx] = v;
            update_palette_entry(int(idx >> 1));
        }
        return;
    }
    if (a >= 0x1a4000 && a < 0x1a4800) {
        sprite_buf_[(a & 0x7ff) >> 1] = v;
        return;
    }
    if (a >= 0x1b4000 && a < 0x1b4002) {
        sprite_ram_ = sprite_buf_;
        return;
    }
    if (a >= 0x1bc000 && a < 0x1c0000) {
        deco104_.write_data(prot_address(a - 0x1bc000), v);
        return;
    }
}

void CavemanNinja::update_palette_entry(int index) {
    if (index < 0 || index >= 0x800) return;
    const uint16_t b = pal_ram_[size_t(index) * 2];
    const uint16_t gr = pal_ram_[size_t(index) * 2 + 1];
    const uint8_t blue = uint8_t(b & 0xff);
    const uint8_t green = uint8_t(gr >> 8);
    const uint8_t red = uint8_t(gr & 0xff);
    palette_[size_t(index)] = 0xff000000u | uint32_t(red) << 16 | uint32_t(green) << 8 | blue;
}

// ---------------------------------------------------------------------------
// Sound CPU (deco16_snd_double)

uint8_t CavemanNinja::sound_read(uint32_t a) {
    if (a < 0x10000) return sound_rom_[a];
    switch (a) {
        case 0x100000: return ym2203_.status();
        case 0x100001: return ym2203_.read();
        case 0x110001: return ym2151_.status();
        case 0x120000:
        case 0x120001: return oki1_.read();
        case 0x130000:
        case 0x130001: return oki2_.read();
        case 0x140000:
        case 0x140001: return sound_latch_;
        default: break;
    }
    if (a >= 0x1f0000 && a < 0x1f2000) return sound_ram_[a & 0x1fff];
    if (a >= 0x1fec00 && a < 0x1fec02) return sound_cpu_.timer_r();
    if (a >= 0x1ff400 && a < 0x1ff404) return sound_cpu_.irq_status_r(uint8_t(a & 3));
    return 0xff;
}

void CavemanNinja::sound_write(uint32_t a, uint8_t v) {
    if (a < 0x10000) return;
    switch (a) {
        case 0x100000: ym2203_.control(v); return;
        case 0x100001: ym2203_.write(v); return;
        case 0x110000: ym2151_.select_register(v); return;
        case 0x110001: ym2151_.write(v); return;
        case 0x120000:
        case 0x120001: oki1_.write(v); return;
        case 0x130000:
        case 0x130001: oki2_.write(v); return;
        default: break;
    }
    if (a >= 0x1f0000 && a < 0x1f2000) sound_ram_[a & 0x1fff] = v;
    else if (a >= 0x1fec00 && a < 0x1fec02) sound_cpu_.timer_w(uint8_t(a & 1), v);
    else if (a >= 0x1ff400 && a < 0x1ff404) sound_cpu_.irq_status_w(uint8_t(a & 3), v);
}

void CavemanNinja::on_sound_cycles(int cycles) {
    ym_accum_ += int64_t(cycles) * kYm2151Clock;
    const int ym_cycles = int(ym_accum_ / kSoundClock);
    ym_accum_ -= int64_t(ym_cycles) * kSoundClock;
    ym2151_.run_timers(ym_cycles);

    oki1_accum_ += int64_t(cycles) * oki1_.sample_frequency();
    while (oki1_accum_ >= int64_t(kSoundClock)) {
        oki1_accum_ -= int64_t(kSoundClock);
        oki1_level_ = oki1_.update();
    }
    oki2_accum_ += int64_t(cycles) * oki2_.sample_frequency();
    while (oki2_accum_ >= int64_t(kSoundClock)) {
        oki2_accum_ -= int64_t(kSoundClock);
        oki2_level_ = oki2_.update();
    }
    audio_accum_ += int64_t(cycles) * YM2151::kSampleRate;
    while (audio_accum_ >= int64_t(kSoundClock)) {
        audio_accum_ -= int64_t(kSoundClock);
        const int32_t s =
            (ym2151_.update() * 2 + ym2203_.update() * 2 + oki1_level_ * 2 + oki2_level_) / 6;
        audio_.push_back(int16_t(std::clamp<int32_t>(s, -32768, 32767)));
    }
}

void CavemanNinja::drain_audio(std::vector<int16_t>& out) {
    out.insert(out.end(), audio_.begin(), audio_.end());
    audio_.clear();
}

// ---------------------------------------------------------------------------
// Video

void CavemanNinja::draw_sprites(int priority) {
    constexpr int kGlobalX = 240;
    constexpr int kColorAdd = 0x300;
    for (int f = 0; f < 0x100; ++f) {
        const uint16_t x0 = sprite_ram_[size_t(f) * 4 + 2];
        if (((x0 >> 8) & 0xc0) != priority) continue;
        const uint16_t y0 = sprite_ram_[size_t(f) * 4];
        if ((y0 & 0x1000) && (frame_counter_ & 1)) continue;
        const int color = (x0 >> 9) & 0x1f;
        const bool flip_x = (y0 & 0x2000) != 0;
        const bool flip_y = (y0 & 0x4000) != 0;
        int multi = (1 << ((y0 & 0x0600) >> 9)) - 1;
        int code = sprite_ram_[size_t(f) * 4 + 1] & ~multi;
        int inc;
        if (flip_y) {
            inc = -1;
        } else {
            code += multi;
            inc = 1;
        }
        if (code == 0) continue;
        const int sx = (kGlobalX - x0) & 0x1ff;
        const int sy = (240 - y0) & 0x1ff;
        for (; multi >= 0; --multi) {
            const int tile = code - multi * inc;
            const int py0 = (sy - 16 * multi) & 0x1ff;
            const uint8_t* g = sprites_.element(tile);
            for (int r = 0; r < 16; ++r) {
                const int py = ((py0 + r) & 0x1ff) - kFirstLine;
                if (py < 0 || py >= kHeight) continue;
                const uint8_t* row = g + (flip_y ? 15 - r : r) * 16;
                uint32_t* out = &screen_[size_t(py) * kWidth];
                for (int c = 0; c < 16; ++c) {
                    const int px = sx + c;
                    if (px < 0 || px >= kWidth) continue;
                    const int pen = row[flip_x ? 15 - c : c];
                    if (pen) out[px] = palette_[size_t(kColorAdd + color * 16 + pen) & 0x7ff];
                }
            }
        }
    }
}

void CavemanNinja::update_video() {
    auto pf = [&](int chip, int playfield, bool transparent) {
        if (!tilegen_[chip].enabled(playfield)) return;
        tilegen_[chip].draw(playfield, screen_.data(), kWidth, kHeight, kFirstLine, palette_.data(),
                            !transparent);
    };
    std::fill(screen_.begin(), screen_.end(), palette_[0]);
    // update_video_cninja: chip1.pf2, spr $80, chip1.pf1, spr $40, chip0.pf2, spr 0, chip0.pf1
    pf(1, 1, false);
    draw_sprites(0x80);
    pf(1, 0, true);
    draw_sprites(0x40);
    pf(0, 1, true);
    draw_sprites(0);
    pf(0, 0, true);

    if (flip_screen_) {
        for (int y = 0; y < kHeight; ++y) {
            const uint32_t* src = &screen_[size_t(kHeight - 1 - y) * kWidth];
            uint32_t* dst = &framebuffer_[size_t(y) * kWidth];
            for (int x = 0; x < kWidth; ++x) dst[x] = src[kWidth - 1 - x];
        }
    } else {
        framebuffer_ = screen_;
    }
}

// ---------------------------------------------------------------------------

void CavemanNinja::run_frame() {
    const double main_cycles = double(kMainClock) / kFramesPerSecond / kScanlines;
    const double sound_cycles = double(kSoundClock) / kFramesPerSecond / kScanlines;
    for (int line = 0; line < kScanlines; ++line) {
        scanline_ = line;
        if (line <= 240) {
            if (raster_irq_ && irq_line_ == line) {
                if (irq_mask_ & 0x10) main_cpu_.set_irq(3, IrqLine::Assert);
                else main_cpu_.set_irq(4, IrqLine::Assert);
                raster_irq_ = false;
            }
            if (line == 8) in1_ &= ~0x08;
        }
        if (line == 248) {
            main_cpu_.set_irq(5, IrqLine::Hold);
            update_video();
            in1_ |= 0x08;
        }
        main_debt_ += main_cycles;
        main_debt_ -= main_cpu_.run(int(main_debt_));
        sound_debt_ += sound_cycles;
        sound_debt_ -= sound_cpu_.run(int(sound_debt_));
    }
    ++frame_counter_;
}

void CavemanNinja::set_inputs(const MachineInputs& in) {
    auto pad = [](const InputState& p) {
        uint16_t v = 0xff;
        if (p.up) v &= ~0x01;
        if (p.down) v &= ~0x02;
        if (p.left) v &= ~0x04;
        if (p.right) v &= ~0x08;
        if (p.button1) v &= ~0x10;
        if (p.button2) v &= ~0x20;
        if (p.button3) v &= ~0x40;
        if (p.start) v &= ~0x80;
        return v;
    };
    in0_ = uint16_t(pad(in.player1) | (pad(in.player2) << 8));
    uint16_t sys = uint16_t(in1_ & 0x08) | 0x07;
    if (in.coin1) sys &= ~0x01;
    if (in.coin2) sys &= ~0x02;
    in1_ = sys;
}

void CavemanNinja::set_dip_switch(int bank, uint8_t value) {
    if (bank == 0) dsw_ = uint16_t((dsw_ & 0xff00) | value);
    else if (bank == 1) dsw_ = uint16_t((dsw_ & 0x00ff) | (uint16_t(value) << 8));
}

}  // namespace dsp
