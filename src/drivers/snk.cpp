#include "drivers/snk.h"

#include <algorithm>
#include <cstring>

#include "core/rom_loader.h"

namespace dsp {
namespace {

inline uint8_t pal4bit(uint8_t value) { return uint8_t((value & 0x0f) * 0x11); }

const std::vector<int> kPbX = {4 * 1,     4 * 0,     4 * 3,     4 * 2,     4 * 5,     4 * 4,
                               4 * 7,     4 * 6,     32 + 4 * 1, 32 + 4 * 0, 32 + 4 * 3, 32 + 4 * 2,
                               32 + 4 * 5, 32 + 4 * 4, 32 + 4 * 7, 32 + 4 * 6};

const std::vector<int> kPbY = {0 * 64,  1 * 64,  2 * 64,  3 * 64,  4 * 64,  5 * 64,  6 * 64,  7 * 64,
                               8 * 64,  9 * 64,  10 * 64, 11 * 64, 12 * 64, 13 * 64, 14 * 64, 15 * 64};

const std::vector<int> kPcY = {
    0 * 32,  1 * 32,  2 * 32,  3 * 32,  4 * 32,  5 * 32,  6 * 32,  7 * 32,
    8 * 32,  9 * 32,  10 * 32, 11 * 32, 12 * 32, 13 * 32, 14 * 32, 15 * 32,
    16 * 32 + 0 * 32,  16 * 32 + 1 * 32,  16 * 32 + 2 * 32,  16 * 32 + 3 * 32,
    16 * 32 + 4 * 32,  16 * 32 + 5 * 32,  16 * 32 + 6 * 32,  16 * 32 + 7 * 32,
    16 * 32 + 8 * 32,  16 * 32 + 9 * 32,  16 * 32 + 10 * 32, 16 * 32 + 11 * 32,
    16 * 32 + 12 * 32, 16 * 32 + 13 * 32, 16 * 32 + 14 * 32, 16 * 32 + 15 * 32};

const std::vector<int> kPsX = {7,  6,  5,  4,  3,  2,  1,  0,  15, 14, 13, 12, 11, 10, 9,  8,
                               23, 22, 21, 20, 19, 18, 17, 16, 31, 30, 29, 28, 27, 26, 25, 24};

const std::vector<int> kPsY = {0 * 16,  1 * 16,  2 * 16,  3 * 16,  4 * 16,  5 * 16,  6 * 16,  7 * 16,
                               8 * 16,  9 * 16,  10 * 16, 11 * 16, 12 * 16, 13 * 16, 14 * 16, 15 * 16};

GfxLayout packed_layout(int width, int height, int total, int char_increment,
                        const std::vector<int>& x_offsets, const std::vector<int>& y_offsets,
                        bool rotate_ccw) {
    GfxLayout layout;
    layout.width = width;
    layout.height = height;
    layout.total = total;
    layout.planes = 4;
    layout.char_increment = char_increment;
    layout.rotate_ccw = rotate_ccw;
    layout.plane_offsets = {0, 1, 2, 3};
    layout.x_offsets = x_offsets;
    layout.y_offsets = y_offsets;
    return layout;
}

GfxLayout sprite_layout(int width, int height, int total, int char_increment, int plane_bytes,
                        const std::vector<int>& x_offsets, const std::vector<int>& y_offsets,
                        bool rotate_ccw) {
    GfxLayout layout;
    layout.width = width;
    layout.height = height;
    layout.total = total;
    layout.planes = 3;
    layout.char_increment = char_increment;
    layout.rotate_ccw = rotate_ccw;
    layout.plane_offsets = {2 * plane_bytes * 8, 1 * plane_bytes * 8, 0};
    layout.x_offsets = x_offsets;
    layout.y_offsets = y_offsets;
    return layout;
}

const std::vector<RomEntry> kIkariMain = {{"1.rom|1.4p", 0x10000, 0, 0x52a8b2dd}};
const std::vector<RomEntry> kIkariSub = {{"2.rom|2.8p", 0x10000, 0, 0x45364d55}};
const std::vector<RomEntry> kIkariSnd = {{"3.rom|3.7k", 0x10000, 0, 0x56a26699}};
const std::vector<RomEntry> kIkariChars = {{"7.rom|p7.3b", 0x4000, 0, 0xa7eb4917}};
const std::vector<RomEntry> kIkariTiles = {
    {"17.rom|p17.4d", 0x8000, 0x00000, 0xe0dba976},
    {"18.rom|p18.2d", 0x8000, 0x08000, 0x24947d5f},
    {"19.rom|p19.4b", 0x8000, 0x10000, 0x9ee59e91},
    {"20.rom|p20.2b", 0x8000, 0x18000, 0x5da7ec1a},
};
const std::vector<RomEntry> kIkariSp16 = {
    {"8.rom|p8.3d", 0x8000, 0x00000, 0x9827c14a},
    {"9.rom|p9.3f", 0x8000, 0x08000, 0x545c790c},
    {"10.rom|p10.3h", 0x8000, 0x10000, 0xec9ba07e},
};
const std::vector<RomEntry> kIkariSp32 = {
    {"11.rom|p11.4m", 0x8000, 0x00000, 0x5c75ea8f},
    {"14.rom|p14.2m", 0x8000, 0x08000, 0x3293fde4},
    {"12.rom|p12.4p", 0x8000, 0x10000, 0x95138498},
    {"15.rom|p15.2p", 0x8000, 0x18000, 0x65a61c99},
    {"13.rom|p13.4r", 0x8000, 0x20000, 0x315383d7},
    {"16.rom|p16.2r", 0x8000, 0x28000, 0xe9b03e07},
};
const std::vector<RomEntry> kIkariProms = {
    {"7122er.prm|a6002-1.1k", 0x400, 0x000, 0xb9bf2c2c},
    {"7122eg.prm|a6002-2.2l", 0x400, 0x400, 0x0703a770},
    {"7122eb.prm|a6002-3.1l", 0x400, 0x800, 0x0a11cdde},
};

const std::vector<RomEntry> kAthenaMain = {
    {"up02_p4.rom", 0x4000, 0x0000, 0x900a113c},
    {"up02_m4.rom", 0x8000, 0x4000, 0x61c69474},
};
const std::vector<RomEntry> kAthenaSub = {
    {"up02_p8.rom", 0x4000, 0x0000, 0xdf50af7e},
    {"up02_m8.rom", 0x8000, 0x4000, 0xf3c933df},
};
const std::vector<RomEntry> kAthenaSnd = {
    {"up02_g6.rom", 0x4000, 0x0000, 0x42dbe029},
    {"up02_k6.rom", 0x8000, 0x4000, 0x596f1c8a},
};
const std::vector<RomEntry> kAthenaChars = {{"up01_d2.rom", 0x4000, 0, 0x18b4bcca}};
const std::vector<RomEntry> kAthenaTiles = {{"up01_b2.rom", 0x8000, 0, 0xf269c0eb}};
const std::vector<RomEntry> kAthenaSp16 = {
    {"up01_p2.rom", 0x8000, 0x00000, 0xc63a871f},
    {"up01_s2.rom", 0x8000, 0x08000, 0x760568d8},
    {"up01_t2.rom", 0x8000, 0x10000, 0x57b35c73},
};
const std::vector<RomEntry> kAthenaProms = {
    {"up02_c2.rom", 0x400, 0x000, 0x294279ae},
    {"up02_b1.rom", 0x400, 0x400, 0xd25c9099},
    {"up02_c1.rom", 0x400, 0x800, 0xa4a4e7dc},
};

const std::vector<RomEntry> kTnk3Main = {
    {"tnk3-p1.bin", 0x4000, 0x0000, 0x0d2a8ca9},
    {"tnk3-p2.bin", 0x4000, 0x4000, 0x0ae0a483},
    {"tnk3-p3.bin", 0x4000, 0x8000, 0xd16dd4db},
};
const std::vector<RomEntry> kTnk3Sub = {
    {"tnk3-p4.bin", 0x4000, 0x0000, 0x01b45a90},
    {"tnk3-p5.bin", 0x4000, 0x4000, 0x60db6667},
    {"tnk3-p6.bin", 0x4000, 0x8000, 0x4761fde7},
};
const std::vector<RomEntry> kTnk3Snd = {
    {"tnk3-p10.bin", 0x4000, 0x0000, 0x7bf0a517},
    {"tnk3-p11.bin", 0x4000, 0x4000, 0x0569ce27},
};
const std::vector<RomEntry> kTnk3Chars = {{"tnk3-p14.bin", 0x2000, 0, 0x1fd18c43}};
const std::vector<RomEntry> kTnk3Tiles = {
    {"tnk3-p12.bin", 0x4000, 0x0000, 0xff495a16},
    {"tnk3-p13.bin", 0x4000, 0x4000, 0xf8344843},
};
const std::vector<RomEntry> kTnk3Sp16 = {
    {"tnk3-p7.bin", 0x4000, 0x0000, 0x06b92c88},
    {"tnk3-p8.bin", 0x4000, 0x4000, 0x63d0e2eb},
    {"tnk3-p9.bin", 0x4000, 0x8000, 0x872e3fac},
};
const std::vector<RomEntry> kTnk3Proms = {
    {"7122.2", 0x400, 0x000, 0x34c06bc6},
    {"7122.1", 0x400, 0x400, 0x6d0ac66a},
    {"7122.0", 0x400, 0x800, 0x4662b4c8},
};

const std::vector<RomEntry> kAsoMain = {
    {"p1.8d", 0x4000, 0x0000, 0x84981f3c},
    {"p2.7d", 0x4000, 0x4000, 0xcfe912a6},
    {"p3.5d", 0x4000, 0x8000, 0x39a666d2},
};
const std::vector<RomEntry> kAsoSub = {
    {"p4.3d", 0x4000, 0x0000, 0xa4122355},
    {"p5.2d", 0x4000, 0x4000, 0x9879e506},
    {"p6.1d", 0x4000, 0x8000, 0xc0bfdf1f},
};
const std::vector<RomEntry> kAsoSnd = {
    {"p7.4f", 0x4000, 0x0000, 0xdbc19736},
    {"p8.3f", 0x4000, 0x4000, 0x537726a9},
    {"p9.2f", 0x4000, 0x8000, 0xaef5a4f4},
};
const std::vector<RomEntry> kAsoChars = {{"p14.1h", 0x2000, 0, 0x8baa2253}};
const std::vector<RomEntry> kAsoTiles = {{"p10.14h", 0x8000, 0, 0x00dff996}};
const std::vector<RomEntry> kAsoSp16 = {
    {"p11.11h", 0x8000, 0x04000, 0x7feac86c},
    {"p12.9h", 0x8000, 0x0c000, 0x6895990b},
    {"p13.8h", 0x8000, 0x14000, 0x87a81ce1},
};
const std::vector<RomEntry> kAsoProms = {
    {"mb7122h.12f", 0x400, 0x000, 0x5b0a0059},
    {"mb7122h.13f", 0x400, 0x400, 0x37e28dd8},
    {"mb7122h.14f", 0x400, 0x800, 0xc3fd1dd3},
};

}  // namespace

Snk::Snk(Game game)
    : game_(game),
      main_cpu_(kMainClock),
      sub_cpu_(kSubClock),
      sound_cpu_(kSoundClock),
      ym0_(kYmClock, YM3812::kYM3526, 2.0f),
      ym1_(kYmClock, YM3812::kYM3526, 2.0f) {
    if (is_ikari()) {
        screen_width_ = 216;
        screen_height_ = 288;
        dsw_a_ = 0x3b;
        dsw_b_ = 0x4b;
        dsw_c_ = 0x34;
    } else if (is_athena()) {
        screen_width_ = 288;
        screen_height_ = 216;
        dsw_a_ = 0x39;
        dsw_b_ = 0xcb;
        dsw_c_ = 0x34;
    } else if (is_tnk3()) {
        screen_width_ = 216;
        screen_height_ = 288;
        dsw_a_ = 0x3d;
        dsw_b_ = 0x76;
        dsw_c_ = 0xc1;
    } else {
        screen_width_ = 216;
        screen_height_ = 288;
        dsw_a_ = 0x3c;
        dsw_b_ = 0xf6;
        dsw_c_ = 0xc1;
    }

    text_.assign(288 * 288, kTransparent);
    background_.assign(size_t(kWorkWidth) * kWorkHeight, 0xff000000u);
    composite_.assign(size_t(kWorkWidth) * kWorkHeight, 0xff000000u);
    framebuffer_.assign(size_t(screen_width_) * size_t(screen_height_), 0xff000000u);
    palette_[0x401] = kShadowRgb;

    main_cpu_.set_memory_handlers(
        [this](uint16_t address) { return main_read(address); },
        [this](uint16_t address, uint8_t value) { main_write(address, value); });
    sub_cpu_.set_memory_handlers(
        [this](uint16_t address) { return sub_read(address); },
        [this](uint16_t address, uint8_t value) { sub_write(address, value); });
    sound_cpu_.set_memory_handlers(
        [this](uint16_t address) { return sound_read(address); },
        [this](uint16_t address, uint8_t value) { sound_write(address, value); });
    sound_cpu_.set_cycle_handler([this](int cycles) { on_sound_cycles(cycles); });

    ym0_.set_irq_handler([this](bool state) {
        if (state) {
            sound_status_ = uint8_t(sound_status_ | 0x01);
            sound_timer_enabled_ = true;
        }
    });
    ym1_.set_irq_handler([this](bool state) {
        if (state) {
            sound_status_ = uint8_t(sound_status_ | 0x02);
            sound_timer_enabled_ = true;
        }
    });
}

const char* Snk::title() const {
    switch (game_) {
        case Game::Ikari: return "Ikari Warriors";
        case Game::Athena: return "Athena";
        case Game::Tnk3: return "TNK III";
        case Game::Aso: return "ASO";
    }
    return "SNK";
}

bool Snk::init(const std::string& rom_path, std::string* error) {
    if (!load_roms(rom_path, error)) return false;
    reset();
    return true;
}

bool Snk::load_roms(const std::string& rom_path, std::string* error) {
    RomLoader loader;
    if (!loader.open(rom_path, error)) return false;

    auto load_into = [&](const std::vector<RomEntry>& entries, std::vector<uint8_t>& dest) {
        return loader.load(entries, dest, error);
    };

    if (is_ikari()) {
        std::vector<uint8_t> main_rom(0x10000, 0);
        std::vector<uint8_t> sub_rom(0x10000, 0);
        std::vector<uint8_t> snd_rom(0x10000, 0);
        std::vector<uint8_t> chars(0x4000, 0);
        std::vector<uint8_t> tiles(0x20000, 0);
        std::vector<uint8_t> sp16(0x18000, 0);
        std::vector<uint8_t> sp32(0x30000, 0);
        std::vector<uint8_t> prom(0xc00, 0);
        if (!load_into(kIkariMain, main_rom) || !load_into(kIkariSub, sub_rom) ||
            !load_into(kIkariSnd, snd_rom) || !load_into(kIkariChars, chars) ||
            !load_into(kIkariTiles, tiles) || !load_into(kIkariSp16, sp16) ||
            !load_into(kIkariSp32, sp32) || !load_into(kIkariProms, prom)) {
            return false;
        }
        std::copy(main_rom.begin(), main_rom.end(), memory_.begin());
        std::copy(sub_rom.begin(), sub_rom.end(), sub_memory_.begin());
        std::copy(snd_rom.begin(), snd_rom.end(), sound_memory_.begin());
        decode_ikari(chars, tiles, sp16, sp32);
        build_ikari_palette(prom);
    } else if (is_athena()) {
        std::vector<uint8_t> main_rom(0x10000, 0);
        std::vector<uint8_t> sub_rom(0x10000, 0);
        std::vector<uint8_t> snd_rom(0x10000, 0);
        std::vector<uint8_t> chars(0x4000, 0);
        std::vector<uint8_t> tiles(0x8000, 0);
        std::vector<uint8_t> sp16(0x18000, 0);
        std::vector<uint8_t> prom(0xc00, 0);
        if (!load_into(kAthenaMain, main_rom) || !load_into(kAthenaSub, sub_rom) ||
            !load_into(kAthenaSnd, snd_rom) || !load_into(kAthenaChars, chars) ||
            !load_into(kAthenaTiles, tiles) || !load_into(kAthenaSp16, sp16) ||
            !load_into(kAthenaProms, prom)) {
            return false;
        }
        std::copy(main_rom.begin(), main_rom.end(), memory_.begin());
        std::copy(sub_rom.begin(), sub_rom.end(), sub_memory_.begin());
        std::copy(snd_rom.begin(), snd_rom.end(), sound_memory_.begin());
        decode_tnk_family(chars, tiles, sp16, 0x200, 0x400, false);
        build_tnk_palette(prom);
    } else if (is_tnk3()) {
        std::vector<uint8_t> main_rom(0x10000, 0);
        std::vector<uint8_t> sub_rom(0x10000, 0);
        std::vector<uint8_t> snd_rom(0x10000, 0);
        std::vector<uint8_t> chars(0x2000, 0);
        std::vector<uint8_t> tiles(0x8000, 0);
        std::vector<uint8_t> sp16(0xc000, 0);
        std::vector<uint8_t> prom(0xc00, 0);
        if (!load_into(kTnk3Main, main_rom) || !load_into(kTnk3Sub, sub_rom) ||
            !load_into(kTnk3Snd, snd_rom) || !load_into(kTnk3Chars, chars) ||
            !load_into(kTnk3Tiles, tiles) || !load_into(kTnk3Sp16, sp16) ||
            !load_into(kTnk3Proms, prom)) {
            return false;
        }
        std::copy(main_rom.begin(), main_rom.end(), memory_.begin());
        std::copy(sub_rom.begin(), sub_rom.end(), sub_memory_.begin());
        std::copy(snd_rom.begin(), snd_rom.end(), sound_memory_.begin());
        decode_tnk_family(chars, tiles, sp16, 0x200, 0x200, true);
        build_tnk_palette(prom);
    } else {
        std::vector<uint8_t> main_rom(0x10000, 0);
        std::vector<uint8_t> sub_rom(0x10000, 0);
        std::vector<uint8_t> snd_rom(0x10000, 0);
        std::vector<uint8_t> chars(0x2000, 0);
        std::vector<uint8_t> tiles(0x8000, 0);
        std::vector<uint8_t> sp16(0x1c000, 0);
        std::vector<uint8_t> prom(0xc00, 0);
        if (!load_into(kAsoMain, main_rom) || !load_into(kAsoSub, sub_rom) ||
            !load_into(kAsoSnd, snd_rom) || !load_into(kAsoChars, chars) ||
            !load_into(kAsoTiles, tiles) || !load_into(kAsoSp16, sp16) ||
            !load_into(kAsoProms, prom)) {
            return false;
        }
        std::copy(main_rom.begin(), main_rom.end(), memory_.begin());
        std::copy(sub_rom.begin(), sub_rom.end(), sub_memory_.begin());
        std::copy(snd_rom.begin(), snd_rom.end(), sound_memory_.begin());
        std::memmove(sp16.data(), sp16.data() + 0x8000, 0x4000);
        std::memmove(sp16.data() + 0x8000, sp16.data() + 0x10000, 0x4000);
        std::memmove(sp16.data() + 0x10000, sp16.data() + 0x18000, 0x4000);
        sp16.resize(0x18000);
        decode_tnk_family(chars, tiles, sp16, 0x100, 0x400, true);
        build_tnk_palette(prom);
    }

    warnings_ = loader.warnings();
    return true;
}

void Snk::decode_ikari(const std::vector<uint8_t>& chars, const std::vector<uint8_t>& tiles,
                       const std::vector<uint8_t>& sp16, const std::vector<uint8_t>& sp32) {
    std::vector<int> char_x(kPbX.begin(), kPbX.begin() + 8);
    std::vector<int> char_y(kPcY.begin(), kPcY.begin() + 8);
    chars_.decode(packed_layout(8, 8, 0x200, 32 * 8, char_x, char_y, true), chars);

    tiles_.decode(packed_layout(16, 16, 0x400, 64 * 16, kPbX, kPbY, true), tiles);

    int plane16 = int(sp16.size() / 3);
    std::vector<int> sp16_x(kPsX.begin(), kPsX.begin() + 16);
    sprites16_.decode(sprite_layout(16, 16, 0x400, 16 * 16, plane16, sp16_x, kPsY, true), sp16);

    int plane32 = int(sp32.size() / 3);
    sprites32_.decode(sprite_layout(32, 32, 0x200, 16 * 32 * 2, plane32, kPsX, kPcY, true), sp32);
}

void Snk::decode_tnk_family(const std::vector<uint8_t>& chars, const std::vector<uint8_t>& tiles,
                            const std::vector<uint8_t>& sp16, int char_total, int sprite_total,
                            bool duplicate_chars) {
    std::vector<uint8_t> char_rom = chars;
    if (duplicate_chars) {
        char_rom.resize(0x4000, 0);
        std::memcpy(char_rom.data() + 0x2000, char_rom.data(), 0x2000);
    }
    std::vector<int> char_x(kPbX.begin(), kPbX.begin() + 8);
    std::vector<int> char_y(kPcY.begin(), kPcY.begin() + 8);
    chars_.decode(packed_layout(8, 8, char_total, 32 * 8, char_x, char_y, false), char_rom);

    std::vector<int> tile_y(kPcY.begin(), kPcY.begin() + 8);
    tiles_.decode(packed_layout(8, 8, 0x400, 32 * 8, char_x, tile_y, false), tiles);

    int plane16 = int(sp16.size() / 3);
    if (plane16 <= 0) plane16 = 0x8000;
    std::vector<int> sp16_x(kPsX.begin(), kPsX.begin() + 16);
    sprites16_.decode(sprite_layout(16, 16, sprite_total, 16 * 16, plane16, sp16_x, kPsY, false),
                      sp16);
}

void Snk::build_ikari_palette(const std::vector<uint8_t>& prom) {
    for (int index = 0; index < 0x400; index++) {
        uint8_t r = pal4bit(prom[size_t(index)]);
        uint8_t g = pal4bit(prom[size_t(index + 0x400)]);
        uint8_t b = pal4bit(prom[size_t(index + 0x800)]);
        palette_[size_t(index)] = 0xff000000u | (uint32_t(r) << 16) | (uint32_t(g) << 8) | b;
    }
    palette_[0x401] = kShadowRgb;
}

void Snk::build_tnk_palette(const std::vector<uint8_t>& prom) {
    for (int index = 0; index < 0x400; index++) {
        int bit0 = (prom[size_t(index + 0x800)] >> 3) & 1;
        int bit1 = (prom[size_t(index)] >> 1) & 1;
        int bit2 = (prom[size_t(index)] >> 2) & 1;
        int bit3 = (prom[size_t(index)] >> 3) & 1;
        uint8_t r = uint8_t(0x0e * bit0 + 0x1f * bit1 + 0x43 * bit2 + 0x8f * bit3);
        bit0 = (prom[size_t(index + 0x800)] >> 2) & 1;
        bit1 = (prom[size_t(index + 0x400)] >> 2) & 1;
        bit2 = (prom[size_t(index + 0x400)] >> 3) & 1;
        bit3 = (prom[size_t(index)] >> 0) & 1;
        uint8_t g = uint8_t(0x0e * bit0 + 0x1f * bit1 + 0x43 * bit2 + 0x8f * bit3);
        bit0 = (prom[size_t(index + 0x800)] >> 0) & 1;
        bit1 = (prom[size_t(index + 0x800)] >> 1) & 1;
        bit2 = (prom[size_t(index + 0x400)] >> 0) & 1;
        bit3 = (prom[size_t(index + 0x400)] >> 1) & 1;
        uint8_t b = uint8_t(0x0e * bit0 + 0x1f * bit1 + 0x43 * bit2 + 0x8f * bit3);
        palette_[size_t(index)] = 0xff000000u | (uint32_t(r) << 16) | (uint32_t(g) << 8) | b;
    }
    palette_[0x401] = kShadowRgb;
}

void Snk::reset() {
    main_cpu_.reset();
    sub_cpu_.reset();
    sound_cpu_.reset();
    ym0_.reset();
    ym1_.reset();
    txt_offset_ = 0;
    bg_offset_ = 0;
    bg_pal_offset_ = 0;
    sound_latch_ = 0;
    sound_status_ = 0;
    sound_timer_enabled_ = false;
    sound_timer_cycles_ = 0;
    hf_pos_x_ = 0;
    hf_pos_y_ = 0;
    rot_cont_ = 0;
    rot_nibble_ = 0xb0;
    sp16_scroll_x_ = 0;
    sp16_scroll_y_ = 0;
    sp32_scroll_x_ = 0;
    sp32_scroll_y_ = 0;
    scroll_x_ = 0;
    scroll_y_ = 0;
    flip_screen_ = false;
    audio_accumulator_ = 0;
    audio_.clear();
    dirty_txt_.fill(true);
    dirty_bg_.fill(true);
    std::fill(text_.begin(), text_.end(), kTransparent);

    if (is_ikari() || is_athena()) {
        in0_ = 0xfe;
        in1_ = 0xbf;
        in2_ = 0xbf;
    } else if (is_tnk3()) {
        in0_ = 0xdf;
        in1_ = 0x0f;
        in2_ = 0x0f;
    } else {
        in0_ = 0xdf;
        in1_ = 0xff;
        in2_ = 0xff;
    }
    in3_ = 0xff;
}

void Snk::write_sound_latch(uint8_t value) {
    sound_latch_ = value;
    sound_status_ = uint8_t(sound_status_ | 0x0c);
    sound_timer_enabled_ = true;
    sound_timer_cycles_ = 0;
}

void Snk::fire_sound_irq() {
    sound_timer_enabled_ = false;
    if ((sound_status_ & 0x0b) != 0) {
        sound_cpu_.set_irq(IrqLine::Assert);
    } else {
        sound_cpu_.set_irq(IrqLine::Clear);
    }
}

void Snk::on_sound_cycles(int cycles) {
    if (sound_timer_enabled_) {
        sound_timer_cycles_ += cycles;
        if (sound_timer_cycles_ >= kSoundTimerCycles) {
            sound_timer_cycles_ = 0;
            fire_sound_irq();
        }
    }

    audio_accumulator_ += int64_t(cycles) * YM3812::kSampleRate;
    while (audio_accumulator_ >= kSoundClock) {
        audio_accumulator_ -= kSoundClock;
        int32_t sample = ym0_.update();
        if (uses_two_ym()) sample += ym1_.update();
        audio_.push_back(int16_t(std::clamp(sample, -32768, 32767)));
    }
}

uint8_t Snk::hardflags_check(int index) const {
    const uint8_t* sr = &memory_[size_t(0xe800 + 4 * index)];
    uint16_t x = uint16_t(sr[2] + ((sr[3] & 0x80) << 1));
    uint16_t y = uint16_t(sr[0] + ((sr[3] & 0x10) << 4));
    uint16_t dx = uint16_t((x - hf_pos_x_) & 0x1ff);
    uint16_t dy = uint16_t((y - hf_pos_y_) & 0x1ff);
    if (dx > 0x20 && dx <= 0x1e0 && dy > 0x20 && dy <= 0x1e0) return 0;
    return 1;
}

uint8_t Snk::hardflags_check8(int first) const {
    uint8_t value = 0;
    for (int bit = 0; bit < 8; bit++) value = uint8_t(value | (hardflags_check(first + bit) << bit));
    return value;
}

void Snk::write_bg_byte(int offset, uint8_t value) {
    offset &= 0x1fff;
    if (bg_ram_[size_t(offset)] == value) return;
    bg_ram_[size_t(offset)] = value;
    if (is_aso()) {
        dirty_bg_[size_t(offset & 0xfff)] = true;
    } else {
        dirty_bg_[size_t((offset >> 1) & 0xfff)] = true;
    }
}

void Snk::write_txt_byte(int offset, uint8_t value) {
    offset &= 0x7ff;
    if (txt_ram_[size_t(offset)] == value) return;
    txt_ram_[size_t(offset)] = value;
    dirty_txt_[size_t(offset)] = true;
}

uint8_t Snk::main_read(uint16_t address) {
    if (is_ikari()) return ikari_main_read(address);
    if (is_aso()) return aso_main_read(address);
    if (is_tnk3()) return tnk3_main_read(address);
    return athena_main_read(address);
}

void Snk::main_write(uint16_t address, uint8_t value) {
    if (is_ikari()) {
        ikari_main_write(address, value);
    } else if (is_aso()) {
        aso_main_write(address, value);
    } else {
        athena_main_write(address, value);
    }
}

uint8_t Snk::sub_read(uint16_t address) {
    if (is_ikari()) return ikari_sub_read(address);
    if (is_aso()) return aso_sub_read(address);
    return athena_sub_read(address);
}

void Snk::sub_write(uint16_t address, uint8_t value) {
    if (is_ikari()) {
        ikari_sub_write(address, value);
    } else if (is_aso()) {
        aso_sub_write(address, value);
    } else {
        athena_sub_write(address, value);
    }
}

uint8_t Snk::sound_read(uint16_t address) {
    if (is_aso()) return aso_sound_read(address);
    if (is_tnk3()) return tnk3_sound_read(address);
    return ikari_sound_read(address);
}

void Snk::sound_write(uint16_t address, uint8_t value) {
    if (is_aso()) {
        aso_sound_write(address, value);
    } else if (is_tnk3()) {
        tnk3_sound_write(address, value);
    } else {
        ikari_sound_write(address, value);
    }
}

uint8_t Snk::ikari_main_read(uint16_t address) {
    if (address <= 0xbfff || (address >= 0xe000 && address <= 0xf7ff)) return memory_[address];
    switch (address) {
        case 0xc000: return uint8_t(in0_ | ((sound_status_ & 4) >> 2));
        case 0xc100: return in1_;
        case 0xc200: return in2_;
        case 0xc300: return in3_;
        case 0xc500: return uint8_t(dsw_a_ + (dsw_c_ & 0x04));
        case 0xc600: return uint8_t(dsw_b_ + (dsw_c_ & 0x30));
        case 0xc700:
            sub_cpu_.set_nmi(IrqLine::Assert);
            return 0xff;
        case 0xce00: return hardflags_check8(0);
        case 0xce20: return hardflags_check8(8);
        case 0xce40: return hardflags_check8(16);
        case 0xce60: return hardflags_check8(24);
        case 0xce80: return hardflags_check8(32);
        case 0xcea0: return hardflags_check8(40);
        case 0xcee0:
            return uint8_t((hardflags_check(48) << 0) | (hardflags_check(49) << 1) |
                           (hardflags_check(48) << 4) | (hardflags_check(49) << 5));
        default: break;
    }
    if (address >= 0xd000 && address <= 0xdfff) return bg_ram_[address & 0x7ff];
    if (address >= 0xf800) return txt_ram_[address & 0x7ff];
    return 0xff;
}

void Snk::ikari_main_write(uint16_t address, uint8_t value) {
    if (address <= 0xbfff) return;
    switch (address) {
        case 0xc400: write_sound_latch(value); return;
        case 0xc700: main_cpu_.set_nmi(IrqLine::Clear); return;
        case 0xc800: scroll_y_ = uint16_t((scroll_y_ & 0x100) | value); return;
        case 0xc880: scroll_x_ = uint16_t((scroll_x_ & 0x100) | value); return;
        case 0xc900:
            scroll_x_ = uint16_t((scroll_x_ & 0xff) | ((value & 0x02) << 7));
            scroll_y_ = uint16_t((scroll_y_ & 0xff) | ((value & 0x01) << 8));
            return;
        case 0xc980:
            txt_offset_ = uint16_t((value & 0x10) << 4);
            for (int i = 0; i < 0x400; i++) dirty_txt_[size_t(i)] = true;
            return;
        case 0xca00: sp16_scroll_y_ = uint16_t((sp16_scroll_y_ & 0x100) | value); return;
        case 0xca80: sp16_scroll_x_ = uint16_t((sp16_scroll_x_ & 0x100) | value); return;
        case 0xcb00: sp32_scroll_y_ = uint16_t((sp32_scroll_y_ & 0x100) | value); return;
        case 0xcb80: sp32_scroll_x_ = uint16_t((sp32_scroll_x_ & 0x100) | value); return;
        case 0xcc00: hf_pos_y_ = uint16_t((hf_pos_y_ & 0x100) | value); return;
        case 0xcc80: hf_pos_x_ = uint16_t((hf_pos_x_ & 0x100) | value); return;
        case 0xcd00:
            sp32_scroll_x_ = uint16_t((sp32_scroll_x_ & 0xff) | ((value & 0x20) << 3));
            sp16_scroll_x_ = uint16_t((sp16_scroll_x_ & 0xff) | ((value & 0x10) << 4));
            sp32_scroll_y_ = uint16_t((sp32_scroll_y_ & 0xff) | ((value & 0x08) << 5));
            sp16_scroll_y_ = uint16_t((sp16_scroll_y_ & 0xff) | ((value & 0x04) << 6));
            return;
        case 0xcd80:
            hf_pos_x_ = uint16_t((hf_pos_x_ & 0xff) | ((value & 0x80) << 1));
            hf_pos_y_ = uint16_t((hf_pos_y_ & 0xff) | ((value & 0x40) << 2));
            return;
        default: break;
    }
    if (address >= 0xd000 && address <= 0xdfff) {
        write_bg_byte(address & 0x7ff, value);
        return;
    }
    if (address >= 0xe000 && address <= 0xf7ff) {
        memory_[address] = value;
        return;
    }
    if (address >= 0xf800) write_txt_byte(address & 0x7ff, value);
}

uint8_t Snk::ikari_sub_read(uint16_t address) {
    if (address <= 0xbfff) return sub_memory_[address];
    if (address == 0xc000) {
        main_cpu_.set_nmi(IrqLine::Assert);
        return 0xff;
    }
    switch (address) {
        case 0xce00: return hardflags_check8(0);
        case 0xce20: return hardflags_check8(8);
        case 0xce40: return hardflags_check8(16);
        case 0xce60: return hardflags_check8(24);
        case 0xce80: return hardflags_check8(32);
        case 0xcea0: return hardflags_check8(40);
        case 0xcee0:
            return uint8_t((hardflags_check(48) << 0) | (hardflags_check(49) << 1) |
                           (hardflags_check(48) << 4) | (hardflags_check(49) << 5));
        default: break;
    }
    if (address >= 0xd000 && address <= 0xdfff) return bg_ram_[address & 0x7ff];
    if (address >= 0xe000 && address <= 0xf7ff) return memory_[address];
    if (address >= 0xf800) return txt_ram_[address & 0x7ff];
    return 0xff;
}

void Snk::ikari_sub_write(uint16_t address, uint8_t value) {
    if (address <= 0xbfff) return;
    if (address == 0xc000) {
        sub_cpu_.set_nmi(IrqLine::Clear);
        return;
    }
    if (address == 0xc980) {
        txt_offset_ = uint16_t((value & 0x10) << 4);
        for (int i = 0; i < 0x400; i++) dirty_txt_[size_t(i)] = true;
        return;
    }
    if (address == 0xcc00) {
        hf_pos_y_ = uint16_t((hf_pos_y_ & 0x100) | value);
        return;
    }
    if (address == 0xcc80) {
        hf_pos_x_ = uint16_t((hf_pos_x_ & 0x100) | value);
        return;
    }
    if (address == 0xcd80) {
        hf_pos_x_ = uint16_t((hf_pos_x_ & 0xff) | ((value & 0x80) << 1));
        hf_pos_y_ = uint16_t((hf_pos_y_ & 0xff) | ((value & 0x40) << 2));
        return;
    }
    if (address >= 0xd000 && address <= 0xdfff) {
        write_bg_byte(address & 0x7ff, value);
        return;
    }
    if (address >= 0xe000 && address <= 0xf7ff) {
        memory_[address] = value;
        return;
    }
    if (address >= 0xf800) write_txt_byte(address & 0x7ff, value);
}

uint8_t Snk::tnk3_main_read(uint16_t address) {
    if (address <= 0xbfff) return memory_[address];
    switch (address) {
        case 0xc000: return uint8_t(in0_ | ((sound_status_ & 4) << 3));
        case 0xc100: return in1_;
        case 0xc200: return in2_;
        case 0xc300: return in3_;
        case 0xc500: return uint8_t(dsw_a_ + (dsw_c_ & 0xc0));
        case 0xc600: return uint8_t(dsw_b_ + (dsw_c_ & 0x01));
        case 0xc700:
            sub_cpu_.set_nmi(IrqLine::Assert);
            return 0xff;
        default: break;
    }
    if (address >= 0xd000 && address <= 0xd7ff) return sprite_ram_[address & 0x7ff];
    if (address >= 0xd800 && address <= 0xf7ff) return bg_ram_[address - 0xd800];
    if (address >= 0xf800) return txt_ram_[address & 0x7ff];
    return 0xff;
}

uint8_t Snk::athena_main_read(uint16_t address) {
    if (address <= 0xbfff) return memory_[address];
    switch (address) {
        case 0xc000: return uint8_t(in0_ | ((sound_status_ & 4) >> 2));
        case 0xc100: return in1_;
        case 0xc200: return in2_;
        case 0xc500: return uint8_t(dsw_a_ + (dsw_c_ & 0x04));
        case 0xc600: return uint8_t(dsw_b_ + (dsw_c_ & 0x30));
        case 0xc700:
            sub_cpu_.set_nmi(IrqLine::Assert);
            return 0xff;
        default: break;
    }
    if (address >= 0xd000 && address <= 0xd7ff) return sprite_ram_[address & 0x7ff];
    if (address >= 0xd800 && address <= 0xf7ff) return bg_ram_[address - 0xd800];
    if (address >= 0xf800) return txt_ram_[address & 0x7ff];
    return 0xff;
}

void Snk::athena_main_write(uint16_t address, uint8_t value) {
    if (address <= 0xbfff) return;
    switch (address) {
        case 0xc400: write_sound_latch(value); return;
        case 0xc700: main_cpu_.set_nmi(IrqLine::Clear); return;
        case 0xc800:
            flip_screen_ = (value & 0x80) != 0;
            if (txt_offset_ != uint16_t((value & 0x40) << 2)) {
                txt_offset_ = uint16_t((value & 0x40) << 2);
                for (int i = 0; i < 0x400; i++) dirty_txt_[size_t(i)] = true;
            }
            scroll_y_ = uint16_t((scroll_y_ & 0xff) | ((value & 0x10) << 4));
            sp16_scroll_y_ = uint16_t((sp16_scroll_y_ & 0xff) | ((value & 0x08) << 5));
            scroll_x_ = uint16_t((scroll_x_ & 0xff) | ((value & 0x02) << 7));
            sp16_scroll_x_ = uint16_t((sp16_scroll_x_ & 0xff) | ((value & 0x01) << 8));
            return;
        case 0xc900: sp16_scroll_y_ = uint16_t((sp16_scroll_y_ & 0x100) | value); return;
        case 0xca00: sp16_scroll_x_ = uint16_t((sp16_scroll_x_ & 0x100) | value); return;
        case 0xcb00: scroll_y_ = uint16_t((scroll_y_ & 0x100) | value); return;
        case 0xcc00: scroll_x_ = uint16_t((scroll_x_ & 0x100) | value); return;
        default: break;
    }
    if (address >= 0xd000 && address <= 0xd7ff) {
        sprite_ram_[address & 0x7ff] = value;
        return;
    }
    if (address >= 0xd800 && address <= 0xf7ff) {
        write_bg_byte(address - 0xd800, value);
        return;
    }
    if (address >= 0xf800) write_txt_byte(address & 0x7ff, value);
}

uint8_t Snk::athena_sub_read(uint16_t address) {
    if (address <= 0xbfff) return sub_memory_[address];
    if (address == 0xc000 || address == 0xc700) {
        main_cpu_.set_nmi(IrqLine::Assert);
        return 0xff;
    }
    if (address >= 0xc800 && address <= 0xcfff) return sprite_ram_[address & 0x7ff];
    if (address >= 0xd000 && address <= 0xefff) return bg_ram_[address - 0xd000];
    if (address >= 0xf000 && address <= 0xf7ff) return sub_memory_[address];
    if (address >= 0xf800) return txt_ram_[address & 0x7ff];
    return 0xff;
}

void Snk::athena_sub_write(uint16_t address, uint8_t value) {
    if (address <= 0xbfff) return;
    if (address == 0xc000 || address == 0xc700) {
        sub_cpu_.set_nmi(IrqLine::Clear);
        return;
    }
    if (address >= 0xc800 && address <= 0xcfff) {
        sprite_ram_[address & 0x7ff] = value;
        return;
    }
    if (address >= 0xd000 && address <= 0xefff) {
        write_bg_byte(address - 0xd000, value);
        return;
    }
    if (address >= 0xf000 && address <= 0xf7ff) {
        sub_memory_[address] = value;
        return;
    }
    if (address >= 0xf800) write_txt_byte(address & 0x7ff, value);
}

uint8_t Snk::aso_main_read(uint16_t address) {
    if (address <= 0xbfff || (address >= 0xd800 && address <= 0xdfff)) return memory_[address];
    switch (address) {
        case 0xc000: return uint8_t(in0_ | ((sound_status_ & 4) << 3));
        case 0xc100: return in1_;
        case 0xc200: return in2_;
        case 0xc500: return uint8_t(dsw_a_ + (dsw_c_ & 0xc0));
        case 0xc600: return uint8_t(dsw_b_ + (dsw_c_ & 0x01));
        case 0xc700:
            sub_cpu_.set_nmi(IrqLine::Assert);
            return 0xff;
        default: break;
    }
    if (address >= 0xe000 && address <= 0xe7ff) return sprite_ram_[address & 0x7ff];
    if (address >= 0xe800 && address <= 0xf7ff) return bg_ram_[address - 0xe800];
    if (address >= 0xf800) return txt_ram_[address & 0x7ff];
    return 0xff;
}

void Snk::aso_main_write(uint16_t address, uint8_t value) {
    if (address <= 0xbfff) return;
    switch (address) {
        case 0xc400: write_sound_latch(value); return;
        case 0xc700: main_cpu_.set_nmi(IrqLine::Clear); return;
        case 0xc800:
            flip_screen_ = (value & 0x20) != 0;
            scroll_y_ = uint16_t((scroll_y_ & 0xff) | ((value & 0x10) << 4));
            sp16_scroll_y_ = uint16_t((sp16_scroll_y_ & 0xff) | ((value & 0x08) << 5));
            scroll_x_ = uint16_t((scroll_x_ & 0xff) | ((value & 0x02) << 7));
            sp16_scroll_x_ = uint16_t((sp16_scroll_x_ & 0xff) | ((value & 0x01) << 8));
            return;
        case 0xc900: sp16_scroll_y_ = uint16_t((sp16_scroll_y_ & 0x100) | value); return;
        case 0xca00: sp16_scroll_x_ = uint16_t((sp16_scroll_x_ & 0x100) | value); return;
        case 0xcb00: scroll_y_ = uint16_t((scroll_y_ & 0x100) | value); return;
        case 0xcc00: scroll_x_ = uint16_t((scroll_x_ & 0x100) | value); return;
        case 0xcf00: {
            uint16_t pal = uint16_t(((value & 0x0f) ^ 8) << 4);
            uint16_t bank = uint16_t((value & 0x30) << 4);
            if (bg_pal_offset_ != pal || bg_offset_ != bank) {
                bg_pal_offset_ = pal;
                bg_offset_ = bank;
                dirty_bg_.fill(true);
            }
            return;
        }
        default: break;
    }
    if (address >= 0xd800 && address <= 0xdfff) {
        memory_[address] = value;
        return;
    }
    if (address >= 0xe000 && address <= 0xe7ff) {
        sprite_ram_[address & 0x7ff] = value;
        return;
    }
    if (address >= 0xe800 && address <= 0xf7ff) {
        write_bg_byte(address - 0xe800, value);
        return;
    }
    if (address >= 0xf800) write_txt_byte(address & 0x7ff, value);
}

uint8_t Snk::aso_sub_read(uint16_t address) {
    if (address <= 0xbfff) return sub_memory_[address];
    if (address == 0xc000) {
        main_cpu_.set_nmi(IrqLine::Assert);
        return 0xff;
    }
    if (address >= 0xc800 && address <= 0xcfff) return memory_[address + 0x1000];
    if (address >= 0xd000 && address <= 0xd7ff) return sprite_ram_[address & 0x7ff];
    if (address >= 0xd800 && address <= 0xe7ff) return bg_ram_[address - 0xd800];
    if (address >= 0xf800) return txt_ram_[address & 0x7ff];
    return 0xff;
}

void Snk::aso_sub_write(uint16_t address, uint8_t value) {
    if (address <= 0xbfff) return;
    if (address == 0xc000) {
        sub_cpu_.set_nmi(IrqLine::Clear);
        return;
    }
    if (address >= 0xc800 && address <= 0xcfff) {
        memory_[address + 0x1000] = value;
        return;
    }
    if (address >= 0xd000 && address <= 0xd7ff) {
        sprite_ram_[address & 0x7ff] = value;
        return;
    }
    if (address >= 0xd800 && address <= 0xe7ff) {
        write_bg_byte(address - 0xd800, value);
        return;
    }
    if (address >= 0xf800) write_txt_byte(address & 0x7ff, value);
}

uint8_t Snk::ikari_sound_read(uint16_t address) {
    if (address <= 0xcfff) return sound_memory_[address];
    switch (address) {
        case 0xe000: return sound_latch_;
        case 0xe800: return ym0_.status();
        case 0xf000: return ym1_.status();
        case 0xf800: return sound_status_;
        default: return 0xff;
    }
}

void Snk::ikari_sound_write(uint16_t address, uint8_t value) {
    if (address <= 0xbfff) return;
    if (address >= 0xc000 && address <= 0xcfff) {
        sound_memory_[address] = value;
        return;
    }
    switch (address) {
        case 0xe800: ym0_.control(value); return;
        case 0xec00: ym0_.write(value); return;
        case 0xf000: ym1_.control(value); return;
        case 0xf400: ym1_.write(value); return;
        case 0xf800:
            if ((value & 0x10) == 0) sound_status_ = uint8_t(sound_status_ & 0xfe);
            if ((value & 0x20) == 0) sound_status_ = uint8_t(sound_status_ & 0xfd);
            if ((value & 0x40) == 0) sound_status_ = uint8_t(sound_status_ & 0xfb);
            if ((value & 0x80) == 0) sound_status_ = uint8_t(sound_status_ & 0xf7);
            sound_timer_enabled_ = true;
            return;
        default: return;
    }
}

uint8_t Snk::tnk3_sound_read(uint16_t address) {
    if (address <= 0x87ff) return sound_memory_[address];
    switch (address) {
        case 0xa000: return sound_latch_;
        case 0xc000:
            sound_latch_ = 0;
            sound_status_ = uint8_t(sound_status_ & 0xfb);
            sound_timer_enabled_ = true;
            return 0xff;
        case 0xe000: return ym0_.status();
        case 0xe004:
            sound_status_ = uint8_t(sound_status_ & 0xf7);
            sound_timer_enabled_ = true;
            return 0xff;
        case 0xe006:
            sound_status_ = uint8_t(sound_status_ & 0xfe);
            sound_timer_enabled_ = true;
            return 0xff;
        default: return 0xff;
    }
}

void Snk::tnk3_sound_write(uint16_t address, uint8_t value) {
    if (address <= 0x7fff) return;
    if (address >= 0x8000 && address <= 0x87ff) {
        sound_memory_[address] = value;
        return;
    }
    if (address == 0xe000) ym0_.control(value);
    if (address == 0xe001) ym0_.write(value);
}

uint8_t Snk::aso_sound_read(uint16_t address) {
    if (address <= 0xc7ff) return sound_memory_[address];
    switch (address) {
        case 0xd000: return sound_latch_;
        case 0xe000:
            sound_latch_ = 0;
            sound_status_ = uint8_t(sound_status_ & 0xfb);
            sound_timer_enabled_ = true;
            return 0xff;
        case 0xf000: return ym0_.status();
        case 0xf004:
            sound_status_ = uint8_t(sound_status_ & 0xf7);
            sound_timer_enabled_ = true;
            return 0xff;
        case 0xf006:
            sound_status_ = uint8_t(sound_status_ & 0xfe);
            sound_timer_enabled_ = true;
            return 0xff;
        default: return 0xff;
    }
}

void Snk::aso_sound_write(uint16_t address, uint8_t value) {
    if (address <= 0xbfff) return;
    if (address >= 0xc000 && address <= 0xc7ff) {
        sound_memory_[address] = value;
        return;
    }
    if (address == 0xf000) ym0_.control(value);
    if (address == 0xf001) ym0_.write(value);
}

void Snk::put_tile(const GfxSet& gfx, std::vector<uint32_t>& dest, int dest_w, int x, int y, int code,
                   int color, bool transparent, int trans_pen) {
    const uint8_t* pixels = gfx.element(code);
    int width = gfx.width();
    int height = gfx.height();
    for (int row = 0; row < height; row++) {
        int dy = y + row;
        if (dy < 0 || dy * dest_w >= int(dest.size())) continue;
        for (int column = 0; column < width; column++) {
            int dx = x + column;
            if (dx < 0 || dx >= dest_w) continue;
            uint8_t pen = pixels[row * width + column];
            if (transparent && pen == trans_pen) {
                dest[size_t(dy * dest_w + dx)] = kTransparent;
                continue;
            }
            dest[size_t(dy * dest_w + dx)] = palette_[size_t(color + pen)];
        }
    }
}

void Snk::blit_scrolled_bg(int scroll_x, int scroll_y) {
    for (int y = 0; y < kWorkHeight; y++) {
        int sy = (y + scroll_y) & 0x1ff;
        for (int x = 0; x < kWorkWidth; x++) {
            int sx = (x + scroll_x) & 0x1ff;
            composite_[size_t(y * kWorkWidth + x)] = background_[size_t(sy * kWorkWidth + sx)];
        }
    }
}

void Snk::blit_text(int width, int height) {
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            uint32_t pixel = text_[size_t(y * 288 + x)];
            if (pixel == kTransparent) continue;
            composite_[size_t(y * kWorkWidth + x)] = pixel;
        }
    }
}

