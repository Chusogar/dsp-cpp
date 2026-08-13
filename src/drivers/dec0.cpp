#include "drivers/dec0.h"

#include <algorithm>

#include "core/rom_loader.h"

namespace dsp {
namespace {

// ROM sets, taken from dec0_hw.pas.
const std::vector<RomEntry> kRobocopMain = {{"ep05-4.11c", 0x10000, 0x00000, 0x29c35379},
                                            {"ep01-4.11b", 0x10000, 0x00001, 0x77507c69},
                                            {"ep04-3", 0x10000, 0x20000, 0x39181778},
                                            {"ep00-3", 0x10000, 0x20001, 0xe128541f}};
const std::vector<RomEntry> kRobocopMcu = {{"en_24_mb7124e.a2", 0x200, 0, 0xb8e2ca98}};
const std::vector<RomEntry> kRobocopSound = {{"ep03-3", 0x8000, 0x8000, 0x5b164b24}};
const std::vector<RomEntry> kRobocopOki = {{"ep02", 0x10000, 0, 0x711ce46f}};
const std::vector<RomEntry> kRobocopChar = {{"ep23", 0x10000, 0x00000, 0xa77e4ab1},
                                            {"ep22", 0x10000, 0x10000, 0x9fbd6903}};
const std::vector<RomEntry> kRobocopTiles1 = {{"ep20", 0x10000, 0x00000, 0x1d8d38b8},
                                              {"ep21", 0x10000, 0x10000, 0x187929b2},
                                              {"ep18", 0x10000, 0x20000, 0xb6580b5e},
                                              {"ep19", 0x10000, 0x30000, 0x9bad01c7}};
const std::vector<RomEntry> kRobocopTiles2 = {{"ep14", 0x8000, 0x00000, 0xca56ceda},
                                              {"ep15", 0x8000, 0x08000, 0xa945269c},
                                              {"ep16", 0x8000, 0x10000, 0xe7fa4d58},
                                              {"ep17", 0x8000, 0x18000, 0x84aae89d}};
const std::vector<RomEntry> kRobocopSprites = {{"ep07", 0x10000, 0x00000, 0x495d75cf},
                                               {"ep06", 0x08000, 0x10000, 0xa2ae32e2},
                                               {"ep11", 0x10000, 0x20000, 0x62fa425a},
                                               {"ep10", 0x08000, 0x30000, 0xcce3bd95},
                                               {"ep09", 0x10000, 0x40000, 0x11bed656},
                                               {"ep08", 0x08000, 0x50000, 0xc45c7b4c},
                                               {"ep13", 0x10000, 0x60000, 0x8fca9f28},
                                               {"ep12", 0x08000, 0x70000, 0x3cd1d0c3}};

const std::vector<RomEntry> kBadDudesMain = {{"ei04-1.3c", 0x10000, 0x00000, 0x4bf158a7},
                                             {"ei01-1.3a", 0x10000, 0x00001, 0x74f5110c},
                                             {"ei06.6c", 0x10000, 0x40000, 0x3ff8da57},
                                             {"ei03.6a", 0x10000, 0x40001, 0xf8f2bd94}};
const std::vector<RomEntry> kBadDudesMcu = {{"ei31.9a", 0x1000, 0, 0x2a8745d2}};
const std::vector<RomEntry> kBadDudesSound = {{"ei07.8a", 0x8000, 0x8000, 0x9fb1ef4b}};
const std::vector<RomEntry> kBadDudesOki = {{"ei08.2c", 0x10000, 0, 0x3c87463e}};
const std::vector<RomEntry> kBadDudesChar = {{"ei25.15j", 0x8000, 0x0000, 0xbcf59a69},
                                             {"ei26.16j", 0x8000, 0x8000, 0x9aff67b8}};
const std::vector<RomEntry> kBadDudesTiles1 = {{"ei18.14d", 0x10000, 0x00000, 0x05cfc3e5},
                                               {"ei20.17d", 0x10000, 0x10000, 0xe11e988f},
                                               {"ei22.14f", 0x10000, 0x20000, 0xb893d880},
                                               {"ei24.17f", 0x10000, 0x30000, 0x6f226dda}};
const std::vector<RomEntry> kBadDudesTiles2 = {{"ei30.9j", 0x10000, 0x20000, 0x982da0d1},
                                               {"ei28.9f", 0x10000, 0x30000, 0xf01ebb3b}};
const std::vector<RomEntry> kBadDudesSprites = {{"ei15.16c", 0x10000, 0x00000, 0xa38a7d30},
                                                {"ei16.17c", 0x08000, 0x10000, 0x17e42633},
                                                {"ei11.16a", 0x10000, 0x20000, 0x3a77326c},
                                                {"ei12.17a", 0x08000, 0x30000, 0xfea2a134},
                                                {"ei13.13c", 0x10000, 0x40000, 0xe5ae2751},
                                                {"ei14.14c", 0x08000, 0x50000, 0xe83c760a},
                                                {"ei09.13a", 0x10000, 0x60000, 0x6901e628},
                                                {"ei10.14a", 0x08000, 0x70000, 0xeeee8a1a}};

const std::vector<RomEntry> kHippoMain = {{"ew02", 0x10000, 0x00000, 0xdf0d7dc6},
                                          {"ew01", 0x10000, 0x00001, 0xd5670aa7},
                                          {"ew05", 0x10000, 0x20000, 0xc76d65ec},
                                          {"ew00", 0x10000, 0x20001, 0xe9b427a6}};
const std::vector<RomEntry> kHippoMcu = {{"ew08", 0x10000, 0, 0x53010534}};
const std::vector<RomEntry> kHippoSound = {{"ew04", 0x8000, 0x8000, 0x9871b98d}};
const std::vector<RomEntry> kHippoOki = {{"ew03", 0x10000, 0, 0xb606924d}};
const std::vector<RomEntry> kHippoChar = {{"ew14", 0x10000, 0x00000, 0x71ca593d},
                                          {"ew13", 0x10000, 0x10000, 0x86be5fa7}};
const std::vector<RomEntry> kHippoTiles1 = {{"ew19", 0x8000, 0x00000, 0x6b80d7a3},
                                            {"ew18", 0x8000, 0x08000, 0x78d3d764},
                                            {"ew20", 0x8000, 0x10000, 0xce9f5de3},
                                            {"ew21", 0x8000, 0x18000, 0x487a7ba2}};
const std::vector<RomEntry> kHippoTiles2 = {{"ew24", 0x8000, 0x00000, 0x4e1bc2a4},
                                            {"ew25", 0x8000, 0x08000, 0x9eb47dfb},
                                            {"ew23", 0x8000, 0x10000, 0x9ecf479e},
                                            {"ew22", 0x8000, 0x18000, 0xe55669aa}};
const std::vector<RomEntry> kHippoSprites = {{"ew15", 0x10000, 0x00000, 0x95423914},
                                             {"ew16", 0x10000, 0x10000, 0x96233177},
                                             {"ew10", 0x10000, 0x20000, 0x4c25dfe8},
                                             {"ew11", 0x10000, 0x30000, 0xf2e007fc},
                                             {"ew06", 0x10000, 0x40000, 0xe4bb8199},
                                             {"ew07", 0x10000, 0x50000, 0x470b6989},
                                             {"ew17", 0x10000, 0x60000, 0x8c97c757},
                                             {"ew12", 0x10000, 0x70000, 0xa2d244bc}};

const std::vector<RomEntry> kSlySpyMain = {{"fa14-4.17l", 0x10000, 0x00000, 0x60f16e31},
                                           {"fa12-4.9l", 0x10000, 0x00001, 0xb9b9fdcf},
                                           {"fa15.19l", 0x10000, 0x20000, 0x04a79266},
                                           {"fa13.11l", 0x10000, 0x20001, 0x641cc4b3}};
const std::vector<RomEntry> kSlySpySound = {{"fa10.5h", 0x10000, 0, 0xdfd2ff25}};
const std::vector<RomEntry> kSlySpyOki = {{"fa11.11k", 0x20000, 0, 0x4e547bad}};
const std::vector<RomEntry> kSlySpyChar = {{"fa05.11a", 0x8000, 0x0000, 0x09802924},
                                           {"fa04.9a", 0x8000, 0x8000, 0xec25b895}};
const std::vector<RomEntry> kSlySpyTiles1 = {{"fa07.17a", 0x10000, 0x00000, 0xe932268b},
                                             {"fa06.15a", 0x10000, 0x10000, 0xc4dd38c0}};
const std::vector<RomEntry> kSlySpyTiles2 = {{"fa09.22a", 0x20000, 0x00000, 0x1395e9be},
                                             {"fa08.21a", 0x20000, 0x20000, 0x4d7464db}};
const std::vector<RomEntry> kSlySpySprites = {{"fa01.4a", 0x20000, 0x00000, 0x99b0cd92},
                                              {"fa03.7a", 0x20000, 0x20000, 0x0e7ea74d},
                                              {"fa00.2a", 0x20000, 0x40000, 0xf7df3fd7},
                                              {"fa02.5a", 0x20000, 0x60000, 0x84e8da9d}};

const std::vector<RomEntry> kBoulderMain = {{"fw-15-2.17l", 0x10000, 0x00000, 0xca19a967},
                                            {"fw-12-2.9l", 0x10000, 0x00001, 0x242bdc2a},
                                            {"fw-16-2.19l", 0x10000, 0x20000, 0xb7217265},
                                            {"fw-13-2.11l", 0x10000, 0x20001, 0x19209ef4},
                                            {"fw-17-2.20l", 0x10000, 0x40000, 0x78a632a1},
                                            {"fw-14-2.13l", 0x10000, 0x40001, 0x69b6112d}};
const std::vector<RomEntry> kBoulderSound = {{"fn-10", 0x10000, 0, 0xc74106e7}};
const std::vector<RomEntry> kBoulderOki = {{"fn-11", 0x10000, 0, 0x990fd8d9}};
const std::vector<RomEntry> kBoulderChar = {{"fn-04", 0x10000, 0x00000, 0x40f5a760},
                                            {"fn-05", 0x10000, 0x10000, 0x824f2168}};
const std::vector<RomEntry> kBoulderTiles1 = {{"fn-07", 0x10000, 0x00000, 0xeac6a3b3},
                                              {"fn-06", 0x10000, 0x10000, 0x3feee292}};
const std::vector<RomEntry> kBoulderTiles2 = {{"fn-09", 0x20000, 0x00000, 0xc2b27bd2},
                                              {"fn-08", 0x20000, 0x20000, 0x5ac97178}};
const std::vector<RomEntry> kBoulderSprites = {{"fn-01", 0x10000, 0x00000, 0x9333121b},
                                               {"fn-03", 0x10000, 0x10000, 0x254ba60f},
                                               {"fn-00", 0x10000, 0x20000, 0xec18d098},
                                               {"fn-02", 0x10000, 0x30000, 0x4f060cba}};

// 8x8 characters, four bit planes (convert_chars() in the Pascal driver).
GfxLayout char_layout(int total) {
    GfxLayout layout;
    layout.width = 8;
    layout.height = 8;
    layout.total = total;
    layout.planes = 4;
    layout.char_increment = 8 * 8;
    layout.plane_offsets = {0, total * 8 * 8 * 2, total * 8 * 8 * 1, total * 8 * 8 * 3};
    layout.x_offsets = {0, 1, 2, 3, 4, 5, 6, 7};
    layout.y_offsets = {0, 8, 16, 24, 32, 40, 48, 56};
    return layout;
}

// 16x16 tiles and sprites, four bit planes (convert_tiles()).
GfxLayout tile_layout(int total) {
    GfxLayout layout;
    layout.width = 16;
    layout.height = 16;
    layout.total = total;
    layout.planes = 4;
    layout.char_increment = 16 * 16;
    layout.plane_offsets = {total * 16 * 16 * 1, total * 16 * 16 * 3, 0, total * 16 * 16 * 2};
    layout.x_offsets = {16 * 8 + 0, 16 * 8 + 1, 16 * 8 + 2, 16 * 8 + 3, 16 * 8 + 4,
                        16 * 8 + 5, 16 * 8 + 6, 16 * 8 + 7, 0,          1,
                        2,          3,          4,          5,          6,
                        7};
    layout.y_offsets = {0,      1 * 8,  2 * 8,  3 * 8,  4 * 8,  5 * 8,  6 * 8,  7 * 8,
                        8 * 8,  9 * 8,  10 * 8, 11 * 8, 12 * 8, 13 * 8, 14 * 8, 15 * 8};
    return layout;
}

uint8_t pal4bit(uint16_t value) {
    uint8_t nibble = uint8_t(value & 0x0f);
    return uint8_t((nibble << 4) | nibble);
}

uint8_t bitswap8(uint8_t value, int b7, int b6, int b5, int b4, int b3, int b2, int b1, int b0) {
    const int bits[8] = {b7, b6, b5, b4, b3, b2, b1, b0};
    uint8_t result = 0;
    for (int index = 0; index < 8; index++) {
        result = uint8_t(result | (((value >> bits[index]) & 1) << (7 - index)));
    }
    return result;
}

void swap_blocks(std::vector<uint8_t>& data, uint32_t first, uint32_t second, uint32_t length) {
    for (uint32_t index = 0; index < length; index++) {
        std::swap(data[first + index], data[second + index]);
    }
}

}  // namespace

Dec0::Dec0(Variant variant)
    : variant_(variant),
      bac06_(0x000, 0x200, 0x300, 1, 1, 1, 0x100, 0x0f),
      rom_(0x30000, 0),
      framebuffer_(size_t(kScreenWidth) * kScreenHeight, 0xff000000u) {
    main_cpu_.set_memory_handlers(
        [this](uint32_t address) {
            return dec1() ? slyspy_read(address) : main_read(address);
        },
        [this](uint32_t address, uint16_t value) {
            if (dec1()) {
                slyspy_write(address, value);
            } else {
                main_write(address, value);
            }
        });

    if (dec1()) {
        huc6280_ = std::make_unique<HuC6280>(kDec1SoundClock);
        huc6280_->set_memory_handlers(
            [this](uint32_t address) { return slyspy_sound_read(address); },
            [this](uint32_t address, uint8_t value) { slyspy_sound_write(address, value); });
        huc6280_->set_cycle_handler(
            [this](int cycles) { generate_audio(cycles, kDec1SoundClock); });
        ym3812_.set_irq_handler([this](bool state) {
            huc6280_->set_irq_line(1, state ? IrqLine::Assert : IrqLine::Clear);
        });
    } else {
        sound_cpu_ = std::make_unique<M6502>(kSoundClock);
        sound_cpu_->set_memory_handlers(
            [this](uint16_t address) { return sound_read(address); },
            [this](uint16_t address, uint8_t value) { sound_write(address, value); });
        sound_cpu_->set_cycle_handler([this](int cycles) { generate_audio(cycles, kSoundClock); });
        ym3812_.set_irq_handler([this](bool state) {
            sound_cpu_->set_irq(state ? IrqLine::Assert : IrqLine::Clear);
        });
    }

    if (variant_ == Variant::Robocop) {
        huc6280_ = std::make_unique<HuC6280>(kMcuClock);
        huc6280_->set_memory_handlers(
            [this](uint32_t address) { return robocop_mcu_read(address); },
            [this](uint32_t address, uint8_t value) { robocop_mcu_write(address, value); });
    } else if (variant_ == Variant::Hippodrome) {
        huc6280_ = std::make_unique<HuC6280>(kMcuClock);
        huc6280_->set_memory_handlers(
            [this](uint32_t address) { return hippo_mcu_read(address); },
            [this](uint32_t address, uint8_t value) { hippo_mcu_write(address, value); });
    } else if (variant_ == Variant::BadDudes) {
        mcu_ = std::make_unique<Mcs51>(kMcs51Clock);
        mcu_->set_port_read_handler(0, [this]() { return mcu_port0_read(); });
        for (int port = 1; port < 4; port++) {
            mcu_->set_port_read_handler(port, []() { return uint8_t(0xff); });
        }
        for (int port = 0; port < 4; port++) {
            mcu_->set_port_write_handler(
                port, [this, port](uint8_t value) { mcu_port_write(port, value); });
        }
    }
}

const char* Dec0::title() const {
    switch (variant_) {
        case Variant::Robocop: return "Robocop";
        case Variant::BadDudes: return "Bad Dudes vs. Dragonninja";
        case Variant::Hippodrome: return "Hippodrome";
        case Variant::SlySpy: return "Sly Spy";
        case Variant::BoulderDash: return "Boulder Dash";
    }
    return "DEC0";
}

bool Dec0::init(const std::string& rom_path, std::string* error) {
    if (!load_roms(rom_path, error)) return false;
    reset();
    return true;
}

bool Dec0::load_main_rom(RomLoader& loader, const std::vector<RomEntry>& entries,
                         std::string* error) {
    std::vector<uint8_t> temp(0x60000, 0);
    for (const RomEntry& entry : entries) {
        std::vector<uint8_t> data(entry.length, 0);
        RomEntry single{entry.name, entry.length, 0, entry.crc};
        if (!loader.load({single}, data, error)) return false;
        for (uint32_t index = 0; index < entry.length; index++) {
            temp[(entry.offset & ~1u) + index * 2 + (entry.offset & 1u)] = data[index];
        }
    }
    for (size_t index = 0; index < rom_.size(); index++) {
        rom_[index] = uint16_t((temp[index * 2] << 8) | temp[index * 2 + 1]);
    }
    return true;
}

void Dec0::decode_graphics(const std::vector<uint8_t>& chars, const std::vector<uint8_t>& tiles1,
                           const std::vector<uint8_t>& tiles2,
                           const std::vector<uint8_t>& sprites) {
    struct Counts {
        int chars;
        int tiles1;
        int tiles2;
        int sprites;
    };
    Counts counts{};
    switch (variant_) {
        case Variant::Robocop: counts = {0x1000, 0x800, 0x400, 0x1000}; break;
        case Variant::BadDudes: counts = {0x800, 0x800, 0x400, 0x1000}; break;
        case Variant::Hippodrome: counts = {0x1000, 0x400, 0x400, 0x1000}; break;
        case Variant::SlySpy: counts = {0x800, 0x400, 0x800, 0x1000}; break;
        case Variant::BoulderDash: counts = {0x1000, 0x400, 0x800, 0x800}; break;
    }
    chars_.decode(char_layout(counts.chars), chars);
    tiles1_.decode(tile_layout(counts.tiles1), tiles1);
    tiles2_.decode(tile_layout(counts.tiles2), tiles2);
    sprites_.decode(tile_layout(counts.sprites), sprites);
}

bool Dec0::load_roms(const std::string& rom_path, std::string* error) {
    RomLoader loader;
    if (!loader.open(rom_path, error)) return false;

    const std::vector<RomEntry>* main = nullptr;
    const std::vector<RomEntry>* sound = nullptr;
    const std::vector<RomEntry>* oki = nullptr;
    const std::vector<RomEntry>* chars = nullptr;
    const std::vector<RomEntry>* tiles1 = nullptr;
    const std::vector<RomEntry>* tiles2 = nullptr;
    const std::vector<RomEntry>* sprites = nullptr;
    const std::vector<RomEntry>* mcu = nullptr;
    uint32_t chars_size = 0, tiles1_size = 0, tiles2_size = 0, sprites_size = 0, oki_size = 0;

    switch (variant_) {
        case Variant::Robocop:
            main = &kRobocopMain;
            sound = &kRobocopSound;
            oki = &kRobocopOki;
            chars = &kRobocopChar;
            tiles1 = &kRobocopTiles1;
            tiles2 = &kRobocopTiles2;
            sprites = &kRobocopSprites;
            mcu = &kRobocopMcu;
            chars_size = 0x20000;
            tiles1_size = 0x40000;
            tiles2_size = 0x20000;
            sprites_size = 0x80000;
            oki_size = 0x10000;
            break;
        case Variant::BadDudes:
            main = &kBadDudesMain;
            sound = &kBadDudesSound;
            oki = &kBadDudesOki;
            chars = &kBadDudesChar;
            tiles1 = &kBadDudesTiles1;
            tiles2 = &kBadDudesTiles2;
            sprites = &kBadDudesSprites;
            mcu = &kBadDudesMcu;
            chars_size = 0x10000;
            tiles1_size = 0x40000;
            tiles2_size = 0x40000;
            sprites_size = 0x80000;
            oki_size = 0x10000;
            break;
        case Variant::Hippodrome:
            main = &kHippoMain;
            sound = &kHippoSound;
            oki = &kHippoOki;
            chars = &kHippoChar;
            tiles1 = &kHippoTiles1;
            tiles2 = &kHippoTiles2;
            sprites = &kHippoSprites;
            mcu = &kHippoMcu;
            chars_size = 0x20000;
            tiles1_size = 0x20000;
            tiles2_size = 0x20000;
            sprites_size = 0x80000;
            oki_size = 0x10000;
            break;
        case Variant::SlySpy:
            main = &kSlySpyMain;
            sound = &kSlySpySound;
            oki = &kSlySpyOki;
            chars = &kSlySpyChar;
            tiles1 = &kSlySpyTiles1;
            tiles2 = &kSlySpyTiles2;
            sprites = &kSlySpySprites;
            chars_size = 0x10000;
            tiles1_size = 0x20000;
            tiles2_size = 0x40000;
            sprites_size = 0x80000;
            oki_size = 0x20000;
            break;
        case Variant::BoulderDash:
            main = &kBoulderMain;
            sound = &kBoulderSound;
            oki = &kBoulderOki;
            chars = &kBoulderChar;
            tiles1 = &kBoulderTiles1;
            tiles2 = &kBoulderTiles2;
            sprites = &kBoulderSprites;
            chars_size = 0x20000;
            tiles1_size = 0x20000;
            tiles2_size = 0x40000;
            sprites_size = 0x40000;
            oki_size = 0x10000;
            break;
    }

    if (!load_main_rom(loader, *main, error)) return false;

    std::vector<uint8_t> sound_rom(0x10000, 0);
    if (!loader.load(*sound, sound_rom, error)) return false;
    if (dec1()) {
        // The DEC1 sound ROM is scrambled.
        for (uint8_t& byte : sound_rom) byte = bitswap8(byte, 0, 6, 5, 4, 3, 2, 1, 7);
    }
    std::copy(sound_rom.begin(), sound_rom.end(), sound_memory_.begin());

    std::vector<uint8_t> oki_rom(oki_size, 0);
    if (!loader.load(*oki, oki_rom, error)) return false;
    oki_.set_rom(std::move(oki_rom));

    if (mcu != nullptr) {
        mcu_rom_.assign(variant_ == Variant::Robocop ? 0x200 : 0x10000, 0);
        if (variant_ == Variant::BadDudes) mcu_rom_.assign(0x1000, 0);
        if (!loader.load(*mcu, mcu_rom_, error)) return false;
        if (variant_ == Variant::Hippodrome) {
            for (uint8_t& byte : mcu_rom_) byte = bitswap8(byte, 0, 6, 5, 4, 3, 2, 1, 7);
            // Protection areas patched out with RTS, like the Pascal driver.
            mcu_rom_[0x189] = 0x60;
            mcu_rom_[0x1af] = 0x60;
            mcu_rom_[0x1db] = 0x60;
            mcu_rom_[0x21a] = 0x60;
        }
        if (variant_ == Variant::BadDudes) {
            std::copy(mcu_rom_.begin(), mcu_rom_.end(), mcu_->rom());
        }
    }

    std::vector<uint8_t> char_rom(chars_size, 0);
    if (!loader.load(*chars, char_rom, error)) return false;
    if (variant_ == Variant::SlySpy) {
        swap_blocks(char_rom, 0x0000, 0x4000, 0x4000);
        swap_blocks(char_rom, 0x8000, 0xc000, 0x4000);
    } else if (variant_ == Variant::BoulderDash) {
        swap_blocks(char_rom, 0x00000, 0x08000, 0x8000);
        swap_blocks(char_rom, 0x10000, 0x18000, 0x8000);
    }

    std::vector<uint8_t> tiles1_rom(tiles1_size, 0);
    if (!loader.load(*tiles1, tiles1_rom, error)) return false;

    std::vector<uint8_t> tiles2_rom(tiles2_size, 0);
    if (!loader.load(*tiles2, tiles2_rom, error)) return false;
    if (variant_ == Variant::BadDudes) {
        // The two tile 2 ROMs are loaded high and rearranged into the first half.
        std::copy(tiles2_rom.begin() + 0x20000, tiles2_rom.begin() + 0x28000,
                  tiles2_rom.begin() + 0x8000);
        std::copy(tiles2_rom.begin() + 0x28000, tiles2_rom.begin() + 0x30000,
                  tiles2_rom.begin());
        std::copy(tiles2_rom.begin() + 0x30000, tiles2_rom.begin() + 0x38000,
                  tiles2_rom.begin() + 0x18000);
        std::copy(tiles2_rom.begin() + 0x38000, tiles2_rom.begin() + 0x40000,
                  tiles2_rom.begin() + 0x10000);
    }

    std::vector<uint8_t> sprite_rom(sprites_size, 0);
    if (!loader.load(*sprites, sprite_rom, error)) return false;

    decode_graphics(char_rom, tiles1_rom, tiles2_rom, sprite_rom);

    warnings_ = loader.warnings();
    return true;
}

void Dec0::reset() {
    main_cpu_.reset();
    main_debt_ = 0;
    sound_debt_ = 0;
    mcu_debt_ = 0;
    frames_ = 0;
    ram1_.fill(0);
    ram2_.fill(0);
    sprite_buffer_.fill(0);
    palette_ram_.fill(0);
    palette_.fill(0xff000000u);
    mcu_ram_.fill(0);
    mcu_shared_ram_.fill(0);
    pens_.fill(0);

    if (sound_cpu_) sound_cpu_->reset();
    if (huc6280_) huc6280_->reset();
    if (mcu_) {
        mcu_->reset();
        i8751_return_ = 0;
        i8751_command_ = 0;
        i8751_ports_.fill(0);
    }
    ym3812_.reset();
    ym2203_.reset();
    oki_.reset();
    bac06_.reset();

    in0_ = 0xffff;
    in1_ = 0x00f7;
    sound_latch_ = 0;
    priority_ = 0;
    hippodrm_lsb_ = 0;
    slyspy_state_ = 0;
    slyspy_sound_state_ = 0;
    audio_accumulator_ = 0;
    audio_.clear();
}

void Dec0::generate_audio(int cycles, uint32_t clock) {
    audio_accumulator_ += int64_t(cycles) * YM3812::kSampleRate;
    while (audio_accumulator_ >= clock) {
        audio_accumulator_ -= clock;
        int32_t sample = ym3812_.update() + ym2203_.update() + oki_.update();
        audio_.push_back(int16_t(std::clamp(sample, -32768, 32767)));
    }
}

void Dec0::run_frame() {
    const double main_per_line = double(kMainClock) / kFramesPerSecond / kScanlines;
    const uint32_t sound_clock = dec1() ? kDec1SoundClock : kSoundClock;
    const double sound_per_line = double(sound_clock) / kFramesPerSecond / kScanlines;
    double mcu_per_line = 0.0;
    if (variant_ == Variant::Robocop || variant_ == Variant::Hippodrome) {
        mcu_per_line = double(kMcuClock) / kFramesPerSecond / kScanlines;
    } else if (variant_ == Variant::BadDudes) {
        mcu_per_line = double(mcu_->clock()) / kFramesPerSecond / kScanlines;
    }
    double main_budget = main_debt_;
    double sound_budget = sound_debt_;
    double mcu_budget = mcu_debt_;

    const uint16_t vblank_bit = dec1() ? 0x0008 : 0x0080;
    for (int line = 0; line < kScanlines; line++) {
        if (line == 8) {
            in1_ = uint16_t(in1_ & ~vblank_bit);
        } else if (line == 248) {
            main_cpu_.set_irq(6, IrqLine::Hold);
            if (variant_ == Variant::Hippodrome) huc6280_->set_irq_line(0, IrqLine::Hold);
            update_video();
            in1_ = uint16_t(in1_ | vblank_bit);
        }
        main_budget += main_per_line;
        main_budget -= main_cpu_.run(int(main_budget));
        sound_budget += sound_per_line;
        if (dec1()) {
            sound_budget -= huc6280_->run(int(sound_budget));
        } else {
            sound_budget -= sound_cpu_->run(int(sound_budget));
            mcu_budget += mcu_per_line;
            if (variant_ == Variant::BadDudes) {
                mcu_budget -= mcu_->run(int(mcu_budget));
            } else {
                mcu_budget -= huc6280_->run(int(mcu_budget));
            }
        }
    }
    main_debt_ = int(main_budget);
    sound_debt_ = int(sound_budget);
    mcu_debt_ = int(mcu_budget);
    frames_++;
    present();
}

void Dec0::update_video() {
    pens_.fill(0);
    const bool odd_frame = (frames_ & 1) != 0;
    switch (variant_) {
        case Variant::Robocop: {
            const uint8_t trans = uint8_t((priority_ & 4) << 1);
            if ((priority_ & 1) != 0) {
                bac06_.tile_2.draw(tiles1_, false, pens_.data());
                if ((priority_ & 2) != 0) {
                    bac06_.draw_sprites(sprites_, 8, trans, odd_frame, pens_.data());
                }
                bac06_.tile_3.draw(tiles2_, true, pens_.data());
            } else {
                bac06_.tile_3.draw(tiles2_, false, pens_.data());
                if ((priority_ & 2) != 0) {
                    bac06_.draw_sprites(sprites_, 8, trans, odd_frame, pens_.data());
                }
                bac06_.tile_2.draw(tiles1_, true, pens_.data());
            }
            if ((priority_ & 2) != 0) {
                bac06_.draw_sprites(sprites_, 8, uint8_t(trans ^ 8), odd_frame, pens_.data());
            } else {
                bac06_.draw_sprites(sprites_, 0, 0, odd_frame, pens_.data());
            }
            break;
        }
        case Variant::BadDudes:
            if ((priority_ & 1) == 0) {
                bac06_.tile_2.draw(tiles1_, false, pens_.data());
                bac06_.tile_3.draw(tiles2_, true, pens_.data());
                if ((priority_ & 2) != 0) bac06_.tile_2.draw_priority(tiles1_, pens_.data());
                bac06_.draw_sprites(sprites_, 0, 0, odd_frame, pens_.data());
                if ((priority_ & 4) != 0) bac06_.tile_3.draw_priority(tiles2_, pens_.data());
            } else {
                bac06_.tile_3.draw(tiles2_, false, pens_.data());
                bac06_.tile_2.draw(tiles1_, true, pens_.data());
                if ((priority_ & 2) != 0) bac06_.tile_3.draw_priority(tiles2_, pens_.data());
                bac06_.draw_sprites(sprites_, 0, 0, odd_frame, pens_.data());
                if ((priority_ & 4) != 0) bac06_.tile_2.draw_priority(tiles1_, pens_.data());
            }
            break;
        case Variant::Hippodrome:
            if ((priority_ & 1) != 0) {
                bac06_.tile_2.draw(tiles1_, false, pens_.data());
                bac06_.tile_3.draw(tiles2_, true, pens_.data());
            } else {
                bac06_.tile_3.draw(tiles2_, false, pens_.data());
                bac06_.tile_2.draw(tiles1_, true, pens_.data());
            }
            bac06_.draw_sprites(sprites_, 0, 0, odd_frame, pens_.data());
            break;
        case Variant::SlySpy:
        case Variant::BoulderDash:
            bac06_.tile_3.draw(tiles2_, false, pens_.data());
            bac06_.tile_2.draw(tiles1_, true, pens_.data());
            bac06_.draw_sprites(sprites_, 0, 0, odd_frame, pens_.data());
            if ((priority_ & 0x80) != 0) bac06_.tile_2.draw_priority(tiles1_, pens_.data());
            break;
    }
    bac06_.tile_1.draw(chars_, true, pens_.data());
}

void Dec0::present() {
    for (int y = 0; y < kScreenHeight; y++) {
        for (int x = 0; x < kScreenWidth; x++) {
            const uint16_t pen = pens_[size_t((y + 8) * Bac06Layer::kScreenWidth + x)];
            framebuffer_[size_t(y * kScreenWidth + x)] = palette_[size_t(pen & 0x3ff)];
        }
    }
}

void Dec0::update_palette_entry(int index) {
    uint8_t red = 0, green = 0, blue = 0;
    if (dec1()) {
        const uint16_t value = palette_ram_[size_t(index)];
        red = pal4bit(value);
        green = pal4bit(uint16_t(value >> 4));
        blue = pal4bit(uint16_t(value >> 8));
    } else {
        red = uint8_t(palette_ram_[size_t(index)] & 0xff);
        green = uint8_t(palette_ram_[size_t(index)] >> 8);
        blue = uint8_t(palette_ram_[size_t(0x400 + index)] & 0xff);
    }
    palette_[size_t(index)] =
        0xff000000u | (uint32_t(red) << 16) | (uint32_t(green) << 8) | uint32_t(blue);
}

void Dec0::write_palette(int index, uint16_t value, int plane) {
    const size_t position = size_t(index + plane * 0x400);
    if (palette_ram_[position] == value) return;
    palette_ram_[position] = value;
    update_palette_entry(index);
}

uint16_t Dec0::main_read(uint32_t address) {
    if (address < 0x60000) return rom_[address >> 1];
    if (address >= 0x180000 && address <= 0x180fff) {
        return mcu_shared_ram_[(address & 0xfff) >> 1];
    }
    if (address >= 0x240000 && address <= 0x24ffff) {
        // Not readable on the board, but the 68000 reads before a byte write.
        switch (address & 0xfff000) {
            case 0x000000:
                if ((address & 0xff8) == 0x000) return bac06_.tile_1.control_0[(address & 7) >> 1];
                if ((address & 0xff8) == 0x010) return bac06_.tile_1.control_1[(address & 7) >> 1];
                break;
            case 0x006000:
                if ((address & 0xff8) == 0x000) return bac06_.tile_2.control_0[(address & 7) >> 1];
                if ((address & 0xff8) == 0x010) return bac06_.tile_2.control_1[(address & 7) >> 1];
                break;
            case 0x00c000:
                if ((address & 0xff8) == 0x000) return bac06_.tile_3.control_0[(address & 7) >> 1];
                if ((address & 0xff8) == 0x010) return bac06_.tile_3.control_1[(address & 7) >> 1];
                break;
            default: break;
        }
        if (address <= 0x24207f) return bac06_.tile_1.colscroll[(address & 0x7f) >> 1];
        if (address >= 0x242400 && address <= 0x2427ff) {
            return bac06_.tile_1.rowscroll[(address & 0x3ff) >> 1];
        }
        if (address >= 0x242800 && address <= 0x243fff) return ram1_[(address - 0x242800) >> 1];
        if (address >= 0x244000 && address <= 0x245fff) {
            return bac06_.tile_1.data[(address & 0x1fff) >> 1];
        }
        if (address >= 0x248000 && address <= 0x24807f) {
            return bac06_.tile_2.colscroll[(address & 0x7f) >> 1];
        }
        if (address >= 0x248400 && address <= 0x2487ff) {
            return bac06_.tile_2.rowscroll[(address & 0x3ff) >> 1];
        }
        if (address >= 0x24a000 && address <= 0x24a7ff) {
            return bac06_.tile_2.data[(address & 0x7ff) >> 1];
        }
        if (address >= 0x24c800 && address <= 0x24c87f) {
            return bac06_.tile_3.colscroll[(address & 0x7f) >> 1];
        }
        if (address >= 0x24cc00 && address <= 0x24cfff) {
            return bac06_.tile_3.rowscroll[(address & 0x3ff) >> 1];
        }
        if (address >= 0x24d000 && address <= 0x24d7ff) {
            return bac06_.tile_3.data[(address & 0x7ff) >> 1];
        }
        return 0;
    }
    switch (address) {
        case 0x30c000: return in0_;
        case 0x30c002: return in1_;
        case 0x30c004: return dsw_;
        case 0x30c006: return 0xffff;
        case 0x30c008: return i8751_return_;
        default: break;
    }
    if (address >= 0x310000 && address <= 0x3107ff) return palette_ram_[(address & 0x7ff) >> 1];
    if (address >= 0x314000 && address <= 0x3147ff) {
        return palette_ram_[((address & 0x7ff) >> 1) + 0x400];
    }
    if (address >= 0xff8000 && address <= 0xffbfff) return ram2_[(address & 0x3fff) >> 1];
    if (address >= 0xffc000 && address <= 0xffcfff) return sprite_buffer_[(address & 0x7ff) >> 1];
    return 0;
}

void Dec0::main_write(uint32_t address, uint16_t value) {
    if (address < 0x60000) return;  // ROM
    if (address >= 0x180000 && address <= 0x180fff) {
        mcu_shared_ram_[(address & 0xfff) >> 1] = uint8_t(value & 0xff);
        if ((address & 0xfff) == 0xffe && huc6280_) huc6280_->set_irq_line(0, IrqLine::Hold);
        return;
    }
    if (address >= 0x240000 && address <= 0x240007) {
        bac06_.tile_1.change_control0(int((address & 7) >> 1), value);
        return;
    }
    if (address >= 0x240010 && address <= 0x240017) {
        bac06_.tile_1.change_control1(int((address & 7) >> 1), value);
        return;
    }
    if (address >= 0x242000 && address <= 0x24207f) {
        bac06_.tile_1.colscroll[(address & 0x7f) >> 1] = value;
        return;
    }
    if (address >= 0x242400 && address <= 0x2427ff) {
        bac06_.tile_1.rowscroll[(address & 0x3ff) >> 1] = value;
        return;
    }
    if (address >= 0x242800 && address <= 0x243fff) {
        ram1_[(address - 0x242800) >> 1] = value;
        return;
    }
    if (address >= 0x244000 && address <= 0x245fff) {
        bac06_.tile_1.data[(address & 0x1fff) >> 1] = value;
        return;
    }
    if (address >= 0x246000 && address <= 0x246007) {
        bac06_.tile_2.change_control0(int((address & 7) >> 1), value);
        return;
    }
    if (address >= 0x246010 && address <= 0x246017) {
        bac06_.tile_2.change_control1(int((address & 7) >> 1), value);
        return;
    }
    if (address >= 0x248000 && address <= 0x24807f) {
        bac06_.tile_2.colscroll[(address & 0x7f) >> 1] = value;
        return;
    }
    if (address >= 0x248400 && address <= 0x2487ff) {
        bac06_.tile_2.rowscroll[(address & 0x3ff) >> 1] = value;
        return;
    }
    if (address >= 0x24a000 && address <= 0x24a7ff) {
        bac06_.tile_2.data[(address & 0x7ff) >> 1] = value;
        return;
    }
    if (address >= 0x24c000 && address <= 0x24c007) {
        bac06_.tile_3.change_control0(int((address & 7) >> 1), value);
        return;
    }
    if (address >= 0x24c010 && address <= 0x24c017) {
        bac06_.tile_3.change_control1(int((address & 7) >> 1), value);
        return;
    }
    if (address >= 0x24c800 && address <= 0x24c87f) {
        bac06_.tile_3.colscroll[(address & 0x7f) >> 1] = value;
        return;
    }
    if (address >= 0x24cc00 && address <= 0x24cfff) {
        bac06_.tile_3.rowscroll[(address & 0x3ff) >> 1] = value;
        return;
    }
    if (address >= 0x24d000 && address <= 0x24d7ff) {
        bac06_.tile_3.data[(address & 0x7ff) >> 1] = value;
        return;
    }
    if (address >= 0x30c010 && address <= 0x30c01f) {
        switch (address & 0xf) {
            case 0x0: priority_ = uint8_t(value & 0xff); break;
            case 0x2: bac06_.update_sprite_data(sprite_buffer_.data()); break;
            case 0x4:
                sound_latch_ = uint8_t(value & 0xff);
                if (sound_cpu_) sound_cpu_->set_nmi(IrqLine::Pulse);
                break;
            case 0x6:
                i8751_command_ = value;
                if (mcu_ && (i8751_ports_[2] & 8) != 0) mcu_->set_irq1_line(IrqLine::Assert);
                break;
            case 0xe:
                i8751_command_ = 0;
                i8751_return_ = 0;
                break;
            default: break;
        }
        return;
    }
    if (address >= 0x310000 && address <= 0x3107ff) {
        write_palette(int((address & 0x7ff) >> 1), value, 0);
        return;
    }
    if (address >= 0x314000 && address <= 0x3147ff) {
        write_palette(int((address & 0x7ff) >> 1), value, 1);
        return;
    }
    if (address >= 0xff8000 && address <= 0xffbfff) {
        ram2_[(address & 0x3fff) >> 1] = value;
        return;
    }
    if (address >= 0xffc000 && address <= 0xffcfff) {
        sprite_buffer_[(address & 0x7ff) >> 1] = value;
    }
}

uint8_t Dec0::sound_read(uint16_t address) {
    if (address <= 0x07ff || address >= 0x8000) return sound_memory_[address];
    if (address == 0x3000) return sound_latch_;
    if (address == 0x3800) return oki_.read();
    return 0;
}

void Dec0::sound_write(uint16_t address, uint8_t value) {
    if (address <= 0x07ff) {
        sound_memory_[address] = value;
        return;
    }
    switch (address) {
        case 0x0800: ym2203_.control(value); break;
        case 0x0801: ym2203_.write(value); break;
        case 0x1000: ym3812_.control(value); break;
        case 0x1001: ym3812_.write(value); break;
        case 0x3800: oki_.write(value); break;
        default: break;
    }
}

uint8_t Dec0::robocop_mcu_read(uint32_t address) {
    if (address >= 0x1e00 && address <= 0x1fff) return mcu_rom_[address & 0x1ff];
    if (address >= 0x1f0000 && address <= 0x1f1fff) return mcu_ram_[address & 0x1fff];
    if (address >= 0x1f2000 && address <= 0x1f3fff) return mcu_shared_ram_[address & 0x1fff];
    return 0;
}

void Dec0::robocop_mcu_write(uint32_t address, uint8_t value) {
    if (address <= 0xffff) return;  // ROM
    if (address >= 0x1f0000 && address <= 0x1f1fff) {
        mcu_ram_[address & 0x1fff] = value;
        return;
    }
    if (address >= 0x1f2000 && address <= 0x1f3fff) {
        mcu_shared_ram_[address & 0x1fff] = value;
        return;
    }
    if (address >= 0x1ff400 && address <= 0x1ff403) {
        huc6280_->irq_status_w(uint8_t(address & 3), value);
    }
}

uint8_t Dec0::hippo_mcu_read(uint32_t address) {
    if (address <= 0xffff) return mcu_rom_[address];
    if (address >= 0x180000 && address <= 0x1800ff) return mcu_shared_ram_[address & 0xff];
    if (address == 0x1807ff) return 0xff;
    if (address >= 0x1d0000 && address <= 0x1d00ff) {
        // Protection handshake.
        if (hippodrm_lsb_ == 0x45) return 0x4e;
        if (hippodrm_lsb_ == 0x92) return 0x15;
        return 0;
    }
    if (address >= 0x1a1000 && address <= 0x1a17ff) {
        const uint16_t word = bac06_.tile_3.data[(address & 0x7ff) >> 1];
        return uint8_t((address & 1) != 0 ? word >> 8 : word);
    }
    if (address >= 0x1f0000 && address <= 0x1f1fff) return mcu_ram_[address & 0x1fff];
    if (address >= 0x1ff402 && address <= 0x1ff403) return uint8_t(in1_ >> 7);
    return 0;
}

void Dec0::hippo_mcu_write(uint32_t address, uint8_t value) {
    if (address <= 0xffff) return;  // ROM
    if (address >= 0x180000 && address <= 0x1800ff) {
        mcu_shared_ram_[address & 0xff] = value;
        return;
    }
    if (address >= 0x1a0000 && address <= 0x1a0007) {
        const int position = int((address & 7) >> 1);
        const uint16_t current = bac06_.tile_3.control_0[size_t(position)];
        const uint16_t word = (address & 1) != 0
                                  ? uint16_t((current & 0x00ff) | (uint16_t(value) << 8))
                                  : uint16_t((current & 0xff00) | value);
        bac06_.tile_3.change_control0(position, word);
        return;
    }
    if (address >= 0x1a0010 && address <= 0x1a001f) {
        const int position = int((address & 7) >> 1);
        const uint16_t current = bac06_.tile_3.control_1[size_t(position)];
        const uint16_t word = (address & 1) != 0
                                  ? uint16_t((current & 0x00ff) | (uint16_t(value) << 8))
                                  : uint16_t((current & 0xff00) | value);
        bac06_.tile_3.change_control1(position, word);
        return;
    }
    if (address >= 0x1a1000 && address <= 0x1a17ff) {
        const size_t position = (address & 0x7ff) >> 1;
        const uint16_t current = bac06_.tile_3.data[position];
        bac06_.tile_3.data[position] = (address & 1) != 0
                                           ? uint16_t((current & 0x00ff) | (uint16_t(value) << 8))
                                           : uint16_t((current & 0xff00) | value);
        return;
    }
    if (address >= 0x1d0000 && address <= 0x1d00ff) {
        hippodrm_lsb_ = value;
        return;
    }
    if (address >= 0x1f0000 && address <= 0x1f1fff) {
        mcu_ram_[address & 0x1fff] = value;
        return;
    }
    if (address >= 0x1ff400 && address <= 0x1ff403) {
        huc6280_->irq_status_w(uint8_t(address & 3), value);
    }
}

uint8_t Dec0::mcu_port0_read() {
    uint8_t result = 0xff;
    if ((i8751_ports_[2] & 0x10) == 0) result = uint8_t(result & (i8751_command_ >> 8));
    if ((i8751_ports_[2] & 0x20) == 0) result = uint8_t(result & (i8751_command_ & 0xff));
    return result;
}

void Dec0::mcu_port_write(int port, uint8_t value) {
    if (port != 2) {
        i8751_ports_[size_t(port)] = value;
        return;
    }
    if ((value & 4) == 0 && (i8751_ports_[2] & 4) != 0) main_cpu_.set_irq(5, IrqLine::Hold);
    if ((value & 8) == 0) mcu_->set_irq1_line(IrqLine::Clear);
    if ((value & 0x40) != 0 && (i8751_ports_[2] & 0x40) == 0) {
        i8751_return_ = uint16_t((i8751_return_ & 0xff00) | i8751_ports_[0]);
    }
    if ((value & 0x80) != 0 && (i8751_ports_[2] & 0x80) == 0) {
        i8751_return_ = uint16_t((i8751_return_ & 0x00ff) | (uint16_t(i8751_ports_[0]) << 8));
    }
    i8751_ports_[2] = value;
}

uint16_t Dec0::slyspy_read(uint32_t address) {
    if (address < 0x60000) return rom_[address >> 1];
    if (address >= 0x240000 && address <= 0x24ffff) {
        const uint32_t key = (address & 0xffff) | (uint32_t(slyspy_state_) << 16);
        if ((key & 0xffff) == 0x4000) {
            slyspy_state_ = uint8_t((slyspy_state_ + 1) & 3);
            return 0;
        }
        // The banked video area is write only, but the 68000 reads a word before
        // every byte write; returning the current contents keeps both halves.
        switch (key) {
            case 0x0000:
            case 0x0002:
            case 0x0004:
            case 0x0006: return bac06_.tile_2.control_0[(address & 7) >> 1];
            case 0x0010:
            case 0x0012:
            case 0x0014:
            case 0x0016: return bac06_.tile_2.control_1[(address & 7) >> 1];
            case 0x8000:
            case 0x8002:
            case 0x8004:
            case 0x8006: return bac06_.tile_1.control_0[(address & 7) >> 1];
            case 0x8010:
            case 0x8012:
            case 0x8014:
            case 0x8016: return bac06_.tile_1.control_1[(address & 7) >> 1];
            default: break;
        }
        const uint32_t offset = key & 0xffff;
        if (key <= 0xffff) {  // state 0
            if (offset >= 0x2000 && offset <= 0x207f) {
                return bac06_.tile_2.colscroll[(offset & 0x7f) >> 1];
            }
            if (offset >= 0x2400 && offset <= 0x27ff) {
                return bac06_.tile_2.rowscroll[(offset & 0x3ff) >> 1];
            }
            if (offset >= 0x6000 && offset <= 0x7fff) {
                return bac06_.tile_2.data[(offset & 0x1fff) >> 1];
            }
            if (offset >= 0xc000 && offset <= 0xc07f) {
                return bac06_.tile_1.colscroll[(offset & 0x7f) >> 1];
            }
            if (offset >= 0xc400 && offset <= 0xc7ff) {
                return bac06_.tile_1.rowscroll[(offset & 0x3ff) >> 1];
            }
            if (offset >= 0xe000) return bac06_.tile_1.data[(offset & 0x1fff) >> 1];
            return 0;
        }
        if (key >= 0x18000 && key <= 0x19fff) return bac06_.tile_1.data[(offset & 0x1fff) >> 1];
        if (key >= 0x1c000 && key <= 0x1dfff) return bac06_.tile_2.data[(offset & 0x1fff) >> 1];
        if (key >= 0x20000 && key <= 0x21fff) return bac06_.tile_2.data[(offset & 0x1fff) >> 1];
        if (key >= 0x22000 && key <= 0x23fff) return bac06_.tile_1.data[(offset & 0x1fff) >> 1];
        if (key >= 0x30000 && key <= 0x31fff) return bac06_.tile_1.data[(offset & 0x1fff) >> 1];
        if (key >= 0x38000 && key <= 0x39fff) return bac06_.tile_2.data[(offset & 0x1fff) >> 1];
        return 0;
    }
    if (address >= 0x300800 && address <= 0x30087f) {
        return bac06_.tile_3.colscroll[(address & 0x7f) >> 1];
    }
    if (address >= 0x300c00 && address <= 0x300fff) {
        return bac06_.tile_3.rowscroll[(address & 0x3ff) >> 1];
    }
    if (address >= 0x301000 && address <= 0x3017ff) {
        return bac06_.tile_3.data[(address & 0x7ff) >> 1];
    }
    if (address >= 0x304000 && address <= 0x307fff) return ram2_[(address & 0x3fff) >> 1];
    if (address >= 0x308000 && address <= 0x3087ff) return sprite_buffer_[(address & 0x7ff) >> 1];
    if (address >= 0x310000 && address <= 0x3107ff) return palette_ram_[(address & 0x7ff) >> 1];
    if (address >= 0x314008 && address <= 0x31400f) {
        switch ((address & 7) >> 1) {
            case 0: return dsw_;
            case 1: return in0_;
            case 2: return in1_;
            default: return 0xffff;
        }
    }
    if (address >= 0x31c000 && address <= 0x31c00f) {
        switch (address & 0xe) {
            case 0x0:
            case 0x4: return 0;
            case 0x2: return 0x13;
            case 0x6: return 2;
            case 0xc: return uint16_t(ram2_[0x2028 >> 1] >> 8);
            default: return 0;
        }
    }
    return 0;
}

void Dec0::slyspy_write(uint32_t address, uint16_t value) {
    if (address < 0x60000) return;  // ROM
    if (address >= 0x240000 && address <= 0x24ffff) {
        const uint32_t key = (address & 0xffff) | (uint32_t(slyspy_state_) << 16);
        const uint32_t offset = address & 0xffff;
        if (offset == 0xa000) {
            slyspy_state_ = 0;
            return;
        }
        if (key <= 0xffff) {  // state 0
            if (offset <= 0x0007) {
                bac06_.tile_2.change_control0(int((offset & 7) >> 1), value);
            } else if (offset >= 0x0010 && offset <= 0x0017) {
                bac06_.tile_2.change_control1(int((offset & 7) >> 1), value, true);
            } else if (offset >= 0x2000 && offset <= 0x207f) {
                bac06_.tile_2.colscroll[(offset & 0x7f) >> 1] = value;
            } else if (offset >= 0x2400 && offset <= 0x27ff) {
                bac06_.tile_2.rowscroll[(offset & 0x3ff) >> 1] = value;
            } else if (offset >= 0x6000 && offset <= 0x7fff) {
                bac06_.tile_2.data[(offset & 0x1fff) >> 1] = value;
            } else if (offset >= 0x8000 && offset <= 0x8007) {
                bac06_.tile_1.change_control0(int((offset & 7) >> 1), value);
            } else if (offset >= 0x8010 && offset <= 0x8017) {
                bac06_.tile_1.change_control1(int((offset & 7) >> 1), value);
            } else if (offset >= 0xc000 && offset <= 0xc07f) {
                bac06_.tile_1.colscroll[(offset & 0x7f) >> 1] = value;
            } else if (offset >= 0xc400 && offset <= 0xc7ff) {
                bac06_.tile_1.rowscroll[(offset & 0x3ff) >> 1] = value;
            } else if (offset >= 0xe000) {
                bac06_.tile_1.data[(offset & 0x1fff) >> 1] = value;
            }
            return;
        }
        if (key >= 0x18000 && key <= 0x19fff) {
            bac06_.tile_1.data[(offset & 0x1fff) >> 1] = value;
        } else if (key >= 0x1c000 && key <= 0x1dfff) {
            bac06_.tile_2.data[(offset & 0x1fff) >> 1] = value;
        } else if (key >= 0x20000 && key <= 0x21fff) {
            bac06_.tile_2.data[(offset & 0x1fff) >> 1] = value;
        } else if (key >= 0x22000 && key <= 0x23fff) {
            bac06_.tile_1.data[(offset & 0x1fff) >> 1] = value;
        } else if (key >= 0x30000 && key <= 0x31fff) {
            bac06_.tile_1.data[(offset & 0x1fff) >> 1] = value;
        } else if (key >= 0x38000 && key <= 0x39fff) {
            bac06_.tile_2.data[(offset & 0x1fff) >> 1] = value;
        }
        return;
    }
    if (address >= 0x300000 && address <= 0x300007) {
        bac06_.tile_3.change_control0(int((address & 7) >> 1), value);
        return;
    }
    if (address >= 0x300010 && address <= 0x300017) {
        bac06_.tile_3.change_control1(int((address & 7) >> 1), value);
        return;
    }
    if (address >= 0x300800 && address <= 0x30087f) {
        bac06_.tile_3.colscroll[(address & 0x7f) >> 1] = value;
        return;
    }
    if (address >= 0x300c00 && address <= 0x300fff) {
        bac06_.tile_3.rowscroll[(address & 0x3ff) >> 1] = value;
        return;
    }
    if (address >= 0x301000 && address <= 0x3017ff) {
        bac06_.tile_3.data[(address & 0x7ff) >> 1] = value;
        return;
    }
    if (address >= 0x304000 && address <= 0x307fff) {
        ram2_[(address & 0x3fff) >> 1] = value;
        return;
    }
    if (address >= 0x308000 && address <= 0x3087ff) {
        sprite_buffer_[(address & 0x7ff) >> 1] = value;
        bac06_.update_sprite_data(sprite_buffer_.data());
        return;
    }
    if (address >= 0x310000 && address <= 0x3107ff) {
        write_palette(int((address & 0x7ff) >> 1), value, 0);
        return;
    }
    if (address >= 0x314000 && address <= 0x31400f) {
        switch ((address & 0xf) >> 1) {
            case 0:
                sound_latch_ = uint8_t(value & 0xff);
                huc6280_->set_irq_line(0, IrqLine::Hold);
                break;
            case 1: priority_ = uint8_t(value & 0xff); break;
            default: break;
        }
    }
}

uint8_t Dec0::slyspy_sound_read(uint32_t address) {
    if (address <= 0xffff) return sound_memory_[address];
    if (address >= 0x80000 && address <= 0xfffff) {
        const uint32_t key = (address & 0x7ffff) | (uint32_t(slyspy_sound_state_) * 0x80000);
        switch (key) {
            case 0x020000:
            case 0x0a0000:
            case 0x120000:
            case 0x1a0000: slyspy_sound_state_ = uint8_t((slyspy_sound_state_ + 1) & 3); return 0;
            case 0x050000:
            case 0x0d0000:
            case 0x150000:
            case 0x1d0000: slyspy_sound_state_ = 0; return 0;
            case 0x060000:
            case 0x090000:
            case 0x130000:
            case 0x1f0000: return oki_.read();
            case 0x070000:
            case 0x0c0000:
            case 0x110000:
            case 0x1e0000: return sound_latch_;
            default: return 0;
        }
    }
    if (address >= 0x1f0000) return mcu_ram_[address & 0x1fff];
    return 0;
}

void Dec0::slyspy_sound_write(uint32_t address, uint8_t value) {
    if (address <= 0xffff) return;  // ROM
    if (address >= 0x80000 && address <= 0xfffff) {
        const uint32_t key = (address & 0x7ffff) | (uint32_t(slyspy_sound_state_) * 0x80000);
        switch (key) {
            case 0x010000:
            case 0x0f0000:
            case 0x170000:
            case 0x190000: ym3812_.control(value); break;
            case 0x010001:
            case 0x0f0001:
            case 0x170001:
            case 0x190001: ym3812_.write(value); break;
            case 0x030000:
            case 0x0e0000:
            case 0x140000:
            case 0x1c0000: ym2203_.control(value); break;
            case 0x030001:
            case 0x0e0001:
            case 0x140001:
            case 0x1c0001: ym2203_.write(value); break;
            case 0x060000:
            case 0x090000:
            case 0x130000:
            case 0x1f0000: oki_.write(value); break;
            default: break;
        }
        return;
    }
    if (address >= 0x1f0000) mcu_ram_[address & 0x1fff] = value;
}

void Dec0::set_inputs(const MachineInputs& inputs) {
    const InputState& player1 = inputs.player1;
    const InputState& player2 = inputs.player2;
    uint16_t in0 = 0xffff;
    if (player1.up) in0 &= 0xfffe;
    if (player1.down) in0 &= 0xfffd;
    if (player1.left) in0 &= 0xfffb;
    if (player1.right) in0 &= 0xfff7;
    if (player1.button1) in0 &= 0xffef;
    if (player1.button2) in0 &= 0xffdf;
    if (player1.button3) in0 &= 0xffbf;
    if (player2.up) in0 &= 0xfeff;
    if (player2.down) in0 &= 0xfdff;
    if (player2.left) in0 &= 0xfbff;
    if (player2.right) in0 &= 0xf7ff;
    if (player2.button1) in0 &= 0xefff;
    if (player2.button2) in0 &= 0xdfff;
    if (player2.button3) in0 &= 0xbfff;

    const uint16_t vblank_bit = dec1() ? 0x0008 : 0x0080;
    uint16_t in1 = uint16_t(0xffff & ~vblank_bit);
    if (dec1()) {
        // Start buttons live in the player port, coins in the system port.
        if (player1.start) in0 &= 0xff7f;
        if (player2.start) in0 &= 0x7fff;
        if (inputs.coin1) in1 &= 0xfffe;
        if (inputs.coin2) in1 &= 0xfffd;
    } else {
        if (player1.start) in1 &= 0xfffb;
        if (player2.start) in1 &= 0xfff7;
        if (inputs.coin1) in1 &= 0xffef;
        if (inputs.coin2) in1 &= 0xffdf;
    }
    in0_ = in0;
    in1_ = uint16_t((in1 & ~vblank_bit) | (in1_ & vblank_bit));
}

void Dec0::set_dip_switch(int bank, uint8_t value) {
    if (bank == 0) {
        dsw_ = uint16_t((dsw_ & 0xff00) | value);
    } else if (bank == 1) {
        dsw_ = uint16_t((dsw_ & 0x00ff) | (uint16_t(value) << 8));
    }
}

void Dec0::drain_audio(std::vector<int16_t>& out) {
    out.insert(out.end(), audio_.begin(), audio_.end());
    audio_.clear();
}

}  // namespace dsp
