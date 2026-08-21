#include "drivers/sega_system1.h"

#include <algorithm>
#include <cmath>

#include "core/rom_loader.h"
#include "machine/sega_decrypt.h"

namespace dsp {
namespace {

// ---------------------------------------------------------------------------
// ROM sets. Filenames/sizes/offsets/CRCs only -- standard driver metadata
// (the same information any emulator's driver source publishes), not game
// content. The person supplies their own legally dumped ROM files.
// ---------------------------------------------------------------------------

const std::vector<RomEntry> kPitfall2Main = {
    {"epr-6456a.116", 0x4000, 0x0000, 0xbcc8406b},
    {"epr-6457a.109", 0x4000, 0x4000, 0xa016fd2a},
    {"epr-6458a.96", 0x4000, 0x8000, 0x5c30b3e8},
};
const std::vector<RomEntry> kPitfall2Char = {
    {"epr-6474a.62", 0x2000, 0x0000, 0x9f1711b9}, {"epr-6473a.61", 0x2000, 0x2000, 0x8e53b8dd},
    {"epr-6472a.64", 0x2000, 0x4000, 0xe0f34a11}, {"epr-6471a.63", 0x2000, 0x6000, 0xd5bc805c},
    {"epr-6470a.66", 0x2000, 0x8000, 0x1439729f}, {"epr-6469a.65", 0x2000, 0xa000, 0xe4ac6921},
};
const std::vector<RomEntry> kPitfall2Sprites = {
    {"epr-6454a.117", 0x4000, 0x0000, 0xa5d96780},
    {"epr-6455.05", 0x4000, 0x4000, 0x32ee64a1},
};
const std::vector<RomEntry> kPitfall2Sound = {{"epr-6462.120", 0x2000, 0, 0x86bb9185}};
const std::vector<RomEntry> kPitfall2VideoProm = {{"pr-5317.76", 0x100, 0, 0x648350b8}};

const std::vector<RomEntry> kTeddyMain = {
    {"epr-6768.116", 0x4000, 0x0000, 0x5939817e},
    {"epr-6769.109", 0x4000, 0x4000, 0x14a98ddd},
    {"epr-6770.96", 0x4000, 0x8000, 0x67b0c7c2},
};
const std::vector<RomEntry> kTeddyChar = {
    {"epr-6747.62", 0x2000, 0x0000, 0xa0e5aca7}, {"epr-6746.61", 0x2000, 0x2000, 0xcdb77e51},
    {"epr-6745.64", 0x2000, 0x4000, 0x0cab75c3}, {"epr-6744.63", 0x2000, 0x6000, 0x0ef8d2cd},
    {"epr-6743.66", 0x2000, 0x8000, 0xc33062b5}, {"epr-6742.65", 0x2000, 0xa000, 0xc457e8c5},
};
const std::vector<RomEntry> kTeddySprites = {
    {"epr-6735.117", 0x4000, 0x0000, 0x1be35a97}, {"epr-6737.04", 0x4000, 0x4000, 0x6b53aa7a},
    {"epr-6736.110", 0x4000, 0x8000, 0x565c25d0}, {"epr-6738.05", 0x4000, 0xc000, 0xe116285f},
};
const std::vector<RomEntry> kTeddySound = {{"epr6748x.120", 0x2000, 0, 0xc2a1b89d}};
const std::vector<RomEntry> kTeddyVideoProm = {{"pr-5317.76", 0x100, 0, 0x648350b8}};

const std::vector<RomEntry> kWBoyMain = {
    {"epr-7489.116", 0x4000, 0x0000, 0x130f4b70},
    {"epr-7490.109", 0x4000, 0x4000, 0x9e656733},
    {"epr-7491.96", 0x4000, 0x8000, 0x1f7d0efe},
};
const std::vector<RomEntry> kWBoyChar = {
    {"epr-7497.62", 0x2000, 0x0000, 0x08d609ca}, {"epr-7496.61", 0x2000, 0x2000, 0x6f61fdf1},
    {"epr-7495.64", 0x2000, 0x4000, 0x6a0d2c2d}, {"epr-7494.63", 0x2000, 0x6000, 0xa8e281c7},
    {"epr-7493.66", 0x2000, 0x8000, 0x89305df4}, {"epr-7492.65", 0x2000, 0xa000, 0x60f806b1},
};
const std::vector<RomEntry> kWBoySprites = {
    {"epr-7485.117", 0x4000, 0x0000, 0xc2891722}, {"epr-7487.04", 0x4000, 0x4000, 0x2d3a421b},
    {"epr-7486.110", 0x4000, 0x8000, 0x8d622c50}, {"epr-7488.05", 0x4000, 0xc000, 0x007c2f1b},
};
const std::vector<RomEntry> kWBoySound = {{"epr-7498.120", 0x2000, 0, 0x78ae1e7b}};
const std::vector<RomEntry> kWBoyVideoProm = {{"pr-5317.76", 0x100, 0, 0x648350b8}};

const std::vector<RomEntry> kMrVikingMain = {
    {"epr-5873.129", 0x2000, 0x0000, 0x14d21624}, {"epr-5874.130", 0x2000, 0x2000, 0x6df7de87},
    {"epr-5875.131", 0x2000, 0x4000, 0xac226100}, {"epr-5876.132", 0x2000, 0x6000, 0xe77db1dc},
    {"epr-5755.133", 0x2000, 0x8000, 0xedd62ae1}, {"epr-5756.134", 0x2000, 0xa000, 0x11974040},
};
const std::vector<RomEntry> kMrVikingSprites = {
    {"epr-5749.86", 0x4000, 0x0000, 0xe24682cd},
    {"epr-5750.93", 0x4000, 0x4000, 0x6564d1ad},
};
const std::vector<RomEntry> kMrVikingSound = {{"epr-5763.3", 0x2000, 0, 0xd712280d}};
const std::vector<RomEntry> kMrVikingVideoProm = {{"pr-5317.106", 0x100, 0, 0x648350b8}};
const std::vector<RomEntry> kMrVikingChar = {
    {"epr-5762.82", 0x2000, 0x0000, 0x4a91d08a}, {"epr-5761.65", 0x2000, 0x2000, 0xf7d61b65},
    {"epr-5760.81", 0x2000, 0x4000, 0x95045820}, {"epr-5759.64", 0x2000, 0x6000, 0x5f9bae4e},
    {"epr-5758.80", 0x2000, 0x8000, 0x808ee706}, {"epr-5757.63", 0x2000, 0xa000, 0x480f7074},
};

const std::vector<RomEntry> kSegaNinjaMain = {
    {"epr-.116", 0x4000, 0x0000, 0xa5d0c9d0},
    {"epr-.109", 0x4000, 0x4000, 0xb9e6775c},
    {"epr-6552.96", 0x4000, 0x8000, 0xf2eeb0d8},
};
const std::vector<RomEntry> kSegaNinjaSprites = {
    {"epr-6546.117", 0x4000, 0x0000, 0xa4785692}, {"epr-6548.04", 0x4000, 0x4000, 0xbdf278c1},
    {"epr-6547.110", 0x4000, 0x8000, 0x34451b08}, {"epr-6549.05", 0x4000, 0xc000, 0xd2057668},
};
const std::vector<RomEntry> kSegaNinjaSound = {{"epr-6559.120", 0x2000, 0, 0x5a1570ee}};
const std::vector<RomEntry> kSegaNinjaVideoProm = {{"pr-5317.76", 0x100, 0, 0x648350b8}};
const std::vector<RomEntry> kSegaNinjaChar = {
    {"epr-6558.62", 0x2000, 0x0000, 0x2af9eaeb}, {"epr-6592.61", 0x2000, 0x2000, 0x7804db86},
    {"epr-6556.64", 0x2000, 0x4000, 0x79fd26f7}, {"epr-6590.63", 0x2000, 0x6000, 0xbf858cad},
    {"epr-6554.66", 0x2000, 0x8000, 0x5ac9d205}, {"epr-6588.65", 0x2000, 0xa000, 0xdc931dbb},
};

const std::vector<RomEntry> kUpNDownMain = {
    {"epr5516a.129", 0x2000, 0x0000, 0x038c82da}, {"epr5517a.130", 0x2000, 0x2000, 0x6930e1de},
    {"epr-5518.131", 0x2000, 0x4000, 0x2a370c99}, {"epr-5519.132", 0x2000, 0x6000, 0x9d664a58},
    {"epr-5520.133", 0x2000, 0x8000, 0x208dfbdf}, {"epr-5521.134", 0x2000, 0xa000, 0xe7b8d87a},
};
const std::vector<RomEntry> kUpNDownSprites = {
    {"epr-5514.86", 0x4000, 0x0000, 0xfcc0a88b},
    {"epr-5515.93", 0x4000, 0x4000, 0x60908838},
};
const std::vector<RomEntry> kUpNDownSound = {{"epr-5535.3", 0x2000, 0, 0xcf4e4c45}};
const std::vector<RomEntry> kUpNDownVideoProm = {{"pr-5317.106", 0x100, 0, 0x648350b8}};
const std::vector<RomEntry> kUpNDownChar = {
    {"epr-5527.82", 0x2000, 0x0000, 0xb2d616f1}, {"epr-5526.65", 0x2000, 0x2000, 0x8a8b33c2},
    {"epr-5525.81", 0x2000, 0x4000, 0xe749c5ef}, {"epr-5524.64", 0x2000, 0x6000, 0x8b886952},
    {"epr-5523.80", 0x2000, 0x8000, 0xdede35d9}, {"epr-5522.63", 0x2000, 0xa000, 0x5e6d9dff},
};

const std::vector<RomEntry> kFlickyMain = {
    {"epr-5978a.116", 0x4000, 0x0000, 0x296f1492},
    {"epr-5979a.109", 0x4000, 0x4000, 0x64b03ef9},
};
const std::vector<RomEntry> kFlickySprites = {
    {"epr-5855.117", 0x4000, 0x0000, 0xb5f894a1},
    {"epr-5856.110", 0x4000, 0x4000, 0x266af78f},
};
const std::vector<RomEntry> kFlickySound = {{"epr-5869.120", 0x2000, 0, 0x6d220d4e}};
const std::vector<RomEntry> kFlickyVideoProm = {{"pr-5317.76", 0x100, 0, 0x648350b8}};
const std::vector<RomEntry> kFlickyChar = {
    {"epr-5868.62", 0x2000, 0x0000, 0x7402256b}, {"epr-5867.61", 0x2000, 0x2000, 0x2f5ce930},
    {"epr-5866.64", 0x2000, 0x4000, 0x967f1d9a}, {"epr-5865.63", 0x2000, 0x6000, 0x03d9a34c},
    {"epr-5864.66", 0x2000, 0x8000, 0xe659f358}, {"epr-5863.65", 0x2000, 0xa000, 0xa496ca15},
};

const std::vector<RomEntry> kGardiaMain = {
    {"epr-10255.1", 0x8000, 0x00000, 0x89282a6b},
    {"epr-10254.2", 0x8000, 0x08000, 0x2826b6d8},
    {"epr-10253.3", 0x8000, 0x10000, 0x7911260f},
};
const std::vector<RomEntry> kGardiaChar = {
    {"epr-10249.61", 0x4000, 0x0000, 0x4e0ad0f2},
    {"epr-10248.64", 0x4000, 0x4000, 0x3515d124},
    {"epr-10247.66", 0x4000, 0x8000, 0x541e1555},
};
const std::vector<RomEntry> kGardiaSound = {{"epr-10243.120", 0x4000, 0, 0x87220660}};
const std::vector<RomEntry> kGardiaSprites = {
    {"epr-10234.117", 0x8000, 0x00000, 0x8a6aed33}, {"epr-10233.110", 0x8000, 0x08000, 0xc52784d3},
    {"epr-10236.04", 0x8000, 0x10000, 0xb35ab227}, {"epr-10235.5", 0x8000, 0x18000, 0x006a3151},
};
const std::vector<RomEntry> kGardiaVideoProm = {{"pr5317.4", 0x100, 0, 0x648350b8}};
const std::vector<RomEntry> kGardiaProms = {
    {"pr-7345.3", 0x100, 0x000, 0x8eee0f72},
    {"pr-7344.2", 0x100, 0x100, 0x3e7babd7},
    {"pr-7343.1", 0x100, 0x200, 0x371c44a6},
};

struct GameInfo {
    const std::vector<RomEntry>* main_rom;
    size_t main_size;
    const std::vector<RomEntry>* char_rom;
    const std::vector<RomEntry>* sprite_rom;
    size_t sprite_size;
    const std::vector<RomEntry>* sound_rom;
    size_t sound_size;
    const std::vector<RomEntry>* video_prom;
    int sprite_num_banks;
    uint8_t dsw_b;
    const char* title;
};

const GameInfo& game_info(SegaSystem1::Game game) {
    static const GameInfo kInfos[8] = {
        {&kPitfall2Main, 0xc000, &kPitfall2Char, &kPitfall2Sprites, 0x8000, &kPitfall2Sound, 0x2000,
         &kPitfall2VideoProm, 1, 0xdc, "Pitfall II (Sega System 1)"},
        {&kTeddyMain, 0xc000, &kTeddyChar, &kTeddySprites, 0x10000, &kTeddySound, 0x2000,
         &kTeddyVideoProm, 2, 0xfe, "Teddy Boy Blues (Sega System 1)"},
        {&kWBoyMain, 0xc000, &kWBoyChar, &kWBoySprites, 0x10000, &kWBoySound, 0x2000,
         &kWBoyVideoProm, 2, 0xec, "Wonder Boy (Sega System 1)"},
        {&kMrVikingMain, 0xc000, &kMrVikingChar, &kMrVikingSprites, 0x8000, &kMrVikingSound, 0x2000,
         &kMrVikingVideoProm, 1, 0xfc, "Mr. Viking (Sega System 1)"},
        {&kSegaNinjaMain, 0xc000, &kSegaNinjaChar, &kSegaNinjaSprites, 0x10000, &kSegaNinjaSound, 0x2000,
         &kSegaNinjaVideoProm, 2, 0xdc, "Sega Ninja (Sega System 1)"},
        {&kUpNDownMain, 0xc000, &kUpNDownChar, &kUpNDownSprites, 0x8000, &kUpNDownSound, 0x2000,
         &kUpNDownVideoProm, 1, 0xfe, "Up'n Down (Sega System 1)"},
        {&kFlickyMain, 0x8000, &kFlickyChar, &kFlickySprites, 0x8000, &kFlickySound, 0x2000,
         &kFlickyVideoProm, 1, 0xfe, "Flicky (Sega System 1)"},
        {&kGardiaMain, 0x18000, &kGardiaChar, &kGardiaSprites, 0x20000, &kGardiaSound, 0x4000,
         &kGardiaVideoProm, 4, 0x7c, "Gardia (Sega System 2)"},
    };
    return kInfos[int(game)];
}

GfxLayout char_layout() {
    GfxLayout layout;
    layout.width = 8;
    layout.height = 8;
    layout.total = 2048;
    layout.planes = 3;
    layout.char_increment = 8 * 8;
    layout.plane_offsets = {0, 0x4000 * 8, 0x8000 * 8};
    layout.x_offsets = {0, 1, 2, 3, 4, 5, 6, 7};
    layout.y_offsets = {0, 8, 16, 24, 32, 40, 48, 56};
    return layout;
}

}  // namespace

SegaSystem1::SegaSystem1(Game game)
    : game_(game),
      main_cpu_(kMainClock),
      sound_cpu_(kSoundClock),
      psg0_(kPsg0Clock, 0.5f),
      psg1_(kPsg1Clock, 1.0f) {
    main_cpu_.set_memory_handlers([this](uint16_t a) { return read_data(a); },
                                  [this](uint16_t a, uint8_t v) { write_byte(a, v); });
    main_cpu_.set_opcode_read([this](uint16_t a) { return read_opcode(a); });
    main_cpu_.set_io_handlers([this](uint16_t p) { return read_port(p); },
                              [this](uint16_t p, uint8_t v) { write_port(p, v); });

    sound_cpu_.set_memory_handlers([this](uint16_t a) { return snd_read(a); },
                                   [this](uint16_t a, uint8_t v) { snd_write(a, v); });
    sound_cpu_.set_io_handlers([](uint16_t) { return uint8_t(0xff); }, [](uint16_t, uint8_t) {});
    sound_cpu_.set_cycle_handler([this](int cycles) { on_sound_cycles(cycles); });

    pio_.set_port_handlers(Z80Pio::kPortA, nullptr, [this](uint8_t v) { port_a_write(v); },
                           [this](bool s) { pio_ready_a(s); });
    if (game_ == Game::Gardia) {
        pio_.set_port_handlers(Z80Pio::kPortB, nullptr, [this](uint8_t v) { port_b_gardia_write(v); }, nullptr);
    } else {
        pio_.set_port_handlers(Z80Pio::kPortB, nullptr, [this](uint8_t v) { port_b_write(v); }, nullptr);
    }

    ppi_.set_port_handlers(
        nullptr, nullptr, nullptr, [this](uint8_t v) { port_a_write(v); },
        [this](uint8_t v) { port_b_write(v); }, [this](uint8_t v) { port_c_write(v); });

    if (banked() || game_ == Game::MrViking || game_ == Game::UpNDown) {
        display_width_ = 240;
        display_xoffset_ = 8;
    }
    framebuffer_.assign(size_t(display_width_) * size_t(kScreenHeight), 0xff000000u);

    main_cycles_per_line_ = int(double(kMainClock) / (kScanlines * kFramesPerSecond));
    sound_cycles_per_line_ = int(double(kSoundClock) / (kScanlines * kFramesPerSecond));
}

const char* SegaSystem1::title() const { return game_info(game_).title; }

bool SegaSystem1::init(const std::string& rom_path, std::string* error) {
    const GameInfo& info = game_info(game_);

    RomLoader loader;
    if (!loader.open(rom_path, error)) return false;

    std::vector<uint8_t> main_rom(info.main_size, 0);
    if (!loader.load(*info.main_rom, main_rom, error)) return false;

    std::vector<uint8_t> char_rom(0xc000, 0);
    if (!loader.load(*info.char_rom, char_rom, error)) return false;

    sprite_rom_.assign(info.sprite_size, 0);
    if (!loader.load(*info.sprite_rom, sprite_rom_, error)) return false;

    std::vector<uint8_t> sound_rom(info.sound_size, 0);
    if (!loader.load(*info.sound_rom, sound_rom, error)) return false;
    std::copy(sound_rom.begin(), sound_rom.end(), sound_rom_.begin());

    std::vector<uint8_t> video_prom(0x100, 0);
    if (!loader.load(*info.video_prom, video_prom, error)) return false;
    std::copy(video_prom.begin(), video_prom.end(), mix_lookup_.begin());

    if (game_ == Game::Gardia) {
        std::vector<uint8_t> proms(0x300, 0);
        if (!loader.load(kGardiaProms, proms, error)) return false;
        std::copy(proms.begin(), proms.end(), direct_proms_.begin());

        // First 0x8000 is the encrypted, directly addressable window; the
        // remaining 0x10000 splits into 4 unencrypted 0x4000 banks.
        std::vector<uint8_t> window(main_rom.begin(), main_rom.begin() + 0x8000);
        std::vector<uint8_t> data, opcodes;
        sega_decrypt_type2(window, SegaDecrypt2Chip::S317_000X, 2, &data, &opcodes);
        std::copy(data.begin(), data.end(), memory_.begin());
        std::copy(opcodes.begin(), opcodes.end(), opcodes_.begin());
        for (int bank = 0; bank < 4; bank++) {
            std::copy(main_rom.begin() + 0x8000 + bank * 0x4000,
                     main_rom.begin() + 0x8000 + (bank + 1) * 0x4000, banked_rom_[bank].begin());
        }
    } else if (game_ == Game::WonderBoy) {
        std::vector<uint8_t> data, opcodes;
        sega_decrypt_type2(main_rom, SegaDecrypt2Chip::S315_5177, 0, &data, &opcodes);
        std::copy(data.begin(), data.end(), memory_.begin());
        std::copy(opcodes.begin(), opcodes.end(), opcodes_.begin());
    } else {
        static const SegaDecryptGame kTable[8] = {
            SegaDecryptGame::Pitfall2, SegaDecryptGame::TeddyboyBlues, SegaDecryptGame::Pengo,
            SegaDecryptGame::MrViking, SegaDecryptGame::SegaNinja,     SegaDecryptGame::UpNDown,
            SegaDecryptGame::Flicky,   SegaDecryptGame::Pengo,
        };
        std::vector<uint8_t> data, opcodes;
        sega_decrypt(main_rom, kTable[int(game_)], &data, &opcodes);
        std::copy(data.begin(), data.end(), memory_.begin());
        std::copy(opcodes.begin(), opcodes.end(), opcodes_.begin());
        // Bytes above the encrypted window (0x8000+) are used as-is.
        if (main_rom.size() > 0x8000)
            std::copy(main_rom.begin() + 0x8000, main_rom.end(), memory_.begin() + 0x8000);
    }

    decode_graphics(char_rom);
    build_palette_resistor();

    sprite_num_banks_ = info.sprite_num_banks;
    dsw_a_ = 0xff;
    dsw_b_ = info.dsw_b;

    reset();
    return true;
}

void SegaSystem1::build_palette_resistor() {
    const auto weights = compute_resistor_weights(0, 255, -1.0, {
        {{995, 495, 250}, 0, 0},
        {{995, 495, 250}, 0, 0},
        {{495, 250}, 0, 0},
    });
    rweights_ = weights[0];
    gweights_ = weights[1];
    bweights_ = weights[2];
}

void SegaSystem1::set_color_weighted(int index, uint8_t value) {
    int bit0 = (value >> 0) & 1, bit1 = (value >> 1) & 1, bit2 = (value >> 2) & 1;
    const int r = combine_weights(rweights_, {bit0, bit1, bit2});
    bit0 = (value >> 3) & 1; bit1 = (value >> 4) & 1; bit2 = (value >> 5) & 1;
    const int g = combine_weights(gweights_, {bit0, bit1, bit2});
    bit0 = (value >> 6) & 1; bit1 = (value >> 7) & 1;
    const int b = combine_weights(bweights_, {bit0, bit1});
    palette_[index] = 0xff000000u | (uint32_t(r) << 16) | (uint32_t(g) << 8) | uint32_t(b);
}

void SegaSystem1::set_color_direct(int index, uint8_t value) {
    const uint8_t r_raw = direct_proms_[value];
    const uint8_t g_raw = direct_proms_[0x100 + value];
    const uint8_t b_raw = direct_proms_[0x200 + value];
    auto weight = [](uint8_t v) -> int {
        return 0x0e * (v & 1) + 0x1f * ((v >> 1) & 1) + 0x43 * ((v >> 2) & 1) + 0x8f * ((v >> 3) & 1);
    };
    palette_[index] = 0xff000000u | (uint32_t(weight(r_raw)) << 16) | (uint32_t(weight(g_raw)) << 8) |
                      uint32_t(weight(b_raw));
}

void SegaSystem1::decode_graphics(const std::vector<uint8_t>& char_rom) {
    chars_.decode(char_layout(), char_rom);
}

void SegaSystem1::reset() {
    main_cpu_.reset();
    sound_cpu_.reset();
    psg0_.reset();
    psg1_.reset();
    pio_.reset();
    ppi_.reset();

    bg_ram_.fill(0);
    bg_ram_dirty_.fill(false);
    palette_ram_.fill(0);
    mix_collide_.fill(0);
    sprite_collide_.fill(0);
    mix_collide_summary_ = false;
    sprite_collide_summary_ = false;

    for (auto& layer : tile_layer_) layer.fill(0);
    sprite_layer_.fill(0);

    in0_ = in1_ = in2_ = 0xff;
    sound_latch_ = 0;
    videomode_ = 0;
    rom_bank_ = 0;
    bg_ram_bank_ = 0;
    bg_xscroll_ = 0;
    bg_yscroll_ = 0;
    ppi_c_shadow_ = 0;

    audio_accumulator_ = 0;
    sound_irq_accum_ = 0;
    audio_.clear();

    std::fill(palette_.begin(), palette_.end(), 0xff000000u);
    std::fill(framebuffer_.begin(), framebuffer_.end(), 0xff000000u);
}

// ---------------------------------------------------------------------------
// Main CPU memory map
// ---------------------------------------------------------------------------

uint8_t SegaSystem1::read_data(uint16_t addr) {
    if (banked() && addr >= 0x8000 && addr <= 0xbfff) return banked_rom_[rom_bank_ & 3][addr & 0x3fff];
    if (addr <= 0xd7ff) return memory_[addr];
    if (addr <= 0xddff) return palette_ram_[addr & 0x7ff];
    if (addr <= 0xdfff) return memory_[addr];
    if (addr <= 0xefff) return bg_ram_[addr & 0xfff];
    if (addr <= 0xf3ff) return uint8_t(mix_collide_[addr & 0x3f] | 0x7e | (mix_collide_summary_ ? 0x80 : 0));
    if (addr <= 0xf7ff) return 0xff;
    if (addr <= 0xfbff) return uint8_t(sprite_collide_[addr & 0x3ff] | 0x7e | (sprite_collide_summary_ ? 0x80 : 0));
    return 0xff;
}

uint8_t SegaSystem1::read_opcode(uint16_t addr) {
    if (addr <= 0x7fff) return opcodes_[addr];
    return read_data(addr);
}

void SegaSystem1::write_byte(uint16_t addr, uint8_t value) {
    if (addr <= 0xbfff) return;  // ROM (or Gardia's banked ROM window)
    if (addr <= 0xd7ff || (addr >= 0xde00 && addr <= 0xdfff)) {
        memory_[addr] = value;
        return;
    }
    if (addr <= 0xddff) {
        const int pos = addr & 0x7ff;
        if (palette_ram_[pos] != value) {
            palette_ram_[pos] = value;
            if (banked())
                set_color_direct(pos, value);
            else
                set_color_weighted(pos, value);
        }
        return;
    }
    if (addr <= 0xefff) {
        const int pos = addr & 0xfff;
        if (bg_ram_[pos] != value) {
            bg_ram_[pos] = value;
            bg_ram_dirty_[pos >> 1] = true;
        }
        return;
    }
    if (addr <= 0xf3ff) { mix_collide_[addr & 0x3f] = 0; return; }
    if (addr <= 0xf7ff) { mix_collide_summary_ = false; return; }
    if (addr <= 0xfbff) { sprite_collide_[addr & 0x3ff] = 0; return; }
    sprite_collide_summary_ = false;
}

uint8_t SegaSystem1::read_port(uint16_t port) {
    const int p = port & 0x1f;
    if (p <= 0x3) return in1_;
    if (p <= 0x7) return in2_;
    if (p <= 0xb) return in0_;
    if (p == 0xc || p == 0xe) return dsw_a_;
    if (p == 0xd || p == 0xf || (p >= 0x10 && p <= 0x13)) return dsw_b_;
    if (uses_ppi()) {
        if (p >= 0x14 && p <= 0x17) return ppi_.read(p & 3);
    } else {
        if (p >= 0x18 && p <= 0x1b) return pio_.read(p);
    }
    return 0xff;
}

void SegaSystem1::write_port(uint16_t port, uint8_t value) {
    const int p = port & 0x1f;
    if (uses_ppi()) {
        if (p >= 0x14 && p <= 0x17) ppi_.write(p & 3, value);
    } else {
        if (p >= 0x18 && p <= 0x1b) pio_.write(p, value);
    }
}

// ---------------------------------------------------------------------------
// Sound CPU memory map
// ---------------------------------------------------------------------------

uint8_t SegaSystem1::snd_read(uint16_t addr) {
    if (addr <= 0x7fff) return sound_rom_[addr];
    if (addr <= 0x9fff) return sound_ram_[addr & 0x7ff];
    if (addr < 0xe000) return 0xff;
    if (uses_ppi()) {
        const uint8_t c = ppi_c_shadow_;
        ppi_.write(2, uint8_t(c & 0xbf));
        ppi_.write(2, uint8_t(c | 0x40));
        return sound_latch_;
    }
    const uint8_t value = pio_.port_output(Z80Pio::kPortA);
    pio_.strobe_a(false);
    pio_.strobe_a(true);
    return value;
}

void SegaSystem1::snd_write(uint16_t addr, uint8_t value) {
    if (addr <= 0x7fff) return;
    if (addr <= 0x9fff) { sound_ram_[addr & 0x7ff] = value; return; }
    if (addr <= 0xbfff) { psg0_.write(value); return; }
    if (addr <= 0xdfff) { psg1_.write(value); return; }
}

void SegaSystem1::on_sound_cycles(int cycles) {
    audio_accumulator_ += int64_t(cycles) * SN76496::kSampleRate;
    while (audio_accumulator_ >= int64_t(kSoundClock)) {
        audio_accumulator_ -= int64_t(kSoundClock);
        const int32_t sample = (psg0_.update() + psg1_.update()) / 2;
        audio_.push_back(int16_t(std::clamp(sample, -32768, 32767)));
    }
}

// ---------------------------------------------------------------------------
// PPI / PIO glue
// ---------------------------------------------------------------------------

void SegaSystem1::port_a_write(uint8_t value) { sound_latch_ = value; }

void SegaSystem1::port_b_write(uint8_t value) {
    rom_bank_ = (value >> 2) & 3;
    videomode_ = value;
}

void SegaSystem1::port_b_gardia_write(uint8_t value) {
    rom_bank_ = ((value & 0x40) >> 5) | ((value & 0x04) >> 2);
    videomode_ = value;
}

void SegaSystem1::port_c_write(uint8_t value) {
    ppi_c_shadow_ = value;
    sound_cpu_.set_nmi((value & 0x80) != 0 ? IrqLine::Clear : IrqLine::Assert);
    bg_ram_bank_ = (value >> 1) & 3;
}

void SegaSystem1::pio_ready_a(bool) { sound_cpu_.set_nmi(IrqLine::Pulse); }

// ---------------------------------------------------------------------------
// Video
// ---------------------------------------------------------------------------

void SegaSystem1::draw_bg_tile(int layer, int offset) {
    const int source = layer << 11;  // 0 or 0x800
    const uint16_t attrib = uint16_t(bg_ram_[offset * 2 + source] | (bg_ram_[offset * 2 + source + 1] << 8));
    const int nchar = attrib & 0x7ff;
    const int color = ((attrib >> 5) & 0xff) << 3;
    const int tile_x = (offset % 32) * 8;
    const int tile_y = (offset / 32) * 8;
    const uint8_t* pixels = chars_.element(nchar);
    for (int y = 0; y < 8; y++) {
        uint16_t* dst = &tile_layer_[layer][(tile_y + y) * kLayerSize + tile_x];
        for (int x = 0; x < 8; x++) dst[x] = uint16_t(pixels[y * 8 + x] | color);
    }
}

void SegaSystem1::update_background(int layer) {
    const int source_half = layer << 10;  // 0 or 0x400, matches (screen shl 11) shr 1
    for (int f = 0; f < 0x400; f++) {
        if (bg_ram_dirty_[f + source_half]) {
            draw_bg_tile(layer, f);
            bg_ram_dirty_[f + source_half] = false;
        }
    }
}

void SegaSystem1::draw_sprites() {
    for (int f = 0; f < 32; f++) {
        const int base = 0xd000 + f * 0x10;
        int srcaddr = memory_[base + 6] | (memory_[base + 7] << 8);
        const int stride = memory_[base + 4] | (memory_[base + 5] << 8);
        int bank = ((memory_[base + 3] & 0x80) >> 7) | ((memory_[base + 3] & 0x40) >> 5) |
                  ((memory_[base + 3] & 0x20) >> 3);
        const int xstart = ((memory_[base + 2] | (memory_[base + 3] << 8)) & 0x1ff) / 2;
        const int bottom = memory_[base + 1] + 1;
        const int top = memory_[base + 0] + 1;
        const int palette_base = f * 0x10;
        bank %= sprite_num_banks_;
        const size_t gfx_bank_base = size_t(bank) * 0x8000;

        for (int y = top; y < bottom; y++) {
            const int destbase = (y & 0xff) * 256;
            srcaddr = (srcaddr + stride) & 0xffff;
            if (y < 0 || y > 256) continue;
            const int addrdelta = (srcaddr & 0x8000) != 0 ? -1 : 1;
            int curaddr = srcaddr;
            int x = xstart;
            for (int guard = 0; guard < 512; guard++) {
                const size_t rom_index = gfx_bank_base + size_t(curaddr & 0x7fff);
                const uint8_t data = rom_index < sprite_rom_.size() ? sprite_rom_[rom_index] : 0;
                int color1, color2;
                if ((curaddr & 0x8000) == 0) {
                    color1 = data >> 4;
                    color2 = data & 0xf;
                } else {
                    color1 = data & 0xf;
                    color2 = data >> 4;
                }
                if (color1 == 0xf) break;
                if (color1 != 0 && x >= 0 && x <= 255) {
                    uint16_t& dst = sprite_layer_[destbase + x];
                    if ((dst & 0xf) != 0) {
                        sprite_collide_[((dst >> 4) & 0x1f) + 32 * f] = 1;
                        sprite_collide_summary_ = true;
                    }
                    dst = uint16_t(color1 | palette_base);
                }
                if (color2 == 0xf) break;
                if (color2 != 0 && (x + 1) >= 0 && (x + 1) <= 255) {
                    uint16_t& dst = sprite_layer_[destbase + x + 1];
                    if ((dst & 0xf) != 0) {
                        sprite_collide_[((dst >> 4) & 0x1f) + 32 * f] = 1;
                        sprite_collide_summary_ = true;
                    }
                    dst = uint16_t(color2 | palette_base);
                }
                curaddr = (curaddr + addrdelta) & 0xffff;
                x += 2;
            }
        }
    }
}

void SegaSystem1::render_frame() {
    const int width = display_width_;
    const int xoffset = display_xoffset_;

    if ((videomode_ & 0x10) != 0) {
        std::fill(framebuffer_.begin(), framebuffer_.end(), 0xff000000u);
        return;
    }

    sprite_layer_.fill(0);
    if (memory_[0xd000] != 0xff) draw_sprites();

    for (int y = 0; y < kScreenHeight; y++) {
        const int fgbase = y * 256;
        const int sprbase = y * 256;
        const int bgy = (y + bg_yscroll_) & 0xff;
        uint32_t* out = &framebuffer_[size_t(y) * size_t(width)];
        for (int i = 0; i < width; i++) {
            const int x = i + xoffset;
            const int bgx = (x - bg_xscroll_) & 0xff;
            const uint16_t fgpix = tile_layer_[1][fgbase + x];
            const uint16_t bgpix = tile_layer_[0][bgy * 256 + bgx];
            const uint16_t sprpix = sprite_layer_[sprbase + x];

            const int bit0 = (sprpix & 0xf) == 0 ? 1 : 0;
            const int bit1 = (fgpix & 7) == 0 ? 2 : 0;
            const int bit2 = ((fgpix >> 9) & 3) << 2;
            const int bit3 = (bgpix & 7) == 0 ? 16 : 0;
            const int bit4 = ((bgpix >> 9) & 3) << 5;
            const int lookup_index = bit0 | bit1 | bit2 | bit3 | bit4;
            uint8_t lookup_value = mix_lookup_[lookup_index & 0xff];

            if ((lookup_value & 4) == 0) {
                mix_collide_[((lookup_value & 8) << 2) | ((sprpix >> 4) & 0x1f)] = 1;
                mix_collide_summary_ = true;
            }
            lookup_value &= 3;
            uint32_t pixel;
            if (lookup_value == 0) pixel = palette_[0x000 | (sprpix & 0x1ff)];
            else if (lookup_value == 1) pixel = palette_[0x200 | (fgpix & 0x1ff)];
            else pixel = palette_[0x400 | (bgpix & 0x1ff)];
            out[i] = pixel;
        }
    }
}

// ---------------------------------------------------------------------------
// Frame loop
// ---------------------------------------------------------------------------

void SegaSystem1::run_frame() {
    for (int line = 0; line < kScanlines; line++) {
        if (line == kIrqLine) {
            main_cpu_.set_irq(IrqLine::Hold);
            update_background(0);
            update_background(1);
            bg_xscroll_ = int(uint16_t(bg_ram_[0xffc] | (bg_ram_[0xffd] << 8))) / 2 + 14;
            bg_yscroll_ = bg_ram_[0xfbd];
            render_frame();
        }

        sound_irq_accum_ += 64;
        if (sound_irq_accum_ >= kScanlines) {
            sound_irq_accum_ -= kScanlines;
            sound_cpu_.set_irq(IrqLine::Hold);
        }

        main_cpu_.run(main_cycles_per_line_);
        sound_cpu_.run(sound_cycles_per_line_);
    }
}

// ---------------------------------------------------------------------------
// Inputs / DIP / audio
// ---------------------------------------------------------------------------

void SegaSystem1::set_inputs(const MachineInputs& inputs) {
    in0_ = 0xff;
    if (inputs.coin1) in0_ &= uint8_t(~0x01);
    if (inputs.coin2) in0_ &= uint8_t(~0x02);
    if (inputs.player1.start) in0_ &= uint8_t(~0x10);
    if (inputs.player2.start) in0_ &= uint8_t(~0x20);

    in1_ = 0xff;
    if (inputs.player1.button3) in1_ &= uint8_t(~0x01);
    if (inputs.player1.button1) in1_ &= uint8_t(~0x02);
    if (inputs.player1.button2) in1_ &= uint8_t(~0x04);
    if (inputs.player1.down) in1_ &= uint8_t(~0x10);
    if (inputs.player1.up) in1_ &= uint8_t(~0x20);
    if (inputs.player1.right) in1_ &= uint8_t(~0x40);
    if (inputs.player1.left) in1_ &= uint8_t(~0x80);

    in2_ = 0xff;
    if (inputs.player2.button3) in2_ &= uint8_t(~0x01);
    if (inputs.player2.button1) in2_ &= uint8_t(~0x02);
    if (inputs.player2.button2) in2_ &= uint8_t(~0x04);
    if (inputs.player2.down) in2_ &= uint8_t(~0x10);
    if (inputs.player2.up) in2_ &= uint8_t(~0x20);
    if (inputs.player2.right) in2_ &= uint8_t(~0x40);
    if (inputs.player2.left) in2_ &= uint8_t(~0x80);
}

void SegaSystem1::set_dip_switch(int bank, uint8_t value) {
    if (bank == 0) dsw_a_ = value;
    else if (bank == 1) dsw_b_ = value;
}

void SegaSystem1::drain_audio(std::vector<int16_t>& out) {
    out.insert(out.end(), audio_.begin(), audio_.end());
    audio_.clear();
}

}  // namespace dsp