void Snk::blit_sprite_shadow(const GfxSet& gfx, int code, int color, bool flip_x, bool flip_y,
                             int dest_x, int dest_y, int wrap) {
    const uint8_t* pixels = gfx.element(code);
    int width = gfx.width();
    int height = gfx.height();
    for (int row = 0; row < height; row++) {
        int sy = flip_y ? (height - 1 - row) : row;
        int y = (dest_y + row) & wrap;
        for (int column = 0; column < width; column++) {
            int sx = flip_x ? (width - 1 - column) : column;
            uint8_t pen = pixels[sy * width + sx];
            if (pen == 7) continue;
            int x = (dest_x + column) & wrap;
            if (pen == 6) {
                composite_[size_t(y * kWorkWidth + x)] = kShadowRgb;
            } else {
                composite_[size_t(y * kWorkWidth + x)] = palette_[size_t(color + pen)];
            }
        }
    }
}

void Snk::draw_text_ikari() {
    for (int f = 0; f < 28; f++) {
        for (int g = 0; g < 36; g++) {
            int col = g - 2;
            int pos = (col & 0x20) != 0 ? 0x400 + f + ((col & 0x1f) << 5) : f + (col << 5);
            if (!dirty_txt_[size_t(pos & 0x7ff)]) continue;
            int nchar = txt_ram_[size_t(pos & 0x7ff)];
            bool opaque = (pos & 0x400) != 0;
            put_tile(chars_, text_, 288, f * 8, (35 - g) * 8, int(txt_offset_) + nchar, 0x180,
                     !opaque, 15);
            dirty_txt_[size_t(pos & 0x7ff)] = false;
        }
    }
}

