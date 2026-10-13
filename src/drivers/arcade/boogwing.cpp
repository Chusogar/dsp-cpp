#include "drivers/arcade/boogwing.h"

#include <algorithm>

#include "core/rom_loader.h"
#include "machine/deco_crypt.h"

namespace dsp {
namespace {

const std::vector<RomEntry> kProgram = {
    {"kn_00-2.2b", 0x40000, 0x00000, 0xe38892b9}, {"kn_02-2.2e", 0x40000, 0x00001, 0x8426efef},
    {"kn_01-2.4b", 0x40000, 0x80000, 0x3ad4b54c}, {"kn_03-2.4e", 0x40000, 0x80001, 0x10b61f4a}};
const std::vector<RomEntry> kSound = {{"km06.18p", 0x10000, 0, 0x3e8bc4e1}};
const std::vector<RomEntry> kChars = {{"km05.9e", 0x10000, 0, 0xd10aef95},
                                      {"km04.8e", 0x10000, 1, 0x329323a8}};
const std::vector<RomEntry> kTiles1 = {{"mbd-01.9b", 0x100000, 0x000000, 0xd7de4f4b},
                                       {"mbd-00.8b", 0x100000, 0x100000, 0xadb20ba9}};
const std::vector<RomEntry> kTiles1Plane5 = {{"mbd-02.10e", 0x80000, 0, 0xb25aa721}};
const std::vector<RomEntry> kTiles2 = {{"mbd-03.13b", 0x100000, 0x000000, 0xcf798f2c},
                                       {"mbd-04.14b", 0x100000, 0x100000, 0xd9764d0b}};
const std::vector<RomEntry> kSprites1 = {{"mbd-05.16b", 0x200000, 0x200000, 0x1768c66a},
                                         {"mbd-06.17b", 0x200000, 0x000000, 0x7750847a}};
const std::vector<RomEntry> kSprites2 = {{"mbd-07.18b", 0x200000, 0x200000, 0x241faac1},
                                         {"mbd-08.19b", 0x200000, 0x000000, 0xf13b1e56}};
const std::vector<RomEntry> kOki1 = {{"mbd-10.17p", 0x80000, 0, 0xf159f76a}};
const std::vector<RomEntry> kOki2 = {{"mbd-09.16p", 0x80000, 0, 0xf44f2f87}};

// Loads files whose bytes go to every other address (ROM_LOAD16_BYTE).
bool load_interleaved(RomLoader& loader, const std::vector<RomEntry>& entries,
                      std::vector<uint8_t>& dest, std::string* error) {
    for (const RomEntry& e : entries) {
        std::vector<uint8_t> part(e.length, 0xff);
        if (!loader.load({{e.name, e.length, 0, e.crc}}, part, error)) return false;
        for (uint32_t i = 0; i < e.length; ++i) dest[e.offset + 2 * i] = part[i];
    }
    return true;
}

// 16x16 elements of 64 bytes: the right half first, rows of 16 bits.
GfxLayout tile16_layout(int total, std::vector<int> planes) {
    GfxLayout l;
    l.width = l.height = 16;
    l.total = total;
    l.planes = int(planes.size());
    l.char_increment = 32 * 16;
    l.plane_offsets = std::move(planes);
    for (int i = 0; i < 8; ++i) l.x_offsets.push_back(32 * 8 + i);
    for (int i = 0; i < 8; ++i) l.x_offsets.push_back(i);
    for (int i = 0; i < 16; ++i) l.y_offsets.push_back(i * 16);
    return l;
}

int bank_callback(int bank) { return ((bank >> 4) & 7) * 0x1000; }

int bank_callback2(int bank) {
    int offset = ((bank >> 4) & 7) * 0x1000;
    if ((bank & 0xf) == 0xa) offset += 0x800;  // transporter level
    return offset;
}

}  // namespace

BoogieWings::BoogieWings()
    : screen_(size_t(kWidth) * kHeight, 0xff000000u),
      framebuffer_(size_t(kWidth) * kHeight, 0xff000000u) {
    main_cpu_.set_memory_handlers([this](uint32_t a) { return main_read(a); },
                                  [this](uint32_t a, uint16_t v) { main_write(a, v); });
    // Byte writes to the protection chip must not go through a read (reads
    // have side effects there) and only change the addressed byte.
    main_cpu_.set_byte_handlers(
        [this](uint32_t a) {
            const uint16_t w = main_read(a & ~1u);
            return uint8_t((a & 1) ? w : w >> 8);
        },
        [this](uint32_t a, uint8_t v) {
            const uint32_t masked = a & 0xffffff;
            if (masked >= 0x24e000 && masked < 0x24f000) {
                deco104_.write_data(uint16_t(masked & 0x7fe), uint16_t(v << 8 | v),
                                    (a & 1) ? 0x00ff : 0xff00);
                return;
            }
            const uint16_t w = main_read(a & ~1u);
            main_write(a & ~1u, (a & 1) ? uint16_t((w & 0xff00) | v) : uint16_t((w & 0x00ff) | v << 8));
        });
    sound_cpu_.set_memory_handlers([this](uint32_t a) { return sound_read(a); },
                                   [this](uint32_t a, uint8_t v) { sound_write(a, v); });
    sound_cpu_.set_cycle_handler([this](int cycles) { on_sound_cycles(cycles); });
    ym_.set_irq_handler([this](bool state) {
        sound_cpu_.set_irq_line(1, state ? IrqLine::Assert : IrqLine::Clear);
    });
    ym_.set_port_handler([this](uint8_t value) {
        oki1_.set_rom_bank_offset((value & 1) ? 0x40000 : 0);
        oki2_.set_rom_bank_offset((value & 2) ? 0x40000 : 0);
    });
    deco104_.set_port_handlers([this] { return in0_; }, [this] { return in1_; },
                               [this] { return dsw_; });
    deco104_.set_soundlatch_handler([this](uint8_t value) {
        sound_latch_ = value;
        ++sound_commands_;
        sound_cpu_.set_irq_line(0, IrqLine::Hold);
    });

    for (int chip = 0; chip < 2; ++chip) {
        for (int pf = 0; pf < 2; ++pf) {
            Deco16ic::Layer layer;
            layer.gfx8 = &chars_;
            layer.color_base8 = 0;
            if (chip == 0) {
                layer.gfx16 = &tiles1_;  // 5 bpp
                layer.color_base16 = 0x100;
                layer.granularity16 = 32;
                if (pf == 1) layer.bank = bank_callback;
            } else {
                layer.gfx16 = &tiles2_;
                layer.color_base16 = 0x300;
                layer.color_bank = pf == 1 ? 16 : 0;
                layer.bank = bank_callback2;
            }
            tilegen_[chip].configure(pf, std::move(layer));
        }
    }
}

bool BoogieWings::init(const std::string& rom_path, std::string* error) {
    RomLoader loader;
    if (!loader.open(rom_path, error)) return false;

    // 68000 program, decrypted into the opcode and data views.
    std::vector<uint8_t> image(0x100000, 0xff);
    if (!load_interleaved(loader, kProgram, image, error)) return false;
    std::vector<uint16_t> words(0x80000);
    for (size_t i = 0; i < words.size(); ++i) words[i] = uint16_t(image[2 * i] << 8 | image[2 * i + 1]);
    rom_opcodes_.assign(words.size(), 0);
    rom_data_.assign(words.size(), 0);
    deco102_decrypt_cpu(words.data(), rom_opcodes_.data(), rom_data_.data(), words.size(), 0x42ba,
                        0x00, 0x18);

    sound_rom_.assign(0x10000, 0xff);
    if (!loader.load(kSound, sound_rom_, error)) return false;
    std::vector<uint8_t> oki(0x80000, 0);
    if (!loader.load(kOki1, oki, error)) return false;
    oki1_.set_rom(oki);
    std::fill(oki.begin(), oki.end(), 0);
    if (!loader.load(kOki2, oki, error)) return false;
    oki2_.set_rom(std::move(oki));

    // 8x8 characters.
    {
        std::vector<uint8_t> chars(0x20000, 0);
        if (!load_interleaved(loader, kChars, chars, error)) return false;
        deco56_decrypt_gfx(chars.data(), chars.size());
        GfxLayout l;
        l.width = l.height = 8;
        l.total = 0x1000;
        l.planes = 4;
        l.char_increment = 16 * 8;
        l.plane_offsets = {0x10000 * 8 + 8, 0x10000 * 8, 8, 0};
        for (int i = 0; i < 8; ++i) l.x_offsets.push_back(i);
        for (int i = 0; i < 8; ++i) l.y_offsets.push_back(i * 16);
        chars_.decode(l, chars);
    }
    // 16x16 tiles of the first chip: 4 planes encrypted plus a plain 5th one.
    {
        std::vector<uint8_t> gfx(0x300000, 0);
        if (!loader.load(kTiles1, gfx, error)) return false;
        deco56_decrypt_gfx(gfx.data(), 0x200000);
        std::vector<uint8_t> plane5(0x100000, 0);
        if (!load_interleaved(loader, kTiles1Plane5, plane5, error)) return false;
        deco56_remap_gfx(plane5.data(), plane5.size());
        std::copy(plane5.begin(), plane5.end(), gfx.begin() + 0x200000);
        tiles1_.decode(tile16_layout(0x4000, {0x200000 * 8, 0x100000 * 8 + 8, 0x100000 * 8, 8, 0}),
                       gfx);
    }
    {
        std::vector<uint8_t> gfx(0x200000, 0);
        if (!loader.load(kTiles2, gfx, error)) return false;
        deco56_decrypt_gfx(gfx.data(), gfx.size());
        tiles2_.decode(tile16_layout(0x4000, {0x100000 * 8 + 8, 0x100000 * 8, 8, 0}), gfx);
    }
    {
        std::vector<uint8_t> gfx(0x400000, 0);
        if (!loader.load(kSprites1, gfx, error)) return false;
        sprites_[0].decode(tile16_layout(0x8000, {0x200000 * 8 + 8, 0x200000 * 8, 8, 0}), gfx);
        std::fill(gfx.begin(), gfx.end(), 0);
        if (!loader.load(kSprites2, gfx, error)) return false;
        sprites_[1].decode(tile16_layout(0x8000, {0x200000 * 8 + 8, 0x200000 * 8, 8, 0}), gfx);
    }

    warnings_ = loader.warnings();
    reset();
    return true;
}

void BoogieWings::reset() {
    ram_.fill(0);
    sprite_ram_[0].fill(0);
    sprite_ram_[1].fill(0);
    palette_ram_.fill(0);
    palette_buffer_.fill(0);
    ace_ram_.fill(0);
    sound_ram_.fill(0);
    palette_.fill(0xff000000u);
    tilegen_[0].reset();
    tilegen_[1].reset();
    deco104_.reset();
    in1_ = uint16_t((in1_ & ~0x08) | 0x07);
    priority_ = 0;
    sound_latch_ = 0;
    main_debt_ = sound_debt_ = 0;
    ym_accum_ = oki1_accum_ = oki2_accum_ = audio_accum_ = 0;
    oki1_level_ = oki2_level_ = 0;
    audio_.clear();
    main_cpu_.reset();
    sound_cpu_.reset();
    ym_.reset();
    oki1_.reset();
    oki2_.reset();
    oki1_.set_rom_bank_offset(0);
    oki2_.set_rom_bank_offset(0);
}

// ---------------------------------------------------------------------------
// Main CPU

uint16_t BoogieWings::main_read(uint32_t a) {
    a &= 0xffffff;
    if (a < 0x100000) return main_cpu_.opcode() ? rom_opcodes_[a >> 1] : rom_data_[a >> 1];
    if (a >= 0x200000 && a < 0x210000) return ram_[(a & 0xffff) >> 1];
    if (a >= 0x242000 && a < 0x242800) return sprite_ram_[0][(a & 0x7ff) >> 1];
    if (a >= 0x246000 && a < 0x246800) return sprite_ram_[1][(a & 0x7ff) >> 1];
    // The DECO 104 sees address bits 10-0 (bits 13-11 land above its decoder).
    if (a >= 0x24e000 && a < 0x24f000) return deco104_.read_data(uint16_t(a & 0x7ff));
    if (a >= 0x260000 && a < 0x280000) {
        Deco16ic& chip = tilegen_[(a >> 16) & 1];
        switch ((a >> 12) & 0xf) {
            case 0x0: return chip.control_r(int((a & 0xf) >> 1));
            case 0x4:
            case 0x5: return chip.data[0][(a & 0x1fff) >> 1];
            case 0x6:
            case 0x7: return chip.data[1][(a & 0x1fff) >> 1];
            case 0x8: return chip.rowscroll[0][(a & 0xfff) >> 1];
            case 0xa: return chip.rowscroll[1][(a & 0xfff) >> 1];
            default: return 0xffff;
        }
    }
    if (a >= 0x284000 && a < 0x286000) {
        const uint32_t entry = palette_ram_[(a >> 2) & 0x7ff];
        return (a & 2) ? uint16_t(entry) : uint16_t(entry >> 16);
    }
    if (a >= 0x3c0000 && a < 0x3c0050) return ace_ram_[(a & 0xff) >> 1];
    return 0xffff;
}

void BoogieWings::main_write(uint32_t a, uint16_t v) {
    a &= 0xffffff;
    if (a < 0x100000) return;
    if (a >= 0x200000 && a < 0x210000) {
        ram_[(a & 0xffff) >> 1] = v;
        return;
    }
    if (a == 0x220000) {
        priority_ = v;
        return;
    }
    if (a >= 0x242000 && a < 0x242800) {
        sprite_ram_[0][(a & 0x7ff) >> 1] = v;
        return;
    }
    if (a >= 0x246000 && a < 0x246800) {
        sprite_ram_[1][(a & 0x7ff) >> 1] = v;
        return;
    }
    if (a >= 0x24e000 && a < 0x24f000) {
        deco104_.write_data(uint16_t(a & 0x7ff), v);
        return;
    }
    if (a >= 0x260000 && a < 0x280000) {
        Deco16ic& chip = tilegen_[(a >> 16) & 1];
        switch ((a >> 12) & 0xf) {
            case 0x0:
                if ((a & 0xfff) < 0x10) chip.control_w(int((a & 0xf) >> 1), v);
                break;
            case 0x4:
            case 0x5: chip.data[0][(a & 0x1fff) >> 1] = v; break;
            case 0x6:
            case 0x7: chip.data[1][(a & 0x1fff) >> 1] = v; break;
            case 0x8: chip.rowscroll[0][(a & 0xfff) >> 1] = v; break;
            case 0xa: chip.rowscroll[1][(a & 0xfff) >> 1] = v; break;
            default: break;
        }
        return;
    }
    if (a == 0x282008) {  // palette DMA
        palette_buffer_ = palette_ram_;
        update_palette();
        return;
    }
    if (a >= 0x284000 && a < 0x286000) {
        uint32_t& entry = palette_ram_[(a >> 2) & 0x7ff];
        if (a & 2) entry = (entry & 0xffff0000u) | v;
        else entry = (entry & 0x0000ffffu) | (uint32_t(v) << 16);
        return;
    }
    if (a >= 0x3c0000 && a < 0x3c0050) {
        const size_t i = (a & 0xff) >> 1;
        if (ace_ram_[i] != v) {
            ace_ram_[i] = v;
            if (i >= 0x20 && i <= 0x26) update_palette();
        }
    }
}

// DECO ACE: the displayed palette is the buffered one moved towards the fade
// colour (or brightened, mode 0x1000) by the fade strength registers.
void BoogieWings::update_palette() {
    const int fade_r = ace_ram_[0x20] & 0xff, fade_g = ace_ram_[0x21] & 0xff, fade_b = ace_ram_[0x22] & 0xff;
    const int ps_r = ace_ram_[0x23] & 0xff, ps_g = ace_ram_[0x24] & 0xff, ps_b = ace_ram_[0x25] & 0xff;
    const uint16_t mode = ace_ram_[0x26];
    for (size_t i = 0; i < 0x800; ++i) {
        const uint32_t c = palette_buffer_[i];
        int b = int((c >> 16) & 0xff), g = int((c >> 8) & 0xff), r = int(c & 0xff);
        palette_[0x800 + i] = 0xff000000u | uint32_t(r) << 16 | uint32_t(g) << 8 | uint32_t(b);
        if (mode == 0x1000) {
            r += ps_r;
            g += ps_g;
            b += ps_b;
        } else {
            r += (fade_r - r) * ps_r / 255;
            g += (fade_g - g) * ps_g / 255;
            b += (fade_b - b) * ps_b / 255;
        }
        r = std::clamp(r, 0, 255);
        g = std::clamp(g, 0, 255);
        b = std::clamp(b, 0, 255);
        palette_[i] = 0xff000000u | uint32_t(r) << 16 | uint32_t(g) << 8 | uint32_t(b);
    }
}

// ---------------------------------------------------------------------------
// Sound CPU

uint8_t BoogieWings::sound_read(uint32_t a) {
    if (a < 0x10000) return sound_rom_[a];
    switch (a) {
        case 0x110000:
        case 0x110001: return ym_.status();
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

void BoogieWings::sound_write(uint32_t a, uint8_t v) {
    if (a < 0x10000) return;
    switch (a) {
        case 0x110000: ym_.select_register(v); return;
        case 0x110001: ym_.write(v); return;
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

void BoogieWings::on_sound_cycles(int cycles) {
    // The YM2151 timers run from their own clock.
    ym_accum_ += int64_t(cycles) * kYmClock;
    const int ym_cycles = int(ym_accum_ / kSoundClock);
    ym_accum_ -= int64_t(ym_cycles) * kSoundClock;
    ym_.run_timers(ym_cycles);
    // Each OKI makes a new sample at clock/132 Hz, held in between.
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
        // Scaled so the loudest passages stay below full scale.
        const int32_t s = (ym_.update() * 3 + oki1_level_ * 3) / 8 + oki2_level_ * 3 / 16;
        audio_.push_back(int16_t(std::clamp<int32_t>(s, -32768, 32767)));
    }
}

void BoogieWings::drain_audio(std::vector<int16_t>& out) {
    out.insert(out.end(), audio_.begin(), audio_.end());
    audio_.clear();
}

// ---------------------------------------------------------------------------
// Video

// DECO 52 sprites, drawn with the board's flipped orientation. Only the
// sprites whose priority bits (word 2, bits 15-14) equal `priority`.
void BoogieWings::draw_sprites(int set, int priority) {
    const std::array<uint16_t, 0x400>& ram = sprite_ram_[set];
    const GfxSet& gfx = sprites_[set];
    const int color_add = set == 0 ? 0x500 : 0x700;
    for (int f = 0; f < 0x100; ++f) {
        const uint16_t x0 = ram[size_t(f) * 4 + 2];
        if (((x0 >> 8) & 0xc0) != priority) continue;
        const uint16_t y0 = ram[size_t(f) * 4];
        if ((y0 & 0x1000) && (frame_counter_ & 1)) continue;  // flashing
        const int color = (x0 >> 9) & 0x1f;
        const bool flip_x = !(y0 & 0x2000), flip_y = !(y0 & 0x4000);
        int multi = (1 << ((y0 & 0x0600) >> 9)) - 1;  // 1, 2, 4 or 8 tiles high
        int code = ram[size_t(f) * 4 + 1] & ~multi;
        if (code == 0) continue;
        int inc = -1;
        if (!(y0 & 0x4000)) {
            code += multi;
            inc = 1;
        }
        const int sx = 304 - ((304 - x0) & 0x1ff);
        for (; multi >= 0; --multi) {
            const int sy = (y0 + 16 * multi) & 0x1ff;
            const uint8_t* g = gfx.element(code - multi * inc);
            for (int r = 0; r < 16; ++r) {
                const int py = ((sy + r) & 0x1ff) - kFirstLine;
                if (py < 0 || py >= kHeight) continue;
                const uint8_t* row = g + (flip_y ? 15 - r : r) * 16;
                uint32_t* out = &screen_[size_t(py) * kWidth];
                for (int c = 0; c < 16; ++c) {
                    const int px = sx + c;
                    if (px < 0 || px >= kWidth) continue;
                    const int pen = row[flip_x ? 15 - c : c];
                    if (pen) out[px] = palette_[size_t(color_add + color * 16 + pen) & 0xfff];
                }
            }
        }
    }
}

void BoogieWings::update_video() {
    auto pf = [&](int chip, int playfield, bool transparent) {
        if (!tilegen_[chip].enabled(playfield)) return;
        tilegen_[chip].draw(playfield, screen_.data(), kWidth, kHeight, kFirstLine, palette_.data(),
                            !transparent);
    };
    auto sprites = [&](int priority) {
        draw_sprites(0, priority);
        draw_sprites(1, priority);
    };
    std::fill(screen_.begin(), screen_.end(), palette_[0]);
    switch (priority_ & 7) {
        case 5:
            pf(0, 1, false);
            sprites(0x80);
            pf(1, 0, true);
            sprites(0x40);
            pf(1, 1, true);
            break;
        case 4:
            pf(1, 0, false);
            sprites(0x80);
            pf(1, 1, true);
            sprites(0x40);
            pf(0, 1, true);
            break;
        case 1:
        case 2:
        case 3:
            pf(1, 1, false);
            sprites(0x80);
            pf(0, 1, true);
            sprites(0x40);
            pf(1, 0, true);
            break;
        default:
            pf(1, 1, false);
            sprites(0x80);
            pf(1, 0, true);
            sprites(0x40);
            pf(0, 1, true);
            break;
    }
    sprites(0);
    pf(0, 0, true);
    framebuffer_ = screen_;
}

// ---------------------------------------------------------------------------

void BoogieWings::run_frame() {
    const double main_cycles = double(kMainClock) / kFramesPerSecond / kScanlines;
    const double sound_cycles = double(kSoundClock) / kFramesPerSecond / kScanlines;
    for (int line = 0; line < kScanlines; ++line) {
        if (line == 8) in1_ &= ~0x08;
        if (line == 248) {
            main_cpu_.set_irq(6, IrqLine::Hold);
            update_video();
            in1_ |= 0x08;  // VBLANK
        }
        main_debt_ += main_cycles;
        main_debt_ -= main_cpu_.run(int(main_debt_));
        sound_debt_ += sound_cycles;
        sound_debt_ -= sound_cpu_.run(int(sound_debt_));
    }
    ++frame_counter_;
}

void BoogieWings::set_inputs(const MachineInputs& in) {
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
    in0_ = uint16_t(pad(in.player1) | pad(in.player2) << 8);
    uint16_t sys = uint16_t(in1_ & 0x08) | 0x07;
    if (in.coin1) sys &= ~0x01;
    if (in.coin2) sys &= ~0x02;
    if (in.service) sys &= ~0x04;
    in1_ = sys;
}

void BoogieWings::set_dip_switch(int bank, uint8_t value) {
    if (bank == 0) dsw_ = uint16_t((dsw_ & 0xff00) | value);
    else if (bank == 1) dsw_ = uint16_t((dsw_ & 0x00ff) | (value << 8));
}

}  // namespace dsp
