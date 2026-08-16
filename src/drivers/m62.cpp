#include "drivers/m62.h"

#include <algorithm>
#include <cstring>

#include "core/rom_loader.h"

namespace dsp {
namespace {

constexpr uint32_t kTransparent = 0;

const std::vector<RomEntry> kKungFuMain = {
    {"a-4e-c.bin", 0x4000, 0x0000, 0xb6e2d083},
    {"a-4d-c.bin", 0x4000, 0x4000, 0x7532918e},
};
const std::vector<RomEntry> kKungFuPal = {
    {"g-1j-.bin", 0x100, 0x000, 0x668e6bca},
    {"g-1f-.bin", 0x100, 0x100, 0x964b6495},
    {"g-1h-.bin", 0x100, 0x200, 0x550563e1},
    {"b-1m-.bin", 0x100, 0x300, 0x76c05a9c},
    {"b-1n-.bin", 0x100, 0x400, 0x23f06b99},
    {"b-1l-.bin", 0x100, 0x500, 0x35e45021},
    {"b-5f-.bin", 0x020, 0x600, 0x7a601c3d},
};
const std::vector<RomEntry> kKungFuChars = {
    {"g-4c-a.bin", 0x2000, 0x0000, 0x6b2cc9c8},
    {"g-4d-a.bin", 0x2000, 0x2000, 0xc648f558},
    {"g-4e-a.bin", 0x2000, 0x4000, 0xfbe9276e},
};
const std::vector<RomEntry> kKungFuSound = {
    {"a-3e-.bin", 0x2000, 0xa000, 0x58e87ab0},
    {"a-3f-.bin", 0x2000, 0xc000, 0xc81e31ea},
    {"a-3h-.bin", 0x2000, 0xe000, 0xd99fb995},
};
const std::vector<RomEntry> kKungFuSprites = {
    {"b-4k-.bin", 0x2000, 0x00000, 0x16fb5150},
    {"b-4f-.bin", 0x2000, 0x02000, 0x67745a33},
    {"b-4l-.bin", 0x2000, 0x04000, 0xbd1c2261},
    {"b-4h-.bin", 0x2000, 0x06000, 0x8ac5ed3a},
    {"b-3n-.bin", 0x2000, 0x08000, 0x28a213aa},
    {"b-4n-.bin", 0x2000, 0x0a000, 0xd5228df3},
    {"b-4m-.bin", 0x2000, 0x0c000, 0xb16de4f2},
    {"b-3m-.bin", 0x2000, 0x0e000, 0xeba0d66b},
    {"b-4c-.bin", 0x2000, 0x10000, 0x01298885},
    {"b-4e-.bin", 0x2000, 0x12000, 0xc77b87d4},
    {"b-4d-.bin", 0x2000, 0x14000, 0x6a70615f},
    {"b-4a-.bin", 0x2000, 0x16000, 0x6189d626},
};

const std::vector<RomEntry> kSpelunkrMain = {
    {"spra.4e", 0x4000, 0x0000, 0xcf811201},
    {"spra.4d", 0x4000, 0x4000, 0xbb4faa4f},
    {"sprm.7c", 0x4000, 0x8000, 0xfb6197e2},
    {"sprm.7b", 0x4000, 0xc000, 0x26bb25a4},
};
const std::vector<RomEntry> kSpelunkrPal = {
    {"sprm.2k", 0x100, 0x000, 0xfd8fa991},
    {"sprm.2j", 0x100, 0x100, 0x0e3890b4},
    {"sprm.2h", 0x100, 0x200, 0x0478082b},
    {"sprb.1m", 0x100, 0x300, 0x8d8cccad},
    {"sprb.1n", 0x100, 0x400, 0xc40e1cb2},
    {"sprb.1l", 0x100, 0x500, 0x3ec46248},
    {"sprb.5p", 0x020, 0x600, 0x746c6238},
};
const std::vector<RomEntry> kSpelunkrChars = {
    {"sprm.4p", 0x4000, 0x0000, 0x4dfe2e63},
    {"sprm.4l", 0x4000, 0x4000, 0x239f2cd4},
    {"sprm.4m", 0x4000, 0x8000, 0xd6d07d70},
};
const std::vector<RomEntry> kSplSound = {
    {"spra.3d", 0x4000, 0x8000, 0x4110363c},
    {"spra.3f", 0x4000, 0xc000, 0x67a9d2e6},
};
const std::vector<RomEntry> kSpelunkrSprites = {
    {"sprb.4k", 0x4000, 0x00000, 0xe7f0e861},
    {"sprb.4f", 0x4000, 0x04000, 0x32663097},
    {"sprb.3p", 0x4000, 0x08000, 0x8fbaf373},
    {"sprb.4p", 0x4000, 0x0c000, 0x37069b76},
    {"sprb.4c", 0x4000, 0x10000, 0xcfe46a88},
    {"sprb.4e", 0x4000, 0x14000, 0x11c48979},
};
const std::vector<RomEntry> kSpelunkrTiles = {
    {"sprm.1d", 0x4000, 0x00000, 0x4ef7ae89},
    {"sprm.1e", 0x4000, 0x04000, 0xa3755180},
    {"sprm.3c", 0x4000, 0x08000, 0xb4008e6a},
    {"sprm.3b", 0x4000, 0x0c000, 0xf61cf012},
    {"sprm.1c", 0x4000, 0x10000, 0x58b21c76},
    {"sprm.1b", 0x4000, 0x14000, 0xa95cb3e5},
};

const std::vector<RomEntry> kSpelunk2Main = {
    {"sp2-a.4e", 0x4000, 0x00000, 0x96c04bbb},
    {"sp2-a.4d", 0x4000, 0x04000, 0xcb38c2ff},
    {"sp2-r.7d", 0x8000, 0x08000, 0x558837ea},
    {"sp2-r.7c", 0x8000, 0x10000, 0x4b380162},
    {"sp2-r.7b", 0x4000, 0x18000, 0x7709a1fe},
};
const std::vector<RomEntry> kSpelunk2Pal = {
    {"sp2-r.1k", 0x200, 0x000, 0x31c1bcdc},
    {"sp2-r.2k", 0x100, 0x200, 0x1cf5987e},
    {"sp2-r.2j", 0x100, 0x300, 0x1acbe2a5},
    {"sp2-b.1m", 0x100, 0x400, 0x906104c7},
    {"sp2-b.1n", 0x100, 0x500, 0x5a564c06},
    {"sp2-b.1l", 0x100, 0x600, 0x8f4a2e3c},
    {"sp2-b.5p", 0x020, 0x700, 0xcd126f6a},
};
const std::vector<RomEntry> kSpelunk2Chars = {
    {"sp2-r.4l", 0x4000, 0x0000, 0x6a4b2d8b},
    {"sp2-r.4m", 0x4000, 0x4000, 0xe1368b61},
    {"sp2-r.4p", 0x4000, 0x8000, 0xfc138e13},
};
const std::vector<RomEntry> kSpelunk2Sound = {
    {"sp2-a.3d", 0x4000, 0x8000, 0x839ec7e2},
    {"sp2-a.3f", 0x4000, 0xc000, 0xad3ce898},
};
const std::vector<RomEntry> kSpelunk2Sprites = {
    {"sp2-b.4k", 0x4000, 0x00000, 0x6cb67a17},
    {"sp2-b.4f", 0x4000, 0x04000, 0xe4a1166f},
    {"sp2-b.3n", 0x4000, 0x08000, 0xf59e8b76},
    {"sp2-b.4n", 0x4000, 0x0c000, 0xfa65bac9},
    {"sp2-b.4c", 0x4000, 0x10000, 0x1caf7013},
    {"sp2-b.4e", 0x4000, 0x14000, 0x780a463b},
};
const std::vector<RomEntry> kSpelunk2Tiles = {
    {"sp2-r.1d", 0x8000, 0x00000, 0xc19fa4c9},
    {"sp2-r.3b", 0x8000, 0x08000, 0x366604af},
    {"sp2-r.1b", 0x8000, 0x10000, 0x3a0c4d47},
};

const std::vector<RomEntry> kLdrunMain = {
    {"lr-a-4e", 0x2000, 0x0000, 0x5d7e2a4d},
    {"lr-a-4d", 0x2000, 0x2000, 0x96f20473},
    {"lr-a-4b", 0x2000, 0x4000, 0xb041c4a9},
    {"lr-a-4a", 0x2000, 0x6000, 0x645e42aa},
};
const std::vector<RomEntry> kLdrunPal = {
    {"lr-e-3m", 0x100, 0x000, 0x53040416},
    {"lr-e-3l", 0x100, 0x100, 0x67786037},
    {"lr-e-3n", 0x100, 0x200, 0x5b716837},
    {"lr-b-1m", 0x100, 0x300, 0x4bae1c25},
    {"lr-b-1n", 0x100, 0x400, 0x9cd3db94},
    {"lr-b-1l", 0x100, 0x500, 0x08d8cf9a},
    {"lr-b-5p", 0x020, 0x600, 0xe01f69e2},
};
const std::vector<RomEntry> kLdrunChars = {
    {"lr-e-2d", 0x2000, 0x0000, 0x24f9b58d},
    {"lr-e-2j", 0x2000, 0x2000, 0x43175e08},
    {"lr-e-2f", 0x2000, 0x4000, 0xe0317124},
};
const std::vector<RomEntry> kLdrunSound = {
    {"lr-a-3f", 0x2000, 0xc000, 0x7a96accd},
    {"lr-a-3h", 0x2000, 0xe000, 0x3f7f3939},
};
const std::vector<RomEntry> kLdrunSprites = {
    {"lr-b-4k", 0x2000, 0x0000, 0x8141403e},
    {"lr-b-3n", 0x2000, 0x2000, 0x55154154},
    {"lr-b-4c", 0x2000, 0x4000, 0x924e34d0},
};

const std::vector<RomEntry> kLdrun2Main = {
    {"lr2-a-4e.a", 0x2000, 0x0000, 0x22313327},
    {"lr2-a-4d", 0x2000, 0x2000, 0xef645179},
    {"lr2-a-4a.a", 0x2000, 0x4000, 0xb11ddf59},
    {"lr2-a-4a", 0x2000, 0x6000, 0x470cc8a1},
    {"lr2-h-1c.a", 0x2000, 0x8000, 0x7ebcadbc},
    {"lr2-h-1d.a", 0x2000, 0xa000, 0x64cbb7f9},
};
const std::vector<RomEntry> kLdrun2Pal = {
    {"lr2-h-3m", 0x100, 0x000, 0x2c5d834b},
    {"lr2-h-3l", 0x100, 0x100, 0x3ae69aca},
    {"lr2-h-3n", 0x100, 0x200, 0x2b28aec5},
    {"lr2-b-1m", 0x100, 0x300, 0x4ec9bb3d},
    {"lr2-b-1n", 0x100, 0x400, 0x1daf1fa4},
    {"lr2-b-1l", 0x100, 0x500, 0xc8fb708a},
    {"lr2-b-5p", 0x020, 0x600, 0xe01f69e2},
};
const std::vector<RomEntry> kLdrun2Chars = {
    {"lr2-h-1e", 0x2000, 0x0000, 0x9d63a8ff},
    {"lr2-h-1j", 0x2000, 0x2000, 0x40332bbd},
    {"lr2-h-1h", 0x2000, 0x4000, 0x9404727d},
};
const std::vector<RomEntry> kLdrun2Sound = {
    {"lr2-a-3e", 0x2000, 0xa000, 0x853f3898},
    {"lr2-a-3f", 0x2000, 0xc000, 0x7a96accd},
    {"lr2-a-3h", 0x2000, 0xe000, 0x2a0e83ca},
};
const std::vector<RomEntry> kLdrun2Sprites = {
    {"lr2-b-4k", 0x2000, 0x0000, 0x79909871},
    {"lr2-b-4f", 0x2000, 0x2000, 0x06ba1ef4},
    {"lr2-b-3n", 0x2000, 0x4000, 0x3cc5893f},
    {"lr2-b-4n", 0x2000, 0x6000, 0x49c12f42},
    {"lr2-b-4c", 0x2000, 0x8000, 0xfbe6d24c},
    {"lr2-b-4e", 0x2000, 0xa000, 0x75172d1f},
};

// ldrun2_outbyte banks[1..30] in m62_hw.pas.
const uint8_t kLdrun2Banks[30] = {
    0, 0, 0, 0, 0, 1, 0, 1, 0, 0, 0, 1, 1, 1, 1,
    1, 0, 0, 0, 0, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1,
};

GfxLayout char_layout(int total) {
    GfxLayout layout;
    layout.width = 8;
    layout.height = 8;
    layout.total = total;
    layout.planes = 3;
    layout.char_increment = 8 * 8;
    layout.plane_offsets = {2 * total * 8 * 8, total * 8 * 8, 0};
    layout.x_offsets = {0, 1, 2, 3, 4, 5, 6, 7};
    layout.y_offsets = {0 * 8, 1 * 8, 2 * 8, 3 * 8, 4 * 8, 5 * 8, 6 * 8, 7 * 8};
    return layout;
}

GfxLayout sprite_layout(int total) {
    GfxLayout layout;
    layout.width = 16;
    layout.height = 16;
    layout.total = total;
    layout.planes = 3;
    layout.char_increment = 32 * 8;
    layout.plane_offsets = {2 * total * 32 * 8, total * 32 * 8, 0};
    layout.x_offsets = {0,          1,          2,          3,          4,          5,
                        6,          7,          16 * 8 + 0, 16 * 8 + 1, 16 * 8 + 2, 16 * 8 + 3,
                        16 * 8 + 4, 16 * 8 + 5, 16 * 8 + 6, 16 * 8 + 7};
    layout.y_offsets = {0 * 8,  1 * 8,  2 * 8,  3 * 8,  4 * 8,  5 * 8,  6 * 8,  7 * 8,
                        8 * 8,  9 * 8,  10 * 8, 11 * 8, 12 * 8, 13 * 8, 14 * 8, 15 * 8};
    return layout;
}

GfxLayout spelunker_char_layout() {
    GfxLayout layout;
    layout.width = 12;
    layout.height = 8;
    layout.total = 0x200;
    layout.planes = 3;
    layout.char_increment = 8 * 8;
    layout.plane_offsets = {0, 0x4000 * 8, 2 * 0x4000 * 8};
    layout.x_offsets = {0, 1, 2, 3, 0x2000 * 8 + 0, 0x2000 * 8 + 1, 0x2000 * 8 + 2,
                        0x2000 * 8 + 3, 0x2000 * 8 + 4, 0x2000 * 8 + 5, 0x2000 * 8 + 6,
                        0x2000 * 8 + 7};
    layout.y_offsets = {0 * 8, 1 * 8, 2 * 8, 3 * 8, 4 * 8, 5 * 8, 6 * 8, 7 * 8};
    return layout;
}

uint32_t rgb4(uint8_t nibble) {
    uint8_t value = uint8_t(((nibble & 0x0f) << 4) | (nibble & 0x0f));
    return value;
}

}  // namespace

IremM62::IremM62(Game game)
    : game_(game),
      main_cpu_(game == Game::KungFuMaster ? kKungFuClock : kMainClock),
      sound_cpu_(kSoundClock, HD63701::Type::M6803),
      ay0_(kAyClock),
      ay1_(kAyClock),
      msm0_(kMsmClock, 96, 4),
      msm1_(kMsmClock, 0, 4) {
    if (is_kungfu()) {
        screen_width_ = 256;
        crop_x_ = 128;
        crop_y_ = 0;
    } else {
        screen_width_ = 384;
        crop_x_ = 64;
        crop_y_ = is_spelunker() ? 128 : 0;
    }
    framebuffer_.assign(size_t(screen_width_) * kScreenHeight, 0xff000000u);

    main_cpu_.set_memory_handlers(
        [this](uint16_t address) { return main_read(address); },
        [this](uint16_t address, uint8_t value) { main_write(address, value); });
    main_cpu_.set_io_handlers(
        [this](uint16_t port) { return main_in(port); },
        [this](uint16_t port, uint8_t value) { main_out(port, value); });

    sound_cpu_.set_memory_handlers(
        [this](uint16_t address) { return sound_read(address); },
        [this](uint16_t address, uint8_t value) { sound_write(address, value); });
    sound_cpu_.set_port_read(0, [this]() { return in_port1(); });
    sound_cpu_.set_port_read(1, []() { return uint8_t(0); });
    sound_cpu_.set_port_write(0, [this](uint8_t value) { out_port1(value); });
    sound_cpu_.set_port_write(1, [this](uint8_t value) { out_port2(value); });
    sound_cpu_.set_cycle_handler([this](int cycles) { on_sound_cycles(cycles); });

    ay0_.set_port_handlers([this]() { return ay0_port_a_read(); }, nullptr, nullptr,
                           [this](uint8_t value) { ay0_port_b_write(value); });
    msm0_.set_vclk_handler([this]() { sound_cpu_.set_nmi(IrqLine::Pulse); });
}

const char* IremM62::title() const {
    switch (game_) {
        case Game::KungFuMaster: return "Kung-Fu Master";
        case Game::Spelunker: return "Spelunker";
        case Game::Spelunker2: return "Spelunker II";
        case Game::LodeRunner: return "Lode Runner";
        case Game::LodeRunner2: return "Lode Runner II";
    }
    return "Irem M62";
}

bool IremM62::init(const std::string& rom_path, std::string* error) {
    if (!load_roms(rom_path, error)) return false;
    reset();
    return true;
}

bool IremM62::load_roms(const std::string& rom_path, std::string* error) {
    RomLoader loader;
    if (!loader.open(rom_path, error)) return false;

    auto load_into = [&](const std::vector<RomEntry>& entries, std::vector<uint8_t>& dest) {
        return loader.load(entries, dest, error);
    };

    std::vector<uint8_t> main_rom;
    std::vector<uint8_t> sound_rom;
    std::vector<uint8_t> char_rom;
    std::vector<uint8_t> sprite_rom;
    std::vector<uint8_t> tile_rom;
    std::vector<uint8_t> pal_rom;

    switch (game_) {
        case Game::KungFuMaster:
            if (!load_into(kKungFuMain, main_rom)) return false;
            std::copy(main_rom.begin(), main_rom.end(), memory_.begin());
            if (!load_into(kKungFuSound, sound_rom)) return false;
            if (!load_into(kKungFuChars, char_rom)) return false;
            decode_chars(char_rom, 1024);
            if (!load_into(kKungFuSprites, sprite_rom)) return false;
            decode_sprites(sprite_rom, 1024);
            if (!load_into(kKungFuPal, pal_rom)) return false;
            build_palette(pal_rom);
            std::memcpy(sprite_height_.data(), pal_rom.data() + 0x600, 0x20);
            break;

        case Game::Spelunker:
            if (!load_into(kSpelunkrMain, main_rom)) return false;
            std::copy(main_rom.begin(), main_rom.begin() + 0x8000, memory_.begin());
            for (int bank = 0; bank < 4; bank++) {
                std::copy(main_rom.begin() + 0x8000 + bank * 0x2000,
                          main_rom.begin() + 0x8000 + (bank + 1) * 0x2000, mem_rom_[size_t(bank)].begin());
            }
            if (!load_into(kSplSound, sound_rom)) return false;
            if (!load_into(kSpelunkrChars, char_rom)) return false;
            decode_spelunker_chars(char_rom);
            if (!load_into(kSpelunkrSprites, sprite_rom)) return false;
            decode_sprites(sprite_rom, 0x400);
            if (!load_into(kSpelunkrTiles, tile_rom)) return false;
            decode_tiles(tile_rom, 4096);
            if (!load_into(kSpelunkrPal, pal_rom)) return false;
            build_palette(pal_rom);
            std::memcpy(sprite_height_.data(), pal_rom.data() + 0x600, 0x20);
            sprites_sp_ = 1;
            break;

        case Game::Spelunker2:
            if (!load_into(kSpelunk2Main, main_rom)) return false;
            std::copy(main_rom.begin(), main_rom.begin() + 0x8000, memory_.begin());
            for (int bank = 0; bank < 16; bank++) {
                std::copy(main_rom.begin() + 0x8000 + bank * 0x1000,
                          main_rom.begin() + 0x8000 + (bank + 1) * 0x1000,
                          mem_rom2_[size_t(bank)].begin());
            }
            for (int bank = 0; bank < 4; bank++) {
                std::copy(main_rom.begin() + 0x18000 + bank * 0x1000,
                          main_rom.begin() + 0x18000 + (bank + 1) * 0x1000,
                          mem_rom_[size_t(bank)].begin());
            }
            if (!load_into(kSpelunk2Sound, sound_rom)) return false;
            if (!load_into(kSpelunk2Chars, char_rom)) return false;
            decode_spelunker_chars(char_rom);
            if (!load_into(kSpelunk2Sprites, sprite_rom)) return false;
            decode_sprites(sprite_rom, 0x400);
            if (!load_into(kSpelunk2Tiles, tile_rom)) return false;
            decode_tiles(tile_rom, 4096);
            if (!load_into(kSpelunk2Pal, pal_rom)) return false;
            build_palette_spl2(pal_rom);
            std::memcpy(sprite_height_.data(), pal_rom.data() + 0x700, 0x20);
            sprites_sp_ = 2;
            break;

        case Game::LodeRunner:
            if (!load_into(kLdrunMain, main_rom)) return false;
            std::copy(main_rom.begin(), main_rom.end(), memory_.begin());
            if (!load_into(kLdrunSound, sound_rom)) return false;
            if (!load_into(kLdrunChars, char_rom)) return false;
            decode_chars(char_rom, 0x400);
            if (!load_into(kLdrunSprites, sprite_rom)) return false;
            decode_sprites(sprite_rom, 0x100);
            if (!load_into(kLdrunPal, pal_rom)) return false;
            build_palette(pal_rom);
            std::memcpy(sprite_height_.data(), pal_rom.data() + 0x600, 0x20);
            ldrun_color_ = 0x0c;
            break;

        case Game::LodeRunner2:
            if (!load_into(kLdrun2Main, main_rom)) return false;
            std::copy(main_rom.begin(), main_rom.begin() + 0x8000, memory_.begin());
            for (int bank = 0; bank < 2; bank++) {
                std::copy(main_rom.begin() + 0x8000 + bank * 0x2000,
                          main_rom.begin() + 0x8000 + (bank + 1) * 0x2000, mem_rom_[size_t(bank)].begin());
            }
            if (!load_into(kLdrun2Sound, sound_rom)) return false;
            if (!load_into(kLdrun2Chars, char_rom)) return false;
            decode_chars(char_rom, 0x400);
            if (!load_into(kLdrun2Sprites, sprite_rom)) return false;
            decode_sprites(sprite_rom, 0x200);
            if (!load_into(kLdrun2Pal, pal_rom)) return false;
            build_palette(pal_rom);
            std::memcpy(sprite_height_.data(), pal_rom.data() + 0x600, 0x20);
            ldrun_color_ = 4;
            break;
    }

    sound_memory_.fill(0);
    if (sound_rom.size() > sound_memory_.size()) sound_rom.resize(sound_memory_.size());
    std::copy(sound_rom.begin(), sound_rom.end(), sound_memory_.begin());

    warnings_ = loader.warnings();
    return true;
}

void IremM62::decode_chars(const std::vector<uint8_t>& rom, int count) {
    chars_.decode(char_layout(count), rom);
}

void IremM62::decode_spelunker_chars(const std::vector<uint8_t>& rom) {
    std::vector<uint8_t> rearranged(0xc000, 0);
    for (int plane = 0; plane < 3; plane++) {
        for (int block = 0; block < 4; block++) {
            const int src = plane * 0x4000 + block * 0x1000;
            const int dest = plane * 0x4000 + block * 0x800;
            if (src + 0x1000 > int(rom.size())) continue;
            std::memcpy(rearranged.data() + dest, rom.data() + src, 0x800);
            std::memcpy(rearranged.data() + dest + 0x2000, rom.data() + src + 0x800, 0x800);
        }
    }
    chars_.decode(spelunker_char_layout(), rearranged);
}

void IremM62::decode_sprites(const std::vector<uint8_t>& rom, int count) {
    sprites_.decode(sprite_layout(count), rom);
}

void IremM62::decode_tiles(const std::vector<uint8_t>& rom, int count) {
    tiles_.decode(char_layout(count), rom);
}

void IremM62::build_palette(const std::vector<uint8_t>& prom) {
    palette_.fill(0xff000000u);
    for (int index = 0; index < 0x100; index++) {
        uint8_t red = uint8_t(prom.size() > size_t(index) ? prom[size_t(index)] : 0);
        uint8_t green = uint8_t(prom.size() > size_t(index + 0x100) ? prom[size_t(index + 0x100)] : 0);
        uint8_t blue = uint8_t(prom.size() > size_t(index + 0x200) ? prom[size_t(index + 0x200)] : 0);
        palette_[size_t(index)] = 0xff000000u | (rgb4(red) << 16) | (rgb4(green) << 8) | rgb4(blue);
        uint8_t sred = uint8_t(prom.size() > size_t(index + 0x300) ? prom[size_t(index + 0x300)] : 0);
        uint8_t sgreen =
            uint8_t(prom.size() > size_t(index + 0x400) ? prom[size_t(index + 0x400)] : 0);
        uint8_t sblue = uint8_t(prom.size() > size_t(index + 0x500) ? prom[size_t(index + 0x500)] : 0);
        // Matches cargar_paleta in m62_hw.pas, including the mixed low nibbles.
        palette_[size_t(index + 0x100)] = 0xff000000u | (uint32_t((sred & 0x0f) << 4 | (red & 0x0f)) << 16) |
                                          (uint32_t((sgreen & 0x0f) << 4 | (green & 0x0f)) << 8) |
                                          uint32_t((sblue & 0x0f) << 4 | (blue & 0x0f));
    }
}

void IremM62::build_palette_spl2(const std::vector<uint8_t>& prom) {
    palette_.fill(0xff000000u);
    for (int index = 0; index < 0x200; index++) {
        uint8_t packed = uint8_t(prom.size() > size_t(index) ? prom[size_t(index)] : 0);
        uint8_t blue = uint8_t(prom.size() > size_t(index + 0x200) ? prom[size_t(index + 0x200)] : 0);
        uint8_t green = uint8_t(((packed & 0xf0) >> 4) | (packed & 0xf0));
        palette_[size_t(index)] =
            0xff000000u | (rgb4(packed) << 16) | (uint32_t(green) << 8) | rgb4(blue);
    }
    for (int index = 0; index < 0x100; index++) {
        uint8_t red = uint8_t(prom.size() > size_t(index + 0x400) ? prom[size_t(index + 0x400)] : 0);
        uint8_t green =
            uint8_t(prom.size() > size_t(index + 0x500) ? prom[size_t(index + 0x500)] : 0);
        uint8_t blue = uint8_t(prom.size() > size_t(index + 0x600) ? prom[size_t(index + 0x600)] : 0);
        palette_[size_t(index + 0x200)] =
            0xff000000u | (rgb4(red) << 16) | (rgb4(green) << 8) | rgb4(blue);
    }
}

void IremM62::reset() {
    main_cpu_.reset();
    sound_cpu_.reset();
    ay0_.reset();
    ay1_.reset();
    msm0_.reset();
    msm1_.reset();
    sound_command_ = 0;
    val_port1_ = 0;
    val_port2_ = 0;
    rom_bank_ = 0;
    rom_bank2_ = 0;
    pal_bank_ = 0;
    ldrun2_banksw_ = 0;
    old_bank_ = 0;
    scroll_x_ = 0;
    scroll_y_ = 0;
    in0_ = 0xff;
    in1_ = 0xff;
    in2_ = 0xff;
    audio_.clear();
    audio_accumulator_ = 0;
    msm_accumulator_ = 0;
}

uint8_t IremM62::main_read(uint16_t address) {
    switch (game_) {
        case Game::KungFuMaster:
        case Game::LodeRunner:
            if (address <= 0x7fff || (address >= 0xd000 && address <= 0xefff)) {
                return memory_[address];
            }
            break;
        case Game::Spelunker:
            if (address <= 0x7fff || (address >= 0xa000 && address <= 0xbfff) ||
                (address >= 0xc800 && address <= 0xcfff) ||
                (address >= 0xe000 && address <= 0xefff)) {
                return memory_[address];
            }
            if (address >= 0x8000 && address <= 0x9fff) {
                return mem_rom_[rom_bank_ & 3][address & 0x1fff];
            }
            break;
        case Game::Spelunker2:
            if (address <= 0x7fff || (address >= 0xa000 && address <= 0xbfff) ||
                (address >= 0xc800 && address <= 0xcfff) ||
                (address >= 0xe000 && address <= 0xefff)) {
                return memory_[address];
            }
            if (address >= 0x8000 && address <= 0x8fff) {
                return mem_rom_[rom_bank_ & 3][address & 0x0fff];
            }
            if (address >= 0x9000 && address <= 0x9fff) {
                return mem_rom2_[rom_bank2_ & 15][address & 0x0fff];
            }
            break;
        case Game::LodeRunner2:
            if (address <= 0x7fff || (address >= 0xd000 && address <= 0xefff)) {
                return memory_[address];
            }
            if (address >= 0x8000 && address <= 0x9fff) {
                return mem_rom_[rom_bank_ & 1][address & 0x1fff];
            }
            break;
    }
    return 0xff;
}

void IremM62::main_write(uint16_t address, uint8_t value) {
    switch (game_) {
        case Game::KungFuMaster:
            if (address <= 0x7fff) return;
            if (address == 0xa000) {
                scroll_x_ = uint16_t((scroll_x_ & 0x100) | value);
                return;
            }
            if (address == 0xb000) {
                scroll_x_ = uint16_t((scroll_x_ & 0xff) | ((value & 1) << 8));
                return;
            }
            if ((address >= 0xc000 && address <= 0xc0ff) ||
                (address >= 0xe000 && address <= 0xefff)) {
                memory_[address] = value;
                return;
            }
            if (address >= 0xd000 && address <= 0xdfff) {
                memory_[address] = value;
                return;
            }
            return;

        case Game::Spelunker:
            if (address <= 0x9fff) return;
            if (address >= 0xa000 && address <= 0xbfff) {
                memory_[address] = value;
                return;
            }
            if ((address >= 0xc000 && address <= 0xc0ff) ||
                (address >= 0xe000 && address <= 0xefff)) {
                memory_[address] = value;
                return;
            }
            if (address >= 0xc800 && address <= 0xcfff) {
                memory_[address] = value;
                return;
            }
            if (address == 0xd000) {
                scroll_y_ = uint16_t((scroll_y_ & 0x100) | value);
                return;
            }
            if (address == 0xd001) {
                scroll_y_ = uint16_t((scroll_y_ & 0xff) | ((value & 1) << 8));
                return;
            }
            if (address == 0xd002) {
                scroll_x_ = uint16_t((scroll_x_ & 0x100) | value);
                return;
            }
            if (address == 0xd003) {
                scroll_x_ = uint16_t((scroll_x_ & 0xff) | ((value & 1) << 8));
                return;
            }
            if (address == 0xd004) {
                rom_bank_ = uint8_t(value & 3);
                return;
            }
            if (address == 0xd005) pal_bank_ = uint8_t((value & 1) << 4);
            return;

        case Game::Spelunker2:
            if (address <= 0x9fff) return;
            if (address >= 0xa000 && address <= 0xbfff) {
                memory_[address] = value;
                return;
            }
            if ((address >= 0xc000 && address <= 0xc0ff) ||
                (address >= 0xe000 && address <= 0xefff)) {
                memory_[address] = value;
                return;
            }
            if (address >= 0xc800 && address <= 0xcfff) {
                memory_[address] = value;
                return;
            }
            if (address == 0xd000) {
                scroll_y_ = uint16_t((scroll_y_ & 0x100) | value);
                return;
            }
            if (address == 0xd001) {
                scroll_x_ = uint16_t((scroll_x_ & 0x100) | value);
                return;
            }
            if (address == 0xd002) {
                scroll_x_ = uint16_t((scroll_x_ & 0xff) | ((value & 2) << 7));
                scroll_y_ = uint16_t((scroll_y_ & 0xff) | ((value & 1) << 8));
                pal_bank_ = uint8_t((value & 0x0c) << 2);
                return;
            }
            if (address == 0xd003) {
                rom_bank_ = uint8_t((value & 0xc0) >> 6);
                rom_bank2_ = uint8_t((value & 0x3c) >> 2);
            }
            return;

        case Game::LodeRunner:
        case Game::LodeRunner2:
            if (address <= 0x7fff) return;
            if ((address >= 0xc000 && address <= 0xc0ff) ||
                (address >= 0xe000 && address <= 0xefff)) {
                memory_[address] = value;
                return;
            }
            if (address >= 0xd000 && address <= 0xdfff) memory_[address] = value;
            return;
    }
}

uint8_t IremM62::main_in(uint16_t port) {
    switch (port & 0xff) {
        case 0: return in0_;
        case 1: return in1_;
        case 2: return in2_;
        case 3: return dsw_a_;
        case 4: return dsw_b_;
        case 0x80:
            if (game_ == Game::LodeRunner2 && ldrun2_banksw_ != 0) {
                ldrun2_banksw_--;
                if (ldrun2_banksw_ == 0) rom_bank_ = 1;
            }
            return 0;
        default: return 0xff;
    }
}

void IremM62::main_out(uint16_t port, uint8_t value) {
    switch (port & 0xff) {
        case 0:
            if ((value & 0x80) == 0) sound_command_ = uint8_t(value & 0x7f);
            else sound_cpu_.set_irq(IrqLine::Assert);
            break;
        case 0x80:
            if (game_ == Game::LodeRunner2) {
                rom_bank_ = (value >= 1 && value <= 30) ? kLdrun2Banks[value - 1] : 0;
                old_bank_ = value;
            }
            break;
        case 0x81:
            if (game_ == Game::LodeRunner2) {
                ldrun2_banksw_ = (old_bank_ == 1 && value == 0x0d) ? 2 : 0;
            }
            break;
        default: break;
    }
}

uint8_t IremM62::sound_read(uint16_t address) {
    if (address >= 0x4000) return sound_memory_[address];
    return 0xff;
}

void IremM62::sound_write(uint16_t address, uint8_t value) {
    if (address >= 0x0800 && address <= 0x08ff) {
        switch (address & 3) {
            case 0: sound_cpu_.set_irq(IrqLine::Clear); break;
            case 1: msm0_.data_w(value); break;
            case 2: msm1_.data_w(value); break;
            default: break;
        }
    }
}

void IremM62::out_port1(uint8_t value) { val_port1_ = value; }

void IremM62::out_port2(uint8_t value) {
    if ((val_port2_ & 1) != 0 && (value & 1) == 0) {
        if ((val_port2_ & 4) != 0) {
            if ((val_port2_ & 8) != 0) ay0_.control(val_port1_);
            if ((val_port2_ & 0x10) != 0) ay1_.control(val_port1_);
        } else {
            if ((val_port2_ & 8) != 0) ay0_.write(val_port1_);
            if ((val_port2_ & 0x10) != 0) ay1_.write(val_port1_);
        }
    }
    val_port2_ = value;
}

uint8_t IremM62::in_port1() {
    if ((val_port2_ & 8) != 0) return ay0_.read();
    if ((val_port2_ & 0x10) != 0) return ay1_.read();
    return 0xff;
}

uint8_t IremM62::ay0_port_a_read() { return sound_command_; }

void IremM62::ay0_port_b_write(uint8_t value) {
    msm0_.set_reset((value & 1) != 0);
    msm1_.set_reset((value & 2) != 0);
}

void IremM62::on_sound_cycles(int cycles) {
    const uint32_t msm_rate = msm0_.sample_frequency();
    msm_accumulator_ += int64_t(cycles) * msm_rate;
    while (msm_accumulator_ >= int64_t(kSoundClock)) {
        msm_accumulator_ -= int64_t(kSoundClock);
        msm0_.vclk();
        msm1_.vclk();
    }
    audio_accumulator_ += int64_t(cycles) * AY8910::kSampleRate;
    while (audio_accumulator_ >= int64_t(kSoundClock)) {
        audio_accumulator_ -= int64_t(kSoundClock);
        int32_t sample = ay0_.update() + ay1_.update() + msm0_.output() + msm1_.output();
        audio_.push_back(int16_t(std::clamp(sample, -32768, 32767)));
    }
}

void IremM62::draw_tile_8(int dest_x, int dest_y, int code, int color, bool flip_x, bool opaque,
                          std::array<uint32_t, kWorkWidth * kWorkHeight>& dest) {
    const uint8_t* pixels = chars_.element(code);
    const int width = chars_.width();
    const int height = chars_.height();
    for (int y = 0; y < height; y++) {
        int py = dest_y + y;
        if (py < 0 || py >= kWorkHeight) continue;
        for (int x = 0; x < width; x++) {
            int px = dest_x + x;
            if (px < 0 || px >= kWorkWidth) continue;
            int source_x = flip_x ? (width - 1 - x) : x;
            uint8_t pen = pixels[y * width + source_x];
            if (!opaque && pen == 0) {
                dest[size_t(py * kWorkWidth + px)] = kTransparent;
                continue;
            }
            dest[size_t(py * kWorkWidth + px)] = palette_[size_t((color + pen) & 0x2ff)];
        }
    }
}

void IremM62::draw_tile_12(int dest_x, int dest_y, int code, int color,
                           std::array<uint32_t, kWorkWidth * kWorkHeight>& dest) {
    const uint8_t* pixels = chars_.element(code);
    for (int y = 0; y < 8; y++) {
        int py = dest_y + y;
        if (py < 0 || py >= kWorkHeight) continue;
        for (int x = 0; x < 12; x++) {
            int px = dest_x + x;
            if (px < 0 || px >= kWorkWidth) continue;
            uint8_t pen = pixels[y * 12 + x];
            if (pen == 0) {
                dest[size_t(py * kWorkWidth + px)] = kTransparent;
                continue;
            }
            dest[size_t(py * kWorkWidth + px)] = palette_[size_t((color + pen) & 0x2ff)];
        }
    }
}

void IremM62::draw_sprite_tile(int code, int color, bool flip_x, bool flip_y, int pos_x,
                               int pos_y) {
    const uint8_t* pixels = sprites_.element(code);
    for (int y = 0; y < 16; y++) {
        int dest_y = pos_y + y;
        if (dest_y < 0 || dest_y >= kWorkHeight) continue;
        int source_y = flip_y ? (15 - y) : y;
        for (int x = 0; x < 16; x++) {
            int dest_x = pos_x + x;
            if (dest_x < 0 || dest_x >= kWorkWidth) continue;
            int source_x = flip_x ? (15 - x) : x;
            uint8_t pen = pixels[source_y * 16 + source_x];
            if (pen == 0) continue;
            composite_[size_t(dest_y * kWorkWidth + dest_x)] =
                palette_[size_t((color + pen) & 0x2ff)];
        }
    }
}

void IremM62::draw_sprites(int pos, int col, uint8_t col_mask, uint8_t pri_mask, uint8_t pri) {
    for (int index = 0; index < 0x20; index++) {
        uint8_t atrib2 = memory_[0xc000 + index * 8];
        if ((atrib2 & pri_mask) != pri) continue;
        uint8_t atrib = memory_[0xc005 + index * 8];
        int nchar = memory_[0xc004 + index * 8] + ((atrib & 7) << 8);
        int color = ((atrib2 & col_mask) << 3) + (256 * col);
        int x = ((memory_[0xc007 + index * 8] & 1) << 8) + memory_[0xc006 + index * 8];
        int y = 256 + 128 * pos - 15 -
                (256 * (memory_[0xc003 + index * 8] & 1) + memory_[0xc002 + index * 8]);
        bool flip_x = (atrib & 0x40) != 0;
        bool flip_y = (atrib & 0x80) != 0;
        switch (sprite_height_[(nchar >> 5) & 0x1f] & 3) {
            case 1: {
                nchar &= ~1;
                int a = flip_y ? nchar + 1 : nchar;
                int b = flip_y ? nchar : nchar + 1;
                draw_sprite_tile(a, color, flip_x, flip_y, x, y - 16);
                draw_sprite_tile(b, color, flip_x, flip_y, x, y);
                break;
            }
            case 2: {
                nchar &= ~3;
                int a = flip_y ? nchar + 3 : nchar;
                int b = flip_y ? nchar + 2 : nchar + 1;
                int c = flip_y ? nchar + 1 : nchar + 2;
                int d = flip_y ? nchar : nchar + 3;
                draw_sprite_tile(a, color, flip_x, flip_y, x, y - 48);
                draw_sprite_tile(b, color, flip_x, flip_y, x, y - 32);
                draw_sprite_tile(c, color, flip_x, flip_y, x, y - 16);
                draw_sprite_tile(d, color, flip_x, flip_y, x, y);
                break;
            }
            default:
                draw_sprite_tile(nchar, color, flip_x, flip_y, x, y);
                break;
        }
    }
}

uint16_t IremM62::calc_nchar_sp(uint8_t color) const {
    if (game_ == Game::Spelunker2) return uint16_t((color & 0xf0) << 4);
    return uint16_t(((color & 0x10) << 4) + ((color & 0x20) << 6) + ((color & 0xc0) << 3));
}

void IremM62::update_video_kungfum() {
    layer1_.fill(0xff000000u);
    layer3_.fill(kTransparent);
    for (int offset = 0; offset < 0x800; offset++) {
        int x = offset % 64;
        int y = offset / 64;
        uint8_t atrib = memory_[0xd800 + offset];
        int color = (atrib & 0x1f) << 3;
        int nchar = memory_[0xd000 + offset] + ((atrib & 0xc0) << 2);
        bool flip_x = (atrib & 0x20) != 0;
        draw_tile_8(x * 8, y * 8, nchar, color, flip_x, true, layer1_);
        if (!((y < 6) || ((atrib & 0x1f) >> 1) > 0x0c)) {
            for (int row = 0; row < 8; row++) {
                for (int column = 0; column < 8; column++) {
                    layer3_[size_t((y * 8 + row) * kWorkWidth + x * 8 + column)] = kTransparent;
                }
            }
        } else {
            draw_tile_8(x * 8, y * 8, nchar, color, flip_x, true, layer3_);
        }
    }

    composite_ = layer1_;
    for (int y = 48; y < 256; y++) {
        for (int x = 0; x < kWorkWidth; x++) {
            composite_[size_t(y * kWorkWidth + x)] =
                layer1_[size_t(y * kWorkWidth + ((x + scroll_x_) & (kWorkWidth - 1)))];
        }
    }
    draw_sprites(1, 1, 0x1f, 0, 0);
    for (int y = 48; y < 256; y++) {
        for (int x = 0; x < kWorkWidth; x++) {
            uint32_t pixel =
                layer3_[size_t(y * kWorkWidth + ((x + scroll_x_) & (kWorkWidth - 1)))];
            if (pixel != kTransparent) composite_[size_t(y * kWorkWidth + x)] = pixel;
        }
    }
    for (int y = 0; y < 48; y++) {
        for (int x = 0; x < kWorkWidth; x++) {
            composite_[size_t(y * kWorkWidth + x)] = layer1_[size_t(y * kWorkWidth + x)];
        }
    }
}

void IremM62::update_video_ldrun() {
    layer1_.fill(0xff000000u);
    layer3_.fill(kTransparent);
    for (int offset = 0; offset < 0x800; offset++) {
        int x = offset % 64;
        int y = offset / 64;
        uint8_t atrib = memory_[0xd001 + offset * 2];
        int color = (atrib & 0x1f) << 3;
        int nchar = memory_[0xd000 + offset * 2] + ((atrib & 0xc0) << 2);
        bool flip_x = (atrib & 0x20) != 0;
        draw_tile_8(x * 8, y * 8, nchar, color, flip_x, true, layer1_);
        if (!(((atrib & 0x1f) >> 1) >= ldrun_color_)) {
            for (int row = 0; row < 8; row++) {
                for (int column = 0; column < 8; column++) {
                    layer3_[size_t((y * 8 + row) * kWorkWidth + x * 8 + column)] = kTransparent;
                }
            }
        } else {
            draw_tile_8(x * 8, y * 8, nchar, color, flip_x, false, layer3_);
        }
    }
    composite_ = layer1_;
    draw_sprites(1, 1, 0x0f, 0x10, 0);
    for (size_t index = 0; index < composite_.size(); index++) {
        if (layer3_[index] != kTransparent) composite_[index] = layer3_[index];
    }
    draw_sprites(1, 1, 0x0f, 0x10, 0x10);
}

void IremM62::update_video_spelunker() {
    layer3_.fill(kTransparent);
    for (int offset = 0; offset < 0x400; offset++) {
        int x = offset % 32;
        int y = offset / 32;
        uint8_t color = memory_[0xc801 + offset * 2];
        int nchar = memory_[0xc800 + offset * 2] + ((color & 0x10) << 4);
        draw_tile_12(x * 12, y * 8, nchar, (pal_bank_ | (color & 0x0f)) << 3, layer3_);
    }
    layer1_.fill(0xff000000u);
    for (int offset = 0; offset < 0x1000; offset++) {
        int x = offset % 64;
        int y = offset / 64;
        uint8_t color = memory_[0xa001 + offset * 2];
        int nchar = memory_[0xa000 + offset * 2] + calc_nchar_sp(color);
        const uint8_t* pixels = tiles_.element(nchar);
        int palette_base = (pal_bank_ | (color & 0x0f)) << 3;
        for (int row = 0; row < 8; row++) {
            for (int column = 0; column < 8; column++) {
                uint8_t pen = pixels[row * 8 + column];
                layer1_[size_t((y * 8 + row) * kWorkWidth + x * 8 + column)] =
                    palette_[size_t((palette_base + pen) & 0x2ff)];
            }
        }
    }
    for (int y = 0; y < kWorkHeight; y++) {
        int source_y = (y + scroll_y_) & (kWorkHeight - 1);
        for (int x = 0; x < kWorkWidth; x++) {
            int source_x = (x + scroll_x_) & (kWorkWidth - 1);
            composite_[size_t(y * kWorkWidth + x)] =
                layer1_[size_t(source_y * kWorkWidth + source_x)];
        }
    }
    draw_sprites(2, sprites_sp_, 0x1f, 0, 0);
    for (int y = 0; y < 256; y++) {
        for (int x = 0; x < 384; x++) {
            uint32_t pixel = layer3_[size_t(y * kWorkWidth + x)];
            if (pixel != kTransparent) {
                composite_[size_t((y + 128) * kWorkWidth + (x + 64))] = pixel;
            }
        }
    }
}

void IremM62::update_video() {
    if (is_kungfu()) update_video_kungfum();
    else if (is_ldrun()) update_video_ldrun();
    else update_video_spelunker();

    for (int y = 0; y < kScreenHeight; y++) {
        for (int x = 0; x < screen_width_; x++) {
            framebuffer_[size_t(y * screen_width_ + x)] =
                composite_[size_t((y + crop_y_) * kWorkWidth + (x + crop_x_))];
        }
    }
}

void IremM62::run_frame() {
    const int main_cycles = int(main_clock() / kFramesPerSecond / kScanlines);
    const int sound_cycles = int(kSoundClock / kFramesPerSecond / kScanlines);
    for (int line = 0; line < kScanlines; line++) {
        if (line == 256) {
            main_cpu_.set_irq(IrqLine::Hold);
            update_video();
        }
        main_cpu_.run(main_cycles);
        sound_cpu_.run(sound_cycles);
    }
}

void IremM62::set_inputs(const MachineInputs& inputs) {
    const InputState& player1 = inputs.player1;
    in0_ = 0xff;
    in1_ = 0xff;
    in2_ = 0xff;
    if (player1.right) in1_ &= 0xfe;
    if (player1.left) in1_ &= 0xfd;
    if (player1.down) in1_ &= 0xfb;
    if (player1.up) in1_ &= 0xf7;
    if (player1.button1) in1_ &= 0xdf;
    if (player1.button2) in1_ &= 0x7f;
    if (inputs.coin1) in0_ &= 0xf7;
    if (inputs.coin2) in2_ &= 0xef;
    if (player1.start) in0_ &= 0xfe;
    if (inputs.player2.start) in0_ &= 0xfd;
}

void IremM62::set_dip_switch(int bank, uint8_t value) {
    if (bank == 0) dsw_a_ = value;
    if (bank == 1) dsw_b_ = value;
}

void IremM62::drain_audio(std::vector<int16_t>& out) {
    out.insert(out.end(), audio_.begin(), audio_.end());
    audio_.clear();
}

}  // namespace dsp