void Snk::draw_text_tnk3(bool use_txt_offset) {
    for (int f = 0; f < 28; f++) {
        for (int g = 0; g < 36; g++) {
            int col = g - 2;
            int pos = (col & 0x20) != 0 ? 0x400 + f + ((col & 0x1f) << 5) : f + (col << 5);
            if (!dirty_txt_[size_t(pos & 0x7ff)]) continue;
            int nchar = txt_ram_[size_t(pos & 0x7ff)];
            int color = (nchar & 0xe0) >> 1;
            bool opaque = (pos & 0x400) != 0;
            int code = use_txt_offset ? int(txt_offset_) + nchar : nchar;
            int pal = opaque ? 0x180 : 0x180 + color;
            put_tile(chars_, text_, 288, g * 8, f * 8, code, pal, !opaque, 15);
            dirty_txt_[size_t(pos & 0x7ff)] = false;
        }
    }
}

void Snk::draw_bg_ikari() {
    for (int f = 0; f < 0x400; f++) {
        if (!dirty_bg_[size_t(f)]) continue;
        uint8_t atrib = bg_ram_[size_t(1 + f * 2)];
        int color = atrib & 0x70;
        int x = f % 32;
        int y = f / 32;
        int nchar = bg_ram_[size_t(f * 2)] + ((atrib & 0x03) << 8);
        put_tile(tiles_, background_, kWorkWidth, x * 16, (31 - y) * 16, nchar, color + 0x100, false,
                 15);
        dirty_bg_[size_t(f)] = false;
    }
}

