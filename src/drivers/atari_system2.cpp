#include "drivers/atari_system2.h"

#include <algorithm>
#include <cstring>

#include "core/rom_loader.h"

namespace dsp {
namespace {

// MAME atarisy2.cpp, paperboy ROM set. The T-11 program is interleaved: the
// even offset holds the low byte of every word.
const std::vector<RomEntry> kPaperboyMain = {
    {"cpu_l07.rv3", 0x4000, 0x008000, 0x4024bb9b},
    {"cpu_n07.rv3", 0x4000, 0x008001, 0x0260901a},
    {"cpu_f06.rv2", 0x4000, 0x010000, 0x3fea86ac},
    {"cpu_n06.rv2", 0x4000, 0x010001, 0x711b17ba},
    {"cpu_j06.rv1", 0x4000, 0x030000, 0xa754b12d},
    {"cpu_p06.rv1", 0x4000, 0x030001, 0x89a1ff9c},
    {"cpu_k06.rv1", 0x4000, 0x050000, 0x290bb034},
    {"cpu_r06.rv1", 0x4000, 0x050001, 0x826993de},
    {"cpu_l06.rv2", 0x4000, 0x070000, 0x8a754466},
    {"cpu_s06.rv2", 0x4000, 0x070001, 0x224209f9},
};

const std::vector<RomEntry> kPaperboySound = {
    {"cpu_a02.rv3", 0x4000, 0x4000, 0xba251bc4},
    {"cpu_b02.rv2", 0x4000, 0x8000, 0xe4e7a8b9},
    {"cpu_c02.rv2", 0x4000, 0xc000, 0xd44c2aa2},
};

const std::vector<RomEntry> kPaperboyPlayfield = {
    {"vid_a06.rv1", 0x8000, 0x00000, 0xb32ffddf},
    {"vid_b06.rv1", 0x4000, 0x0c000, 0x301b849d},
    {"vid_c06.rv1", 0x8000, 0x10000, 0x7bb59d68},
    {"vid_d06.rv1", 0x4000, 0x1c000, 0x1a1d4ba8},
};

const std::vector<RomEntry> kPaperboyMotion = {
    {"vid_l06.rv1", 0x8000, 0x00000, 0x067ef202},
    {"vid_k06.rv1", 0x8000, 0x08000, 0x76b977c4},
    {"vid_j06.rv1", 0x8000, 0x10000, 0x2a3cc8d0},
    {"vid_h06.rv1", 0x8000, 0x18000, 0x6763a321},
    {"vid_s06.rv1", 0x8000, 0x20000, 0x0a321b7b},
    {"vid_p06.rv1", 0x8000, 0x28000, 0x5bd089ee},
    {"vid_n06.rv1", 0x8000, 0x30000, 0xc34a517d},
    {"vid_m06.rv1", 0x8000, 0x38000, 0xdf723956},
};

const std::vector<RomEntry> kPaperboyAlpha = {
    {"vid_t06.rv1", 0x2000, 0x0000, 0x60d7aebb},
};

const std::vector<RomEntry> kPaperboyEeprom = {
    {"paperboy-eeprom.bin", 0x200, 0x0000, 0x756b90cc},
};

GfxLayout alpha_layout() {
    GfxLayout layout;
    layout.width = 8;
    layout.height = 8;
    layout.total = 0x2000 / 16;
    layout.planes = 2;
    layout.char_increment = 8 * 8 * 2;
    layout.plane_offsets = {0, 4};
    layout.x_offsets = {0, 1, 2, 3, 8, 9, 10, 11};
    layout.y_offsets = {0, 16, 32, 48, 64, 80, 96, 112};
    return layout;
}

GfxLayout playfield_layout() {
    constexpr int kHalf = 0x10000 * 8;
    GfxLayout layout;
    layout.width = 8;
    layout.height = 8;
    layout.total = 0x10000 / 16;
    layout.planes = 4;
    layout.char_increment = 8 * 8 * 2;
    layout.plane_offsets = {0, 4, kHalf + 0, kHalf + 4};
    layout.x_offsets = {0, 1, 2, 3, 8, 9, 10, 11};
    layout.y_offsets = {0, 16, 32, 48, 64, 80, 96, 112};
    return layout;
}

GfxLayout motion_layout() {
    constexpr int kHalf = 0x20000 * 8;
    GfxLayout layout;
    layout.width = 16;
    layout.height = 16;
    layout.total = 0x20000 / 64;
    layout.planes = 4;
    layout.char_increment = 16 * 16 * 2;
    layout.plane_offsets = {0, 4, kHalf + 0, kHalf + 4};
    layout.x_offsets = {0, 1, 2, 3, 8, 9, 10, 11, 16, 17, 18, 19, 24, 25, 26, 27};
    layout.y_offsets = {0,   32,  64,  96,  128, 160, 192, 224,
                        256, 288, 320, 352, 384, 416, 448, 480};
    return layout;
}

AtariMotionObjects::Config motion_config() {
    AtariMotionObjects::Config config;
    config.tile_width = 16;
    config.tile_height = 16;
    config.bankcount = 1;
    config.linked = true;
    config.split = false;
    config.slipheight = 0;
    config.maxperline = 0;
    config.palettebase = 0;
    config.link_entry = {0, 0, 0, 0x07f8};
    config.code_entry = {{0, 0x07ff, 0, 0}, {0x0007, 0, 0, 0}};
    config.color_entry = {{0, 0, 0, 0x3000}, {0, 0, 0, 0}};
    config.xpos_entry = {0, 0, 0xffc0, 0};
    config.ypos_entry = {0x7fc0, 0, 0, 0};
    config.height_entry = {0, 0x3800, 0, 0};
    config.hflip_entry = {0, 0x4000, 0, 0};
    config.priority_entry = {0, 0, 0, 0xc000};
    config.neighbor_entry = {0, 0x8000, 0, 0};
    return config;
}

// MAME atarisy2_state::RRRRGGGGBBBBIIII.
uint32_t palette_entry(uint16_t raw) {
    constexpr int ZB = 115, Z3 = 78, Z2 = 37, Z1 = 17, Z0 = 9;
    static const int kIntensity[16] = {
        0,        ZB + Z0,       ZB + Z1,       ZB + Z1 + Z0,
        ZB + Z2,  ZB + Z2 + Z0,  ZB + Z2 + Z1,  ZB + Z2 + Z1 + Z0,
        ZB + Z3,  ZB + Z3 + Z0,  ZB + Z3 + Z1,  ZB + Z3 + Z1 + Z0,
        ZB + Z3 + Z2, ZB + Z3 + Z2 + Z0, ZB + Z3 + Z2 + Z1, ZB + Z3 + Z2 + Z1 + Z0};
    static const int kColor[16] = {0x0, 0x3, 0x4, 0x5, 0x6, 0x7, 0x8, 0x9,
                                   0xa, 0xb, 0xc, 0xd, 0xe, 0xe, 0xf, 0xf};
    const int i = kIntensity[raw & 15];
    const uint32_t red = uint32_t((kColor[(raw >> 12) & 15] * i) >> 4);
    const uint32_t green = uint32_t((kColor[(raw >> 8) & 15] * i) >> 4);
    const uint32_t blue = uint32_t((kColor[(raw >> 4) & 15] * i) >> 4);
    return 0xff000000u | (red << 16) | (green << 8) | blue;
}

}  // namespace

AtariSystem2::AtariSystem2(Game game)
    : game_(game),
      main_cpu_(kMainClock, 0x36ff),
      sound_cpu_(kSoundClock),
      ym_(kYmClock),
      pokey1_(kPokeyClock),
      pokey2_(kPokeyClock),
      tms_(kMasterClock / 4 / 4 / 2, Tms5220::Variant::Tms5220C),
      slapstic_(105, nullptr) {
    rom_.assign(0x90000 / 2, 0);
    alpha_.assign(size_t(kScreenWidth) * kScreenHeight, kTransparent);
    playfield_.assign(size_t(kPlayfieldWidth) * kPlayfieldHeight, 0);
    playfield_category_.assign(size_t(kPlayfieldWidth) * kPlayfieldHeight, 0);
    mo_pen_.assign(size_t(kScreenWidth) * kScreenHeight, kMoTransparent);
    mo_priority_.assign(size_t(kScreenWidth) * kScreenHeight, 0);
    framebuffer_.assign(size_t(kScreenWidth) * kScreenHeight, 0xff000000u);

    main_cpu_.set_memory_handlers(
        [this](uint16_t address) { return main_read(address); },
        [this](uint16_t address, uint16_t value, uint16_t mem_mask) {
            main_write(address, value, mem_mask);
        });
    sound_cpu_.set_memory_handlers(
        [this](uint16_t address) { return sound_read(address); },
        [this](uint16_t address, uint8_t value) { sound_write(address, value); });
    sound_cpu_.set_cycle_handler([this](int cycles) { on_sound_cycles(cycles); });
    ym_.set_irq_handler(
        [this](bool on) { sound_cpu_.set_irq(on ? IrqLine::Hold : IrqLine::Clear); });
    pokey1_.set_allpot_handler([this](uint8_t) { return dsw_[0]; });
    pokey2_.set_allpot_handler([this](uint8_t) { return dsw_[1]; });

    motion_objects_ = std::make_unique<AtariMotionObjects>(
        motion_config(), nullptr, mob_ram_.data(), kScreenWidth + 16, kScreenHeight + 16);
}

const char* AtariSystem2::title() const {
    switch (game_) {
        default: return "Paperboy";
    }
}

bool AtariSystem2::init(const std::string& rom_path, std::string* error) {
    if (!load_roms(rom_path, error)) return false;
    reset();
    return true;
}

bool AtariSystem2::load_roms(const std::string& rom_path, std::string* error) {
    RomLoader loader;
    if (!loader.open(rom_path, error)) return false;

    std::vector<uint8_t> main_bytes(0x90000, 0);
    for (const RomEntry& entry : kPaperboyMain) {
        std::vector<uint8_t> data(entry.length, 0);
        const RomEntry single{entry.name, entry.length, 0, entry.crc};
        if (!loader.load({single}, data, error)) return false;
        const uint32_t base = entry.offset & ~1u;
        const uint32_t lane = entry.offset & 1u;
        for (uint32_t i = 0; i < entry.length; i++) {
            main_bytes[base + i * 2 + lane] = data[i];
        }
    }
    // MAME init_paperboy(): expand the 16k program ROM pairs into 64k chunks.
    for (uint32_t i = 0x10000; i < 0x90000; i += 0x20000) {
        std::memcpy(&main_bytes[i + 0x08000], &main_bytes[i], 0x8000);
        std::memcpy(&main_bytes[i + 0x10000], &main_bytes[i], 0x8000);
        std::memcpy(&main_bytes[i + 0x18000], &main_bytes[i], 0x8000);
    }
    for (size_t i = 0; i < rom_.size(); i++) {
        rom_[i] = uint16_t(main_bytes[i * 2] | (main_bytes[i * 2 + 1] << 8));
    }

    std::vector<uint8_t> sound_rom(0x10000, 0);
    if (!loader.load(kPaperboySound, sound_rom, error)) return false;
    std::copy(sound_rom.begin(), sound_rom.end(), sound_memory_.begin());

    std::vector<uint8_t> playfield_rom(0x20000, 0);
    if (!loader.load(kPaperboyPlayfield, playfield_rom, error)) return false;
    playfield_gfx_.decode(playfield_layout(), playfield_rom);

    std::vector<uint8_t> motion_rom(0x40000, 0);
    if (!loader.load(kPaperboyMotion, motion_rom, error)) return false;
    // ROMREGION_INVERT: the motion object ROMs are stored inverted, which turns
    // the transparent pen into 15.
    for (uint8_t& value : motion_rom) value = uint8_t(~value);
    motion_gfx_.decode(motion_layout(), motion_rom);

    std::vector<uint8_t> alpha_rom(0x2000, 0);
    if (!loader.load(kPaperboyAlpha, alpha_rom, error)) return false;
    alpha_gfx_.decode(alpha_layout(), alpha_rom);

    std::vector<uint8_t> eeprom(0x200, 0xff);
    std::string eeprom_error;
    if (loader.load(kPaperboyEeprom, eeprom, &eeprom_error)) {
        std::copy(eeprom.begin(), eeprom.end(), eeprom_.begin());
    } else {
        eeprom_.fill(0xff);
        warnings_.push_back("default EEPROM missing: " + eeprom_error);
    }

    const std::vector<std::string>& loader_warnings = loader.warnings();
    warnings_.insert(warnings_.end(), loader_warnings.begin(), loader_warnings.end());
    return true;
}

void AtariSystem2::reset() {
    slapstic_.reset();
    vram_bank_ = slapstic_.current_bank();
    main_cpu_.reset();
    ym_.reset();
    pokey1_.reset();
    pokey2_.reset();
    tms_.reset();
    // /RS is tied high on System 2 hardware.
    tms_.set_rsq(true);
    tms_.set_wsq(true);

    ram_.fill(0);
    alpha_ram_.fill(0);
    mob_ram_.fill(0);
    playfield_top_.fill(0);
    playfield_bottom_.fill(0);
    palette_ram_.fill(0);
    palette_.fill(0xff000000u);

    rom_bank_[0] = 0;
    rom_bank_[1] = 0;
    xscroll_ = 0;
    yscroll_reg_ = 0;
    yscroll_ = 0;
    yscroll_pending_ = 0;
    yscroll_reset_ = false;
    playfield_tile_bank_.fill(0);
    xscroll_line_.fill(0);
    yscroll_line_.fill(0);

    interrupt_enable_ = 0;
    video_int_state_ = false;
    scanline_int_state_ = false;
    p2portwr_state_ = false;
    p2portrd_state_ = false;
    update_interrupts();

    sound_reset_state_ = false;
    sound_cpu_in_reset_ = false;
    set_sound_reset(true);
    sound_latch_ = 0;
    main_latch_ = 0;
    sound_pending_ = false;
    main_pending_ = false;
    sound_irq_counter_ = 0;
    sound_cpu_.set_irq(IrqLine::Clear);
    sound_cpu_.set_nmi(IrqLine::Clear);

    adc_value_ = 0;
    adc_input_ = {0x80, 0x80, 0x00, 0x00};
    buttons_ = 0;
    coin1_ = coin2_ = coin3_ = false;
    service_coin_ = false;
    self_test_ = false;

    line_ = 0;
    audio_accumulator_ = 0;
    audio_.clear();

    alpha_dirty_.fill(true);
    playfield_dirty_.fill(true);
    std::fill(alpha_.begin(), alpha_.end(), kTransparent);
    std::fill(playfield_.begin(), playfield_.end(), 0);
    std::fill(playfield_category_.begin(), playfield_category_.end(), 0);
    std::fill(mo_pen_.begin(), mo_pen_.end(), kMoTransparent);
    std::fill(mo_priority_.begin(), mo_priority_.end(), 0);
    std::fill(framebuffer_.begin(), framebuffer_.end(), 0xff000000u);
}

void AtariSystem2::update_interrupts() {
    main_cpu_.set_irq(T11::CP3_LINE, video_int_state_ ? IrqLine::Assert : IrqLine::Clear);
    main_cpu_.set_irq(T11::CP2_LINE, scanline_int_state_ ? IrqLine::Assert : IrqLine::Clear);
    main_cpu_.set_irq(T11::CP1_LINE, p2portwr_state_ ? IrqLine::Assert : IrqLine::Clear);
    main_cpu_.set_irq(T11::CP0_LINE, p2portrd_state_ ? IrqLine::Assert : IrqLine::Clear);
}

void AtariSystem2::write_sound_chip_reset(uint8_t value) {
    if ((value & 1) == (sound_reset_state_ ? 1 : 0)) return;
    sound_reset_state_ = (value & 1) != 0;
    ym_.reset();
    // Only the 0 -> 1 transition halts the speech chip.
    if (!sound_reset_state_) return;
    tms_.reset();
    tms_.set_rsq(true);
    tms_.set_wsq(true);
}

void AtariSystem2::set_sound_reset(bool in_reset) {
    if (in_reset == sound_cpu_in_reset_) return;
    sound_cpu_in_reset_ = in_reset;
    if (in_reset) {
        sound_cpu_.set_irq(IrqLine::Clear);
        sound_cpu_.set_nmi(IrqLine::Clear);
        sound_pending_ = false;
        main_pending_ = false;
    } else {
        sound_cpu_.reset();
    }
}

void AtariSystem2::bank_select(int index, uint16_t data) {
    uint8_t bank = uint8_t(((data >> 10) & 077) ^ 3);
    // MAME: bitswap<6>(bank, 5, 4, 1, 0, 3, 2).
    bank = uint8_t((bank & 0x30) | (((bank >> 1) & 1) << 3) | ((bank & 1) << 2) |
                   (((bank >> 3) & 1) << 1) | ((bank >> 2) & 1));
    rom_bank_[size_t(index & 1)] = 0x8000u + uint32_t(bank) * 0x1000u;
}

void AtariSystem2::set_palette(int index, uint16_t value) {
    palette_ram_[size_t(index)] = value;
    palette_[size_t(index)] = palette_entry(value);
}

void AtariSystem2::write_xscroll(uint16_t value) {
    xscroll_ = value;
    if (playfield_tile_bank_[0] != (value & 0x0f)) {
        playfield_tile_bank_[0] = uint16_t(value & 0x0f);
        playfield_dirty_.fill(true);
    }
}

void AtariSystem2::write_yscroll(uint16_t value) {
    yscroll_reg_ = value;
    // Bit 4 clear clocks the new scroll value in right away; otherwise it is
    // latched until the top of the next frame.
    if ((value & 0x10) == 0) {
        yscroll_ = uint16_t((value >> 6) - uint16_t(line_));
        yscroll_reset_ = false;
    } else {
        yscroll_pending_ = uint16_t(value >> 6);
        yscroll_reset_ = true;
    }
    if (playfield_tile_bank_[1] != (value & 0x0f)) {
        playfield_tile_bank_[1] = uint16_t(value & 0x0f);
        playfield_dirty_.fill(true);
    }
}

uint16_t AtariSystem2::switch_r() const {
    // "1800": bits 0-3 unused, 4 = main latch pending, 5 = sound latch pending,
    // 6/7 = buttons (active low). "1801" bit 7 = self test (active low).
    uint16_t low = 0xcf;
    if (main_pending_) low |= 0x10;
    if (sound_pending_) low |= 0x20;
    low = uint16_t(low & ~uint16_t(buttons_));
    uint16_t high = 0xff;
    if (self_test_) high = uint16_t(high & ~0x80);
    return uint16_t(low | (high << 8));
}

uint8_t AtariSystem2::switch_6502_r() const {
    uint8_t result = 0xf4;
    if (sound_pending_) result |= 0x01;
    if (main_pending_) result |= 0x02;
    if (!tms_.readyq()) result = uint8_t(result & ~0x04);
    if (coin3_) result = uint8_t(result & ~0x20);
    if (coin1_) result = uint8_t(result & ~0x40);
    if (coin2_) result = uint8_t(result & ~0x80);
    if (service_coin_) result = uint8_t(result & ~0x10);
    return result;
}

uint8_t AtariSystem2::adc_channel_value(int channel) const {
    return adc_input_[size_t(channel & 3)];
}

uint16_t AtariSystem2::main_read(uint16_t address) {
    if (address < 0x1000) return ram_[address >> 1];
    if (address < 0x1400) return palette_ram_[(address >> 1) & 0xff];
    if (address < 0x1480) return adc_value_;
    if (address < 0x1800) return 0xffff;
    if (address < 0x1c00) return switch_r();
    if (address < 0x2000) {
        p2portwr_state_ = false;
        update_interrupts();
        main_pending_ = false;
        return uint16_t(main_latch_ | 0xff00);
    }
    if (address < 0x4000) {
        const uint16_t offset = uint16_t((address - 0x2000) >> 1);
        switch (vram_bank_) {
            case 0:
                if (address < 0x3800) return alpha_ram_[offset];
                return mob_ram_[offset & 0x3ff];
            case 2: return playfield_top_[offset];
            case 3: return playfield_bottom_[offset];
            default: return 0xffff;
        }
    }
    if (address < 0x6000) return rom_[rom_bank_[0] + ((address & 0x1fff) >> 1)];
    if (address < 0x8000) return rom_[rom_bank_[1] + ((address & 0x1fff) >> 1)];
    if (address < 0x8200) {
        const uint16_t value = rom_[address >> 1];
        vram_bank_ = slapstic_.tweak(uint16_t((address & 0x1ff) >> 1));
        return value;
    }
    return rom_[address >> 1];
}

void AtariSystem2::main_write(uint16_t address, uint16_t value, uint16_t mem_mask) {
    auto combine = [&](uint16_t old) {
        return uint16_t((old & ~mem_mask) | (value & mem_mask));
    };
    // The 8 bit registers live on the low half of an even address, so a write
    // that only covers the odd half never reaches them.
    const bool low_byte = (mem_mask & 0x00ff) != 0;
    if (address < 0x1000) {
        ram_[address >> 1] = combine(ram_[address >> 1]);
        return;
    }
    if (address < 0x1400) {
        const int index = (address >> 1) & 0xff;
        set_palette(index, combine(palette_ram_[size_t(index)]));
        return;
    }
    if (address < 0x1480) {
        bank_select((address >> 1) & 1, value);
        return;
    }
    if (address < 0x1500) {
        if (low_byte) adc_value_ = adc_channel_value((address >> 1) & 7);
        return;
    }
    if (address < 0x1580) return;
    if (address < 0x15a0) {
        if (!low_byte) return;
        p2portrd_state_ = false;
        update_interrupts();
        return;
    }
    if (address < 0x15c0) {
        if (!low_byte) return;
        set_sound_reset((value & 1) != 0);
        // MAME sound_reset_w() also releases the sound chip reset line.
        write_sound_chip_reset(0);
        return;
    }
    if (address < 0x15e0) {
        if (!low_byte) return;
        scanline_int_state_ = false;
        update_interrupts();
        return;
    }
    if (address < 0x1600) {
        if (!low_byte) return;
        video_int_state_ = false;
        update_interrupts();
        return;
    }
    if (address < 0x1680) {
        if (low_byte) interrupt_enable_ = uint8_t(value & 0x0f);
        return;
    }
    if (address < 0x1700) {
        if (!low_byte) return;
        sound_latch_ = uint8_t(value);
        sound_pending_ = true;
        sound_cpu_.set_nmi(IrqLine::Assert);
        return;
    }
    if (address < 0x1780) {
        write_xscroll(combine(xscroll_));
        return;
    }
    if (address < 0x1800) {
        write_yscroll(combine(yscroll_reg_));
        return;
    }
    if (address < 0x2000) return;  // watchdog / sound response are read only
    if (address < 0x4000) {
        const uint16_t offset = uint16_t((address - 0x2000) >> 1);
        switch (vram_bank_) {
            case 0:
                if (address < 0x3800) {
                    const uint16_t data = combine(alpha_ram_[offset]);
                    if (alpha_ram_[offset] != data) {
                        alpha_ram_[offset] = data;
                        alpha_dirty_[offset] = true;
                    }
                } else {
                    const uint16_t index = uint16_t(offset & 0x3ff);
                    mob_ram_[index] = combine(mob_ram_[index]);
                }
                return;
            case 2: {
                const uint16_t data = combine(playfield_top_[offset]);
                if (playfield_top_[offset] != data) {
                    playfield_top_[offset] = data;
                    playfield_dirty_[offset] = true;
                }
                return;
            }
            case 3: {
                const uint16_t data = combine(playfield_bottom_[offset]);
                if (playfield_bottom_[offset] != data) {
                    playfield_bottom_[offset] = data;
                    playfield_dirty_[size_t(offset) + 0x1000] = true;
                }
                return;
            }
            default: return;
        }
    }
    if (address >= 0x8000 && address < 0x8200) {
        vram_bank_ = slapstic_.tweak(uint16_t((address & 0x1ff) >> 1));
    }
}

uint8_t AtariSystem2::sound_read(uint16_t address) {
    if ((address & ~0x2000) <= 0x0fff) return sound_memory_[address & 0x0fff];
    if (address >= 0x4000) return sound_memory_[address];
    const uint16_t base = address & ~0x2600;
    if (base >= 0x1000 && base <= 0x11ff) return eeprom_[address & 0x1ff];
    if ((address & ~0x2780) >= 0x1800 && (address & ~0x2780) <= 0x180f) {
        return pokey1_.read(address & 0x0f);
    }
    if ((address & ~0x278c) >= 0x1810 && (address & ~0x278c) <= 0x1813) {
        return 0xff;  // LETA analog inputs, unused on Paperboy
    }
    if ((address & ~0x2780) >= 0x1830 && (address & ~0x2780) <= 0x183f) {
        return pokey2_.read(address & 0x0f);
    }
    if ((address & ~0x278f) == 0x1840) return switch_6502_r();
    if ((address & ~0x278e) == 0x1850 || (address & ~0x278e) == 0x1851) return ym_.status();
    if ((address & ~0x278f) == 0x1860) {
        p2portrd_state_ = (interrupt_enable_ & 0x01) != 0;
        update_interrupts();
        sound_pending_ = false;
        sound_cpu_.set_nmi(IrqLine::Clear);
        return sound_latch_;
    }
    return 0xff;
}

void AtariSystem2::sound_write(uint16_t address, uint8_t value) {
    if ((address & ~0x2000) <= 0x0fff) {
        sound_memory_[address & 0x0fff] = value;
        return;
    }
    if (address >= 0x4000) return;
    const uint16_t base = address & ~0x2600;
    if (base >= 0x1000 && base <= 0x11ff) {
        eeprom_[address & 0x1ff] = value;
        return;
    }
    if ((address & ~0x2780) >= 0x1800 && (address & ~0x2780) <= 0x180f) {
        pokey1_.write(address & 0x0f, value);
        return;
    }
    if ((address & ~0x2780) >= 0x1830 && (address & ~0x2780) <= 0x183f) {
        pokey2_.write(address & 0x0f, value);
        return;
    }
    if ((address & ~0x278e) == 0x1850) {
        ym_.select_register(value);
        return;
    }
    if ((address & ~0x278e) == 0x1851) {
        ym_.write(value);
        return;
    }
    const uint16_t io = address & ~0x2781;
    switch (io) {
        case 0x1870:  // speech data
            tms_.set_data_latch(value);
            return;
        case 0x1874:  // response to the main CPU
            p2portwr_state_ = (interrupt_enable_ & 0x02) != 0;
            update_interrupts();
            main_latch_ = value;
            main_pending_ = true;
            return;
        case 0x1876: return;  // coin counters
        case 0x1878:
            sound_cpu_.set_irq(IrqLine::Clear);
            return;
        case 0x187a: return;  // mixer
        case 0x187c:
            // Speech clock select: MASTER_CLOCK/4 / (16 - (12 | bit 5)) / 2.
            tms_.set_clock(kMasterClock / 4 / uint32_t(16 - (12 | ((value >> 5) & 1))) / 2);
            return;
        case 0x187e:
            write_sound_chip_reset(value);
            return;
        default: break;
    }
    if ((address & ~0x2780) == 0x1872 || (address & ~0x2780) == 0x1873) {
        tms_.set_wsq((address & 1) == 0);
        return;
    }
}

void AtariSystem2::on_sound_cycles(int cycles) {
    ym_.run_timers(cycles * 2);
    pokey1_.run(cycles);
    pokey2_.run(cycles);
    const int tms_clocks =
        int((int64_t(cycles) * int64_t(tms_.clock()) + (kSoundClock / 2)) / kSoundClock);
    tms_.tick(tms_clocks);

    // Periodic sound IRQ at MASTER_CLOCK/2/16/16/16/10 = 244.140625 Hz.
    sound_irq_counter_ += cycles;
    constexpr int kSoundIrqCycles = 7331;
    while (sound_irq_counter_ >= kSoundIrqCycles) {
        sound_irq_counter_ -= kSoundIrqCycles;
        sound_cpu_.set_irq(IrqLine::Assert);
    }

    audio_accumulator_ += int64_t(cycles) * YM2151::kSampleRate;
    while (audio_accumulator_ >= kSoundClock) {
        audio_accumulator_ -= kSoundClock;
        const int32_t sample =
            ym_.update() + pokey1_.update() + pokey2_.update() + tms_.last_sample();
        audio_.push_back(int16_t(std::clamp(sample, int32_t(-32768), int32_t(32767))));
    }
}

void AtariSystem2::draw_alpha_tile(int offset) {
    const int x = offset % 64;
    const int y = offset / 64;
    if (y >= 48) return;
    const uint16_t data = alpha_ram_[size_t(offset)];
    const int color = (data >> 13) & 0x07;
    const int base = 64 + (color << 2);
    const uint8_t* pixels = alpha_gfx_.element(data & 0x3ff);
    for (int row = 0; row < 8; row++) {
        const size_t target = size_t((y * 8 + row) * kScreenWidth + x * 8);
        for (int column = 0; column < 8; column++) {
            const uint8_t pen = pixels[row * 8 + column];
            alpha_[target + size_t(column)] =
                pen == 0 ? kTransparent : int16_t(base + pen);
        }
    }
}

void AtariSystem2::draw_playfield_tile(int offset) {
    const int x = offset % 128;
    const int y = offset / 128;
    const uint16_t data = offset < 0x1000 ? playfield_top_[size_t(offset)]
                                          : playfield_bottom_[size_t(offset) & 0xfff];
    const int code = (int(playfield_tile_bank_[(data >> 10) & 1]) << 10) | (data & 0x3ff);
    const int color = (data >> 11) & 7;
    const uint8_t category = uint8_t((~data >> 14) & 3);
    const int base = 128 + (color << 4);
    const uint8_t* pixels = playfield_gfx_.element(code);
    for (int row = 0; row < 8; row++) {
        const size_t target = size_t((y * 8 + row) * kPlayfieldWidth + x * 8);
        for (int column = 0; column < 8; column++) {
            playfield_[target + size_t(column)] = uint16_t(base + pixels[row * 8 + column]);
            playfield_category_[target + size_t(column)] = category;
        }
    }
}

void AtariSystem2::draw_motion_objects() {
    std::fill(mo_pen_.begin(), mo_pen_.end(), kMoTransparent);
    std::fill(mo_priority_.begin(), mo_priority_.end(), 0);
    motion_objects_->draw(0, 0, -1,
                          [this](int code, int color, bool hflip, bool vflip, int x, int y, int gfx,
                                 int priority) {
                              (void)gfx;
                              const uint8_t* pixels = motion_gfx_.element(code);
                              for (int row = 0; row < 16; row++) {
                                  const int ty = y + row;
                                  if (ty < 0 || ty >= kScreenHeight) continue;
                                  const int src_row = vflip ? (15 - row) : row;
                                  for (int column = 0; column < 16; column++) {
                                      const int tx = x + column;
                                      if (tx < 0 || tx >= kScreenWidth) continue;
                                      const int src_col = hflip ? (15 - column) : column;
                                      const uint8_t pen = pixels[src_row * 16 + src_col];
                                      if (pen == 15) continue;
                                      const size_t index = size_t(ty * kScreenWidth + tx);
                                      mo_pen_[index] = uint16_t((color + pen) & 0xff);
                                      mo_priority_[index] = uint8_t(priority);
                                  }
                              }
                          });
}

void AtariSystem2::compose_frame() {
    for (int y = 0; y < kScreenHeight; y++) {
        const int sx = int(xscroll_line_[size_t(y)]) & (kPlayfieldWidth - 1);
        const int sy = int(yscroll_line_[size_t(y)]) & (kPlayfieldHeight - 1);
        const int py = (y + sy) & (kPlayfieldHeight - 1);
        for (int x = 0; x < kScreenWidth; x++) {
            const int px = (x + sx) & (kPlayfieldWidth - 1);
            const size_t pf_index = size_t(py * kPlayfieldWidth + px);
            const size_t screen = size_t(y * kScreenWidth + x);
            uint16_t pen = playfield_[pf_index];
            const uint16_t mo = mo_pen_[screen];
            if (mo != kMoTransparent) {
                const int mopriority = int(mo_priority_[screen]);
                if ((mopriority + int(playfield_category_[pf_index])) & 2) {
                    // High priority playfield: the object only wins when the
                    // playfield pen is below 8.
                    if ((pen & 0x08) == 0) pen = mo;
                } else {
                    pen = mo;
                }
            }
            const int16_t character = alpha_[screen];
            if (character != kTransparent) pen = uint16_t(character);
            framebuffer_[screen] = palette_[size_t(pen) & 0xff];
        }
    }
}

void AtariSystem2::update_video() {
    for (int offset = 0; offset < int(alpha_dirty_.size()); offset++) {
        if (!alpha_dirty_[size_t(offset)]) continue;
        draw_alpha_tile(offset);
        alpha_dirty_[size_t(offset)] = false;
    }
    for (int offset = 0; offset < int(playfield_dirty_.size()); offset++) {
        if (!playfield_dirty_[size_t(offset)]) continue;
        draw_playfield_tile(offset);
        playfield_dirty_[size_t(offset)] = false;
    }
    draw_motion_objects();
    compose_frame();
}

void AtariSystem2::run_frame() {
    const int main_cycles = int(double(kMainClock) / kFramesPerSecond / kScanlines + 0.5);
    const int sound_cycles = int(double(kSoundClock) / kFramesPerSecond / kScanlines + 0.5);

    if (yscroll_reset_) {
        yscroll_ = yscroll_pending_;
        yscroll_reset_ = false;
    }

    for (line_ = 0; line_ < kScanlines; line_++) {
        // The 32V interrupt is clocked every 64 scanlines.
        if ((line_ % 64) == 0) {
            scanline_int_state_ = (interrupt_enable_ & 0x04) != 0;
            update_interrupts();
        }
        main_cpu_.run(main_cycles);
        if (!sound_cpu_in_reset_) sound_cpu_.run(sound_cycles);
        if (line_ < kScreenHeight) {
            xscroll_line_[size_t(line_)] = uint16_t(xscroll_ >> 6);
            yscroll_line_[size_t(line_)] = yscroll_;
        }
        if (line_ == kScreenHeight) {
            update_video();
            video_int_state_ = (interrupt_enable_ & 0x08) != 0;
            update_interrupts();
        }
    }
    line_ = 0;
}

void AtariSystem2::set_inputs(const MachineInputs& inputs) {
    buttons_ = 0;
    if (inputs.player1.button1 || inputs.player1.start) buttons_ |= 0x80;
    if (inputs.player1.button2 || inputs.player2.start) buttons_ |= 0x40;
    coin1_ = inputs.coin1;
    coin2_ = inputs.coin2;
    coin3_ = false;
    service_coin_ = inputs.player2.select;
    self_test_ = inputs.player1.select;

    // Paperboy steers with a pair of 8 bit pots limited to $10-$f0.
    uint8_t x = 0x80;
    uint8_t y = 0x80;
    if (inputs.player1.left && !inputs.player1.right) x = 0x10;
    if (inputs.player1.right && !inputs.player1.left) x = 0xf0;
    if (inputs.player1.up && !inputs.player1.down) y = 0x10;
    if (inputs.player1.down && !inputs.player1.up) y = 0xf0;
    adc_input_[0] = x;
    adc_input_[1] = y;
}

void AtariSystem2::set_dip_switch(int bank, uint8_t value) {
    if (bank < 0 || bank > 1) return;
    dsw_[bank] = value;
}

void AtariSystem2::drain_audio(std::vector<int16_t>& out) {
    out.insert(out.end(), audio_.begin(), audio_.end());
    audio_.clear();
}

}  // namespace dsp
