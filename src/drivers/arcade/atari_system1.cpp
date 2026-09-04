#include "drivers/arcade/atari_system1.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>

#include "core/rom_loader.h"

namespace dsp {
namespace {

const std::vector<RomEntry> kBiosRoms = {
    {"136032.205.l13|136032.205", 0x4000, 0x0, 0x88d0be26},
    {"136032.206.l12|136032.206", 0x4000, 0x1, 0x3c79ef05},
};

const std::vector<RomEntry> kCharRoms = {
    {"136032.104.f5|136032.104|136032.107.b2", 0x2000, 0x0, 0x7a29dc07},
};

const std::vector<RomEntry> kPeterRom = {
    {"136028.142", 0x4000, 0x00000, 0x4f9fc020},
    {"136028.143", 0x4000, 0x00001, 0x9fb257cc},
    {"136028.144", 0x4000, 0x08000, 0x50267619},
    {"136028.145", 0x4000, 0x08001, 0x7b6a5004},
    {"136028.146", 0x4000, 0x10000, 0x4183a67a},
    {"136028.147", 0x4000, 0x10001, 0x14e2d97b},
    {"136028.148", 0x4000, 0x20000, 0x230e8ba9},
    {"136028.149", 0x4000, 0x20001, 0x0ff0c13a},
};
const std::vector<RomEntry> kPeterSound = {
    {"136028.101", 0x4000, 0x8000, 0xff712aa2},
    {"136028.102", 0x4000, 0xc000, 0x89ea21a1},
};
const std::vector<RomEntry> kPeterBack = {
    {"136028.138", 0x8000, 0x00000, 0x53eaa018},
    {"136028.139", 0x8000, 0x10000, 0x354a19cb},
    {"136028.140", 0x8000, 0x20000, 0x8d2c4717},
    {"136028.141", 0x8000, 0x30000, 0xbf59ea19},
    {"136028.150", 0x8000, 0x80000, 0x83362483},
    {"136028.151", 0x8000, 0x90000, 0x6e95094e},
    {"136028.152", 0x8000, 0xa0000, 0x9553f084},
    {"136028.153", 0x8000, 0xb0000, 0xc2a9b028},
    {"136028.105", 0x4000, 0x104000, 0xac9a5a44},
    {"136028.108", 0x4000, 0x114000, 0x51941e64},
    {"136028.111", 0x4000, 0x124000, 0x246599f3},
    {"136028.114", 0x4000, 0x134000, 0x918a5082},
};
const std::vector<RomEntry> kPeterProms = {
    {"136028.136", 0x200, 0x000, 0x861cfa36},
    {"136028.137", 0x200, 0x200, 0x8507e5ea},
};

const std::vector<RomEntry> kIndyRom = {
    {"136036.432", 0x8000, 0x00000, 0xd888cdf1},
    {"136036.431", 0x8000, 0x00001, 0xb7ac7431},
    {"136036.434", 0x8000, 0x10000, 0x802495fd},
    {"136036.433", 0x8000, 0x10001, 0x3a914e5c},
    {"136036.456", 0x4000, 0x20000, 0xec146b09},
    {"136036.457", 0x4000, 0x20001, 0x6628de01},
    {"136036.358", 0x4000, 0x28000, 0xd9351106},
    {"136036.359", 0x4000, 0x28001, 0xe731caea},
};
const std::vector<RomEntry> kIndySound = {
    {"136036.153", 0x4000, 0x4000, 0x95294641},
    {"136036.154", 0x4000, 0x8000, 0xcbfc6adb},
    {"136036.155", 0x4000, 0xc000, 0x4c8233ac},
};
const std::vector<RomEntry> kIndyBack = {
    {"136036.135", 0x8000, 0x000000, 0xffa8749c},
    {"136036.139", 0x8000, 0x010000, 0xb682bfca},
    {"136036.143", 0x8000, 0x020000, 0x7697da26},
    {"136036.147", 0x8000, 0x030000, 0x4e9d664c},
    {"136036.136", 0x8000, 0x080000, 0xb2b403aa},
    {"136036.140", 0x8000, 0x090000, 0xec0c19ca},
    {"136036.144", 0x8000, 0x0a0000, 0x4407df98},
    {"136036.148", 0x8000, 0x0b0000, 0x70dce06d},
    {"136036.137", 0x8000, 0x100000, 0x3f352547},
    {"136036.141", 0x8000, 0x110000, 0x9cbdffd0},
    {"136036.145", 0x8000, 0x120000, 0xe828e64b},
    {"136036.149", 0x8000, 0x130000, 0x81503a23},
    {"136036.138", 0x8000, 0x180000, 0x48c4d79d},
    {"136036.142", 0x8000, 0x190000, 0x7faae75f},
    {"136036.146", 0x8000, 0x1a0000, 0x8ae5a7b5},
    {"136036.150", 0x8000, 0x1b0000, 0xa10c4bd9},
};
const std::vector<RomEntry> kIndyProms = {
    {"136036.152", 0x200, 0x000, 0x4f96e57c},
    {"136036.151", 0x200, 0x200, 0x7daf351f},
};

const std::vector<RomEntry> kMarbleRom = {
    {"136033.623", 0x4000, 0x00000, 0x284ed2e9},
    {"136033.624", 0x4000, 0x00001, 0xd541b021},
    {"136033.625", 0x4000, 0x08000, 0x563755c7},
    {"136033.626", 0x4000, 0x08001, 0x860feeb3},
    {"136033.627", 0x4000, 0x10000, 0xd1dbd439},
    {"136033.628", 0x4000, 0x10001, 0x957d6801},
    {"136033.229|136033.129", 0x4000, 0x18000, 0xc81d5c14},
    {"136033.630|136033.130", 0x4000, 0x18001, 0x687a09f7},
    {"136033.107", 0x4000, 0x20000, 0xf3b8745b},
    {"136033.108", 0x4000, 0x20001, 0xe51eecaa},
};
const std::vector<RomEntry> kMarbleSound = {
    {"136033.421", 0x4000, 0x8000, 0x78153dc3},
    {"136033.422", 0x4000, 0xc000, 0x2e66300e},
};
const std::vector<RomEntry> kMarbleBack = {
    {"136033.137", 0x4000, 0x00000, 0x7a45f5c1},
    {"136033.138", 0x4000, 0x04000, 0x7e954a88},
    {"136033.139", 0x4000, 0x10000, 0x1eb1bb5f},
    {"136033.140", 0x4000, 0x14000, 0x8a82467b},
    {"136033.141", 0x4000, 0x20000, 0x52448965},
    {"136033.142", 0x4000, 0x24000, 0xb4a70e4f},
    {"136033.143", 0x4000, 0x30000, 0x7156e449},
    {"136033.144", 0x4000, 0x34000, 0x4c3e4c79},
    {"136033.145", 0x4000, 0x40000, 0x9062be7f},
    {"136033.146", 0x4000, 0x44000, 0x14566dca},
    {"136033.149", 0x4000, 0x84000, 0xb6658f06},
    {"136033.151", 0x4000, 0x94000, 0x84ee1c80},
    {"136033.153", 0x4000, 0xa4000, 0xdaa02926},
};
const std::vector<RomEntry> kMarbleProms = {
    {"136033.118", 0x200, 0x000, 0x2101b0ed},
    {"136033.119", 0x200, 0x200, 0x19f6e767},
};

const std::vector<RomEntry> kRoadRom = {
    {"136040-228.11c|136040.228|136040-228", 0x8000, 0x10000, 0xb66c629a},
    {"136040-229.11a|136040.229|136040-229", 0x8000, 0x10001, 0x5638959f},
    {"136040-230.13c|136040.230|136040-230", 0x8000, 0x20000, 0xcd7956a3},
    {"136040-231.13a|136040.231|136040-231", 0x8000, 0x20001, 0x722f2d3b},
    {"136040-134.12c|136040.134|136040-134", 0x8000, 0x50000, 0x18f431fe},
    {"136040-135.12a|136040.135|136040-135", 0x8000, 0x50001, 0xcb06f9ab},
    {"136040-136.14c|136040.136|136040-136", 0x8000, 0x60000, 0x8050bce4},
    {"136040-137.14a|136040.137|136040-137", 0x8000, 0x60001, 0x3372a5cf},
    {"136040-138.16c|136040.138|136040-138", 0x8000, 0x70000, 0xa83155ee},
    {"136040-139.16a|136040.139|136040-139", 0x8000, 0x70001, 0x23aead1c},
    {"136040-140.17c|136040.140|136040-140", 0x4000, 0x80000, 0xd1464c88},
    {"136040-141.17a|136040.141|136040-141", 0x4000, 0x80001, 0xf8f2acdf},
};
const std::vector<RomEntry> kRoadSound = {
    {"136040-143.15e|136040.143|136040-143", 0x4000, 0x8000, 0x62b9878e},
    {"136040-144.17e|136040.144|136040-144", 0x4000, 0xc000, 0x6ef1b804},
};
const std::vector<RomEntry> kRoadBack = {
    {"136040-101.4b|136040.101|136040-101", 0x8000, 0x000000, 0x26d9f29c},
    {"136040-107.9b|136040.107|136040-107", 0x8000, 0x010000, 0x8aac0ba4},
    {"136040-113.4f|136040.113|136040-113", 0x8000, 0x020000, 0x48b74c52},
    {"136040-119.9f|136040.119|136040-119", 0x8000, 0x030000, 0x17a6510c},
    {"136040-102.3b|136040.102|136040-102", 0x8000, 0x080000, 0xae88f54b},
    {"136040-108.8b|136040.108|136040-108", 0x8000, 0x090000, 0xa2ac13d4},
    {"136040-114.3f|136040.114|136040-114", 0x8000, 0x0a0000, 0xc91c3fcb},
    {"136040-120.8f|136040.120|136040-120", 0x8000, 0x0b0000, 0x42d25859},
    {"136040-103.2b|136040.103|136040-103", 0x8000, 0x100000, 0xf2d7ef55},
    {"136040-109.7b|136040.109|136040-109", 0x8000, 0x110000, 0x11a843dc},
    {"136040-115.2f|136040.115|136040-115", 0x8000, 0x120000, 0x8b1fa5bc},
    {"136040-121.7f|136040.121|136040-121", 0x8000, 0x130000, 0xecf278f2},
    {"136040-104.1b|136040.104|136040-104", 0x8000, 0x180000, 0x0203d89c},
    {"136040-110.6b|136040.110|136040-110", 0x8000, 0x190000, 0x64801601},
    {"136040-116.1f|136040.116|136040-116", 0x8000, 0x1a0000, 0x52b23a36},
    {"136040-122.6f|136040.122|136040-122", 0x8000, 0x1b0000, 0xb1137a9d},
    {"136040-105.4d|136040.105|136040-105", 0x8000, 0x200000, 0x398a36f8},
    {"136040-111.9d|136040.111|136040-111", 0x8000, 0x210000, 0xf08b418b},
    {"136040-117.2d|136040.117|136040-117", 0x8000, 0x220000, 0xc4394834},
    {"136040-123.7d|136040.123|136040-123", 0x8000, 0x230000, 0xdafd3dbe},
    {"136040-106.3d|136040.106|136040-106", 0x8000, 0x280000, 0x36a77bc5},
    {"136040-112.8d|136040.112|136040-112", 0x8000, 0x290000, 0xb6624f3c},
    {"136040-118.1d|136040.118|136040-118", 0x8000, 0x2a0000, 0xf489a968},
    {"136040-124.6d|136040.124|136040-124", 0x8000, 0x2b0000, 0x524d65f7},
};
const std::vector<RomEntry> kRoadProms = {
    {"136040-126.7a|136040.126|136040-126", 0x200, 0x000, 0x1713c0cd},
    {"136040-125.5a|136040.125|136040-125", 0x200, 0x200, 0xa9ca8795},
};

GfxLayout char_layout() {
    GfxLayout layout;
    layout.width = 8;
    layout.height = 8;
    layout.total = 0x200;
    layout.planes = 2;
    layout.char_increment = 16 * 8;
    layout.plane_offsets = {0, 4};
    layout.x_offsets = {0, 1, 2, 3, 8, 9, 10, 11};
    layout.y_offsets = {0 * 16, 1 * 16, 2 * 16, 3 * 16, 4 * 16, 5 * 16, 6 * 16, 7 * 16};
    return layout;
}

GfxLayout bank_layout(int bpp) {
    GfxLayout layout;
    layout.width = 8;
    layout.height = 8;
    layout.total = 0x1000;
    layout.planes = bpp;
    layout.char_increment = 8 * 8;
    layout.x_offsets = {0, 1, 2, 3, 4, 5, 6, 7};
    layout.y_offsets = {0 * 8, 1 * 8, 2 * 8, 3 * 8, 4 * 8, 5 * 8, 6 * 8, 7 * 8};
    layout.plane_offsets.resize(size_t(bpp));
    for (int plane = 0; plane < bpp; plane++) {
        layout.plane_offsets[size_t(plane)] = (bpp - 1 - plane) * 8 * 0x10000;
    }
    return layout;
}

AtariMotionObjects::Config motion_config() {
    AtariMotionObjects::Config config;
    config.tile_width = 8;
    config.tile_height = 8;
    config.bankcount = 8;
    config.linked = true;
    config.split = true;
    config.slipheight = 0;
    config.maxperline = 0x38;
    config.palettebase = 0x100;
    config.link_entry = {0, 0, 0, 0x003f};
    config.code_entry = {{0, 0xffff, 0, 0}, {0, 0, 0, 0}};
    config.color_entry = {{0, 0xff00, 0, 0}, {0, 0, 0, 0}};
    config.xpos_entry = {0, 0, 0x3fe0, 0};
    config.ypos_entry = {0x3fe0, 0, 0, 0};
    config.height_entry = {0x000f, 0, 0, 0};
    config.hflip_entry = {0x8000, 0, 0, 0};
    config.priority_entry = {0, 0, 0x8000, 0};
    config.special_entry = {0, 0xffff, 0, 0};
    config.specialvalue = 0xffff;
    return config;
}

uint8_t pal4bit_intensity(uint16_t bits, uint16_t intensity) {
    static const uint8_t kTable[16] = {0x0, 0x3, 0x4, 0x5, 0x6, 0x7, 0x8, 0x9,
                                       0xa, 0xb, 0xc, 0xd, 0xe, 0xf, 0x10, 0x11};
    return uint8_t((bits & 0x0f) * kTable[intensity & 0x0f]);
}

bool load_16w(RomLoader& loader, const std::vector<RomEntry>& entries, std::vector<uint16_t>& words,
              std::string* error) {
    uint32_t needed = 0;
    for (const RomEntry& entry : entries) {
        needed = std::max(needed, (entry.offset & ~1u) + entry.length * 2);
    }
    std::vector<uint8_t> temp(needed, 0);
    for (const RomEntry& entry : entries) {
        std::vector<uint8_t> data(entry.length, 0);
        RomEntry single{entry.name, entry.length, 0, entry.crc};
        if (!loader.load({single}, data, error)) return false;
        for (uint32_t i = 0; i < entry.length; i++) {
            temp[(entry.offset & ~1u) + i * 2 + (entry.offset & 1u)] = data[i];
        }
    }
    words.assign(needed / 2, 0);
    for (uint32_t i = 0; i + 1 < needed; i += 2) {
        words[i >> 1] = uint16_t((temp[i] << 8) | temp[i + 1]);
    }
    return true;
}

bool try_open_loader(RomLoader& loader, const std::string& path) {
    std::string ignored;
    return loader.open(path, &ignored);
}

}  // namespace

AtariSystem1::AtariSystem1(Game game)
    : game_(game),
      main_cpu_(kMainClock, M68000::Type::M68010),
      sound_cpu_(kSoundClock),
      ym_(kYmClock),
      pokey_(kSoundClock),
      via_(kSoundClock),
      tms_(kAtariClock / 2 / 11),
      slapstic_(105, &main_cpu_) {
    alpha_.assign(size_t(kAlphaWidth) * kAlphaHeight, kTransparent);
    playfield_.assign(size_t(kPlayfieldWidth) * kPlayfieldHeight, 0);
    pf_index_.assign(size_t(kScreenWidth) * kScreenHeight, 0);
    framebuffer_.assign(size_t(kScreenWidth) * kScreenHeight, 0xff000000u);

    main_cpu_.set_memory_handlers([this](uint32_t address) { return main_read(address); },
                                  [this](uint32_t address, uint16_t value) {
                                      main_write(address, value);
                                  });
    sound_cpu_.set_memory_handlers(
        [this](uint16_t address) { return sound_read(address); },
        [this](uint16_t address, uint8_t value) { sound_write(address, value); });
    sound_cpu_.set_cycle_handler([this](int cycles) { on_sound_cycles(cycles); });
    // Hold: the 6502 consumes the line after one IRQ. YM2151 then deasserts on
    // the Timer A ack ($14 bit 4) and re-asserts on the next wrap.
    ym_.set_irq_handler([this](bool on) { sound_cpu_.set_irq(on ? IrqLine::Hold : IrqLine::Clear); });

    via_.set_port_a([this]() { return tms_.status(); },
                    [this](uint8_t value) {
                        tms_.set_data_latch(value);
                        tms_.write_data(value);
                    });
    via_.set_port_b(
        [this]() {
            uint8_t value = 0;
            if (tms_.readyq()) value |= 0x04;
            if (tms_.intq()) value |= 0x08;
            return value;
        },
        [this](uint8_t value) {
            tms_.set_wsq((value & 0x01) != 0);
            tms_.set_rsq((value & 0x02) != 0);
            const int div = 5 | ((value >> 3) & 2);
            tms_.set_clock(kAtariClock / 2 / uint32_t(16 - div));
        });

    motion_objects_ = std::make_unique<AtariMotionObjects>(
        motion_config(), nullptr, &ram3_[0x2000 >> 1], kScreenWidth + 8, kScreenHeight + 8);
}

const char* AtariSystem1::title() const {
    switch (game_) {
        case Game::PeterPak: return "Peter Pack Rat";
        case Game::Indy: return "Indiana Jones and the Temple of Doom";
        case Game::RoadRunner: return "Road Runner";
        default: return "Marble Madness";
    }
}

bool AtariSystem1::has_speech() const {
    return game_ == Game::Indy || game_ == Game::RoadRunner;
}

bool AtariSystem1::has_adc() const { return game_ != Game::Marble; }

bool AtariSystem1::via_selected(uint16_t address) const {
    const uint16_t base = uint16_t(address & ~0x27f0);
    return base >= 0x1000 && base <= 0x100f;
}

bool AtariSystem1::init(const std::string& rom_path, std::string* error) {
    if (!load_roms(rom_path, error)) return false;
    eeprom_.fill(0xff);
    reset();
    return true;
}

bool AtariSystem1::load_roms(const std::string& rom_path, std::string* error) {
    RomLoader game_loader;
    if (!game_loader.open(rom_path, error)) return false;

    RomLoader bios_loader;
    bool bios_separate = false;
    namespace fs = std::filesystem;
    const fs::path given(rom_path);
    const fs::path sibling = given.has_parent_path() ? given.parent_path() / "atarisy1.zip"
                                                     : fs::path("atarisy1.zip");
    if (try_open_loader(bios_loader, sibling.string())) bios_separate = true;

    auto load_bytes = [&](const std::vector<RomEntry>& entries, std::vector<uint8_t>& dest) -> bool {
        std::string attempt;
        if (game_loader.load(entries, dest, &attempt)) return true;
        if (bios_separate && bios_loader.load(entries, dest, &attempt)) return true;
        if (error) *error = attempt;
        return false;
    };
    auto load_words = [&](const std::vector<RomEntry>& entries, std::vector<uint16_t>& dest) -> bool {
        std::string attempt;
        if (load_16w(game_loader, entries, dest, &attempt)) return true;
        if (bios_separate && load_16w(bios_loader, entries, dest, &attempt)) return true;
        if (error) *error = attempt;
        return false;
    };

    std::vector<uint16_t> bios;
    if (!load_words(kBiosRoms, bios)) return false;
    for (size_t i = 0; i < bios.size() && i < rom_.size(); i++) rom_[i] = bios[i];

    std::vector<uint8_t> char_rom(0x2000, 0);
    if (!load_bytes(kCharRoms, char_rom)) return false;
    chars_.decode(char_layout(), char_rom);
    gfx_[0] = chars_;

    const std::vector<RomEntry>* program = nullptr;
    const std::vector<RomEntry>* sound = nullptr;
    const std::vector<RomEntry>* back = nullptr;
    const std::vector<RomEntry>* proms = nullptr;
    int slapstic_type = 105;
    uint32_t slapstic_word = 0;
    uint32_t program_bytes = 0;
    uint32_t gfx_bytes = 0;
    bool absolute_program = false;

    switch (game_) {
        case Game::PeterPak:
            program = &kPeterRom;
            sound = &kPeterSound;
            back = &kPeterBack;
            proms = &kPeterProms;
            slapstic_type = 107;
            slapstic_word = 0x20000 / 2;
            program_bytes = 0x8000 * 3;
            gfx_bytes = 0x180000;
            break;
        case Game::Indy:
            program = &kIndyRom;
            sound = &kIndySound;
            back = &kIndyBack;
            proms = &kIndyProms;
            slapstic_type = 105;
            slapstic_word = 0x28000 / 2;
            program_bytes = 0x8000 * 5;
            gfx_bytes = 0x200000;
            break;
        case Game::Marble:
            program = &kMarbleRom;
            sound = &kMarbleSound;
            back = &kMarbleBack;
            proms = &kMarbleProms;
            slapstic_type = 103;
            slapstic_word = 0x20000 / 2;
            program_bytes = 0x8000 * 4;
            gfx_bytes = 0x100000;
            break;
        case Game::RoadRunner:
            program = &kRoadRom;
            sound = &kRoadSound;
            back = &kRoadBack;
            proms = &kRoadProms;
            slapstic_type = 108;
            slapstic_word = 0x80000 / 2;
            gfx_bytes = 0x300000;
            absolute_program = true;
            break;
    }

    std::vector<uint16_t> game_words;
    if (!load_words(*program, game_words)) return false;
    if (absolute_program) {
        // Keep the System 1 BIOS at $00000; overlay cart program at MAME offsets.
        for (size_t i = 0x10000 / 2; i < game_words.size() && i < rom_.size(); i++) {
            rom_[i] = game_words[i];
        }
    } else {
        const size_t copy_words = std::min(size_t(program_bytes / 2), game_words.size());
        for (size_t i = 0; i < copy_words; i++) rom_[(0x10000 >> 1) + i] = game_words[i];
    }
    slapstic_.set_type(slapstic_type);
    for (int bank = 0; bank < 4; bank++) {
        for (int i = 0; i < 0x1000; i++) {
            const size_t src = size_t(slapstic_word) + size_t(bank) * 0x1000 + size_t(i);
            slapstic_rom_[size_t(bank)][size_t(i)] = src < game_words.size() ? game_words[src] : 0;
        }
    }

    std::vector<uint8_t> sound_rom(0x10000, 0);
    if (!load_bytes(*sound, sound_rom)) return false;
    std::copy(sound_rom.begin(), sound_rom.end(), sound_memory_.begin());

    std::vector<uint8_t> proms_data(0x400, 0);
    if (!load_bytes(*proms, proms_data)) return false;
    std::vector<uint8_t> gfx_rom(gfx_bytes, 0xff);
    if (!load_bytes(*back, gfx_rom)) return false;
    convert_background(gfx_rom, proms_data);

    std::vector<uint16_t>& codes = motion_objects_->code_lookup();
    for (size_t i = 0; i < codes.size(); i++) {
        codes[i] = uint16_t((i & 0xff) | ((motable_[i >> 8] & 0xff) << 8));
    }
    std::vector<uint16_t>& colors = motion_objects_->color_lookup();
    std::vector<uint8_t>& gfxs = motion_objects_->gfx_lookup();
    for (size_t i = 0; i < colors.size() && i < 256; i++) {
        // AtariMotionObjects shifts by 4 (×16). MAME uses granularity 8 after a
        // ×2 PROM nibble, which is the same as storing the raw nibble here.
        colors[i] = uint16_t((motable_[i] >> 12) & 0xf);
    }
    for (size_t i = 0; i < gfxs.size() && i < 256; i++) {
        gfxs[i] = uint8_t((motable_[i] >> 8) & 0xf);
    }

    warnings_ = game_loader.warnings();
    if (bios_separate) {
        warnings_.insert(warnings_.end(), bios_loader.warnings().begin(),
                         bios_loader.warnings().end());
    }
    return true;
}

uint8_t AtariSystem1::decode_bank(uint8_t prom1, uint8_t prom2, int bpp,
                                  const std::vector<uint8_t>& gfx_rom) {
    constexpr uint8_t kProm1Bank1 = 0x10;
    constexpr uint8_t kProm1Bank2 = 0x20;
    constexpr uint8_t kProm1Bank3 = 0x40;
    constexpr uint8_t kProm1Bank4 = 0x80;
    constexpr uint8_t kProm2Bank5 = 0x40;
    constexpr uint8_t kProm2Bank6Or7 = 0x80;
    constexpr uint8_t kProm2Bank7 = 0x08;

    int bank_index = 0;
    if ((prom1 & kProm1Bank1) == 0) bank_index = 1;
    else if ((prom1 & kProm1Bank2) == 0) bank_index = 2;
    else if ((prom1 & kProm1Bank3) == 0) bank_index = 3;
    else if ((prom1 & kProm1Bank4) == 0) bank_index = 4;
    else if ((prom2 & kProm2Bank5) == 0) bank_index = 5;
    else if ((prom2 & kProm2Bank6Or7) == 0) bank_index = ((prom2 & kProm2Bank7) == 0) ? 7 : 6;
    else return 0;

    if (bank_gfx_[size_t(bpp - 4)][size_t(bank_index)] != 0) {
        return bank_gfx_[size_t(bpp - 4)][size_t(bank_index)];
    }
    const size_t offset = size_t(0x80000) * size_t(bank_index - 1);
    if (offset >= gfx_rom.size()) return 0;

    const uint8_t gfx_index = next_gfx_index_++;
    if (gfx_index >= gfx_.size()) return 0;
    std::vector<uint8_t> bank(0x80000, 0xff);
    const size_t copy = std::min(bank.size(), gfx_rom.size() - offset);
    std::memcpy(bank.data(), gfx_rom.data() + offset, copy);
    gfx_[gfx_index].decode(bank_layout(bpp), bank);
    bank_gfx_[size_t(bpp - 4)][size_t(bank_index)] = gfx_index;
    bank_color_shift_[gfx_index] = uint8_t(bpp - 3);
    return gfx_index;
}

void AtariSystem1::convert_background(std::vector<uint8_t>& gfx_rom,
                                      const std::vector<uint8_t>& proms) {
    for (uint8_t& byte : gfx_rom) byte = uint8_t(~byte);
    for (auto& row : bank_gfx_) row.fill(0);
    next_gfx_index_ = 1;
    bank_color_shift_.fill(1);

    constexpr uint8_t kPlane4 = 0x10;
    constexpr uint8_t kPlane5 = 0x20;
    constexpr uint8_t kOffsetMask = 0x0f;
    constexpr uint8_t kPfColor = 0x0f;
    constexpr uint8_t kMoColor = 0x07;

    for (int obj = 0; obj < 2; obj++) {
        for (int i = 0; i < 256; i++) {
            const uint8_t prom1 = proms[size_t(i + 0x100 * obj)];
            const uint8_t prom2 = proms[size_t(0x200 + i + 0x100 * obj)];
            int bpp = 4;
            if (prom2 & kPlane4) {
                bpp = 5;
                if (prom2 & kPlane5) bpp = 6;
            }
            const uint8_t offset = uint8_t(prom1 & kOffsetMask);
            uint8_t bank = decode_bank(prom1, prom2, bpp, gfx_rom);
            if (obj == 0) {
                uint8_t color = uint8_t((uint8_t(~prom2) & kPfColor) >> (bpp - 4));
                uint8_t used_offset = offset;
                if (bank == 0) {
                    bank = 1;
                    used_offset = 0;
                    color = 0;
                }
                playfield_lookup_[size_t(i)] =
                    uint16_t(used_offset | (uint16_t(bank) << 8) | (uint16_t(color) << 12));
            } else {
                const uint8_t color = uint8_t((uint8_t(~prom2) & kMoColor) >> (bpp - 4));
                motable_[size_t(i)] =
                    uint16_t(offset | (uint16_t(bank) << 8) | (uint16_t(color) << 12));
            }
        }
    }
}

void AtariSystem1::reset() {
    slapstic_.reset();
    rom_bank_ = slapstic_.current_bank();
    main_cpu_.reset();
    sound_cpu_.reset();
    ym_.reset();
    pokey_.reset();
    via_.reset();
    tms_.reset();
    in0_ = 0xff6f;
    in2_ = 0x87;
    analog_x_ = analog_y_ = 0x80;
    joy_bits_ = 0;
    adc_channel_ = 0;
    adc_value_ = 0;
    adc_irq_enable_ = true;
    adc_busy_ = false;
    main_cpu_.set_irq(2, IrqLine::Clear);
    scroll_x_ = 0;
    scroll_y_ = 0;
    scroll_y_latch_ = 0;
    scroll_x_line_.fill(0);
    scroll_y_line_.fill(0);
    vblank_ = 0x10;
    bankselect_ = 0;
    playfield_tile_bank_ = 0;
    playfield_priority_pens_ = 0;
    int3_line_ = -1;
    int3_off_line_ = -1;
    int3_state_ = false;
    main_cpu_.set_irq(3, IrqLine::Clear);
    write_eeprom_ = false;
    sound_pending_ = false;
    main_pending_ = false;
    main_latch_ = 0;
    sound_latch_ = 0;
    // Keep the 6502 running from power-on, matching dsp-emulator. Starting it
    // halted deadlocks Indiana Jones: the 68K BIOS waits for a 6502 reply
    // before it writes bankselect bit 7, so $1820 coin switches are never read.
    sound_cpu_halted_ = false;
    line_ = 0;
    audio_accumulator_ = 0;
    audio_.clear();
    alpha_dirty_.fill(true);
    playfield_dirty_.fill(true);
    std::fill(alpha_.begin(), alpha_.end(), kTransparent);
    std::fill(playfield_.begin(), playfield_.end(), 0);
    std::fill(pf_index_.begin(), pf_index_.end(), 0);
    std::fill(framebuffer_.begin(), framebuffer_.end(), 0xff000000u);
}

void AtariSystem1::set_sound_reset(bool running) {
    if (running == !sound_cpu_halted_) return;
    if (running) {
        sound_cpu_halted_ = false;
        ym_.clear_external_irq();
        sound_cpu_.set_irq(IrqLine::Clear);
        sound_cpu_.set_nmi(IrqLine::Clear);
        // Continue from the halted PC. A forced 6502 reset runs the sound ROM
        // zero-page clear and wipes the $FE38 coin debounce / credit counters.
    } else {
        // MAME bankselect_w: holding the 6502 in reset also resets the VIA
        // and acknowledges the main latch.
        sound_cpu_halted_ = true;
        via_.reset();
        tms_.reset();
        main_pending_ = false;
        main_cpu_.set_irq(6, IrqLine::Clear);
    }
}

uint16_t AtariSystem1::main_read(uint32_t address) {
    address &= 0xffffff;
    if (address <= 0x7ffff) return rom_[(address >> 1) & 0x3ffff];
    if (address >= 0x80000 && address <= 0x87fff) {
        const uint16_t value = slapstic_rom_[rom_bank_ & 3][(address & 0x1fff) >> 1];
        rom_bank_ = slapstic_.tweak(uint16_t((address & 0x7fff) >> 1));
        return value;
    }
    if (address == 0x2e0000) return int3_state_ ? 0x0080 : 0;
    if (address >= 0x400000 && address <= 0x401fff) return ram_[(address & 0x1fff) >> 1];
    if (address >= 0x900000 && address <= 0x9fffff) return ram2_[(address & 0xfffff) >> 1];
    if (address >= 0xa00000 && address <= 0xa03fff) return ram3_[(address & 0x3fff) >> 1];
    if (address >= 0xb00000 && address <= 0xb007ff) return palette_ram_[(address & 0x7ff) >> 1];
    if (address >= 0xf00000 && address <= 0xf00fff) {
        return uint16_t(eeprom_[(address & 0xfff) >> 1]);
    }
    if (address >= 0xf20000 && address <= 0xf20007) return 0x00ff;
    if (address >= 0xf40000 && address <= 0xf4001f) {
        if (!has_adc()) return 0;
        const uint8_t value = adc_value_;
        adc_start(address);
        return value;
    }
    if (address >= 0xf60000 && address <= 0xf60003) {
        return uint16_t(in0_ | vblank_ | (sound_pending_ ? 0x80 : 0));
    }
    if (address == 0xfc0000) {
        main_pending_ = false;
        main_cpu_.set_irq(6, IrqLine::Clear);
        return main_latch_;
    }
    return 0xffff;
}

void AtariSystem1::main_write(uint32_t address, uint16_t value) {
    address &= 0xffffff;
    if (address <= 0x7ffff) return;
    if (address >= 0x80000 && address <= 0x87fff) {
        rom_bank_ = slapstic_.tweak(uint16_t((address & 0x7fff) >> 1));
        return;
    }
    if (address >= 0x400000 && address <= 0x401fff) {
        ram_[(address & 0x1fff) >> 1] = value;
        return;
    }
    if (address == 0x800000) {
        scroll_x_ = value;
        return;
    }
    if (address == 0x820000) {
        scroll_y_latch_ = value;
        scroll_y_ = (line_ < 240) ? uint16_t(value - (line_ + 1)) : value;
        return;
    }
    if (address == 0x840000) {
        playfield_priority_pens_ = value;
        return;
    }
    if (address == 0x860000) {
        const uint16_t diff = uint16_t(bankselect_ ^ value);
        if (diff & 0x04) {
            playfield_tile_bank_ = uint8_t((value >> 2) & 1);
            playfield_dirty_.fill(true);
        }
        if (diff & 0x80) set_sound_reset((value & 0x80) != 0);
        motion_objects_->set_bank((value >> 3) & 7);
        bankselect_ = value;
        if (diff & 0x38) reschedule_int3(line_);
        return;
    }
    if (address == 0x880000) return;  // watchdog
    if (address == 0x8a0000) {
        main_cpu_.set_irq(4, IrqLine::Clear);
        return;
    }
    if (address == 0x8c0000) {
        write_eeprom_ = true;
        return;
    }
    if (address >= 0x900000 && address <= 0x9fffff) {
        ram2_[(address & 0xfffff) >> 1] = value;
        return;
    }
    if (address >= 0xa00000 && address <= 0xa01fff) {
        const uint16_t offset = uint16_t((address & 0x1fff) >> 1);
        if (ram3_[offset] != value) {
            ram3_[offset] = value;
            playfield_dirty_[offset] = true;
        }
        return;
    }
    if (address >= 0xa02000 && address <= 0xa02fff) {
        const int spr_off = int((address & 0xfff) >> 1);
        const uint16_t old = ram3_[(address & 0x3fff) >> 1];
        ram3_[(address & 0x3fff) >> 1] = value;
        if (game_ == Game::RoadRunner && old != value &&
            (spr_off >> 8) == int(motion_objects_->bank())) {
            const uint16_t* spr = &ram3_[0x1000];
            if (((spr_off & 0xc0) == 0x00 && spr[spr_off | 0x40] == 0xffff) ||
                ((spr_off & 0xc0) == 0x40 && (value == 0xffff || old == 0xffff))) {
                reschedule_int3(line_);
            }
        }
        return;
    }
    if (address >= 0xa03000 && address <= 0xa03fff) {
        const uint16_t offset = uint16_t((address & 0xfff) >> 1);
        if (ram3_[(0x3000 >> 1) + offset] != value) {
            ram3_[(0x3000 >> 1) + offset] = value;
            alpha_dirty_[offset] = true;
        }
        return;
    }
    if (address >= 0xb00000 && address <= 0xb007ff) {
        const int index = int((address & 0x7ff) >> 1);
        if (palette_ram_[size_t(index)] != value) set_palette(index, value);
        return;
    }
    if (address >= 0xf00000 && address <= 0xf00fff) {
        if (write_eeprom_) {
            eeprom_[(address & 0xfff) >> 1] = uint8_t(value);
            write_eeprom_ = false;
        }
        return;
    }
    if (address >= 0xf40000 && address <= 0xf4001f) {
        if (has_adc()) adc_start(address);
        return;
    }
    if (address == 0xf80000 || address == 0xfe0000) {
        sound_latch_ = uint8_t(value);
        sound_pending_ = true;
        sound_cpu_.set_nmi(IrqLine::Assert);
        return;
    }
}

void AtariSystem1::set_palette(int index, uint16_t value) {
    palette_ram_[size_t(index)] = value;
    const uint16_t intensity = uint16_t(value >> 12);
    const uint8_t red = pal4bit_intensity(uint16_t(value >> 8), intensity);
    const uint8_t green = pal4bit_intensity(uint16_t(value >> 4), intensity);
    const uint8_t blue = pal4bit_intensity(value, intensity);
    palette_[size_t(index)] =
        0xff000000u | (uint32_t(red) << 16) | (uint32_t(green) << 8) | uint32_t(blue);
    if (index < 0x20) alpha_dirty_.fill(true);
    else playfield_dirty_.fill(true);
}

uint8_t AtariSystem1::sound_read(uint16_t address) {
    // MAME: RAM $0000-$0FFF mirrored at $2000; I/O at $1800-$187F with
    // mirrors 0x278e / 0x278f / 0x2780.
    if ((address & ~0x2000) <= 0x0fff) return sound_memory_[address & 0x0fff];
    if (address >= 0x4000) return sound_memory_[address];
    if (has_speech() && via_selected(address)) return via_.read(uint8_t(address & 0x0f));
    if ((address & ~0x278e) == 0x1800 || (address & ~0x278e) == 0x1801) return ym_.status();
    if ((address & ~0x278f) == 0x1810) {
        sound_pending_ = false;
        sound_cpu_.set_nmi(IrqLine::Clear);
        return sound_latch_;
    }
    if ((address & ~0x278f) == 0x1820) {
        uint8_t value = uint8_t(in2_ | (sound_pending_ ? 0x08 : 0) | (main_pending_ ? 0x10 : 0));
        // MAME switch_6502_r: service (F60000 bit 6) inverts the self-test bit.
        if ((in0_ & 0x0040) == 0) value ^= 0x80;
        return value;
    }
    if ((address & ~0x2780) >= 0x1870 && (address & ~0x2780) <= 0x187f) {
        return pokey_.read(address & 0x0f);
    }
    return 0xff;
}

void AtariSystem1::sound_write(uint16_t address, uint8_t value) {
    if ((address & ~0x2000) <= 0x0fff) {
        sound_memory_[address & 0x0fff] = value;
        return;
    }
    if (has_speech() && via_selected(address)) {
        via_.write(uint8_t(address & 0x0f), value);
        return;
    }
    if ((address & ~0x278e) == 0x1800) {
        ym_.select_register(value);
        return;
    }
    if ((address & ~0x278e) == 0x1801) {
        ym_.write(value);
        return;
    }
    if ((address & ~0x278f) == 0x1810) {
        main_latch_ = value;
        main_pending_ = true;
        main_cpu_.set_irq(6, IrqLine::Assert);
        return;
    }
    // MAME: $1820-$1827 LS259. Q0 is YM2151 reset (active low). Absorb the
    // writes so they are not treated as open bus; do not pulse ym_.reset()
    // here — that would stop Timer A, which is what drives the $FE38 coin scan.
    const uint16_t outlatch = uint16_t(address & ~0x2788);
    if (outlatch >= 0x1820 && outlatch <= 0x1827) return;
    if ((address & ~0x2780) >= 0x1870 && (address & ~0x2780) <= 0x187f) {
        pokey_.write(address & 0x0f, value);
    }
}

void AtariSystem1::on_sound_cycles(int cycles) {
    ym_.run_timers(cycles * 2);
    pokey_.run(cycles);
    if (has_speech()) via_.tick(cycles);
    const int tms_clocks =
        int((int64_t(cycles) * int64_t(tms_.clock()) + (kSoundClock / 2)) / kSoundClock);
    tms_.tick(tms_clocks);
    audio_accumulator_ += int64_t(cycles) * YM2151::kSampleRate;
    while (audio_accumulator_ >= kSoundClock) {
        audio_accumulator_ -= kSoundClock;
        const int32_t sample = ym_.update() + pokey_.update() + tms_.last_sample();
        audio_.push_back(int16_t(std::clamp(sample, int32_t(-32768), int32_t(32767))));
    }
}

void AtariSystem1::draw_alpha_tile(int offset) {
    const int x = offset % 64;
    const int y = offset / 64;
    const uint16_t atrib = ram3_[(0x3000 >> 1) + offset];
    const int color = (atrib >> 10) & 7;
    const int base = color << 2;
    const bool opaque = (atrib & 0x2000) != 0;
    const uint8_t* pixels = chars_.element(atrib & 0x3ff);
    for (int row = 0; row < 8; row++) {
        const size_t target = size_t((y * 8 + row) * kAlphaWidth + x * 8);
        for (int column = 0; column < 8; column++) {
            const uint8_t pen = pixels[row * 8 + column];
            if (!opaque && pen == 0) alpha_[target + size_t(column)] = kTransparent;
            else alpha_[target + size_t(column)] = int16_t(base + pen);
        }
    }
}

void AtariSystem1::draw_playfield_tile(int offset) {
    const int x = offset % 64;
    const int y = offset / 64;
    const uint16_t atrib = ram3_[size_t(offset)];
    const uint16_t lookup =
        playfield_lookup_[((atrib >> 8) & 0x7f) | (uint16_t(playfield_tile_bank_) << 7)];
    const int gfx_index = (lookup >> 8) & 0xf;
    const int shift = bank_color_shift_[size_t(gfx_index)];
    const int color = 0x20 + ((((lookup >> 12) & 0xf) << shift));
    // MAME gfx granularity is 8, colorbase 0x100: palette = 0x100 + color * 8.
    const int base = 0x100 + (color << 3);
    const int code = ((lookup & 0xff) << 8) | (atrib & 0xff);
    const bool hflip = (atrib & 0x8000) != 0;
    const GfxSet& tiles = gfx_[size_t(gfx_index)].total() > 0 ? gfx_[size_t(gfx_index)] : gfx_[1];
    if (tiles.total() == 0) return;
    const uint8_t* pixels = tiles.element(code);
    for (int row = 0; row < 8; row++) {
        const size_t target = size_t((y * 8 + row) * kPlayfieldWidth + x * 8);
        for (int column = 0; column < 8; column++) {
            const int src = hflip ? (7 - column) : column;
            playfield_[target + size_t(column)] = int16_t(base + pixels[row * 8 + src]);
        }
    }
}

void AtariSystem1::update_video() {
    for (int offset = 0; offset < 0x800; offset++) {
        if (!alpha_dirty_[size_t(offset)]) continue;
        draw_alpha_tile(offset);
        alpha_dirty_[size_t(offset)] = false;
    }
    for (int offset = 0; offset < 0x1000; offset++) {
        if (!playfield_dirty_[size_t(offset)]) continue;
        draw_playfield_tile(offset);
        playfield_dirty_[size_t(offset)] = false;
    }

    for (int y = 0; y < kScreenHeight; y++) {
        const int sx = int(scroll_x_line_[size_t(y)]) & (kPlayfieldWidth - 1);
        const int sy = int(scroll_y_line_[size_t(y)]) & (kPlayfieldHeight - 1);
        const int py = (y + sy) & (kPlayfieldHeight - 1);
        for (int x = 0; x < kScreenWidth; x++) {
            const int px = (x + sx) & (kPlayfieldWidth - 1);
            pf_index_[size_t(y * kScreenWidth + x)] =
                uint16_t(playfield_[size_t(py * kPlayfieldWidth + px)] & 0x3ff);
        }
    }

    motion_objects_->draw(0, 256, -1,
                          [this](int code, int color, bool hflip, bool vflip, int x, int y,
                                 int gfx, int priority) {
                              const GfxSet& tiles =
                                  (gfx > 0 && gfx < int(gfx_.size()) && gfx_[size_t(gfx)].total() > 0)
                                      ? gfx_[size_t(gfx)]
                                      : (gfx_[1].total() > 0 ? gfx_[1] : chars_);
                              if (tiles.total() == 0) return;
                              const uint8_t* pixels = tiles.element(code);
                              for (int row = 0; row < 8; row++) {
                                  const int ty = y + row;
                                  if (ty < 0 || ty >= kScreenHeight) continue;
                                  const int src_row = vflip ? (7 - row) : row;
                                  for (int column = 0; column < 8; column++) {
                                      const int tx = x + column;
                                      if (tx < 0 || tx >= kScreenWidth) continue;
                                      const int src_col = hflip ? (7 - column) : column;
                                      const uint8_t pen = pixels[src_row * 8 + src_col];
                                      if (pen == 0) continue;
                                      const uint16_t mo = uint16_t(((color + pen) & 0x0fff) |
                                                                   ((priority ? 1 : 0) << 12));
                                      mix_motion_object_pixel(tx, ty, mo);
                                  }
                              }
                          });

    for (int y = 0; y < kScreenHeight; y++) {
        for (int x = 0; x < kScreenWidth; x++) {
            const int16_t character = alpha_[size_t(y * kAlphaWidth + x)];
            uint16_t index = pf_index_[size_t(y * kScreenWidth + x)];
            if (character != kTransparent) index = uint16_t(character & 0x3ff);
            framebuffer_[size_t(y * kScreenWidth + x)] = palette_[size_t(index) & 0x3ff];
        }
    }
}

void AtariSystem1::mix_motion_object_pixel(int x, int y, uint16_t mo) {
    uint16_t& pf = pf_index_[size_t(y * kScreenWidth + x)];
    if (mo & 0xf000) {
        // High priority: mix PF pen into palette 0x300 unless the MO pen is 1.
        if ((mo & 0x0f) != 1) {
            pf = uint16_t(0x300 + ((pf & 0x0f) << 4) + (mo & 0x0f));
        }
    } else if ((pf & 0xf8) != 0 || (playfield_priority_pens_ & (1u << (pf & 7))) == 0) {
        pf = uint16_t(mo & 0x0fff);
    }
}

void AtariSystem1::reschedule_int3(int scanline) {
    if (game_ != Game::RoadRunner) {
        int3_line_ = -1;
        return;
    }
    const uint16_t* spr = &ram3_[0x1000];
    const int offset = int(motion_objects_->bank()) * 256;
    int link = 0;
    int best = scanline;
    bool found = false;
    std::array<bool, 64> visited{};
    while (!visited[size_t(link & 63)]) {
        visited[size_t(link & 63)] = true;
        if (spr[offset + link + 0x40] == 0xffff) {
            const uint16_t data = spr[offset + link];
            const int vsize = (data & 15) + 1;
            const int ypos = (256 - (data >> 5) - vsize * 8 - 1) & 0x1ff;
            found = true;
            if (best <= scanline) {
                if ((ypos <= scanline && ypos < best) || ypos > scanline) best = ypos;
            } else if (ypos < best) {
                best = ypos;
            }
        }
        link = spr[offset + link + 0xc0] & 0x3f;
    }
    if (!found) best = -1;
    // Timers past the last scanline fire on the next frame after reschedule(-1).
    if (best >= kScanlines) best = -1;
    int3_line_ = best;
}

void AtariSystem1::run_frame() {
    const int main_cycles = int(double(kMainClock) / kFramesPerSecond / kScanlines + 0.5);
    const int sound_cycles = int(double(kSoundClock) / kFramesPerSecond / kScanlines + 0.5);

    reschedule_int3(-1);
    for (line_ = 0; line_ < kScanlines; line_++) {
        main_cpu_.run(main_cycles);
        if (!sound_cpu_halted_) sound_cpu_.run(sound_cycles);
        adc_complete();
        scroll_x_line_[size_t(line_)] = scroll_x_;
        scroll_y_line_[size_t(line_)] = scroll_y_;
        if (int3_off_line_ == line_) {
            int3_state_ = false;
            int3_off_line_ = -1;
            main_cpu_.set_irq(3, IrqLine::Clear);
        }
        if (int3_line_ == line_) {
            int3_state_ = true;
            main_cpu_.set_irq(3, IrqLine::Assert);
            int3_off_line_ = (line_ + 1) % kScanlines;
            reschedule_int3(line_);
        }
        if (line_ == 239) {
            update_video();
            vblank_ = 0;
            main_cpu_.set_irq(4, IrqLine::Assert);
        }
        if (line_ == 261) vblank_ = 0x10;
    }
    scroll_y_ = scroll_y_latch_;
    line_ = 0;
}

void AtariSystem1::set_inputs(const MachineInputs& inputs) {
    in0_ = 0xff6f;
    in2_ = 0x87;
    if (inputs.player1.button1) in0_ &= uint16_t(~0x0001);
    if (inputs.player2.start || inputs.player1.start) in0_ &= uint16_t(~0x0002);
    if (inputs.player1.button2) in0_ &= uint16_t(~0x0004);
    if (game_ == Game::RoadRunner && inputs.player1.button2) in0_ &= uint16_t(~0x0002);
    if (inputs.coin1) in2_ = uint8_t(in2_ & ~0x01);
    if (inputs.coin2) in2_ = uint8_t(in2_ & ~0x02);

    joy_bits_ = 0;
    if (inputs.player1.up) joy_bits_ |= 0x10;
    if (inputs.player1.down) joy_bits_ |= 0x20;
    if (inputs.player1.left) joy_bits_ |= 0x40;
    if (inputs.player1.right) joy_bits_ |= 0x80;

    analog_y_ = 0x80;
    analog_x_ = 0x80;
    if (inputs.player1.up) analog_y_ = 0x10;
    if (inputs.player1.down) analog_y_ = 0xf0;
    if (inputs.player1.up && inputs.player1.down) analog_y_ = 0x80;
    // Road Runner X is PORT_REVERSE: left is high, right is low.
    if (inputs.player1.left) analog_x_ = 0xf0;
    if (inputs.player1.right) analog_x_ = 0x10;
    if (inputs.player1.left && inputs.player1.right) analog_x_ = 0x80;
}

void AtariSystem1::adc_start(uint32_t address) {
    const int offset = int((address >> 1) & 0xf);
    adc_channel_ = uint8_t(offset & 7);
    adc_irq_enable_ = (offset & 8) == 0;
    adc_busy_ = true;
    main_cpu_.set_irq(2, IrqLine::Clear);
}

void AtariSystem1::adc_complete() {
    if (!has_adc() || !adc_busy_) return;
    adc_busy_ = false;
    adc_value_ = adc_channel_value(adc_channel_);
    if (adc_irq_enable_) main_cpu_.set_irq(2, IrqLine::Hold);
}

uint8_t AtariSystem1::adc_channel_value(int channel) const {
    if (game_ == Game::RoadRunner) {
        if (channel == 6) return analog_y_;
        if (channel == 7) return analog_x_;
        return 0x80;
    }
    if (channel >= 0 && channel <= 3) {
        const int bit = 7 - channel;
        return (joy_bits_ & (1 << bit)) ? 0xf0 : 0x00;
    }
    return 0;
}

void AtariSystem1::set_dip_switch(int, uint8_t) {}

void AtariSystem1::drain_audio(std::vector<int16_t>& out) {
    out.insert(out.end(), audio_.begin(), audio_.end());
    audio_.clear();
}

}  // namespace dsp