void Snk::draw_bg_tnk3() {
    for (int f = 0; f < 0x1000; f++) {
        if (!dirty_bg_[size_t(f)]) continue;
        uint8_t atrib = bg_ram_[size_t(1 + f * 2)];
        int color = ((atrib & 0x0f) ^ 8) << 4;
        int x = f / 64;
        int y = f % 64;
        int nchar = bg_ram_[size_t(f * 2)] + ((atrib & 0x30) << 4);
        put_tile(tiles_, background_, kWorkWidth, x * 8, y * 8, nchar, color + 0x80, false, 15);
        dirty_bg_[size_t(f)] = false;
    }
}

void Snk::draw_bg_aso() {
    for (int f = 0; f < 0x1000; f++) {
        if (!dirty_bg_[size_t(f)]) continue;
        int x = f / 64;
        int y = f % 64;
        int nchar = int(bg_offset_) + bg_ram_[size_t(f)];
        put_tile(tiles_, background_, kWorkWidth, x * 8, y * 8, nchar, 0x80 + int(bg_pal_offset_),
                 false, 15);
        dirty_bg_[size_t(f)] = false;
    }
}

void Snk::draw_sprites16_ikari(int bank) {
    int base = 0xe800 + bank * 25 * 4;
    for (int f = 0; f < 25; f++) {
        uint8_t atrib = memory_[size_t(base + f * 4 + 3)];
        int nchar = memory_[size_t(base + f * 4 + 1)] | ((atrib & 0x60) << 3);
        int color = atrib & 0x0f;
        int sx = int(sp16_scroll_x_) + 300 - 16 - memory_[size_t(base + f * 4 + 2)];
        int sy = -int(sp16_scroll_y_) + 7 - 16 - 8 + memory_[size_t(base + f * 4 + 0)];
        sx += (atrib & 0x80) << 1;
        sy += (atrib & 0x10) << 4;
        sx &= 0x1ff;
        sy &= 0x1ff;
        blit_sprite_shadow(sprites16_, nchar, color << 3, false, false, sy, (272 - sx) & 0x1ff,
                           0x1ff);
    }
}

