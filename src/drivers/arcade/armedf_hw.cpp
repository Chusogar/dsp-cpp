#include "drivers/arcade/armedf_hw.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "core/rom_loader.h"

namespace dsp {
namespace {

uint32_t pal4bit(uint16_t n) {
    n &= 0xf;
    return uint32_t(n) * 255 / 15;
}
uint32_t argb(uint8_t r, uint8_t g, uint8_t b) { return 0xff000000u | (uint32_t(r) << 16) | (uint32_t(g) << 8) | b; }

struct WordRom {
    const char* name;
    uint32_t size;
    uint32_t p;  // p>>1 = destination word index, p&1 = byte lane (0=low,1=high)
    uint32_t crc;
};

bool load_word_interleaved(RomLoader& loader, const std::vector<WordRom>& entries,
                           std::array<uint16_t, 0x30000>& dest, std::string* error) {
    for (const auto& e : entries) {
        std::vector<uint8_t> buf;
        if (!loader.try_read(e.name, buf) || buf.size() < e.size) {
            if (error) *error = std::string("missing ROM file: ") + e.name;
            return false;
        }
        const uint32_t word_base = e.p >> 1;
        const uint32_t lane = e.p & 1;
        for (uint32_t i = 0; i < e.size && word_base + i < dest.size(); i++) {
            uint16_t& w = dest[word_base + i];
            if (lane == 0) w = uint16_t((w & 0xff00) | buf[i]);
            else w = uint16_t((w & 0x00ff) | (uint16_t(buf[i]) << 8));
        }
    }
    return true;
}

// clang-format off
const std::vector<WordRom> kArmedfProg = {
    {"06.3d", 0x10000, 1, 0x0f9015e2}, {"01.3f", 0x10000, 0, 0x816ff7c5},
    {"07.5d", 0x10000, 0x20001, 0x5b3144a5}, {"02.4f", 0x10000, 0x20000, 0xfa10c29d},
    {"af_08.rom", 0x10000, 0x40001, 0xd1d43600}, {"af_03.rom", 0x10000, 0x40000, 0xbbe1fe2d},
};
const std::vector<WordRom> kTerrafProg = {
    {"8.6e", 0x10000, 1, 0xfd58fa06}, {"3.6h", 0x10000, 0, 0x54823a7d},
    {"7.4e", 0x10000, 0x20001, 0xfde8de7e}, {"2.4h", 0x10000, 0x20000, 0xdb987414},
    {"6.3e", 0x10000, 0x40001, 0xa5bb8c3b}, {"1.3h", 0x10000, 0x40000, 0xd2de6d28},
};
const std::vector<WordRom> kCclimbr2Prog = {
    {"4.bin", 0x10000, 1, 0x7922ea14}, {"1.bin", 0x10000, 0, 0x2ac7ed67},
    {"6.bin", 0x10000, 0x20001, 0x7905c992}, {"5.bin", 0x10000, 0x20000, 0x47be6c1e},
    {"3.bin", 0x10000, 0x40001, 0x1fb110d6}, {"2.bin", 0x10000, 0x40000, 0x0024c15b},
};
const std::vector<WordRom> kLegionProg = {
    {"lg1.bin", 0x10000, 0, 0xc4aeb724}, {"lg3.bin", 0x10000, 1, 0x777e4935},
    {"legion.1b", 0x10000, 0x20000, 0xc306660a}, {"legion.1d", 0x10000, 0x20001, 0xc2e45e1e},
};

const std::vector<RomEntry> kArmedfSound = {{"af_10.rom", 0x10000, 0, 0xc5eacb87}};
const std::vector<RomEntry> kTerrafSound = {{"11.17k", 0x10000, 0, 0x4407d475}};
const std::vector<RomEntry> kCclimbr2Sound = {{"11.bin", 0x4000, 0, 0xfe0175be}, {"12.bin", 0x8000, 0x4000, 0x5ddf18f2}};
const std::vector<RomEntry> kLegionSound = {{"legion.1h", 0x4000, 0, 0x2ca4f7f0}, {"legion.1i", 0x8000, 0x4000, 0x79f4a827}};

const std::vector<RomEntry> kTerrafNb = {{"10.11c", 0x4000, 0, 0xac705812}};
const std::vector<RomEntry> kCclimbr2Nb = {{"9.bin", 0x4000, 0, 0x740d260f}};
const std::vector<RomEntry> kLegionNb = {{"lg7.bin", 0x4000, 0, 0x533e2b58}};

const std::vector<RomEntry> kArmedfChar = {{"09.11c", 0x8000, 0, 0x5c6993d5}};
const std::vector<RomEntry> kTerrafChar = {{"9.11e", 0x8000, 0, 0xbc6f7cbc}};
const std::vector<RomEntry> kCclimbr2Char = {{"10.bin", 0x8000, 0, 0x7f475266}};
const std::vector<RomEntry> kLegionChar = {{"lg8.bin", 0x8000, 0, 0xe0596570}};

const std::vector<RomEntry> kArmedfBg = {{"af_14.rom", 0x10000, 0, 0x8c5dc5a7}, {"af_13.rom", 0x10000, 0x10000, 0x136a58a3}};
const std::vector<RomEntry> kTerrafBg = {{"15.8a", 0x10000, 0, 0x2144d8e0}, {"14.6a", 0x10000, 0x10000, 0x744f5c9e}};
const std::vector<RomEntry> kCclimbr2Bg = {{"17.bin", 0x10000, 0, 0xe24bb2d7}, {"18.bin", 0x10000, 0x10000, 0x56834554}};
const std::vector<RomEntry> kLegionBg = {{"legion.1l", 0x10000, 0, 0x29b8adaa}};

const std::vector<RomEntry> kArmedfFg = {{"af_04.rom", 0x10000, 0, 0x44d3af4f}, {"af_05.rom", 0x10000, 0x10000, 0x92076cab}};
const std::vector<RomEntry> kTerrafFg = {{"5.15h", 0x10000, 0, 0x25d23dfd}, {"4.13h", 0x10000, 0x10000, 0xb9b0fe27}};
const std::vector<RomEntry> kCclimbr2Fg = {{"7.bin", 0x10000, 0, 0xcbdd3906}, {"8.bin", 0x10000, 0x10000, 0xb2a613c0}};
const std::vector<RomEntry> kLegionFg = {{"legion.1e", 0x10000, 0, 0xa9d70faf}, {"legion.1f", 0x8000, 0x18000, 0xf018313b}};

const std::vector<RomEntry> kArmedfSprites = {{"af_11.rom", 0x20000, 0, 0xb46c473c}, {"af_12.rom", 0x20000, 0x20000, 0x23cb6bfe}};
const std::vector<RomEntry> kTerrafSprites = {{"12.7d", 0x10000, 0, 0x2d1f2ceb}, {"13.9d", 0x10000, 0x10000, 0x1d2f92d6}};
const std::vector<RomEntry> kCclimbr2Sprites = {
    {"15.bin", 0x10000, 0, 0x4bf838be}, {"13.bin", 0x10000, 0x20000, 0x6b6ec999},
    {"16.bin", 0x10000, 0x10000, 0x21a265c5}, {"14.bin", 0x10000, 0x30000, 0xf426a4ad},
};
const std::vector<RomEntry> kLegionSprites = {{"legion.1k", 0x10000, 0, 0xff5a0db9}, {"legion.1j", 0x10000, 0x10000, 0xbae220c8}};
// clang-format on

}  // namespace

ArmedfHw::ArmedfHw(Game game)
    : game_(game), main_cpu_(kMainClock), sound_cpu_(kSoundClock),
      ym_(kSoundClock, game == Game::ArmedF ? YM3812::kYM3812 : YM3812::kYM3526, 0.4f) {
    if (game_ == Game::CrazyClimber2 || game_ == Game::Legion) {
        region_w_ = 288;
        region_h_ = 224;
        crop_x_ = 16;
        crop_y_ = 8;
    } else {
        region_w_ = 320;
        region_h_ = 240;
        crop_x_ = 0;
        crop_y_ = 0;
    }
    if (game_ == Game::ArmedF || game_ == Game::Legion) {
        rotated_ = true;
        screen_width_ = region_h_;
        screen_height_ = region_w_;
    } else {
        screen_width_ = region_w_;
        screen_height_ = region_h_;
    }
    if (game_ == Game::CrazyClimber2) {
        irq_level_ = 2;
        sprite_offset_ = 0;
        sprite_count_ = 0x200;
    } else if (game_ == Game::Legion) {
        irq_level_ = 2;
        sprite_offset_ = 0;
        sprite_count_ = 0x80;
    } else if (game_ == Game::TerraForce) {
        irq_level_ = 1;
        sprite_offset_ = 0x80;
        sprite_count_ = 0x80;
    } else {
        irq_level_ = 1;
        sprite_offset_ = 0x80;
        sprite_count_ = 0x200;
    }

    framebuffer_.assign(size_t(screen_width_) * size_t(screen_height_), 0xff000000u);
    bg_canvas_.assign(1024u * 512u, 0);
    fg_canvas_.assign(1024u * 512u, 0);
    composite_.assign(512u * 512u, 0xff000000u);

    main_cpu_.set_memory_handlers([this](uint32_t a) { return main_read(a); },
                                  [this](uint32_t a, uint16_t v) { main_write(a, v); });
    sound_cpu_.set_memory_handlers([this](uint16_t a) { return sound_read(a); },
                                   [this](uint16_t a, uint8_t v) { sound_write(a, v); });
    sound_cpu_.set_io_handlers([this](uint16_t p) { return sound_in(p); },
                               [this](uint16_t p, uint8_t v) { sound_out(p, v); });
    sound_cpu_.set_cycle_handler([this](int cycles) { on_sound_cycles(cycles); });

    main_cycles_per_line_ = int(kMainClock / uint32_t(kScanlines * kFramesPerSecond));
}

const char* ArmedfHw::title() const {
    switch (game_) {
        case Game::ArmedF: return "Armed F";
        case Game::TerraForce: return "Terra Force";
        case Game::CrazyClimber2: return "Crazy Climber 2";
        case Game::Legion: return "Legion";
    }
    return "Armed F";
}

// ---------------------------------------------------------------------------
// Loading
// ---------------------------------------------------------------------------

bool ArmedfHw::load_roms(const std::string& rom_path, std::string* error) {
    RomLoader loader;
    if (!loader.open(rom_path, error)) return false;

    const std::vector<WordRom>* prog = nullptr;
    const std::vector<RomEntry>*snd = nullptr, *nb = nullptr, *chr = nullptr, *bg = nullptr, *fg = nullptr,
        *spr = nullptr;
    switch (game_) {
        case Game::ArmedF:
            prog = &kArmedfProg; snd = &kArmedfSound; chr = &kArmedfChar;
            bg = &kArmedfBg; fg = &kArmedfFg; spr = &kArmedfSprites;
            break;
        case Game::TerraForce:
            prog = &kTerrafProg; snd = &kTerrafSound; nb = &kTerrafNb; chr = &kTerrafChar;
            bg = &kTerrafBg; fg = &kTerrafFg; spr = &kTerrafSprites;
            break;
        case Game::CrazyClimber2:
            prog = &kCclimbr2Prog; snd = &kCclimbr2Sound; nb = &kCclimbr2Nb; chr = &kCclimbr2Char;
            bg = &kCclimbr2Bg; fg = &kCclimbr2Fg; spr = &kCclimbr2Sprites;
            break;
        case Game::Legion:
            prog = &kLegionProg; snd = &kLegionSound; nb = &kLegionNb; chr = &kLegionChar;
            bg = &kLegionBg; fg = &kLegionFg; spr = &kLegionSprites;
            break;
    }

    if (!load_word_interleaved(loader, *prog, rom_, error)) return false;
    if (game_ == Game::Legion) {
        // Two protection-check patches applied directly to the decoded
        // program image in the reference driver (no MCU emulation).
        rom_[0x0001d6 / 2] = 1;
        rom_[0x000488 / 2] = 0x4e71;
    }

    std::vector<uint8_t> snd_bytes(0x10000, 0);
    if (!loader.load(*snd, snd_bytes, error)) return false;
    std::copy(snd_bytes.begin(), snd_bytes.end(), sound_ram_.begin());

    if (nb) {
        std::vector<uint8_t> nb_bytes;
        if (!loader.load(*nb, nb_bytes, error)) return false;
        std::copy(nb_bytes.begin(), nb_bytes.end(), nb_rom_.begin());
    }

    // Chars: 8x8, 4bpp, 2 pixels/byte (high nibble then low nibble).
    std::vector<uint8_t> char_rom;
    if (!loader.load(*chr, char_rom, error)) return false;
    {
        GfxLayout layout;
        layout.width = 8;
        layout.height = 8;
        layout.total = 0x400;
        layout.planes = 4;
        layout.char_increment = 32 * 8;
        layout.plane_offsets = {0, 1, 2, 3};
        layout.x_offsets = {4, 0, 12, 8, 20, 16, 28, 24};
        layout.y_offsets = {0 * 32, 1 * 32, 2 * 32, 3 * 32, 4 * 32, 5 * 32, 6 * 32, 7 * 32};
        chars_gfx_.decode(layout, char_rom);
    }

    auto tile_layout = [](int total) {
        GfxLayout layout;
        layout.width = 16;
        layout.height = 16;
        layout.total = total;
        layout.planes = 4;
        layout.char_increment = 128 * 8;
        layout.plane_offsets = {0, 1, 2, 3};
        layout.x_offsets = {4, 0, 12, 8, 20, 16, 28, 24, 32 + 4, 32 + 0, 32 + 12, 32 + 8, 32 + 20, 32 + 16, 32 + 28, 32 + 24};
        layout.y_offsets.resize(16);
        for (int i = 0; i < 16; i++) layout.y_offsets[size_t(i)] = i * 64;
        return layout;
    };
    std::vector<uint8_t> bg_rom;
    if (!loader.load(*bg, bg_rom, error)) return false;
    bg_gfx_.decode(tile_layout(0x400), bg_rom);

    std::vector<uint8_t> fg_rom;
    if (!loader.load(*fg, fg_rom, error)) return false;
    fg_gfx_.decode(tile_layout(0x400), fg_rom);

    // Sprites: 16x16, 4bpp, same nibble-pair packing but split across two
    // ROM-sized regions (the second pair of columns lives `base` bits on).
    std::vector<uint8_t> spr_rom;
    if (!loader.load(*spr, spr_rom, error)) return false;
    {
        const bool wide_base = (game_ == Game::ArmedF || game_ == Game::CrazyClimber2);
        const uint32_t base = wide_base ? 0x800u * 64u * 8u : 0x400u * 64u * 8u;
        const int total = wide_base ? 0x800 : 0x400;
        GfxLayout layout;
        layout.width = 16;
        layout.height = 16;
        layout.total = total;
        layout.planes = 4;
        layout.char_increment = 64 * 8;
        layout.plane_offsets = {0, 1, 2, 3};
        layout.x_offsets = {4,           0,           int(base) + 4,  int(base) + 0,  12,
                             8,           int(base) + 12, int(base) + 8,  20,             16,
                             int(base) + 20, int(base) + 16, 28,             24,
                             int(base) + 28, int(base) + 24};
        layout.y_offsets.resize(16);
        for (int i = 0; i < 16; i++) layout.y_offsets[size_t(i)] = i * 32;
        sprites_gfx_.decode(layout, spr_rom);
    }

    reset();
    return true;
}

bool ArmedfHw::init(const std::string& rom_path, std::string* error) {
    if (!load_roms(rom_path, error)) return false;
    reset();
    return true;
}

void ArmedfHw::reset() {
    main_cpu_.reset();
    sound_cpu_.reset();
    ym_.reset();
    dac0_.reset();
    dac1_.reset();
    frame_counter_ = 0;
    scroll_fg_x_ = scroll_fg_y_ = scroll_bg_x_ = scroll_bg_y_ = 0;
    sound_latch_ = 0;
    video_reg_ = 0;
    in0_ = in1_ = 0xffff;
}

// ---------------------------------------------------------------------------
// Main CPU memory map
// ---------------------------------------------------------------------------

uint16_t ArmedfHw::main_read(uint32_t addr) {
    addr &= 0xfffff;
    if (addr <= 0x5ffff) return rom_[addr >> 1];

    if (game_ == Game::ArmedF) {
        if (addr >= 0x60000 && addr <= 0x60fff) return ram_sprites_[(addr & 0xfff) >> 1];
        if ((addr >= 0x61000 && addr <= 0x65fff) || (addr >= 0x6c008 && addr <= 0x6c7ff))
            return ram_[(addr - 0x60000) >> 1];
        if (addr >= 0x66000 && addr <= 0x66fff) return ram_bg_[(addr & 0xfff) >> 1];
        if (addr >= 0x67000 && addr <= 0x67fff) return ram_fg_[(addr & 0xfff) >> 1];
        if (addr >= 0x68000 && addr <= 0x69fff) return ram_txt_[(addr & 0x1fff) >> 1];
        if (addr >= 0x6a000 && addr <= 0x6afff) return palette_raw_[(addr & 0xfff) >> 1];
        if (addr >= 0x6b000 && addr <= 0x6bfff) return ram_clut_[(addr & 0xfff) >> 1];
        if (addr == 0x6c000) return in0_;
        if (addr == 0x6c002) return in1_;
        if (addr == 0x6c004) return dsw_a_;
        if (addr == 0x6c006) return dsw_b_;
        return 0xffff;
    }

    // Terra Force / Crazy Climber 2 / Legion share this map.
    if (addr >= 0x60000 && addr <= 0x603ff) return ram_sprites_[(addr & 0xfff) >> 1];
    if ((addr >= 0x60400 && addr <= 0x63fff) || (addr >= 0x6a000 && addr <= 0x6a9ff))
        return ram_[(addr - 0x60000) >> 1];
    if (addr >= 0x64000 && addr <= 0x64fff) return palette_raw_[(addr & 0xfff) >> 1];
    if (addr >= 0x68000 && addr <= 0x69fff) return ram_txt_[(addr & 0x1fff) >> 1];
    if (addr >= 0x6c000 && addr <= 0x6cfff) return ram_clut_[(addr & 0xfff) >> 1];
    if (addr >= 0x70000 && addr <= 0x70fff) return ram_fg_[(addr & 0xfff) >> 1];
    if (addr >= 0x74000 && addr <= 0x74fff) return ram_bg_[(addr & 0xfff) >> 1];
    if (addr == 0x78000) return in0_;
    if (addr == 0x78002) return in1_;
    if (addr == 0x78004) return dsw_a_;
    if (addr == 0x78006) return dsw_b_;
    return 0xffff;
}

void ArmedfHw::write_palette(int index, uint16_t value) {
    if (index < 0 || size_t(index) >= palette_raw_.size()) return;
    palette_raw_[size_t(index)] = value;
    palette_[size_t(index)] =
        argb(uint8_t(pal4bit(value >> 8)), uint8_t(pal4bit(value >> 4)), uint8_t(pal4bit(value)));
}

void ArmedfHw::main_write(uint32_t addr, uint16_t value) {
    addr &= 0xfffff;
    if (addr <= 0x5ffff) return;  // ROM

    if (game_ == Game::ArmedF) {
        if (addr >= 0x60000 && addr <= 0x60fff) { ram_sprites_[(addr & 0xfff) >> 1] = value; return; }
        if (addr >= 0x61000 && addr <= 0x65fff) { ram_[(addr - 0x60000) >> 1] = value; return; }
        if (addr >= 0x6c000 && addr <= 0x6c7ff) { ram_[(addr - 0x60000) >> 1] = value; return; }
        if (addr >= 0x66000 && addr <= 0x66fff) { ram_bg_[(addr & 0xfff) >> 1] = value; return; }
        if (addr >= 0x67000 && addr <= 0x67fff) { ram_fg_[(addr & 0xfff) >> 1] = value; return; }
        if (addr >= 0x68000 && addr <= 0x69fff) { ram_txt_[(addr & 0x1fff) >> 1] = uint8_t(value); return; }
        if (addr >= 0x6a000 && addr <= 0x6afff) { write_palette(int((addr & 0xfff) >> 1), value); return; }
        if (addr >= 0x6b000 && addr <= 0x6bfff) { ram_clut_[(addr & 0xfff) >> 1] = value; return; }
        if (addr == 0x6d000) { video_reg_ = value; return; }
        if (addr == 0x6d002) { scroll_bg_x_ = value; return; }
        if (addr == 0x6d004) { scroll_bg_y_ = value; return; }
        if (addr == 0x6d006) { scroll_fg_x_ = value; return; }
        if (addr == 0x6d008) { scroll_fg_y_ = value; return; }
        if (addr == 0x6d00a) { sound_latch_ = uint8_t(((value & 0x7f) << 1) | 1); return; }
        if (addr == 0x6d00e) { main_cpu_.set_irq(irq_level_, IrqLine::Clear); return; }
        return;
    }

    if (addr >= 0x60000 && addr <= 0x603ff) { ram_sprites_[(addr & 0xfff) >> 1] = value; return; }
    if ((addr >= 0x60400 && addr <= 0x63fff) || (addr >= 0x6a000 && addr <= 0x6a9ff)) {
        ram_[(addr - 0x60000) >> 1] = value;
        return;
    }
    if (addr >= 0x64000 && addr <= 0x64fff) { write_palette(int((addr & 0xfff) >> 1), value); return; }
    if (addr >= 0x68000 && addr <= 0x69fff) { ram_txt_[(addr & 0x1fff) >> 1] = uint8_t(value); return; }
    if (addr >= 0x6c000 && addr <= 0x6cfff) { ram_clut_[(addr & 0xfff) >> 1] = value; return; }
    if (addr >= 0x70000 && addr <= 0x70fff) { ram_fg_[(addr & 0xfff) >> 1] = value; return; }
    if (addr >= 0x74000 && addr <= 0x74fff) { ram_bg_[(addr & 0xfff) >> 1] = value; return; }
    if (addr == 0x7c000) {
        if (uses_nb1414() && (value & 0x4000) != 0 && (video_reg_ & 0x4000) == 0) nb_exec();
        video_reg_ = value;
        return;
    }
    if (addr == 0x7c002) { scroll_bg_x_ = value; return; }
    if (addr == 0x7c004) { scroll_bg_y_ = value; return; }
    if (addr == 0x7c00a) { sound_latch_ = uint8_t(((value & 0x7f) << 1) | 1); return; }
    if (addr == 0x7c00e) { main_cpu_.set_irq(irq_level_, IrqLine::Clear); return; }
}

// ---------------------------------------------------------------------------
// Sound CPU
// ---------------------------------------------------------------------------

uint8_t ArmedfHw::sound_read(uint16_t addr) { return sound_ram_[addr]; }

void ArmedfHw::sound_write(uint16_t addr, uint8_t value) {
    const uint16_t rom_top = (game_ == Game::CrazyClimber2 || game_ == Game::Legion) ? 0xbfff : 0xf7ff;
    if (addr <= rom_top) return;
    sound_ram_[addr] = value;
}

uint8_t ArmedfHw::sound_in(uint16_t port) {
    switch (port & 0xff) {
        case 4: sound_latch_ = 0; return 0;
        case 6: return sound_latch_;
        default: return 0xff;
    }
}

void ArmedfHw::sound_out(uint16_t port, uint8_t value) {
    switch (port & 0xff) {
        case 0: ym_.control(value); break;
        case 1: ym_.write(value); break;
        case 2: dac0_.data8_w(value); break;
        case 3: dac1_.data8_w(value); break;
        default: break;
    }
}

void ArmedfHw::on_sound_cycles(int cycles) {
    sound_irq_accumulator_ += cycles;
    const int64_t period = int64_t(kSoundClock) / (int64_t(kSoundClock) / 512);
    while (sound_irq_accumulator_ >= period) {
        sound_irq_accumulator_ -= period;
        sound_cpu_.set_irq(IrqLine::Hold);
    }

    audio_accumulator_ += int64_t(cycles) * int64_t(YM3812::kSampleRate);
    while (audio_accumulator_ >= int64_t(kSoundClock)) {
        audio_accumulator_ -= int64_t(kSoundClock);
        const int32_t s = ym_.update() + dac0_.update() + dac1_.update();
        audio_.push_back(int16_t(std::max(-32768, std::min(32767, s))));
    }
}

void ArmedfHw::drain_audio(std::vector<int16_t>& out) {
    out.insert(out.end(), audio_.begin(), audio_.end());
    audio_.clear();
}

// ---------------------------------------------------------------------------
// NB1414M4 message coprocessor
// ---------------------------------------------------------------------------

void ArmedfHw::nb_dma(int src, int dst, int size, bool condition) {
    for (int f = 0; f < size; f++) {
        if (f + dst < 18) continue;  // don't clobber the command/parameter block
        ram_txt_[size_t(f + dst) & 0xfff] = condition ? nb_rom_[size_t(f + src) & 0x3fff] : 0x20;
        ram_txt_[(size_t(f + dst) + 0x400) & 0xfff] =
            condition ? nb_rom_[size_t(f + size + src) & 0x3fff] : nb_rom_[0x13];
    }
}

void ArmedfHw::nb_fill(int dst, uint8_t tile, uint8_t pal) {
    for (int f = 0; f <= 0x3ff; f++) {
        if (f + dst < 18) continue;
        ram_txt_[size_t(f + dst) & 0xfff] = tile;
        ram_txt_[(size_t(f + dst) + 0x400) & 0xfff] = pal;
    }
}

void ArmedfHw::nb_kozure_score_msg(int dst, int src_base) {
    int first_digit = 0;
    for (int f = 0; f < 6; f++) {
        const uint8_t res = uint8_t((ram_txt_[size_t((f >> 1) + 5 + src_base * 3)] >> ((~(f & 1) & 1) * 4)) & 0xf);
        if (first_digit != 0 || res != 0) {
            ram_txt_[size_t(f + dst) & 0xfff] = uint8_t(res + 0x30);
            first_digit = 1;
        } else {
            ram_txt_[size_t(f + dst) & 0xfff] = 0x20;
        }
        ram_txt_[(size_t(f + dst) + 0x400) & 0xfff] = nb_rom_[size_t(0x10f + src_base * 0x1c + f) & 0x3fff];
    }
    ram_txt_[size_t(6 + dst) & 0xfff] = 0x30;
    ram_txt_[(size_t(6 + dst) + 0x400) & 0xfff] = nb_rom_[size_t(0x10f + src_base * 0x1c + 6) & 0x3fff];
    ram_txt_[size_t(7 + dst) & 0xfff] = 0x30;
    ram_txt_[(size_t(7 + dst) + 0x400) & 0xfff] = nb_rom_[size_t(0x10f + src_base * 0x1c + 7) & 0x3fff];
}

void ArmedfHw::nb_insert_coin_msg() {
    const bool flicker = (frame_counter_ & 0x10) != 0;
    if (ram_txt_[0xf] == 0) {
        const int dst = ((nb_rom_[1] << 8) | nb_rom_[2]) & 0x3fff;
        nb_dma(3, dst, 0x10, flicker);
    } else {
        const int dst = ((nb_rom_[0x49] << 8) | nb_rom_[0x4a]) & 0x3fff;
        nb_dma(0x4b, dst, 0x18, true);
    }
}

void ArmedfHw::nb_credit_msg() {
    const uint8_t credit_count = ram_txt_[0xf];
    const bool flicker = (frame_counter_ & 0x10) != 0;

    int dst = ((nb_rom_[0x23] << 8) | nb_rom_[0x24]) & 0x3fff;
    nb_dma(0x25, dst, 0x10, true);

    dst = (((nb_rom_[0x45] << 8) | nb_rom_[0x46]) & 0x3fff);
    ram_txt_[size_t(dst) & 0xfff] = (credit_count & 0xf0) ? uint8_t(((credit_count & 0xf0) >> 4) + 0x30) : 0x20;
    ram_txt_[(size_t(dst) + 0x400) & 0xfff] = nb_rom_[0x47];
    ram_txt_[(size_t(dst) + 1) & 0xfff] = uint8_t((credit_count & 0x0f) + 0x30);
    ram_txt_[(size_t(dst) + 0x401) & 0xfff] = nb_rom_[0x48];

    if (credit_count == 1) {
        dst = ((nb_rom_[0x7b] << 8) | nb_rom_[0x7c]) & 0x3fff;
        nb_dma(0x7d, dst, 0x18, flicker);
    } else if (credit_count > 1) {
        dst = ((nb_rom_[0xad] << 8) | nb_rom_[0xae]) & 0x3fff;
        nb_dma(0xaf, dst, 0x18, flicker);
    }
}

void ArmedfHw::nb_cmd_0200(uint8_t command) {
    const int dst = ((nb_rom_[0x330 + (command & 0xf) * 2] << 8) | nb_rom_[0x331 + (command & 0xf) * 2]) & 0x3fff;
    if ((dst & 0x7ff) != 0) nb_fill(0, nb_rom_[size_t(dst)], nb_rom_[size_t(dst) + 1]);
    else nb_dma(dst, 0, 0x400, true);
}

void ArmedfHw::nb_cmd_0600(uint8_t is2p) {
    int dst = ((nb_rom_[0x1f5] << 8) | nb_rom_[0x1f6]) & 0x3fff;
    ram_txt_[size_t(dst) & 0xfff] = uint8_t((ram_txt_[7] & 7) + 0x30);

    dst = ((nb_rom_[0x1f8] << 8) | nb_rom_[0x1f9]) & 0x3fff;
    nb_dma(0x1fa + (((ram_txt_[7] & 0x30) >> 4) * 0x18), dst, 12, true);

    dst = ((nb_rom_[0x262] << 8) | nb_rom_[0x263]) & 0x3fff;
    nb_dma(0x264 + (((ram_txt_[7] & 0x80) >> 7) * 0x18), dst, 12, true);

    dst = ((nb_rom_[0x294] << 8) | nb_rom_[0x295]) & 0x3fff;
    nb_dma(0x296 + (((ram_txt_[7] & 0x40) >> 6) * 0x18), dst, 12, true);

    dst = ((nb_rom_[0x2c6] << 8) | nb_rom_[0x2c7]) & 0x3fff;
    ram_txt_[size_t(dst) & 0xfff] = uint8_t(((ram_txt_[0xf] & 0xf0) >> 4) + 0x30);
    dst = ((nb_rom_[0x2c9] << 8) | nb_rom_[0x2ca]) & 0x3fff;
    ram_txt_[size_t(dst) & 0xfff] = uint8_t((ram_txt_[0xf] & 0x0f) + 0x30);
    dst = ((nb_rom_[0x2cc] << 8) | nb_rom_[0x2cd]) & 0x3fff;
    ram_txt_[size_t(dst) & 0xfff] = uint8_t(((ram_txt_[0x10] & 0xf0) >> 4) + 0x30);
    dst = ((nb_rom_[0x2cf] << 8) | nb_rom_[0x2d0]) & 0x3fff;
    ram_txt_[size_t(dst) & 0xfff] = uint8_t((ram_txt_[0x10] & 0x0f) + 0x30);
    dst = ((nb_rom_[0x2d2] << 8) | nb_rom_[0x2d3]) & 0x3fff;
    ram_txt_[size_t(dst) & 0xfff] = uint8_t(((ram_txt_[0x11] & 0xf0) >> 4) + 0x30);
    ram_txt_[(size_t(dst) + 1) & 0xfff] = uint8_t((ram_txt_[0x11] & 0x0f) + 0x30);

    dst = ((nb_rom_[0x2d6] << 8) | nb_rom_[0x2d7]) & 0x3fff;
    nb_dma(0x2d8 + is2p * 0x18, dst, 12, true);

    dst = ((nb_rom_[0x308] << 8) | nb_rom_[0x309]) & 0x3fff;
    for (int f = 0; f <= 4; f++)
        nb_dma(0x310 + (((ram_txt_[4] >> (4 - f)) & 1) * 6), dst + f * 0x20, 3, true);

    dst = ((nb_rom_[0x30a] << 8) | nb_rom_[0x30b]) & 0x3fff;
    for (int f = 0; f <= 6; f++)
        nb_dma(0x310 + (((ram_txt_[2 + is2p] >> (6 - f)) & 1) * 6), dst + f * 0x20, 3, true);

    dst = ((nb_rom_[0x30c] << 8) | nb_rom_[0x30d]) & 0x3fff;
    for (int f = 0; f <= 7; f++)
        nb_dma(0x310 + (((ram_txt_[5] >> (7 - f)) & 1) * 6), dst + f * 0x20, 3, true);

    dst = ((nb_rom_[0x30e] << 8) | nb_rom_[0x30f]) & 0x3fff;
    for (int f = 0; f <= 7; f++)
        nb_dma(0x310 + (((ram_txt_[6] >> (7 - f)) & 1) * 6), dst + f * 0x20, 3, true);
}

void ArmedfHw::nb_cmd_0e00(uint8_t command) {
    int dst = ((nb_rom_[0xdf] << 8) | nb_rom_[0xe0]) & 0x3fff;
    nb_dma(0xe1, dst, 8, true);

    if ((command & 0x04) != 0) {
        dst = ((nb_rom_[0xfb] << 8) | nb_rom_[0xfc]) & 0x3fff;
        nb_dma(0xfd, dst, 8, (command & 1) == 0);
        dst = ((nb_rom_[0x10d] << 8) | nb_rom_[0x10e]) & 0x3fff;
        nb_kozure_score_msg(dst, 0);
        if ((command & 0x80) != 0) {
            dst = ((nb_rom_[0x117] << 8) | nb_rom_[0x118]) & 0x3fff;
            nb_dma(0x119, dst, 8, (command & 2) == 0);
            dst = ((nb_rom_[0x129] << 8) | nb_rom_[0x12a]) & 0x3fff;
            nb_kozure_score_msg(dst, 1);
        }
    } else {
        dst = ((nb_rom_[0x133] << 8) | nb_rom_[0x134]) & 0x3fff;
        nb_dma(0x135, dst, 0x10, (command & 1) == 0);
        nb_insert_coin_msg();
        if ((command & 0x18) == 0) nb_credit_msg();
    }
}

void ArmedfHw::nb_exec() {
    scroll_fg_x_ = uint16_t(ram_txt_[0xd] | (ram_txt_[0xe] << 8));
    scroll_fg_y_ = uint16_t(ram_txt_[0xb] | (ram_txt_[0xc] << 8));
    const uint16_t command = uint16_t((ram_txt_[0] << 8) | ram_txt_[1]);
    switch (command & 0xff00) {
        case 0x0000: nb_insert_coin_msg(); nb_credit_msg(); break;
        case 0x0200: nb_cmd_0200(uint8_t(command & 0x87)); break;
        case 0x0600: nb_cmd_0600(uint8_t(command & 1)); break;
        case 0x0e00: nb_cmd_0e00(uint8_t(command & 0xff)); break;
        default: break;  // 0x8000/0xff00: attract/POST markers, no-op
    }
}

// ---------------------------------------------------------------------------
// Inputs
// ---------------------------------------------------------------------------

void ArmedfHw::set_inputs(const MachineInputs& inputs) {
    uint16_t v0 = 0xffff;
    if (inputs.player1.up) v0 &= ~0x0001;
    if (inputs.player1.down) v0 &= ~0x0002;
    if (inputs.player1.left) v0 &= ~0x0004;
    if (inputs.player1.right) v0 &= ~0x0008;
    if (inputs.player1.button1) v0 &= ~0x0010;
    if (inputs.player1.button2) v0 &= ~0x0020;
    if (inputs.player1.button3) v0 &= ~0x0040;
    if (inputs.player1.start) v0 &= ~0x0100;
    if (inputs.player2.start) v0 &= ~0x0200;
    if (inputs.coin1) v0 &= ~0x0400;
    if (inputs.coin2) v0 &= ~0x0800;
    in0_ = v0;

    uint16_t v1 = 0xffff;
    if (inputs.player2.up) v1 &= ~0x0001;
    if (inputs.player2.down) v1 &= ~0x0002;
    if (inputs.player2.left) v1 &= ~0x0004;
    if (inputs.player2.right) v1 &= ~0x0008;
    if (inputs.player2.button1) v1 &= ~0x0010;
    if (inputs.player2.button2) v1 &= ~0x0020;
    if (inputs.player2.button3) v1 &= ~0x0040;
    in1_ = v1;
}

void ArmedfHw::set_dip_switch(int bank, uint8_t value) {
    if (bank == 0) dsw_a_ = value;
    if (bank == 1) dsw_b_ = value;
}

// ---------------------------------------------------------------------------
// Video
// ---------------------------------------------------------------------------

int ArmedfHw::text_pos(int x, int y) const {
    switch (game_) {
        case Game::ArmedF: return x * 32 + y;
        case Game::Legion: return (x & 0x1f) * 32 + y + 0x800 * (x / 32);
        default: return 32 * (31 - y) + (x & 0x1f) + 0x800 * (x / 32);  // Terra Force / Crazy Climber 2
    }
}

void ArmedfHw::draw_tile_layer(const std::array<uint16_t, 0x800>& ram, const GfxSet& gfx, int color_base,
                               int code_mask, std::vector<uint32_t>& canvas) {
    for (int f = 0; f < 0x800; f++) {
        const int x = f / 32, y = f % 32;
        const uint16_t attr = ram[size_t(f)];
        const int color = (attr >> 11) & 0x1f;
        const int nchar = attr & code_mask;
        const uint8_t* px = gfx.element(nchar);
        const int base_x = x * 16, base_y = y * 16;
        for (int py = 0; py < 16; py++)
            for (int pxi = 0; pxi < 16; pxi++) {
                const uint8_t p = px[py * 16 + pxi];
                if (p == 15) continue;  // transparent
                canvas[size_t(base_y + py) * 1024 + size_t(base_x + pxi)] =
                    palette_[size_t((color << 4) + color_base + p) % palette_.size()];
            }
    }
}

void ArmedfHw::draw_text_layer() {
    const bool is_armedf = (game_ == Game::ArmedF);
    const int attr_base = is_armedf ? 0x800 : 0x400;
    // MAME armedf_v.cpp VIDEO_START(terraf): tx_tilemap->set_scrollx(0, -128).
    // composite_[x] is screen x, so tilemap 0 lands at screen x = 128.
    const int h_scroll = is_armedf ? 0 : 128;
    for (int f = 0; f <= 0x7ff; f++) {
        const int x = f / 32, y = f % 32;
        const int pos = text_pos(x, y);
        if (pos < 0 || pos >= 0x1000) continue;
        uint8_t attr;
        int nchar;
        int color;
        if (!is_armedf && pos < 0x12) {
            attr = 0;
            nchar = 0;
            color = 0;
        } else {
            attr = ram_txt_[size_t(attr_base + pos) & 0xfff];
            nchar = ram_txt_[size_t(pos) & 0xfff] | ((attr & 3) << 8);
            color = (attr >> 4) & 0xf;
        }
        const uint8_t* px = chars_gfx_.element(nchar & 0x3ff);
        const int base_x = ((x * 8) + h_scroll) & 511, base_y = y * 8;
        // attr bit 3: category 1 is drawn under BG/FG; category 0 over them
        // (MAME: bit 3 clear → NB1414M4 text has priority over the other layers).
        const bool under = (attr & 8) != 0;
        for (int py = 0; py < 8; py++) {
            const int dy = base_y + py;
            if (dy < 0 || dy >= 256) continue;
            for (int pxi = 0; pxi < 8; pxi++) {
                const int dx = base_x + pxi;
                if (dx < 0 || dx >= 512) continue;
                const uint8_t p = px[py * 8 + pxi];
                if (p == 15) continue;
                const uint32_t opaque = palette_[size_t((color << 4) + p) % palette_.size()];
                if (under)
                    composite_[size_t(dy) * 512 + size_t(dx)] = opaque;
                else
                    fg_text_[size_t(dy) * 512 + size_t(dx)] = opaque;
            }
        }
    }
}

void ArmedfHw::draw_sprites(int priority) {
    // MAME walks spriteram backwards so later entries lose to earlier ones.
    for (int f = sprite_count_ - 1; f >= 0; f--) {
        const size_t base = size_t(f) * 4;
        const uint16_t w0 = sprite_buffer_[base + 0];
        if (int((w0 >> 12) & 3) != priority) continue;
        const uint16_t w1 = sprite_buffer_[base + 1];
        const int nchar = w1 & 0xfff;
        const bool flip_x = (w1 & 0x2000) != 0;
        const bool flip_y = (w1 & 0x1000) != 0;
        const uint16_t atrib = sprite_buffer_[base + 2];
        const int color = (atrib >> 8) & 0x1f;
        const int clut = atrib & 0x7f;
        // MAME armedf_v.cpp draw_sprites: sx is the raw spriteram word (no 0x1ff mask).
        const int sx = int(sprite_buffer_[base + 3]);
        const int sy = sprite_offset_ + 240 - int(w0 & 0x1ff);

        const uint8_t* px = sprites_gfx_.element(nchar);
        for (int py = 0; py < 16; py++) {
            const int dy = sy + (flip_y ? 15 - py : py);
            if (dy < 0 || dy >= 512) continue;
            for (int pxi = 0; pxi < 16; pxi++) {
                const int dx = sx + (flip_x ? 15 - pxi : pxi);
                if (dx < 0 || dx >= 512) continue;
                const uint8_t raw = px[py * 16 + pxi];
                const uint8_t idx = uint8_t(ram_clut_[size_t(clut) * 16 + raw] & 0xf);
                if (idx == 15) continue;  // transparent sprite pixel: colorkey, leave underlying layer
                composite_[size_t(dy) * 512 + size_t(dx)] =
                    palette_[size_t(0x200 + (color << 4) + idx) % palette_.size()];
            }
        }
    }
}

void ArmedfHw::render_frame() {
    // FBNeo (scroll_type 5, original terraf) re-reads FG scroll from the NB1414M4
    // parameter block every frame. MAME only latches on the $7c000 bit14 0→1
    // edge; the game updates $6800b..e continuously during the attract cinema.
    if (uses_nb1414()) {
        scroll_fg_x_ = uint16_t(ram_txt_[0xd] | (ram_txt_[0xe] << 8));
        scroll_fg_y_ = uint16_t(ram_txt_[0xb] | (ram_txt_[0xc] << 8));
    }

    std::fill(bg_canvas_.begin(), bg_canvas_.end(), 0u);
    std::fill(fg_canvas_.begin(), fg_canvas_.end(), 0u);
    if (fg_text_.size() != 512u * 256u) fg_text_.assign(512u * 256u, 0u);
    else std::fill(fg_text_.begin(), fg_text_.end(), 0u);
    std::fill(composite_.begin(), composite_.end(), 0xff000000u);

    draw_tile_layer(ram_bg_, bg_gfx_, 0x600, 0x3ff, bg_canvas_);
    draw_tile_layer(ram_fg_, fg_gfx_, 0x400, 0x7ff, fg_canvas_);

    // MAME screen_update: tx cat1, bg, fg, tx cat0, then sprites with pmask.
    const bool text_enabled = (video_reg_ & 0x100) != 0;
    if (text_enabled) draw_text_layer();

    auto blit_scrolled = [&](const std::vector<uint32_t>& src, int sx, int sy) {
        for (int y = 0; y < 256; y++)
            for (int x = 0; x < 512; x++) {
                const uint32_t c = src[size_t((y + sy) & 511) * 1024 + size_t((x + sx) & 1023)];
                if (c != 0) composite_[size_t(y) * 512 + size_t(x)] = c;
            }
    };

    if ((video_reg_ & 0x800) != 0) blit_scrolled(bg_canvas_, scroll_bg_x_, scroll_bg_y_);
    if ((video_reg_ & 0x200) != 0) draw_sprites(2);
    if ((video_reg_ & 0x400) != 0) blit_scrolled(fg_canvas_, scroll_fg_x_ & 0x3ff, scroll_fg_y_ & 0x3ff);
    if ((video_reg_ & 0x200) != 0) draw_sprites(1);
    if (text_enabled)
        for (size_t i = 0; i < fg_text_.size(); i++)
            if (fg_text_[i] != 0) composite_[i] = fg_text_[i];
    if ((video_reg_ & 0x200) != 0) draw_sprites(0);

    const int ox = 96 + crop_x_, oy = 8 + crop_y_;
    if (rotated_) {
        // rot270_screen: rotate the cropped region 270° for the display.
        for (int C = 0; C < region_h_; C++) {
            const uint32_t* src = &composite_[size_t(oy + C) * 512 + size_t(ox)];
            for (int R = 0; R < region_w_; R++)
                framebuffer_[size_t(R) * size_t(region_h_) + size_t(C)] = src[(region_w_ - 1) - R];
        }
    } else {
        for (int y = 0; y < screen_height_; y++) {
            const uint32_t* src = &composite_[size_t(y + oy) * 512 + size_t(ox)];
            std::copy(src, src + screen_width_, &framebuffer_[size_t(y) * size_t(screen_width_)]);
        }
    }

    frame_counter_++;
    std::copy(ram_sprites_.begin(), ram_sprites_.end(), sprite_buffer_.begin());
}

void ArmedfHw::run_frame() {
    for (int line = 0; line < kScanlines; line++) {
        if (line == 248) {
            main_cpu_.set_irq(irq_level_, IrqLine::Assert);
            render_frame();
        }
        main_cpu_.run(main_cycles_per_line_);
        sound_cpu_.run(main_cycles_per_line_ / 2);
    }
}

}  // namespace dsp