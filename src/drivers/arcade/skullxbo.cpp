#include "drivers/arcade/skullxbo.h"

#include <algorithm>
#include <set>

#include "core/rom_loader.h"

namespace dsp {
namespace {

// 68000 program, loaded as byte pairs: the even offsets hold the high bytes.
const std::vector<RomEntry> kMainRoms = {
    {"136072-5150.228a", 0x10000, 0x000000, 0x9546d88b},
    {"136072-5151.228c", 0x10000, 0x000001, 0xb9ed8bd4},
    {"136072-5152.213a", 0x10000, 0x020000, 0xc07e44fc},
    {"136072-5153.213c", 0x10000, 0x020001, 0xfef8297f},
    {"136072-1154.200a", 0x10000, 0x040000, 0xde4101a3},
    {"136072-1155.200c", 0x10000, 0x040001, 0x78c0f6ad},
    {"136072-1156.185a", 0x08000, 0x070000, 0xcde16b55},
    {"136072-1157.185c", 0x08000, 0x070001, 0x31c77376},
};

const std::vector<RomEntry> kSoundRoms = {
    {"136072-1149.1b", 0x10000, 0x00000, 0x8d730e7a},
};

const std::vector<RomEntry> kSpriteRoms = {
    {"136072-1102.13r", 0x10000, 0x000000, 0x90becdfa},
    {"136072-1104.28r", 0x10000, 0x010000, 0x33609071},
    {"136072-1106.41r", 0x10000, 0x020000, 0x71962e9f},
    {"136072-1101.13p", 0x10000, 0x030000, 0x4d41701e},
    {"136072-1103.28p", 0x10000, 0x040000, 0x3011da3b},
    {"136072-1108.53r", 0x10000, 0x050000, 0x386c7edc},
    {"136072-1110.67r", 0x10000, 0x060000, 0xa54d16e6},
    {"136072-1112.81r", 0x10000, 0x070000, 0x669411f6},
    {"136072-1107.53p", 0x10000, 0x080000, 0xcaaeb57a},
    {"136072-1109.67p", 0x10000, 0x090000, 0x61cb4e28},
    {"136072-1114.95r", 0x10000, 0x0a0000, 0xe340d5a1},
    {"136072-1116.109r", 0x10000, 0x0b0000, 0xf25b8aca},
    {"136072-1118.123r", 0x10000, 0x0c0000, 0x8cf73585},
    {"136072-1113.95p", 0x10000, 0x0d0000, 0x899b59af},
    {"136072-1115.109p", 0x10000, 0x0e0000, 0xcf4fd19a},
    {"136072-1120.137r", 0x10000, 0x0f0000, 0xfde7c03d},
    {"136072-1122.151r", 0x10000, 0x100000, 0x6ff6a9f2},
    {"136072-1124.165r", 0x10000, 0x110000, 0xf11909f1},
    {"136072-1119.137p", 0x10000, 0x120000, 0x6f8003a1},
    {"136072-1121.151p", 0x10000, 0x130000, 0x8ff0a1ec},
    {"136072-1125.123n", 0x10000, 0x140000, 0x3aa7c756},
    {"136072-1126.137n", 0x10000, 0x150000, 0xcb82c9aa},
    {"136072-1128.151n", 0x10000, 0x160000, 0xdce32863},
    // 0x170000-0x18ffff is empty on the board
};

const std::vector<RomEntry> kPlayfieldRoms = {
    {"136072-2129.180p", 0x10000, 0x000000, 0x36b1a578},
    {"136072-2131.193p", 0x10000, 0x010000, 0x7b7c04a1},
    {"136072-2133.208p", 0x10000, 0x020000, 0xe03fe4d9},
    {"136072-2135.221p", 0x10000, 0x030000, 0x7d497110},
    {"136072-2137.235p", 0x10000, 0x040000, 0xf91e7872},
    {"136072-2130.180r", 0x10000, 0x050000, 0xb25368cc},
    {"136072-2132.193r", 0x10000, 0x060000, 0x112f2d20},
    {"136072-2134.208r", 0x10000, 0x070000, 0x84884ed6},
    {"136072-2136.221r", 0x10000, 0x080000, 0xbc028690},
    {"136072-2138.235r", 0x10000, 0x090000, 0x60cec955},
};

const std::vector<RomEntry> kCharRoms = {
    {"136072-2141.250k", 0x8000, 0x0000, 0x60d6d6df},
};

const std::vector<RomEntry> kOkiRoms = {
    {"136072-1145.7k", 0x10000, 0x00000, 0xd9475d58},
    {"136072-1146.7j", 0x10000, 0x10000, 0x133e6aef},
    {"136072-1147.7e", 0x10000, 0x20000, 0xba4d556e},
    {"136072-1148.7d", 0x10000, 0x30000, 0xc48df49a},
};

constexpr int kSpriteRegion = 0x190000;
constexpr int kPlayfieldRegion = 0x0a0000;

// molayout: 16x8, five planes spread over the fifths of the sprite region.
GfxLayout sprite_layout() {
    const int unit = (kSpriteRegion / 5) * 8;  // one fifth, in bits
    GfxLayout layout;
    layout.width = 16;
    layout.height = 8;
    layout.total = (kSpriteRegion / 5) / 16;
    layout.planes = 5;
    layout.char_increment = 16 * 8;
    layout.plane_offsets = {4 * unit, 3 * unit, 2 * unit, 1 * unit, 0};
    layout.x_offsets = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
    layout.y_offsets = {0 * 8, 2 * 8, 4 * 8, 6 * 8, 8 * 8, 10 * 8, 12 * 8, 14 * 8};
    return layout;
}

// pflayout: 16x8, four planes packed as nibbles, the halves of the region hold
// the even and odd pixel pairs.
GfxLayout playfield_layout() {
    const int half = (kPlayfieldRegion / 2) * 8;  // in bits
    GfxLayout layout;
    layout.width = 16;
    layout.height = 8;
    layout.total = (kPlayfieldRegion / 2) / 16;
    layout.planes = 4;
    layout.char_increment = 16 * 8;
    layout.plane_offsets = {0, 1, 2, 3};
    layout.x_offsets = {half + 0,  half + 0,  half + 4,  half + 4,  0,         0,
                        4,         4,         half + 8,  half + 8,  half + 12, half + 12,
                        8,         8,         12,        12};
    layout.y_offsets = {0 * 8, 2 * 8, 4 * 8, 6 * 8, 8 * 8, 10 * 8, 12 * 8, 14 * 8};
    return layout;
}

// anlayout: 16x8, two planes, every pixel doubled horizontally.
GfxLayout char_layout() {
    GfxLayout layout;
    layout.width = 16;
    layout.height = 8;
    layout.total = 0x8000 / 16;
    layout.planes = 2;
    layout.char_increment = 8 * 16;
    layout.plane_offsets = {0, 1};
    layout.x_offsets = {0, 0, 2, 2, 4, 4, 6, 6, 8, 8, 10, 10, 12, 12, 14, 14};
    layout.y_offsets = {0 * 16, 1 * 16, 2 * 16, 3 * 16, 4 * 16, 5 * 16, 6 * 16, 7 * 16};
    return layout;
}

AtariMotionObjects::Config motion_object_config() {
    AtariMotionObjects::Config config;
    config.tile_width = 16;
    config.tile_height = 8;
    config.bankcount = 2;
    config.linked = true;
    config.split = false;
    config.slipheight = 8;
    config.maxperline = 0;
    config.palettebase = 0x000;
    config.link_entry = {0x00ff, 0, 0, 0};
    config.code_entry = {{0, 0x7fff, 0, 0}, {0, 0, 0, 0}};
    config.color_entry = {{0, 0, 0x000f, 0}, {0, 0, 0, 0}};
    config.xpos_entry = {0, 0, 0xffc0, 0};
    config.ypos_entry = {0, 0, 0, 0xff80};
    config.width_entry = {0, 0, 0, 0x0070};
    config.height_entry = {0, 0, 0, 0x000f};
    config.hflip_entry = {0, 0x8000, 0, 0};
    config.priority_entry = {0, 0, 0x0030, 0};
    return config;
}

uint8_t pal6bit(uint8_t bits) {
    bits = uint8_t(bits & 0x3f);
    return uint8_t((bits << 2) | (bits >> 4));
}

}  // namespace

Skullxbo::Skullxbo()
    : main_cpu_(kMainClock),
      sound_cpu_(kSoundClock),
      ym_(kJsaClock),
      oki_(kOkiClock, true),
      rom_(0x40000, 0),
      sound_rom_(0x10000, 0) {
    framebuffer_.assign(size_t(kScreenWidth) * kScreenHeight, 0xff000000u);
    mo_index_.assign(size_t(kScreenWidth) * kScreenHeight, kMoTransparent);
    mo_priority_.assign(size_t(kScreenWidth) * kScreenHeight, 0);

    main_cpu_.set_memory_handlers(
        [this](uint32_t address) { return main_read(address); },
        [this](uint32_t address, uint16_t value) { main_write(address, value); });
    sound_cpu_.set_memory_handlers(
        [this](uint16_t address) { return sound_read(address); },
        [this](uint16_t address, uint8_t value) { sound_write(address, value); });
    sound_cpu_.set_cycle_handler([this](int cycles) { on_sound_cycles(cycles); });

    ym_.set_irq_handler([this](bool state) {
        ym_int_ = state;
        update_sound_irq();
    });
    ym_.set_port_handler([this](uint8_t data) {
        ym_ct1_ = (data & 1) != 0 ? 1.0f : 0.0f;
    });

    motion_objects_ = std::make_unique<AtariMotionObjects>(
        motion_object_config(), &alpha_ram_[0x7c0], &mob_ram_[0], kScreenWidth + 16,
        kScreenHeight + 8);
    // The sprites are five bits deep, so every colour selects 32 palette entries
    // instead of the 16 the shared component assumes.
    std::vector<uint16_t>& colors = motion_objects_->color_lookup();
    for (size_t index = 0; index < colors.size(); index++) colors[index] = uint16_t(index * 2);
}

bool Skullxbo::init(const std::string& rom_path, std::string* error) {
    if (!load_roms(rom_path, error)) return false;
    eeprom_.fill(0xff);
    reset();
    return true;
}

bool Skullxbo::load_roms(const std::string& rom_path, std::string* error) {
    RomLoader loader;
    if (!loader.open(rom_path, error)) return false;

    std::vector<uint8_t> program(0x80000, 0);
    for (const RomEntry& entry : kMainRoms) {
        std::vector<uint8_t> data(entry.length, 0);
        RomEntry single{entry.name, entry.length, 0, entry.crc};
        if (!loader.load({single}, data, error)) return false;
        for (uint32_t byte = 0; byte < entry.length; byte++) {
            program[(entry.offset & ~1u) + byte * 2 + (entry.offset & 1u)] = data[byte];
        }
    }
    for (uint32_t index = 0; index < 0x80000; index += 2) {
        rom_[index >> 1] = uint16_t((program[index] << 8) | program[index + 1]);
    }

    std::vector<uint8_t> sound(0x10000, 0);
    if (!loader.load(kSoundRoms, sound, error)) return false;
    sound_rom_ = std::move(sound);

    std::vector<uint8_t> sprites(kSpriteRegion, 0);
    if (!loader.load(kSpriteRoms, sprites, error)) return false;
    sprites_.decode(sprite_layout(), sprites);

    std::vector<uint8_t> playfield(kPlayfieldRegion, 0);
    if (!loader.load(kPlayfieldRoms, playfield, error)) return false;
    for (uint8_t& byte : playfield) byte = uint8_t(~byte);  // ROMREGION_INVERT
    playfield_gfx_.decode(playfield_layout(), playfield);

    std::vector<uint8_t> chars(0x8000, 0);
    if (!loader.load(kCharRoms, chars, error)) return false;
    chars_.decode(char_layout(), chars);

    std::vector<uint8_t> samples(0x40000, 0);
    if (!loader.load(kOkiRoms, samples, error)) return false;
    oki_.set_rom(std::move(samples));

    warnings_ = loader.warnings();
    return true;
}

void Skullxbo::reset() {
    main_cpu_.reset();
    sound_cpu_.reset();
    ym_.reset();
    oki_.reset();
    oki_.set_pin7(true);

    playfield_ram_.fill(0);
    playfield_ext_.fill(0);
    alpha_ram_.fill(0);
    mob_ram_.fill(0);
    ram_.fill(0);
    sound_ram_.fill(0);
    palette_ram_.fill(0);
    palette_.fill(0xff000000u);
    std::fill(mo_index_.begin(), mo_index_.end(), kMoTransparent);
    std::fill(mo_priority_.begin(), mo_priority_.end(), uint8_t(0));

    scanline_ = 0;
    pf_scrollx_ = pf_scrolly_ = mo_yscroll_ = 0;
    xscroll_reg_ = yscroll_reg_ = 0;
    playfield_latch_ = -1;
    eeprom_unlocked_ = false;
    halt_until_hblank_ = false;
    pending_scanline_irq_ = -1;
    motion_objects_->set_bank(0);

    sound_bank_ = 0;
    sound_to_main_ready_ = main_to_sound_ready_ = false;
    sound_to_main_data_ = main_to_sound_data_ = 0;
    timed_int_ = ym_int_ = false;
    ym_gain_ = 1.0f;
    oki_gain_ = 1.0f;
    ym_ct1_ = 0.0f;
    sound_irq_accumulator_ = 0;

    in0_ = 0xffff;
    in1_ = uint16_t(0xffff & ~0x0030);  // HBLANK/VBLANK are active high
    coin1_ = coin2_ = false;

    audio_accumulator_ = oki_accumulator_ = 0;
    last_oki_ = 0;
    audio_.clear();
}

int Skullxbo::debug_palette_used() const {
    std::set<uint16_t> used(palette_ram_.begin(), palette_ram_.end());
    return int(used.size());
}

int Skullxbo::debug_motion_object_pixels() const {
    return int(std::count_if(mo_index_.begin(), mo_index_.end(),
                             [](uint16_t value) { return value != kMoTransparent; }));
}

uint16_t Skullxbo::main_read(uint32_t address) {
    if (address < 0x80000) return rom_[address >> 1];
    if (address >= 0xff2000 && address <= 0xff2fff) return palette_ram_[(address & 0xfff) >> 1];
    if (address >= 0xff5000 && address <= 0xff5001) {
        return uint16_t(0xff00 | read_sound_response());
    }
    if (address >= 0xff5800 && address <= 0xff5803) {
        if ((address & 2) == 0) return in0_;
        uint16_t value = in1_;
        if (scanline_ >= kScreenHeight) value = uint16_t(value | 0x0020);
        if (main_to_sound_ready_) value = uint16_t(value & ~0x0040);
        return value;
    }
    if (address >= 0xff6000 && address <= 0xff6fff) {
        return uint16_t(0xff00 | eeprom_[(address & 0xfff) >> 1]);
    }
    if (address >= 0xff8000 && address <= 0xff9fff) return playfield_ram_[(address & 0x1fff) >> 1];
    if (address >= 0xffa000 && address <= 0xffbfff) return playfield_ext_[(address & 0x1fff) >> 1];
    if (address >= 0xffc000 && address <= 0xffcfff) return alpha_ram_[(address & 0xfff) >> 1];
    if (address >= 0xffd000 && address <= 0xffdfff) return mob_ram_[(address & 0xfff) >> 1];
    if (address >= 0xffe000) return ram_[(address & 0x1fff) >> 1];
    return 0xffff;
}

void Skullxbo::main_write(uint32_t address, uint16_t value) {
    if (address < 0x80000) return;  // ROM
    if (address >= 0xff0000 && address <= 0xff07ff) {
        motion_objects_->set_bank((address >> 10) & 1);
        return;
    }
    if (address >= 0xff0800 && address <= 0xff0bff) {
        halt_until_hblank_ = true;
        return;
    }
    if (address >= 0xff0c00 && address <= 0xff0fff) {
        eeprom_unlocked_ = true;
        return;
    }
    if (address >= 0xff1000 && address <= 0xff13ff) {
        main_cpu_.set_irq(2, IrqLine::Clear);
        return;
    }
    if (address >= 0xff1400 && address <= 0xff17ff) {
        write_sound_command(uint8_t(value));
        return;
    }
    if (address >= 0xff1800 && address <= 0xff1bff) {
        set_sound_reset();
        return;
    }
    if (address >= 0xff1c00 && address <= 0xff1fff) {
        // The 0xff1e00 block mirrors the 0xff1c00 one.
        switch ((address >> 7) & 3) {
            case 0: playfield_latch_ = int(value); break;
            case 1: write_xscroll(value); break;
            case 2: main_cpu_.set_irq(1, IrqLine::Clear); break;
            default: break;  // watchdog
        }
        return;
    }
    if (address >= 0xff2000 && address <= 0xff2fff) {
        set_palette(int((address & 0xfff) >> 1), value);
        return;
    }
    if (address >= 0xff4000 && address <= 0xff47ff) {
        write_yscroll(value);
        return;
    }
    if (address >= 0xff4800 && address <= 0xff4fff) return;  // unknown MOBWR port
    if (address >= 0xff6000 && address <= 0xff6fff) {
        if (eeprom_unlocked_) {
            eeprom_[(address & 0xfff) >> 1] = uint8_t(value);
            eeprom_unlocked_ = false;
        }
        return;
    }
    if (address >= 0xff8000 && address <= 0xff9fff) {
        const size_t offset = (address & 0x1fff) >> 1;
        playfield_ram_[offset] = value;
        if (playfield_latch_ != -1) {
            playfield_ext_[offset] =
                uint16_t((playfield_ext_[offset] & 0xff00) | (uint16_t(playfield_latch_) & 0xff));
        }
        return;
    }
    if (address >= 0xffa000 && address <= 0xffbfff) {
        playfield_ext_[(address & 0x1fff) >> 1] = value;
        return;
    }
    if (address >= 0xffc000 && address <= 0xffcfff) {
        alpha_ram_[(address & 0xfff) >> 1] = value;
        return;
    }
    if (address >= 0xffd000 && address <= 0xffdfff) {
        mob_ram_[(address & 0xfff) >> 1] = value;
        return;
    }
    if (address >= 0xffe000) ram_[(address & 0x1fff) >> 1] = value;
}

void Skullxbo::set_palette(int index, uint16_t value) {
    palette_ram_[size_t(index)] = value;
    const uint8_t intensity = uint8_t((value >> 15) & 1);
    const uint8_t red = pal6bit(uint8_t(((value >> 9) & 0x3e) | intensity));
    const uint8_t green = pal6bit(uint8_t(((value >> 4) & 0x3e) | intensity));
    const uint8_t blue = pal6bit(uint8_t(((value << 1) & 0x3e) | intensity));
    palette_[size_t(index)] =
        0xff000000u | (uint32_t(red) << 16) | (uint32_t(green) << 8) | uint32_t(blue);
}

void Skullxbo::write_xscroll(uint16_t value) {
    xscroll_reg_ = value;
    pf_scrollx_ = 2 * (value >> 7);
}

void Skullxbo::write_yscroll(uint16_t value) {
    int scanline = scanline_;
    yscroll_reg_ = value;
    if (scanline >= kScreenHeight) scanline = 0;
    // A new vertical scroll latches the offset into a counter that counts up
    // with the scanlines, so it has to be corrected for the current line.
    set_yscroll(int(value >> 7) - scanline);
}

void Skullxbo::set_yscroll(int value) {
    pf_scrolly_ = value & 0x1ff;
    mo_yscroll_ = value & 0x1ff;
}

void Skullxbo::scanline_update(int scanline) {
    int offset = (scanline / 8) * 64 + 42;
    if (offset >= 0x7c0) return;

    // Scanline 0 re-latches the raw scroll register.
    if (scanline == 0) set_yscroll(int(yscroll_reg_ >> 7) & 0x1ff);

    for (int x = 42; x < 64; x++) {
        const uint16_t data = alpha_ram_[size_t(offset++)];
        if ((data & 0x000f) != 0x0d) continue;  // the only command the game uses
        set_yscroll(int(data >> 7) - scanline);
        yscroll_reg_ = data;
    }
}

void Skullxbo::draw_motion_object_band(int line) {
    const int clip_bottom = std::min(line + 7, kScreenHeight - 1);
    for (int y = line; y <= clip_bottom; y++) {
        const size_t base = size_t(y) * kScreenWidth;
        std::fill(mo_index_.begin() + base, mo_index_.begin() + base + kScreenWidth,
                  kMoTransparent);
    }

    const int band = motion_objects_->band_for_line(line, mo_yscroll_);
    motion_objects_->draw_band(
        band, pf_scrollx_, mo_yscroll_, -1,
        [&](int code, int color, bool hflip, bool vflip, int x, int y, int, int priority) {
            int sx = x;
            if (sx >= kScreenWidth) sx -= kMoPlaneWidth;
            int sy = y;
            if (sy >= kScreenHeight) sy -= kMoPlaneHeight;
            const uint8_t* pixels = sprites_.element(code);
            for (int row = 0; row < 8; row++) {
                const int target_y = sy + row;
                if (target_y < line || target_y > clip_bottom) continue;
                const int source_row = vflip ? (7 - row) : row;
                for (int column = 0; column < 16; column++) {
                    const int target_x = sx + column;
                    if (target_x < 0 || target_x >= kScreenWidth) continue;
                    const int source_column = hflip ? (15 - column) : column;
                    const uint8_t pen = pixels[source_row * 16 + source_column];
                    if (pen == 0) continue;  // transparent pen
                    const size_t offset = size_t(target_y) * kScreenWidth + size_t(target_x);
                    mo_index_[offset] = uint16_t(color + pen);
                    mo_priority_[offset] = uint8_t(priority);
                }
            }
        });
}

void Skullxbo::render_line(int line) {
    const int source_y = (line + pf_scrolly_) & 0x1ff;
    const int playfield_row = source_y >> 3;
    const int playfield_pixel_row = source_y & 7;
    const int alpha_row = (line >> 3) * 64;
    const int alpha_pixel_row = line & 7;

    uint32_t* target = &framebuffer_[size_t(line) * kScreenWidth];
    for (int x = 0; x < kScreenWidth; x++) {
        // Playfield: 64x64 tilemap of 16x8 tiles, scanned in columns.
        const int source_x = (x + pf_scrollx_) & 0x3ff;
        const size_t tile = size_t((source_x >> 4) * 64 + playfield_row);
        const uint16_t data = playfield_ram_[tile];
        const int code = data & 0x7fff;
        const bool flipx = (data & 0x8000) != 0;
        const int color = playfield_ext_[tile] & 0x0f;
        int column = source_x & 15;
        if (flipx) column = 15 - column;
        const uint8_t pen = playfield_gfx_.element(code)[playfield_pixel_row * 16 + column];
        int index = 0x200 + color * 16 + pen;

        // Merge the motion objects with the Atari priority equations, taken from
        // the GALs on the real board (see the MAME driver).
        const size_t offset = size_t(line) * kScreenWidth + size_t(x);
        const uint16_t object = mo_index_[offset];
        if (object != kMoTransparent) {
            const int mopriority = mo_priority_[offset];
            const int mopix = object & 0x1f;
            const int pfcolor = (index >> 4) & 0x0f;
            const int pfpix = index & 0x0f;
            const bool o17 = (index & 0xc8) == 0xc8;

            if ((mopriority == 0 && !o17 && mopix >= 2) ||
                (mopriority == 1 && mopix >= 2 && !(pfcolor & 0x08)) ||
                ((mopriority & 2) && mopix >= 2 && !(pfcolor & 0x0c)) ||
                (!(pfpix & 8) && mopix >= 2)) {
                index = object;
            } else if ((mopriority == 0 && !o17 && mopix == 1) ||
                       (mopriority == 1 && mopix == 1 && !(pfcolor & 0x08)) ||
                       ((mopriority & 2) && mopix == 1 && !(pfcolor & 0x0c)) ||
                       (!(pfpix & 8) && mopix == 1)) {
                index |= 0x400;  // shadow
            }
        }

        // The alpha layer goes on top; pen 0 is transparent unless the tile is
        // flagged as opaque.
        const uint16_t alpha = alpha_ram_[size_t(alpha_row + (x >> 4))];
        const int alpha_code = (alpha ^ 0x400) & 0x7ff;
        const uint8_t alpha_pen =
            chars_.element(alpha_code)[alpha_pixel_row * 16 + (x & 15)];
        if (alpha_pen != 0 || (alpha & 0x8000) != 0) {
            index = 0x300 + ((alpha >> 11) & 0x0f) * 4 + alpha_pen;
        }

        target[x] = palette_[size_t(index) & 0x7ff];
    }
}

void Skullxbo::run_frame() {
    const int main_cycles =
        int(double(kMainClock) / kFramesPerSecond / (kScanlines * kCpuSync) + 0.5);
    const int sound_cycles =
        int(double(kSoundClock) / kFramesPerSecond / (kScanlines * kCpuSync) + 0.5);

    for (int line = 0; line < kScanlines; line++) {
        scanline_ = line;
        if ((line & 7) == 0) {
            // A scanline interrupt is requested from the alpha RAM; it happens on
            // the HBLANK of the sixth scanline that follows.
            const int offset = (line / 8) * 64 + 42;
            if (offset < 0x7c0 && (alpha_ram_[size_t(offset)] & 0x8000) != 0) {
                pending_scanline_irq_ = line + 6;
            }
            scanline_update(line);
        }
        if (line == pending_scanline_irq_) {
            main_cpu_.set_irq(1, IrqLine::Assert);
            pending_scanline_irq_ = -1;
        }
        if (line == kScreenHeight) main_cpu_.set_irq(2, IrqLine::Assert);

        halt_until_hblank_ = false;
        for (int step = 0; step < kCpuSync; step++) {
            // The last quarter of the line stands in for the HBLANK window that
            // the program waits on.
            if (step == kCpuSync - 1) in1_ = uint16_t(in1_ | 0x0010);
            else in1_ = uint16_t(in1_ & ~0x0010);
            if (!halt_until_hblank_ || step == kCpuSync - 1) main_cpu_.run(main_cycles);
            sound_cpu_.run(sound_cycles);
        }
        in1_ = uint16_t(in1_ & ~0x0010);

        if (line < kScreenHeight) {
            if ((line & 7) == 0) draw_motion_object_band(line);
            render_line(line);
        }
    }
}

void Skullxbo::write_sound_command(uint8_t value) {
    main_to_sound_data_ = value;
    main_to_sound_ready_ = true;
    sound_cpu_.set_nmi(IrqLine::Assert);
}

uint8_t Skullxbo::read_sound_command() {
    main_to_sound_ready_ = false;
    sound_cpu_.set_nmi(IrqLine::Clear);
    return main_to_sound_data_;
}

void Skullxbo::write_sound_response(uint8_t value) {
    sound_to_main_data_ = value;
    sound_to_main_ready_ = true;
    main_cpu_.set_irq(4, IrqLine::Assert);
}

uint8_t Skullxbo::read_sound_response() {
    sound_to_main_ready_ = false;
    main_cpu_.set_irq(4, IrqLine::Clear);
    return sound_to_main_data_;
}

void Skullxbo::update_sound_irq() {
    sound_cpu_.set_irq(timed_int_ || ym_int_ ? IrqLine::Assert : IrqLine::Clear);
}

void Skullxbo::set_sound_reset() {
    sound_cpu_.reset();
    ym_.reset();
    oki_.reset();
    sound_bank_ = 0;
    sound_to_main_ready_ = false;
    main_cpu_.set_irq(4, IrqLine::Clear);
    timed_int_ = ym_int_ = false;
    ym_gain_ = 1.0f;
    oki_gain_ = 1.0f;
    ym_ct1_ = 0.0f;
    update_sound_irq();
}

uint8_t Skullxbo::sound_read(uint16_t address) {
    if (address < 0x2000) return sound_ram_[address & 0x1fff];
    if (address < 0x2800) return ym_.status();
    if (address < 0x2a00) {
        switch (address & 0x0006) {
            case 0x0000: return oki_.read();
            case 0x0002: return read_sound_command();
            case 0x0004: {
                uint8_t result = 0x1c;  // three pulled up inputs
                if (coin1_) result = uint8_t(result | 0x01);
                if (coin2_) result = uint8_t(result | 0x02);
                if (sound_to_main_ready_) result = uint8_t(result | 0x20);
                if (!main_to_sound_ready_) result = uint8_t(result | 0x40);
                if (!service_) result = uint8_t(result | 0x80);
                return result;
            }
            default:
                timed_int_ = false;
                update_sound_irq();
                return 0;
        }
    }
    if (address < 0x3000) return 0xff;
    if (address < 0x4000) {
        return sound_rom_[size_t(sound_bank_) * 0x1000 + (address & 0x0fff)];
    }
    return sound_rom_[address];
}

void Skullxbo::sound_write(uint16_t address, uint8_t value) {
    if (address < 0x2000) {
        sound_ram_[address & 0x1fff] = value;
        return;
    }
    if (address < 0x2800) {
        if ((address & 1) == 0) ym_.select_register(value);
        else ym_.write(value);
        return;
    }
    if (address < 0x2a00) return;  // read only block
    if (address < 0x2c00) {
        switch (address & 0x0006) {
            case 0x0000: oki_.write(value); break;
            case 0x0002: write_sound_response(value); break;
            case 0x0004:
                // /WRIO: ROM bank, coin counters, OKI frequency and resets.
                sound_bank_ = uint8_t((value >> 6) & 3);
                oki_.set_pin7((value & 0x08) != 0);
                if ((value & 0x04) == 0) oki_.reset();
                if ((value & 0x01) == 0) ym_.reset();
                break;
            default:
                // /MIX: the YM2151 volume plus the OKI attenuation bit.
                ym_gain_ = float((value >> 1) & 7) / 7.0f;
                oki_gain_ = (value & 1) != 0 ? 1.0f : 0.5f;
                break;
        }
        return;
    }
}

void Skullxbo::on_sound_cycles(int cycles) {
    ym_.run_timers(cycles * 2);  // the YM2151 runs at twice the 6502 clock

    // Periodic sound interrupt: JSA master clock / 4 / 16 / 16 / 14.
    sound_irq_accumulator_ += int64_t(cycles) * (int64_t(kJsaClock) / 4 / 16 / 16);
    const int64_t period = int64_t(kSoundClock) * 14;
    while (sound_irq_accumulator_ >= period) {
        sound_irq_accumulator_ -= period;
        timed_int_ = true;
        update_sound_irq();
    }

    oki_accumulator_ += int64_t(cycles) * oki_.sample_frequency();
    while (oki_accumulator_ >= int64_t(kSoundClock)) {
        oki_accumulator_ -= int64_t(kSoundClock);
        last_oki_ = oki_.update();
    }

    audio_accumulator_ += int64_t(cycles) * YM2151::kSampleRate;
    while (audio_accumulator_ >= int64_t(kSoundClock)) {
        audio_accumulator_ -= int64_t(kSoundClock);
        const int32_t sample = int32_t(float(ym_.update()) * ym_gain_ * 0.6f) +
                               int32_t(float(last_oki_) * oki_gain_ * ym_ct1_ * 0.75f);
        audio_.push_back(int16_t(std::clamp(sample, int32_t(-32768), int32_t(32767))));
    }
}

void Skullxbo::set_inputs(const MachineInputs& inputs) {
    const InputState& player1 = inputs.player1;
    const InputState& player2 = inputs.player2;
    in0_ = 0xffff;
    in1_ = uint16_t(0xffff & ~0x0030);  // HBLANK/VBLANK are active high

    if (player1.button1) in0_ = uint16_t(in0_ & ~0x0100);
    if (player1.button2) in0_ = uint16_t(in0_ & ~0x0200);
    if (player1.right) in0_ = uint16_t(in0_ & ~0x1000);
    if (player1.left) in0_ = uint16_t(in0_ & ~0x2000);
    if (player1.down) in0_ = uint16_t(in0_ & ~0x4000);
    if (player1.up) in0_ = uint16_t(in0_ & ~0x8000);

    if (player2.button1) in1_ = uint16_t(in1_ & ~0x0100);
    if (player2.button2) in1_ = uint16_t(in1_ & ~0x0200);
    if (player2.right) in1_ = uint16_t(in1_ & ~0x1000);
    if (player2.left) in1_ = uint16_t(in1_ & ~0x2000);
    if (player2.down) in1_ = uint16_t(in1_ & ~0x4000);
    if (player2.up) in1_ = uint16_t(in1_ & ~0x8000);
    if (service_) in1_ = uint16_t(in1_ & ~0x0080);

    coin1_ = inputs.coin1;
    coin2_ = inputs.coin2;
}

void Skullxbo::set_dip_switch(int bank, uint8_t value) {
    // The board has no DIP switches: the settings live in the EEPROM and are
    // changed from the self test, which bank 0 bit 0 enables.
    if (bank == 0) service_ = (value & 1) != 0;
}

void Skullxbo::drain_audio(std::vector<int16_t>& out) {
    out.insert(out.end(), audio_.begin(), audio_.end());
    audio_.clear();
}

}  // namespace dsp