void Snk::draw_sprites32_ikari() {
    for (int f = 0; f < 25; f++) {
        uint8_t atrib = memory_[size_t(0xe000 + f * 4 + 3)];
        int nchar = memory_[size_t(0xe000 + f * 4 + 1)] | ((atrib & 0x40) << 2);
        int color = atrib & 0x0f;
        int sx = int(sp32_scroll_x_) + 300 - 32 - memory_[size_t(0xe000 + f * 4 + 2)];
        int sy = -int(sp32_scroll_y_) + 7 - 32 - 8 + memory_[size_t(0xe000 + f * 4 + 0)];
        sx += (atrib & 0x80) << 1;
        sy += (atrib & 0x10) << 4;
        sx &= 0x1ff;
        sy &= 0x1ff;
        blit_sprite_shadow(sprites32_, nchar, (color << 3) + 0x80, false, false, sy,
                           (256 - sx) & 0x1ff, 0x1ff);
    }
}

void Snk::draw_sprites_athena() {
    for (int f = 0; f < 50; f++) {
        uint8_t atrib = sprite_ram_[size_t(f * 4 + 3)];
        int nchar = sprite_ram_[size_t(f * 4 + 1)] | ((atrib & 0x40) << 2) | ((atrib & 0x20) << 4);
        int color = atrib & 0x0f;
        int sx = int(sp16_scroll_x_) + 301 - 16 - sprite_ram_[size_t(f * 4 + 2)];
        int sy = -int(sp16_scroll_y_) + 7 - 24 + sprite_ram_[size_t(f * 4 + 0)];
        sx += (atrib & 0x80) << 1;
        sy += (atrib & 0x10) << 4;
        sx &= 0x1ff;
        sy &= 0x1ff;
        blit_sprite_shadow(sprites16_, nchar, color << 3, false, false, sx, sy, 0x1ff);
    }
}

