#include "drivers/arcade/doubledragon.h"

#include <algorithm>

#include "core/rom_loader.h"

namespace dsp {
namespace {

// The supplied sets use slightly different file names than the ones documented
// in doubledragon_hw.pas, so every entry accepts both spellings.
const std::vector<RomEntry> kMainRoms = {
    {"21j-1.26|21j-1-5", 0x8000, 0x00000, 0},
    {"21j-2-3.25|21j-2-3", 0x8000, 0x08000, 0x5779705e},
    {"21a-3.24|21j-3", 0x8000, 0x10000, 0},
    {"21j-4.23|21j-4-1", 0x8000, 0x18000, 0},
};
const std::vector<RomEntry> kSubRoms = {{"21jm-0.ic55|63701.bin", 0x4000, 0, 0xf5232d03}};
const std::vector<RomEntry> kSoundRoms = {{"21j-0-1", 0x8000, 0x8000, 0x9efa95bb}};
const std::vector<RomEntry> kCharRoms = {{"21j-5", 0x8000, 0, 0x7a8b8db4}};
const std::vector<RomEntry> kTileRoms = {
    {"21j-8", 0x10000, 0x00000, 0x7c435887},
    {"21j-9", 0x10000, 0x10000, 0xc6640aed},
    {"21j-i", 0x10000, 0x20000, 0x5effb0a0},
    {"21j-j", 0x10000, 0x30000, 0x5fb42e7c},
};
const std::vector<RomEntry> kSpriteRoms = {
    {"21j-a", 0x10000, 0x00000, 0x574face3}, {"21j-b", 0x10000, 0x10000, 0x40507a76},
    {"21j-c", 0x10000, 0x20000, 0xbb0bc76f}, {"21j-d", 0x10000, 0x30000, 0xcb4f231b},
    {"21j-e", 0x10000, 0x40000, 0xa0a0c261}, {"21j-f", 0x10000, 0x50000, 0x6ba152f6},
    {"21j-g", 0x10000, 0x60000, 0x3220a0b6}, {"21j-h", 0x10000, 0x70000, 0x65c7517d},
};
const std::vector<RomEntry> kAdpcmRoms = {
    {"21j-6", 0x10000, 0x00000, 0x34755de3},
    {"21j-7", 0x10000, 0x10000, 0x904de6f8},
};

const std::vector<RomEntry> kMainRoms2 = {
    {"26a9-04.bin", 0x8000, 0x00000, 0xf2cfc649},
    {"26aa-03.bin", 0x8000, 0x08000, 0x44dd5d4b},
    {"26ab-0.bin", 0x8000, 0x10000, 0x49ddddcd},
    {"26ac-0e.63|26ac-02.bin", 0x8000, 0x18000, 0},
};
const std::vector<RomEntry> kSubRoms2 = {{"26ae-0.bin", 0x10000, 0, 0xea437867}};
const std::vector<RomEntry> kSoundRoms2 = {{"26ad-0.bin", 0x8000, 0, 0x75e36cd6}};
const std::vector<RomEntry> kCharRoms2 = {{"26a8-0e.19|26a8-0.bin", 0x10000, 0, 0}};
const std::vector<RomEntry> kTileRoms2 = {
    {"26j4-0.bin", 0x20000, 0x00000, 0xa8c93e76},
    {"26j5-0.bin", 0x20000, 0x20000, 0xee555237},
};
const std::vector<RomEntry> kSpriteRoms2 = {
    {"26j0-0.bin", 0x20000, 0x00000, 0xdb309c84},
    {"26j1-0.bin", 0x20000, 0x20000, 0xc3081e0c},
    {"26af-0.bin", 0x20000, 0x40000, 0x3a615aad},
    {"26j2-0.bin", 0x20000, 0x60000, 0x589564ae},
    {"26j3-0.bin", 0x20000, 0x80000, 0xdaf040d6},
    {"26a10-0.bin", 0x20000, 0xa0000, 0x6d16d889},
};
const std::vector<RomEntry> kAdpcmRoms2 = {
    {"26j6-0.bin", 0x20000, 0x00000, 0xa84b2a29},
    {"26j7-0.bin", 0x20000, 0x20000, 0xbc6a48d5},
};

const std::vector<int> kTileX = {3,          2,          1,          0,
                                 16 * 8 + 3, 16 * 8 + 2, 16 * 8 + 1, 16 * 8 + 0,
                                 32 * 8 + 3, 32 * 8 + 2, 32 * 8 + 1, 32 * 8 + 0,
                                 48 * 8 + 3, 48 * 8 + 2, 48 * 8 + 1, 48 * 8 + 0};
const std::vector<int> kTileY = {0 * 8,  1 * 8,  2 * 8,  3 * 8, 4 * 8,  5 * 8,  6 * 8,  7 * 8,
                                 8 * 8,  9 * 8,  10 * 8, 11 * 8, 12 * 8, 13 * 8, 14 * 8, 15 * 8};

GfxLayout char_layout(int total) {
    GfxLayout layout;
    layout.width = 8;
    layout.height = 8;
    layout.total = total;
    layout.planes = 4;
    layout.char_increment = 32 * 8;
    layout.plane_offsets = {0, 2, 4, 6};
    layout.x_offsets = {1, 0, 8 * 8 + 1, 8 * 8 + 0, 16 * 8 + 1, 16 * 8 + 0, 24 * 8 + 1, 24 * 8 + 0};
    layout.y_offsets = std::vector<int>(kTileY.begin(), kTileY.begin() + 8);
    return layout;
}

GfxLayout tile_layout(int total, int plane_bytes) {
    GfxLayout layout;
    layout.width = 16;
    layout.height = 16;
    layout.total = total;
    layout.planes = 4;
    layout.char_increment = 64 * 8;
    layout.plane_offsets = {plane_bytes * 8, plane_bytes * 8 + 4, 0, 4};
    layout.x_offsets = kTileX;
    layout.y_offsets = kTileY;
    return layout;
}

inline uint8_t pal4bit(uint8_t value) { return uint8_t((value & 0x0f) * 0x11); }

}  // namespace

DoubleDragon::DoubleDragon(Variant variant)
    : variant_(variant),
      main_cpu_(kMainClock),
      sub_cpu_(kSubClock),
      sound_cpu_(kSoundClock),
      sub_cpu2_(kSub2Clock),
      sound_cpu2_(kSound2Clock),
      ym_(kYmClock),
      msm0_(kMsmClock, 48, 4),
      msm1_(kMsmClock, 48, 4),
      oki_(kOkiClock, true) {
    // Double Dragon II boots with a different default for DIP bank B.
    if (is_dd2()) dsw_b_ = 0x96;
    framebuffer_.assign(size_t(kScreenWidth) * kScreenHeight, 0xff000000u);
    banked_rom_.assign(6 * 0x4000, 0);

    main_cpu_.set_memory_handlers(
        [this](uint16_t address) { return main_read(address); },
        [this](uint16_t address, uint8_t value) { main_write(address, value); });

    if (is_dd2()) {
        sub_cpu2_.set_memory_handlers(
            [this](uint16_t address) { return sub2_read(address); },
            [this](uint16_t address, uint8_t value) { sub2_write(address, value); });
        sub_cpu2_.set_io_handlers([](uint16_t) { return uint8_t(0xff); }, [](uint16_t, uint8_t) {});
        sound_cpu2_.set_memory_handlers(
            [this](uint16_t address) { return sound2_read(address); },
            [this](uint16_t address, uint8_t value) { sound2_write(address, value); });
        sound_cpu2_.set_io_handlers([](uint16_t) { return uint8_t(0xff); },
                                    [](uint16_t, uint8_t) {});
        sound_cpu2_.set_cycle_handler([this](int cycles) { on_sound2_cycles(cycles); });
        ym_.set_irq_handler([this](bool state) {
            sound_cpu2_.set_irq(state ? IrqLine::Assert : IrqLine::Clear);
        });
    } else {
        sub_cpu_.set_memory_handlers(
            [this](uint16_t address) { return sub_read(address); },
            [this](uint16_t address, uint8_t value) { sub_write(address, value); });
        // Port 6 drives the main CPU IRQ and acknowledges the sub CPU NMI.
        sub_cpu_.set_portx_write(1, [this](uint8_t value) {
            if ((value & 1) == 0) sub_cpu_.set_nmi(IrqLine::Clear);
            if ((value & 2) != 0 && (sub_port_ & 2) != 0) main_cpu_.set_irq(IrqLine::Assert);
            sub_port_ = value;
        });
        sound_cpu_.set_memory_handlers(
            [this](uint16_t address) { return sound_read(address); },
            [this](uint16_t address, uint8_t value) { sound_write(address, value); });
        sound_cpu_.set_cycle_handler([this](int cycles) { on_sound_cycles(cycles); });
        ym_.set_irq_handler([this](bool state) {
            sound_cpu_.set_firq(state ? IrqLine::Assert : IrqLine::Clear);
        });
    }
}

bool DoubleDragon::init(const std::string& rom_path, std::string* error) {
    if (!load_roms(rom_path, error)) return false;
    reset();
    return true;
}

bool DoubleDragon::load_roms(const std::string& rom_path, std::string* error) {
    RomLoader loader;
    if (!loader.open(rom_path, error)) return false;

    std::vector<uint8_t> main_rom(0x20000, 0);
    if (!loader.load(is_dd2() ? kMainRoms2 : kMainRoms, main_rom, error)) return false;
    std::copy(main_rom.begin(), main_rom.begin() + 0x8000, memory_.begin() + 0x8000);
    std::copy(main_rom.begin() + 0x8000, main_rom.begin() + 0x20000, banked_rom_.begin());

    std::vector<uint8_t> char_rom(is_dd2() ? 0x10000 : 0x8000, 0);
    if (!loader.load(is_dd2() ? kCharRoms2 : kCharRoms, char_rom, error)) return false;

    std::vector<uint8_t> tile_rom(0x40000, 0);
    if (!loader.load(is_dd2() ? kTileRoms2 : kTileRoms, tile_rom, error)) return false;

    std::vector<uint8_t> sprite_rom(is_dd2() ? 0xc0000 : 0x80000, 0);
    if (!loader.load(is_dd2() ? kSpriteRoms2 : kSpriteRoms, sprite_rom, error)) return false;

    if (is_dd2()) {
        std::vector<uint8_t> sub_rom(0x10000, 0);
        if (!loader.load(kSubRoms2, sub_rom, error)) return false;
        std::copy(sub_rom.begin(), sub_rom.end(), sub_memory_.begin());

        std::vector<uint8_t> sound_rom(0x8000, 0);
        if (!loader.load(kSoundRoms2, sound_rom, error)) return false;
        std::copy(sound_rom.begin(), sound_rom.end(), sound_memory_.begin());

        std::vector<uint8_t> adpcm(0x40000, 0);
        if (!loader.load(kAdpcmRoms2, adpcm, error)) return false;
        oki_.set_rom(std::move(adpcm));

        decode_graphics(char_rom, tile_rom, sprite_rom, 6);
    } else {
        std::vector<uint8_t> sub_rom(0x4000, 0);
        if (!loader.load(kSubRoms, sub_rom, error)) return false;
        std::copy(sub_rom.begin(), sub_rom.end(), sub_cpu_.internal_rom().begin());

        std::vector<uint8_t> sound_rom(0x10000, 0);
        if (!loader.load(kSoundRoms, sound_rom, error)) return false;
        std::copy(sound_rom.begin(), sound_rom.end(), sound_memory_.begin());

        std::vector<uint8_t> adpcm(0x20000, 0);
        if (!loader.load(kAdpcmRoms, adpcm, error)) return false;
        msm0_.set_rom(std::vector<uint8_t>(adpcm.begin(), adpcm.begin() + 0x10000));
        msm1_.set_rom(std::vector<uint8_t>(adpcm.begin() + 0x10000, adpcm.end()));

        decode_graphics(char_rom, tile_rom, sprite_rom, 4);
    }

    warnings_ = loader.warnings();
    return true;
}

void DoubleDragon::decode_graphics(const std::vector<uint8_t>& char_rom,
                                   const std::vector<uint8_t>& tile_rom,
                                   const std::vector<uint8_t>& sprite_rom, int sprite_banks) {
    chars_.decode(char_layout(is_dd2() ? 0x800 : 0x400), char_rom);
    tiles_.decode(tile_layout(0x800, 0x20000), tile_rom);
    sprites_.decode(tile_layout(is_dd2() ? 0x1800 : 0x1000, sprite_banks * 0x10000), sprite_rom);
}

void DoubleDragon::reset() {
    main_cpu_.reset();
    ym_.reset();
    if (is_dd2()) {
        sub_cpu2_.reset();
        sound_cpu2_.reset();
        oki_.reset();
    } else {
        sub_cpu_.reset();
        sound_cpu_.reset();
        msm0_.reset();
        msm1_.reset();
    }
    in0_ = 0xff;
    in1_ = 0xff;
    in2_ = 0xe7;
    sound_latch_ = 0;
    rom_bank_ = 0;
    sub_port_ = 0;
    scroll_x_ = 0;
    scroll_y_ = 0;
    flip_screen_ = false;
    sub_halt_ = false;
    sub_reset_ = false;
    audio_accumulator_ = 0;
    msm_accumulator_ = 0;
    audio_.clear();
    background_.fill(0xff000000u);
    composite_.fill(0xff000000u);
}

uint8_t DoubleDragon::main_read(uint16_t address) {
    if (!is_dd2() && address >= 0x1000 && address <= 0x13ff) return palette_ram_[address & 0x3ff];
    if (is_dd2() && address >= 0x3c00 && address <= 0x3fff) return palette_ram_[address & 0x3ff];
    if (address >= 0x2000 && address <= 0x27ff) {
        if (!sub_stopped()) return 0xff;
        return is_dd2() ? sub_memory_[0xc000 + (address & 0x1ff)] : shared_ram_[address & 0x1ff];
    }
    if (address >= 0x3800 && address <= 0x380f) {
        switch (address) {
            case 0x3800: return in0_;
            case 0x3801: return in1_;
            case 0x3802: return uint8_t(in2_ | (sub_stopped() ? 0x00 : 0x10));
            case 0x3803: return dsw_a_;
            case 0x3804: return dsw_b_;
            case 0x380b: main_cpu_.set_nmi(IrqLine::Clear); return 0xff;
            case 0x380c: main_cpu_.set_firq(IrqLine::Clear); return 0xff;
            case 0x380d: main_cpu_.set_irq(IrqLine::Clear); return 0xff;
            case 0x380e:
                if (is_dd2()) sound_cpu2_.set_nmi(IrqLine::Assert);
                else sound_cpu_.set_irq(IrqLine::Assert);
                return sound_latch_;
            case 0x380f:
                if (is_dd2()) sub_cpu2_.set_nmi(IrqLine::Assert);
                else sub_cpu_.set_nmi(IrqLine::Assert);
                return 0xff;
            default: return 0xff;
        }
    }
    if (address >= 0x4000 && address <= 0x7fff) {
        return banked_rom_[size_t(rom_bank_ % 6) * 0x4000 + (address & 0x3fff)];
    }
    return memory_[address];
}

void DoubleDragon::main_write(uint16_t address, uint8_t value) {
    uint16_t palette_base = is_dd2() ? 0x3c00 : 0x1000;
    if (address >= palette_base && address < palette_base + 0x400) {
        palette_ram_[address & 0x3ff] = value;
        update_palette(uint16_t(address & 0x1ff));
        return;
    }
    if (address >= 0x2000 && address <= 0x27ff) {
        if (!sub_stopped()) return;
        if (is_dd2()) sub_memory_[0xc000 + (address & 0x1ff)] = value;
        else shared_ram_[address & 0x1ff] = value;
        return;
    }
    if (address >= 0x3800 && address <= 0x380f) {
        switch (address) {
            case 0x3808: write_control(value); break;
            case 0x3809: scroll_x_ = uint16_t((scroll_x_ & 0x100) | value); break;
            case 0x380a: scroll_y_ = uint16_t((scroll_y_ & 0x100) | value); break;
            case 0x380b: main_cpu_.set_nmi(IrqLine::Clear); break;
            case 0x380c: main_cpu_.set_firq(IrqLine::Clear); break;
            case 0x380d: main_cpu_.set_irq(IrqLine::Clear); break;
            case 0x380e:
                sound_latch_ = value;
                if (is_dd2()) sound_cpu2_.set_nmi(IrqLine::Assert);
                else sound_cpu_.set_irq(IrqLine::Assert);
                break;
            case 0x380f:
                if (is_dd2()) sub_cpu2_.set_nmi(IrqLine::Assert);
                else sub_cpu_.set_nmi(IrqLine::Assert);
                break;
            default: break;
        }
        return;
    }
    if (address >= 0x4000) return;  // ROM
    memory_[address] = value;
}

void DoubleDragon::write_control(uint8_t value) {
    scroll_x_ = uint16_t((scroll_x_ & 0xff) | ((value & 0x01) << 8));
    scroll_y_ = uint16_t((scroll_y_ & 0xff) | ((value & 0x02) << 7));
    flip_screen_ = (value & 0x04) == 0;
    bool reset_line = (value & 0x08) == 0;
    bool halt_line = (value & 0x10) != 0;
    if (!is_dd2()) {
        sub_cpu_.set_reset(reset_line ? IrqLine::Assert : IrqLine::Clear);
        sub_cpu_.set_halt(halt_line ? IrqLine::Assert : IrqLine::Clear);
    } else if (sub_reset_ && !reset_line) {
        sub_cpu2_.reset();
    }
    sub_reset_ = reset_line;
    sub_halt_ = halt_line;
    rom_bank_ = uint8_t((value & 0xe0) >> 5);
}

uint8_t DoubleDragon::sub_read(uint16_t address) {
    if (address >= 0x8000 && address <= 0x81ff) return shared_ram_[address & 0x1ff];
    return 0xff;
}

void DoubleDragon::sub_write(uint16_t address, uint8_t value) {
    if (address >= 0x8000 && address <= 0x81ff) shared_ram_[address & 0x1ff] = value;
}

uint8_t DoubleDragon::sub2_read(uint16_t address) {
    if (address < 0xc400) return sub_memory_[address];
    return 0xff;
}

void DoubleDragon::sub2_write(uint16_t address, uint8_t value) {
    if (address >= 0xc000 && address <= 0xc3ff) {
        sub_memory_[address] = value;
        return;
    }
    if (address == 0xd000) {
        sub_cpu2_.set_nmi(IrqLine::Clear);
        return;
    }
    if (address == 0xe000) main_cpu_.set_irq(IrqLine::Assert);
}

uint8_t DoubleDragon::sound_read(uint16_t address) {
    if (address <= 0x0fff || address >= 0x8000) return sound_memory_[address];
    switch (address) {
        case 0x1000:
            sound_cpu_.set_irq(IrqLine::Clear);
            return sound_latch_;
        case 0x1800: return uint8_t(uint8_t(msm0_.idle()) | (uint8_t(msm1_.idle()) << 1));
        case 0x2801: return ym_.status();
        default: return 0xff;
    }
}

void DoubleDragon::sound_write(uint16_t address, uint8_t value) {
    if (address <= 0x0fff) {
        sound_memory_[address] = value;
        return;
    }
    if (address == 0x2800) {
        ym_.select_register(value);
        return;
    }
    if (address == 0x2801) {
        ym_.write(value);
        return;
    }
    if (address >= 0x3800 && address <= 0x3807) {
        switch (address & 7) {
            case 0: msm0_.set_reset(false); break;
            case 1: msm1_.set_reset(false); break;
            case 2: msm0_.set_end(uint32_t(value & 0x7f) * 0x200); break;
            case 3: msm1_.set_end(uint32_t(value & 0x7f) * 0x200); break;
            case 4: msm0_.set_start(uint32_t(value & 0x7f) * 0x200); break;
            case 5: msm1_.set_start(uint32_t(value & 0x7f) * 0x200); break;
            case 6: msm0_.set_reset(true); break;
            case 7: msm1_.set_reset(true); break;
            default: break;
        }
    }
}

uint8_t DoubleDragon::sound2_read(uint16_t address) {
    if (address <= 0x87ff) return sound_memory_[address];
    switch (address) {
        case 0x8801: return ym_.status();
        case 0x9800: return oki_.read();
        case 0xa000:
            sound_cpu2_.set_nmi(IrqLine::Clear);
            return sound_latch_;
        default: return 0xff;
    }
}

void DoubleDragon::sound2_write(uint16_t address, uint8_t value) {
    if (address >= 0x8000 && address <= 0x87ff) {
        sound_memory_[address] = value;
        return;
    }
    switch (address) {
        case 0x8800: ym_.select_register(value); break;
        case 0x8801: ym_.write(value); break;
        case 0x9800: oki_.write(value); break;
        default: break;
    }
}

void DoubleDragon::on_sound_cycles(int cycles) {
    // The YM2151 runs from its own oscillator, faster than the 6809.
    int64_t ym_cycles = int64_t(cycles) * kYmClock / kSoundClock;
    ym_.run_timers(int(ym_cycles));

    msm_accumulator_ += int64_t(cycles) * msm0_.sample_frequency();
    while (msm_accumulator_ >= kSoundClock) {
        msm_accumulator_ -= kSoundClock;
        msm0_.vclk();
        msm1_.vclk();
    }

    audio_accumulator_ += int64_t(cycles) * YM2151::kSampleRate;
    while (audio_accumulator_ >= kSoundClock) {
        audio_accumulator_ -= kSoundClock;
        int32_t sample = ym_.update() + (msm0_.output() + msm1_.output()) * 2 / 5;
        audio_.push_back(int16_t(std::clamp(sample, -32768, 32767)));
    }
}

void DoubleDragon::on_sound2_cycles(int cycles) {
    ym_.run_timers(cycles);
    audio_accumulator_ += int64_t(cycles) * YM2151::kSampleRate;
    while (audio_accumulator_ >= kSound2Clock) {
        audio_accumulator_ -= kSound2Clock;
        int32_t sample = ym_.update() + oki_.update() / 2;
        audio_.push_back(int16_t(std::clamp(sample, -32768, 32767)));
    }
}

void DoubleDragon::update_palette(uint16_t offset) {
    uint8_t low = palette_ram_[offset];
    uint8_t high = palette_ram_[size_t(offset) + 0x200];
    palette_[offset] = 0xff000000u | (uint32_t(pal4bit(low)) << 16) |
                       (uint32_t(pal4bit(uint8_t(low >> 4))) << 8) | uint32_t(pal4bit(high));
}

void DoubleDragon::draw_background() {
    for (int index = 0; index < 0x400; index++) {
        int x = index % 32;
        int y = index / 32;
        int position = (x & 0x0f) + ((y & 0x0f) << 4) + ((x & 0x10) << 4) + ((y & 0x10) << 5);
        uint8_t attributes = memory_[size_t(position * 2 + 0x3000)];
        int color = 0x100 + (((attributes & 0x38) >> 3) << 4);
        int code = memory_[size_t(position * 2 + 0x3001)] + ((attributes & 0x07) << 8);
        bool flip_x = (attributes & 0x40) != 0;
        bool flip_y = (attributes & 0x80) != 0;
        const uint8_t* pixels = tiles_.element(code);
        for (int row = 0; row < 16; row++) {
            int source_y = flip_y ? 15 - row : row;
            size_t target = size_t((y * 16 + row) * 512 + x * 16);
            for (int column = 0; column < 16; column++) {
                int source_x = flip_x ? 15 - column : column;
                background_[target + size_t(column)] =
                    palette_[size_t(color + pixels[source_y * 16 + source_x])];
            }
        }
    }
}

void DoubleDragon::draw_sprite_element(int code, int color, bool flip_x, bool flip_y, int pos_x,
                                       int pos_y) {
    const uint8_t* pixels = sprites_.element(code);
    for (int row = 0; row < 16; row++) {
        int y = pos_y + row;
        if (y < 0 || y >= 256) continue;
        int source_y = flip_y ? 15 - row : row;
        for (int column = 0; column < 16; column++) {
            int x = pos_x + column;
            if (x < 0 || x >= 256) continue;
            int source_x = flip_x ? 15 - column : column;
            uint8_t pen = pixels[source_y * 16 + source_x];
            if (pen == 0) continue;
            composite_[size_t(y * 256 + x)] = palette_[size_t(color + pen)];
        }
    }
}

void DoubleDragon::draw_sprites() {
    for (int index = 0; index < 0x40; index++) {
        const uint8_t* entry = &memory_[size_t(0x2800 + index * 5)];
        uint8_t attributes = entry[1];
        if ((attributes & 0x80) == 0) continue;
        int x = 240 - entry[4] + ((attributes & 0x02) << 7);
        int y = 240 - entry[0] + ((attributes & 0x01) << 8);
        int size = (attributes & 0x30) >> 4;
        bool flip_x = (attributes & 0x08) != 0;
        bool flip_y = (attributes & 0x04) != 0;
        int color;
        int code;
        if (is_dd2()) {
            color = ((entry[2] >> 5) << 4) + 0x80;
            code = entry[3] + ((entry[2] & 0x1f) << 8);
        } else {
            color = (((entry[2] >> 4) & 0x07) << 4) + 0x80;
            code = entry[3] + ((entry[2] & 0x0f) << 8);
        }
        code &= ~size;
        switch (size) {
            case 1:  // double height
                draw_sprite_element(code, color, flip_x, flip_y, x, y - 16);
                draw_sprite_element(code + 1, color, flip_x, flip_y, x, y);
                break;
            case 2:  // double width
                draw_sprite_element(code, color, flip_x, flip_y, x - 16, y);
                draw_sprite_element(code + 1, color, flip_x, flip_y, x, y);
                break;
            case 3:
                draw_sprite_element(code, color, flip_x, flip_y, x - 16, y - 16);
                draw_sprite_element(code + 1, color, flip_x, flip_y, x, y - 16);
                draw_sprite_element(code + 2, color, flip_x, flip_y, x - 16, y);
                draw_sprite_element(code + 3, color, flip_x, flip_y, x, y);
                break;
            default: draw_sprite_element(code, color, flip_x, flip_y, x, y); break;
        }
    }
}

void DoubleDragon::draw_foreground() {
    for (int index = 0; index < 0x400; index++) {
        int x = index % 32;
        int y = index / 32;
        uint8_t attributes = memory_[size_t(0x1800 + index * 2)];
        int color = ((attributes & 0xe0) >> 5) << 4;
        int code = memory_[size_t(0x1801 + index * 2)] + ((attributes & 0x07) << 8);
        const uint8_t* pixels = chars_.element(code);
        for (int row = 0; row < 8; row++) {
            size_t target = size_t((y * 8 + row) * 256 + x * 8);
            for (int column = 0; column < 8; column++) {
                uint8_t pen = pixels[row * 8 + column];
                if (pen == 0) continue;
                composite_[target + size_t(column)] = palette_[size_t(color + pen)];
            }
        }
    }
}

void DoubleDragon::update_video() {
    draw_background();
    for (int y = 0; y < 256; y++) {
        size_t source = size_t(((y + scroll_y_) & 0x1ff) * 512);
        size_t target = size_t(y * 256);
        for (int x = 0; x < 256; x++) {
            composite_[target + size_t(x)] = background_[source + ((x + scroll_x_) & 0x1ff)];
        }
    }
    draw_sprites();
    draw_foreground();

    // Visible area: 256x240 starting at line 8 of the 256x256 work surface.
    for (int y = 0; y < kScreenHeight; y++) {
        for (int x = 0; x < kScreenWidth; x++) {
            uint32_t pixel = composite_[size_t((y + 8) * 256 + x)];
            size_t target = flip_screen_ ? size_t((kScreenHeight - 1 - y) * kScreenWidth +
                                                  (kScreenWidth - 1 - x))
                                         : size_t(y * kScreenWidth + x);
            framebuffer_[target] = pixel;
        }
    }
}

void DoubleDragon::run_frame() {
    const int main_slice = int(kMainClock / kFramesPerSecond / (kScanlines * kCpuSync));
    const int sub_slice =
        int((is_dd2() ? kSub2Clock : kSubClock) / kFramesPerSecond / (kScanlines * kCpuSync));
    const int sound_slice =
        int((is_dd2() ? kSound2Clock : kSoundClock) / kFramesPerSecond / (kScanlines * kCpuSync));

    for (int line = 0; line < kScanlines; line++) {
        if (line == 8) in2_ = uint8_t(in2_ & 0xf7);
        if (line >= 16 && line <= 240 && (line % 16) == 0) main_cpu_.set_firq(IrqLine::Assert);
        if (line == 248) {
            in2_ = uint8_t(in2_ | 0x08);
            main_cpu_.set_nmi(IrqLine::Assert);
            update_video();
        }
        if (line == 264) main_cpu_.set_firq(IrqLine::Assert);

        for (int slice = 0; slice < kCpuSync; slice++) {
            main_cpu_.run(main_slice);
            if (is_dd2()) {
                if (!sub_stopped()) sub_cpu2_.run(sub_slice);
                sound_cpu2_.run(sound_slice);
            } else {
                sub_cpu_.run(sub_slice);
                sound_cpu_.run(sound_slice);
            }
        }
    }
}

void DoubleDragon::set_inputs(const MachineInputs& inputs) {
    const InputState& player1 = inputs.player1;
    const InputState& player2 = inputs.player2;
    in0_ = 0xff;
    in1_ = 0xff;
    in2_ = uint8_t(0xe7 | (in2_ & 0x08));

    if (player1.right) in0_ &= 0xfe;
    if (player1.left) in0_ &= 0xfd;
    if (player1.up) in0_ &= 0xfb;
    if (player1.down) in0_ &= 0xf7;
    if (player1.button1) in0_ &= 0xef;
    if (player1.button2) in0_ &= 0xdf;
    if (player1.start) in0_ &= 0xbf;
    if (player2.start) in0_ &= 0x7f;
    if (player1.button3) in2_ &= 0xfd;  // jump
    if (player2.button3) in2_ &= 0xfb;

    if (player2.right) in1_ &= 0xfe;
    if (player2.left) in1_ &= 0xfd;
    if (player2.up) in1_ &= 0xfb;
    if (player2.down) in1_ &= 0xf7;
    if (player2.button1) in1_ &= 0xef;
    if (player2.button2) in1_ &= 0xdf;
    if (inputs.coin1) in1_ &= 0xbf;
    if (inputs.coin2) in1_ &= 0x7f;
}

void DoubleDragon::set_dip_switch(int bank, uint8_t value) {
    switch (bank) {
        case 0: dsw_a_ = value; break;
        case 1: dsw_b_ = value; break;
        default: break;
    }
}

void DoubleDragon::drain_audio(std::vector<int16_t>& out) {
    out.insert(out.end(), audio_.begin(), audio_.end());
    audio_.clear();
}

}  // namespace dsp
