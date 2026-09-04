#include "drivers/arcade/gauntlet.h"

#include <array>
#include <algorithm>

#include "core/rom_loader.h"

namespace dsp {
namespace {

// Revision 14 (the current MAME parent set) and revision 9, which only differ in
// the program ROMs outside of the SLAPSTIC protected area.
const std::vector<std::vector<RomEntry>> kMainRomSets = {
    {
        {"136037-1307.9a", 0x8000, 0x00000, 0x46fe8743},
        {"136037-1308.9b", 0x8000, 0x00001, 0x276e15c4},
        {"136037-205.10a", 0x4000, 0x38000, 0x6d99ed51},
        {"136037-206.10b", 0x4000, 0x38001, 0x545ead91},
        {"136037-1409.7a", 0x8000, 0x40000, 0x6fb8419c},
        {"136037-1410.7b", 0x8000, 0x40001, 0x931bd2a0},
    },
    {
        {"136041-507.9a", 0x8000, 0x00000, 0x8784133f},
        {"136041-508.9b", 0x8000, 0x00001, 0x2843bde3},
        {"136037-205.10a", 0x4000, 0x38000, 0x6d99ed51},
        {"136037-206.10b", 0x4000, 0x38001, 0x545ead91},
        {"136041-609.7a", 0x8000, 0x40000, 0x5b4ee415},
        {"136041-610.7b", 0x8000, 0x40001, 0x41f5c9e2},
    },
};

const std::vector<RomEntry> kSoundRoms = {
    {"136037-120.16r", 0x4000, 0x4000, 0x6ee7f3cc},
    {"136037-119.16s", 0x8000, 0x8000, 0xfa19861f},
};

// The character ROM is 8K on the later revisions and 16K on the earlier ones.
// SLAPSTIC number used by each main ROM set: the 4 player parent uses a 104 and
// the 2 player version (136041-xxx) a 107.
const std::array<int, 2> kSlapsticTypes = {104, 107};

const std::vector<std::vector<RomEntry>> kCharRomSets = {
    {{"136037-104.6p", 0x2000, 0x0000, 0x9e2a5b59}},
    {{"136037-104.6p", 0x4000, 0x0000, 0x6c276a1d}},
};

const std::vector<RomEntry> kTileRoms = {
    {"136037-111.1a", 0x8000, 0x00000, 0x91700f33},
    {"136037-112.1b", 0x8000, 0x08000, 0x869330be},
    {"136037-113.1l", 0x8000, 0x10000, 0xd497d0a8},
    {"136037-114.1mn", 0x8000, 0x18000, 0x29ef9882},
    {"136037-115.2a", 0x8000, 0x20000, 0x9510b898},
    {"136037-116.2b", 0x8000, 0x28000, 0x11e0ac5b},
    {"136037-117.2l", 0x8000, 0x30000, 0x29a5db41},
    {"136037-118.2mn", 0x8000, 0x38000, 0x8bf3b263},
};

GfxLayout char_layout(size_t rom_size) {
    GfxLayout layout;
    layout.width = 8;
    layout.height = 8;
    layout.total = int(rom_size / 16);
    layout.planes = 2;
    layout.char_increment = 16 * 8;
    layout.plane_offsets = {0, 4};
    layout.x_offsets = {0, 1, 2, 3, 8, 9, 10, 11};
    layout.y_offsets = {0 * 16, 1 * 16, 2 * 16, 3 * 16, 4 * 16, 5 * 16, 6 * 16, 7 * 16};
    return layout;
}

GfxLayout tile_layout() {
    constexpr int kPlane = 0x2000 * 8 * 8;
    GfxLayout layout;
    layout.width = 8;
    layout.height = 8;
    layout.total = 0x2000;
    layout.planes = 4;
    layout.char_increment = 8 * 8;
    layout.plane_offsets = {kPlane * 3, kPlane * 2, kPlane * 1, kPlane * 0};
    layout.x_offsets = {0, 1, 2, 3, 4, 5, 6, 7};
    layout.y_offsets = {0 * 8, 1 * 8, 2 * 8, 3 * 8, 4 * 8, 5 * 8, 6 * 8, 7 * 8};
    return layout;
}

AtariMotionObjects::Config motion_object_config() {
    AtariMotionObjects::Config config;
    config.tile_width = 8;
    config.tile_height = 8;
    config.bankcount = 1;
    config.linked = true;
    config.split = true;
    config.slipheight = 8;
    config.maxperline = 0;
    config.palettebase = 0x100;
    config.link_entry = {0, 0, 0, 0x03ff};
    config.code_entry = {{0x7fff, 0, 0, 0}, {0, 0, 0, 0}};
    config.color_entry = {{0, 0x000f, 0, 0}, {0, 0, 0, 0}};
    config.xpos_entry = {0, 0xff80, 0, 0};
    config.ypos_entry = {0, 0, 0xff80, 0};
    config.width_entry = {0, 0, 0x0038, 0};
    config.height_entry = {0, 0, 0x0007, 0};
    config.hflip_entry = {0, 0, 0x0040, 0};
    return config;
}

// The palette entries hold four bit components plus a shared intensity nibble.
uint8_t pal4bit_intensity(uint16_t bits, uint16_t intensity) {
    static const uint8_t kTable[16] = {0x0, 0x3, 0x4, 0x5, 0x6, 0x7, 0x8, 0x9,
                                       0xa, 0xb, 0xc, 0xd, 0xe, 0xf, 0x10, 0x11};
    return uint8_t((bits & 0x0f) * kTable[intensity & 0x0f]);
}

}  // namespace

Gauntlet::Gauntlet()
    : main_cpu_(kMainClock, M68000::Type::M68010),
      sound_cpu_(kSoundClock),
      ym_(kYmClock),
      pokey_(kPokeyClock),
      tms_(14318180 / 2 / 11),  // ~650 kHz nominal
      slapstic_(107, &main_cpu_),
      rom_(0x40000, 0) {
    char_back_.assign(size_t(kCharPlaneWidth) * kCharPlaneHeight, kTransparent);
    char_front_.assign(size_t(kCharPlaneWidth) * kCharPlaneHeight, kTransparent);
    tile_back_.assign(size_t(kTilePlaneWidth) * kTilePlaneHeight, 0);
    tile_front_.assign(size_t(kTilePlaneWidth) * kTilePlaneHeight, kTransparent);
    composite_.assign(size_t(kTilePlaneWidth) * kTilePlaneHeight, 0xff000000u);
    framebuffer_.assign(size_t(kScreenWidth) * kScreenHeight, 0xff000000u);

    main_cpu_.set_memory_handlers([this](uint32_t address) { return main_read(address); },
                                  [this](uint32_t address, uint16_t value) {
                                      main_write(address, value);
                                  });
    sound_cpu_.set_memory_handlers(
        [this](uint16_t address) { return sound_read(address); },
        [this](uint16_t address, uint8_t value) { sound_write(address, value); });
    sound_cpu_.set_cycle_handler([this](int cycles) { on_sound_cycles(cycles); });

    motion_objects_ = std::make_unique<AtariMotionObjects>(
        motion_object_config(), &ram2_[0x5f80 >> 1], &ram2_[0x2000 >> 1], kScreenWidth + 8,
        kScreenHeight + 8);
    // The motion object codes are stored inverted with respect to the tile ROMs.
    std::vector<uint16_t>& codes = motion_objects_->code_lookup();
    for (size_t index = 0; index < codes.size(); index++) codes[index] ^= 0x800;
}

bool Gauntlet::init(const std::string& rom_path, std::string* error) {
    if (!load_roms(rom_path, error)) return false;
    eeprom_.fill(0xff);
    reset();
    return true;
}

bool Gauntlet::load_roms(const std::string& rom_path, std::string* error) {
    RomLoader loader;
    if (!loader.open(rom_path, error)) return false;

    // Main ROMs are 16 bit: even offsets hold the high byte of each word.
    std::vector<uint8_t> temp(0x80000, 0);
    bool loaded = false;
    for (size_t set_index = 0; set_index < kMainRomSets.size(); set_index++) {
        const std::vector<RomEntry>& set = kMainRomSets[set_index];
        std::string attempt;
        std::vector<std::vector<uint8_t>> data;
        bool complete = true;
        for (const RomEntry& entry : set) {
            data.emplace_back(entry.length, 0);
            RomEntry single{entry.name, entry.length, 0, entry.crc};
            if (!loader.load({single}, data.back(), &attempt)) {
                complete = false;
                break;
            }
        }
        if (!complete) {
            if (error) *error = attempt;
            continue;
        }
        for (size_t index = 0; index < set.size(); index++) {
            const RomEntry& entry = set[index];
            for (uint32_t byte = 0; byte < entry.length; byte++) {
                temp[(entry.offset & ~1u) + byte * 2 + (entry.offset & 1u)] = data[index][byte];
            }
        }
        slapstic_.set_type(kSlapsticTypes[set_index]);
        loaded = true;
        break;
    }
    if (!loaded) return false;
    // The two 32K halves of every 64K block are swapped on the board.
    auto pack = [&](uint32_t destination, uint32_t source) {
        for (uint32_t index = 0; index < 0x8000; index += 2) {
            rom_[(destination + index) >> 1] =
                uint16_t((temp[source + index] << 8) | temp[source + index + 1]);
        }
    };
    for (uint32_t base : {0x00000u, 0x40000u, 0x50000u, 0x60000u, 0x70000u}) {
        pack(base, base + 0x8000);
        pack(base + 0x8000, base);
    }
    for (int bank = 0; bank < 4; bank++) {
        for (uint32_t index = 0; index < 0x2000; index += 2) {
            uint32_t source = 0x38000 + uint32_t(bank) * 0x2000 + index;
            slapstic_rom_[size_t(bank)][index >> 1] =
                uint16_t((temp[source] << 8) | temp[source + 1]);
        }
    }

    std::vector<uint8_t> sound_rom(0x10000, 0);
    if (!loader.load(kSoundRoms, sound_rom, error)) return false;
    std::copy(sound_rom.begin(), sound_rom.end(), sound_memory_.begin());

    std::vector<uint8_t> char_rom;
    loaded = false;
    for (const std::vector<RomEntry>& set : kCharRomSets) {
        char_rom.assign(set[0].length, 0);
        if (loader.load(set, char_rom, error)) {
            loaded = true;
            break;
        }
    }
    if (!loaded) return false;

    std::vector<uint8_t> tile_rom(0x40000, 0);
    if (!loader.load(kTileRoms, tile_rom, error)) return false;

    decode_graphics(char_rom, tile_rom);
    warnings_ = loader.warnings();
    return true;
}

void Gauntlet::decode_graphics(const std::vector<uint8_t>& char_rom,
                               std::vector<uint8_t>& tile_rom) {
    chars_.decode(char_layout(char_rom.size()), char_rom);
    for (uint8_t& byte : tile_rom) byte = uint8_t(~byte);
    tiles_.decode(tile_layout(), tile_rom);
}

void Gauntlet::reset() {
    slapstic_.reset();
    rom_bank_ = slapstic_.current_bank();
    main_cpu_.reset();
    sound_cpu_.reset();
    ym_.reset();
    pokey_.reset();
    tms_.reset();
    soundctl_ = 0xff;
    in0_ = 0xffff;
    in1_ = 0xffff;
    in2_ = 0xff;
    scroll_x_ = 0;
    main_to_sound_ready_ = false;
    sound_to_main_ready_ = false;
    sound_to_main_data_ = 0;
    main_to_sound_data_ = 0;
    sound_reset_value_ = 1;
    sound_cpu_halted_ = false;
    vblank_ = 0x40;
    write_eeprom_ = false;
    tile_bank_ = 0;
    char_dirty_.fill(true);
    tile_dirty_.fill(true);
    audio_accumulator_ = 0;
    audio_.clear();
}

uint16_t Gauntlet::main_read(uint32_t address) {
    if (address < 0x38000 || (address >= 0x40000 && address < 0x80000)) {
        return rom_[address >> 1];
    }
    if (address < 0x40000) {  // SLAPSTIC protected bank
        uint16_t value = slapstic_rom_[rom_bank_][(address & 0x1fff) >> 1];
        rom_bank_ = slapstic_.tweak(uint16_t((address & 0x7fff) >> 1));
        return value;
    }
    if (address >= 0x800000 && address <= 0x801fff) return ram_[(address & 0x1fff) >> 1];
    if (address >= 0x802000 && address <= 0x802fff) {
        return uint16_t(0xff00 | eeprom_[(address & 0xfff) >> 1]);
    }
    if (address >= 0x900000 && address <= 0x905fff) return ram2_[(address & 0x7fff) >> 1];
    if (address >= 0x910000 && address <= 0x9107ff) return palette_ram_[(address & 0x7ff) >> 1];
    switch (address) {
        case 0x803000: return in0_;
        case 0x803002: return in1_;
        case 0x803008:
            return uint16_t(0xff87 | dsw_a_ | vblank_ | (uint16_t(sound_to_main_ready_) << 4) |
                            (uint16_t(main_to_sound_ready_) << 5));
        case 0x80300e:
            sound_to_main_ready_ = false;
            main_cpu_.set_irq(6, IrqLine::Clear);
            return uint16_t(0xff00 | sound_to_main_data_);
        default: break;
    }
    return 0xffff;
}

void Gauntlet::main_write(uint32_t address, uint16_t value) {
    if (address < 0x38000 || (address >= 0x40000 && address < 0x80000)) return;  // ROM
    if (address < 0x40000) {
        rom_bank_ = slapstic_.tweak(uint16_t((address & 0x7fff) >> 1));
        return;
    }
    if (address >= 0x800000 && address <= 0x801fff) {
        ram_[(address & 0x1fff) >> 1] = value;
        return;
    }
    if (address >= 0x802000 && address <= 0x802fff) {
        if (write_eeprom_) {
            eeprom_[(address & 0xfff) >> 1] = uint8_t(value);
            write_eeprom_ = false;
        }
        return;
    }
    if ((address >= 0x900000 && address <= 0x905fff) ||
        (address >= 0xb00000 && address <= 0xb05fff)) {
        uint32_t offset = (address & 0x7fff) >> 1;
        ram2_[offset] = value;
        if (offset < 0x1000) tile_dirty_[offset] = true;
        if (offset >= 0x2800) char_dirty_[offset & 0x7ff] = true;
        return;
    }
    if (address >= 0x910000 && address <= 0x9107ff) {
        int index = int((address & 0x7ff) >> 1);
        if (palette_ram_[size_t(index)] != value) set_palette(index, value);
        return;
    }
    switch (address) {
        case 0x803100: break;  // watchdog
        case 0x803140: main_cpu_.set_irq(4, IrqLine::Clear); break;
        case 0x803150: write_eeprom_ = true; break;
        case 0x803170:
            main_to_sound_data_ = uint8_t(value);
            main_to_sound_ready_ = true;
            sound_cpu_.set_nmi(IrqLine::Assert);
            break;
        case 0x930000:
        case 0xb30000: scroll_x_ = value; break;
        default:
            if (address >= 0x803120 && address <= 0x80312e) set_sound_reset(value);
            break;
    }
}

void Gauntlet::set_sound_reset(uint16_t value) {
    uint16_t old = sound_reset_value_;
    sound_reset_value_ = value;
    if (((old ^ sound_reset_value_) & 1) == 0) return;
    if ((sound_reset_value_ & 1) != 0) {
        sound_cpu_halted_ = false;
        sound_cpu_.reset();
    } else {
        sound_cpu_halted_ = true;
    }
    sound_to_main_ready_ = false;
    main_cpu_.set_irq(6, IrqLine::Clear);
    if ((sound_reset_value_ & 1) != 0) ym_.reset();
}

void Gauntlet::set_palette(int index, uint16_t value) {
    palette_ram_[size_t(index)] = value;
    uint16_t intensity = uint16_t(value >> 12);
    uint8_t red = pal4bit_intensity(uint16_t(value >> 8), intensity);
    uint8_t green = pal4bit_intensity(uint16_t(value >> 4), intensity);
    uint8_t blue = pal4bit_intensity(value, intensity);
    palette_[size_t(index)] =
        0xff000000u | (uint32_t(red) << 16) | (uint32_t(green) << 8) | uint32_t(blue);
}

uint8_t Gauntlet::sound_read(uint16_t address) {
    if (address <= 0x0fff || address >= 0x4000) return sound_memory_[address];
    if (address >= 0x1010 && address <= 0x101f) {
        main_to_sound_ready_ = false;
        sound_cpu_.set_nmi(IrqLine::Clear);
        return main_to_sound_data_;
    }
    if (address >= 0x1020 && address <= 0x102f) return in2_;
    if (address >= 0x1030 && address <= 0x103f) {
        // switch_6502_r (MAME): TMS readyq on bit5
        uint8_t value = 0x30;
        if (main_to_sound_ready_) value ^= 0x80;
        if (sound_to_main_ready_) value ^= 0x40;
        // MAME switch_6502_r: if (!m_tms5220->readyq_r()) temp ^= 0x20;
        // readyq_r() true = busy (/READY high). XOR when READY (not busy).
        if (!tms_.readyq()) value ^= 0x20;
        if (dsw_a_ == 8) value ^= 0x10;
        return value;
    }
    if (address >= 0x1800 && address <= 0x180f) return pokey_.read(uint16_t(address & 0x0f));
    if (address == 0x1811) return ym_.status();
    if (address >= 0x1830 && address <= 0x183f) {
        sound_cpu_.set_irq(IrqLine::Clear);
        return 0;
    }
    return 0xff;
}

void Gauntlet::sound_write(uint16_t address, uint8_t value) {
    if (address <= 0x0fff) {
        sound_memory_[address] = value;
        return;
    }
    if (address >= 0x1000 && address <= 0x100f) {
        sound_to_main_data_ = value;
        sound_to_main_ready_ = true;
        main_cpu_.set_irq(6, IrqLine::Assert);
        return;
    }
    if (address >= 0x1020 && address <= 0x102f) {
        // mixer_w: bits 0-2 YM, 3-4 Pokey, 5-7 TMS volume
        ym_gain_ = float(value & 7) / 7.0f;
        pokey_gain_ = float((value >> 3) & 3) / 3.0f;
        tms_.set_volume(float((value >> 5) & 7) / 7.0f);
        return;
    }
    if (address >= 0x1030 && address <= 0x103f) {
        // LS259 soundctl: D7 of each offset 0..7
        const int bit = address & 7;
        const bool level = (value & 0x80) != 0;
        if (level) soundctl_ = uint8_t(soundctl_ | (1 << bit));
        else soundctl_ = uint8_t(soundctl_ & ~(1 << bit));
        // Q0: YM reset (active low)
        if (bit == 0 && !level) ym_.reset();
        // Q1: TMS WSQ (active low write)
        if (bit == 1) tms_.set_wsq(level);
        // Q2: TMS /RS (active low). Combined with /WS, both low resets a TMS5220C.
        if (bit == 2) tms_.set_rsq(level);
        // Q3: speech squeak — 650 kHz (low) or ~795 kHz (high)
        if (bit == 3) tms_.set_clock(kAtariClock / 2 / uint32_t(level ? 9 : 11));
        return;
    }
    if (address == 0x1810) {
        ym_.select_register(value);
        return;
    }
    if (address == 0x1811) {
        ym_.write(value);
        return;
    }
    if (address >= 0x1800 && address <= 0x180f) {
        pokey_.write(uint16_t(address & 0x0f), value);
        return;
    }
    if (address >= 0x1820 && address <= 0x182f) {
        // MAME maps this to tms5220 data_w: latch + accept when not in pure pin-timing mode.
        // Also keep latch for /WS-strobe path used by soundctl Q1.
        tms_.set_data_latch(value);
        tms_.write_data(value);
        return;
    }
    if (address >= 0x1830 && address <= 0x183f) sound_cpu_.set_irq(IrqLine::Clear);
}

void Gauntlet::on_sound_cycles(int cycles) {
    ym_.run_timers(cycles * 2);  // the YM2151 runs at twice the 6502 clock
    pokey_.run(cycles);
    const int tms_clocks =
        int((int64_t(cycles) * int64_t(tms_.clock()) + (kSoundClock / 2)) / kSoundClock);
    tms_.tick(tms_clocks);
    audio_accumulator_ += int64_t(cycles) * YM2151::kSampleRate;
    while (audio_accumulator_ >= kSoundClock) {
        audio_accumulator_ -= kSoundClock;
        const int32_t sample = int32_t(float(ym_.update()) * ym_gain_) +
                               int32_t(float(pokey_.update()) * pokey_gain_) +
                               int32_t(tms_.last_sample());
        audio_.push_back(int16_t(std::clamp(sample, int32_t(-32768), int32_t(32767))));
    }
}

void Gauntlet::draw_char(int offset) {
    int x = offset % 64;
    int y = offset / 64;
    uint16_t attributes = ram2_[size_t(0x2800 + offset)];
    int color = ((attributes >> 10) & 0x0f) | ((attributes >> 9) & 0x20);
    int base = color << 2;
    bool above_objects = (attributes & 0x8000) != 0;
    const uint8_t* pixels = chars_.element(attributes & 0x3ff);

    for (int row = 0; row < 8; row++) {
        size_t target = size_t((y * 8 + row) * kCharPlaneWidth + x * 8);
        for (int column = 0; column < 8; column++) {
            uint8_t pen = pixels[row * 8 + column];
            if (above_objects) {
                char_front_[target + size_t(column)] = int16_t(base + pen);
                char_back_[target + size_t(column)] = kTransparent;
            } else {
                char_back_[target + size_t(column)] =
                    pen == 0 ? kTransparent : int16_t(base + pen);
                char_front_[target + size_t(column)] = kTransparent;
            }
        }
    }
}

void Gauntlet::draw_tile(int offset) {
    int x = offset / 64;
    int y = offset % 64;
    uint16_t attributes = ram2_[size_t(offset)];
    int color = (attributes >> 12) & 7;
    int base = ((color + 0x18) << 4) + 0x100;
    int code = ((tile_bank_ * 0x1000) + (attributes & 0xfff)) ^ 0x800;
    bool above_objects = (attributes & 0x8000) != 0;
    const uint8_t* pixels = tiles_.element(code);

    for (int row = 0; row < 8; row++) {
        size_t target = size_t((y * 8 + row) * kTilePlaneWidth + x * 8);
        for (int column = 0; column < 8; column++) {
            uint8_t pen = pixels[row * 8 + column];
            if (above_objects) {
                tile_front_[target + size_t(column)] =
                    pen == 0 ? kTransparent : int16_t(base + pen);
                tile_back_[target + size_t(column)] = 0;
            } else {
                tile_back_[target + size_t(column)] = int16_t(base + pen);
                tile_front_[target + size_t(column)] = kTransparent;
            }
        }
    }
}

void Gauntlet::update_video() {
    uint16_t control = ram2_[0x5f6f >> 1];
    int scroll_y = (control >> 7) & 0x1ff;
    uint8_t bank = uint8_t(control & 3);
    if (bank != tile_bank_) {
        tile_bank_ = bank;
        tile_dirty_.fill(true);
    }

    for (int offset = 0; offset < 0x800; offset++) {
        if (!char_dirty_[size_t(offset)]) continue;
        draw_char(offset);
        char_dirty_[size_t(offset)] = false;
    }
    for (int offset = 0; offset < 0x1000; offset++) {
        if (!tile_dirty_[size_t(offset)]) continue;
        draw_tile(offset);
        tile_dirty_[size_t(offset)] = false;
    }

    // Playfield behind the motion objects, then the low priority characters.
    for (int y = 0; y < kScreenHeight; y++) {
        int source_y = (y + scroll_y) & (kTilePlaneHeight - 1);
        for (int x = 0; x < kScreenWidth; x++) {
            int source_x = (x + scroll_x_) & (kTilePlaneWidth - 1);
            int16_t index = tile_back_[size_t(source_y * kTilePlaneWidth + source_x)];
            int16_t character = char_back_[size_t(y * kCharPlaneWidth + x)];
            if (character != kTransparent) index = character;
            composite_[size_t(y * kTilePlaneWidth + x)] = palette_[size_t(index)];
        }
    }

    motion_objects_->draw(scroll_x_, uint16_t(scroll_y), -1,
                          [this](int code, int color, bool hflip, bool vflip, int x, int y, int,
                                 int) {
                              const uint8_t* pixels = tiles_.element(code);
                              for (int row = 0; row < 8; row++) {
                                  int target_y = y + row;
                                  if (target_y < 0 || target_y >= kTilePlaneHeight) continue;
                                  int source_row = vflip ? (7 - row) : row;
                                  for (int column = 0; column < 8; column++) {
                                      int target_x = x + column;
                                      if (target_x < 0 || target_x >= kTilePlaneWidth) continue;
                                      int source_column = hflip ? (7 - column) : column;
                                      uint8_t pen = pixels[source_row * 8 + source_column];
                                      if (pen == 0) continue;
                                      composite_[size_t(target_y * kTilePlaneWidth + target_x)] =
                                          palette_[size_t((color + pen) & 0x3ff)];
                                  }
                              }
                          });

    // Playfield and characters drawn on top of the motion objects.
    for (int y = 0; y < kScreenHeight; y++) {
        int source_y = (y + scroll_y) & (kTilePlaneHeight - 1);
        for (int x = 0; x < kScreenWidth; x++) {
            int source_x = (x + scroll_x_) & (kTilePlaneWidth - 1);
            int16_t index = tile_front_[size_t(source_y * kTilePlaneWidth + source_x)];
            int16_t character = char_front_[size_t(y * kCharPlaneWidth + x)];
            if (character != kTransparent) index = character;
            if (index != kTransparent) {
                composite_[size_t(y * kTilePlaneWidth + x)] = palette_[size_t(index)];
            }
				framebuffer_[size_t(y * kScreenWidth + x)] =
				composite_[size_t(y * kTilePlaneWidth + x)];
        }
    }
}

void Gauntlet::run_frame() {
    const int main_cycles =
        int(double(kMainClock) / kFramesPerSecond / (kScanlines * kCpuSync) + 0.5);
    const int sound_cycles =
        int(double(kSoundClock) / kFramesPerSecond / (kScanlines * kCpuSync) + 0.5);

    for (int line = 0; line < kScanlines; line++) {
        switch (line) {
            case 0:
                vblank_ = 0x40;
                sound_cpu_.set_irq(IrqLine::Clear);
                break;
            case 64:
            case 128:
            case 192:
            case 256: sound_cpu_.set_irq(IrqLine::Clear); break;
            case 32:
            case 96:
            case 160:
            case 224: sound_cpu_.set_irq(IrqLine::Assert); break;
            case 240:
                update_video();
                vblank_ = 0;
                main_cpu_.set_irq(4, IrqLine::Assert);
                break;
            default: break;
        }
        for (int step = 0; step < kCpuSync; step++) {
            main_cpu_.run(main_cycles);
            if (!sound_cpu_halted_) sound_cpu_.run(sound_cycles);
        }
    }
}

void Gauntlet::set_inputs(const MachineInputs& inputs) {
    const InputState& player1 = inputs.player1;
    const InputState& player2 = inputs.player2;
    in0_ = 0xffff;
    in1_ = 0xffff;
    in2_ = 0xff;

    if (player1.button2) in0_ &= 0xfffe;
    if (player1.button1) in0_ &= 0xfffd;
    if (player1.right) in0_ &= 0xffef;
    if (player1.left) in0_ &= 0xffdf;
    if (player1.down) in0_ &= 0xffbf;
    if (player1.up) in0_ &= 0xff7f;

    if (player2.button2) in1_ &= 0xfffe;
    if (player2.button1) in1_ &= 0xfffd;
    if (player2.right) in1_ &= 0xffef;
    if (player2.left) in1_ &= 0xffdf;
    if (player2.down) in1_ &= 0xffbf;
    if (player2.up) in1_ &= 0xff7f;

    if (inputs.coin2) in2_ &= 0xfb;
    if (inputs.coin1) in2_ &= 0xf7;
}

void Gauntlet::set_dip_switch(int bank, uint8_t value) {
    if (bank == 0) dsw_a_ = uint8_t(value & 0x08);
}

void Gauntlet::drain_audio(std::vector<int16_t>& out) {
    out.insert(out.end(), audio_.begin(), audio_.end());
    audio_.clear();
}

}  //  namespace dsp