void Snk::draw_sprites_tnk3() {
    for (int f = 0; f < 50; f++) {
        uint8_t atrib = sprite_ram_[size_t(f * 4 + 3)];
        int nchar = sprite_ram_[size_t(f * 4 + 1)] | ((atrib & 0x40) << 2);
        int color = atrib & 0x0f;
        int sx = int(sp16_scroll_x_) + 301 - 16 - sprite_ram_[size_t(f * 4 + 2)];
        int sy = -int(sp16_scroll_y_) + 7 - 24 + sprite_ram_[size_t(f * 4 + 0)];
        sx += (atrib & 0x80) << 1;
        sy += (atrib & 0x10) << 4;
        sx &= 0x1ff;
        sy &= 0x1ff;
        blit_sprite_shadow(sprites16_, nchar, color << 3, false, (atrib & 0x20) != 0, sx, sy, 0x1ff);
    }
}

void Snk::copy_final(int src_w, int src_h) {
    if (rotate_final()) {
        // rot270 in actualiza_trozo_final: dest[sy, 287-sx] with swapped window 216x288.
        for (int sy = 0; sy < src_h; sy++) {
            for (int sx = 0; sx < src_w; sx++) {
                uint32_t pixel = composite_[size_t(sy * kWorkWidth + sx)];
                int dx = sy;
                int dy = (src_w - 1) - sx;
                if (flip_screen_) {
                    dx = screen_width_ - 1 - dx;
                    dy = screen_height_ - 1 - dy;
                }
                if (dx >= 0 && dx < screen_width_ && dy >= 0 && dy < screen_height_) {
                    framebuffer_[size_t(dy * screen_width_ + dx)] = pixel;
                }
            }
        }
        return;
    }

    for (int y = 0; y < src_h; y++) {
        for (int x = 0; x < src_w; x++) {
            uint32_t pixel = composite_[size_t(y * kWorkWidth + x)];
            int dx = x;
            int dy = y;
            if (flip_screen_) {
                dx = src_w - 1 - x;
                dy = src_h - 1 - y;
            }
            if (dx >= 0 && dx < screen_width_ && dy >= 0 && dy < screen_height_) {
                framebuffer_[size_t(dy * screen_width_ + dx)] = pixel;
            }
        }
    }
}

void Snk::update_video_ikari() {
    draw_text_ikari();
    draw_bg_ikari();
    blit_scrolled_bg(scroll_y_, 239 - int(scroll_x_));
    draw_sprites16_ikari(0);
    draw_sprites32_ikari();
    draw_sprites16_ikari(1);
    blit_text(224, 288);
    copy_final(216, 288);
}

void Snk::update_video_tnk3() {
    draw_text_tnk3(true);
    draw_bg_tnk3();
    blit_scrolled_bg(int(scroll_x_) - 16, int(scroll_y_));
    if (is_athena()) {
        draw_sprites_athena();
    } else {
        draw_sprites_tnk3();
    }
    blit_text(288, 224);
    copy_final(288, 216);
}

void Snk::update_video_aso() {
    draw_text_tnk3(false);
    draw_bg_aso();
    blit_scrolled_bg(int(scroll_x_) - 16 + 256, int(scroll_y_));
    draw_sprites_tnk3();
    blit_text(288, 224);
    copy_final(288, 216);
}

void Snk::update_video() {
    if (is_ikari()) {
        update_video_ikari();
    } else if (is_aso()) {
        update_video_aso();
    } else {
        update_video_tnk3();
    }
}

void Snk::run_frame() {
    const int main_slice = int(kMainClock / kFramesPerSecond / (kScanlines * kCpuSync));
    const int sub_slice = int(kSubClock / kFramesPerSecond / (kScanlines * kCpuSync));
    const int sound_slice = int(kSoundClock / kFramesPerSecond / (kScanlines * kCpuSync));

    for (int line = 0; line < kScanlines; line++) {
        if (line == 0) {
            main_cpu_.set_irq(IrqLine::Hold);
            sub_cpu_.set_irq(IrqLine::Hold);
            update_video();
        }
        for (int slice = 0; slice < kCpuSync; slice++) {
            main_cpu_.run(main_slice);
            sub_cpu_.run(sub_slice);
            sound_cpu_.run(sound_slice);
        }
    }
}

void Snk::set_inputs(const MachineInputs& inputs) {
    const InputState& p1 = inputs.player1;
    const InputState& p2 = inputs.player2;

    auto step_rotary = [this](bool increase) {
        rot_cont_ = uint8_t(rot_cont_ + 1);
        if (rot_cont_ != 0x0f) return;
        rot_cont_ = 0;
        if (increase) {
            rot_nibble_ = uint8_t(rot_nibble_ + 0x10);
            if ((rot_nibble_ & 0xf0) == 0xc0) rot_nibble_ = 0;
        } else {
            rot_nibble_ = uint8_t(rot_nibble_ - 0x10);
            if ((rot_nibble_ & 0xf0) == 0xf0) rot_nibble_ = 0xb0;
        }
    };

    if (is_ikari()) {
        in0_ = 0xfe;
        in1_ = 0x0f;
        in2_ = 0xbf;
        in3_ = 0x0f;
        if (inputs.coin2) in0_ &= 0xef;
        if (inputs.coin1) in0_ &= 0xdf;
        if (p2.start) in0_ &= 0xbf;
        if (p1.start) in0_ &= 0x7f;
        if (p1.up) in1_ &= 0xfe;
        if (p1.down) in1_ &= 0xfd;
        if (p1.left) in1_ &= 0xfb;
        if (p1.right) in1_ &= 0xf7;
        if (p1.button2) step_rotary(true);
        if (p1.button3) step_rotary(false);
        if (p2.up) in2_ &= 0xfe;
        if (p2.down) in2_ &= 0xfd;
        if (p2.left) in2_ &= 0xfb;
        if (p2.right) in2_ &= 0xf7;
        if (p2.button2) step_rotary(true);
        if (p2.button3) step_rotary(false);
        in1_ = uint8_t((in1_ & 0x0f) | (rot_nibble_ & 0xf0));
        if (p1.button1) in3_ &= 0xfe;
        if (p1.button2) in3_ &= 0xfd;
        if (p2.button1) in3_ &= 0xf7;
        if (p2.button2) in3_ &= 0xef;
        return;
    }

    if (is_athena()) {
        in0_ = 0xfe;
        in1_ = 0xbf;
        in2_ = 0xbf;
        if (inputs.coin2) in0_ &= 0xef;
        if (inputs.coin1) in0_ &= 0xdf;
        if (p2.start) in0_ &= 0xbf;
        if (p1.start) in0_ &= 0x7f;
        if (p1.up) in1_ &= 0xfe;
        if (p1.down) in1_ &= 0xfd;
        if (p1.left) in1_ &= 0xfb;
        if (p1.right) in1_ &= 0xf7;
        if (p1.button2) in1_ &= 0xef;
        if (p1.button1) in1_ &= 0xdf;
        if (p2.up) in2_ &= 0xfe;
        if (p2.down) in2_ &= 0xfd;
        if (p2.left) in2_ &= 0xfb;
        if (p2.right) in2_ &= 0xf7;
        if (p2.button2) in2_ &= 0xef;
        if (p2.button1) in2_ &= 0xdf;
        return;
    }

    if (is_tnk3()) {
        in0_ = 0xdf;
        in1_ = 0x0f;
        in2_ = 0x0f;
        in3_ = 0xff;
        if (inputs.coin1) in0_ &= 0xfe;
        if (p1.start) in0_ &= 0xf7;
        if (p2.start) in0_ &= 0xef;
        if (p1.up) in1_ &= 0xfe;
        if (p1.down) in1_ &= 0xfd;
        if (p1.left) in1_ &= 0xfb;
        if (p1.right) in1_ &= 0xf7;
        if (p1.button2) step_rotary(true);
        if (p1.button3) step_rotary(false);
        if (p2.up) in2_ &= 0xfe;
        if (p2.down) in2_ &= 0xfd;
        if (p2.left) in2_ &= 0xfb;
        if (p2.right) in2_ &= 0xf7;
        if (p2.button2) step_rotary(true);
        if (p2.button3) step_rotary(false);
        in1_ = uint8_t((in1_ & 0x0f) | (rot_nibble_ & 0xf0));
        if (p1.button2) in3_ &= 0xfe;
        if (p1.button1) in3_ &= 0xfd;
        if (p2.button2) in3_ &= 0xf7;
        if (p2.button1) in3_ &= 0xef;
        return;
    }

    in0_ = 0xdf;
    in1_ = 0xff;
    in2_ = 0xff;
    if (inputs.coin1) in0_ &= 0xfe;
    if (p1.start) in0_ &= 0xf7;
    if (p2.start) in0_ &= 0xef;
    if (p1.up) in1_ &= 0xfe;
    if (p1.down) in1_ &= 0xfd;
    if (p1.left) in1_ &= 0xfb;
    if (p1.right) in1_ &= 0xf7;
    if (p1.button3) in1_ &= 0xef;
    if (p1.button1) in1_ &= 0xdf;
    if (p1.button2) in1_ &= 0xbf;
    if (p2.up) in2_ &= 0xfe;
    if (p2.down) in2_ &= 0xfd;
    if (p2.left) in2_ &= 0xfb;
    if (p2.right) in2_ &= 0xf7;
    if (p2.button3) in2_ &= 0xef;
    if (p2.button1) in2_ &= 0xdf;
    if (p2.button2) in2_ &= 0xbf;
}

void Snk::set_dip_switch(int bank, uint8_t value) {
    if (bank == 0) dsw_a_ = value;
    if (bank == 1) dsw_b_ = value;
    if (bank == 2) dsw_c_ = value;
}

void Snk::drain_audio(std::vector<int16_t>& out) {
    out.insert(out.end(), audio_.begin(), audio_.end());
    audio_.clear();
}

}  // namespace dsp
