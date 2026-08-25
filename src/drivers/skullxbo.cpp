#include "drivers/skullxbo.h"

#include <algorithm>
#include <cstring>

#include "core/rom_loader.h"

namespace dsp {
namespace {

uint32_t irgb1555(uint16_t value) {
    uint32_t r = uint32_t((value >> 10) & 0x1f) * 255 / 31;
    uint32_t g = uint32_t((value >> 5) & 0x1f) * 255 / 31;
    uint32_t b = uint32_t(value & 0x1f) * 255 / 31;
    if ((value & 0x8000) == 0) {
        r = r * 2 / 3;
        g = g * 2 / 3;
        b = b * 2 / 3;
    }
    return 0xff000000u | (r << 16) | (g << 8) | b;
}

bool load_interleaved(RomLoader& loader, const std::vector<RomEntry>& entries,
                      std::vector<uint8_t>& dest, std::string* error) {
    for (const RomEntry& entry : entries) {
        std::vector<uint8_t> data(entry.length, 0);
        RomEntry single{entry.name, entry.length, 0, entry.crc};
        if (!loader.load({single}, data, error)) return false;
        for (uint32_t i = 0; i < entry.length; i++) {
            const uint32_t dest_off = (entry.offset & ~1u) + i * 2 + (entry.offset & 1u);
            if (dest_off < dest.size()) dest[dest_off] = data[i];
        }
    }
    return true;
}

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

const std::vector<RomEntry> kSoundCpu = {{"136072-1149.1b", 0x10000, 0, 0x8d730e7a}};

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

const std::vector<RomEntry> kCharRoms = {{"136072-2141.250k", 0x8000, 0, 0x60d6d6df}};

const std::vector<RomEntry> kOkiRoms = {
    {"136072-1145.7k", 0x10000, 0x00000, 0xd9475d58},
    {"136072-1146.7j", 0x10000, 0x10000, 0x133e6aef},
    {"136072-1147.7e", 0x10000, 0x20000, 0xba4d556e},
    {"136072-1148.7d", 0x10000, 0x30000, 0xc48df49a},
};

AtariMotionObjects::Config motion_object_config() {
    AtariMotionObjects::Config config;
    config.tile_width = 16;
    config.tile_height = 8;
    config.bankcount = 2;
    config.linked = true;
    config.split = false;
    config.slipheight = 8;
    config.palettebase = 0;
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

}  // namespace

SkullXbo::SkullXbo()
    : main_cpu_(kMainClock),
      sound_cpu_(kSoundClock),
      ym_(kJsaClock),
      oki_(kOkiClock, /*pin7_high=*/true) {
    framebuffer_.assign(size_t(kScreenWidth) * size_t(kScreenHeight), 0xff000000u);
    pf_index_.assign(size_t(kScreenWidth) * size_t(kScreenHeight), 0);
    mo_index_.assign(size_t(kScreenWidth) * size_t(kScreenHeight), 0xffff);

    main_cpu_.set_memory_handlers([this](uint32_t a) { return main_read(a); },
                                  [this](uint32_t a, uint16_t v) { main_write(a, v); });
    sound_cpu_.set_memory_handlers([this](uint16_t a) { return sound_read(a); },
                                   [this](uint16_t a, uint8_t v) { sound_write(a, v); });
    sound_cpu_.set_cycle_handler([this](int cycles) { on_sound_cycles(cycles); });
    ym_.set_irq_handler([this](bool asserted) {
        ym_int_ = asserted;
        update_sound_irq();
    });
    ym_.set_port_handler([this](uint8_t data) {
        ym_ct_ = data;
        update_volumes();
    });

    motion_objects_ = std::make_unique<AtariMotionObjects>(
        motion_object_config(), slip_ram_.data(), sprite_ram_.data(), kScreenWidth * 2 + 16,
        kScreenHeight + 8);

    main_cycles_per_line_ = int(kMainClock / (kScanlines * kFramesPerSecond));
    sound_cycles_per_line_ = int(kSoundClock / (kScanlines * kFramesPerSecond));
    sound_irq_lines_ = std::max(1, kScanlines / 4);
}

bool SkullXbo::init(const std::string& rom_path, std::string* error) {
    if (!load_roms(rom_path, error)) return false;
    decode_graphics();
    eeprom_.fill(0xff);
    reset();
    return true;
}

bool SkullXbo::load_roms(const std::string& rom_path, std::string* error) {
    RomLoader loader;
    if (!loader.open(rom_path, error)) return false;

    std::vector<uint8_t> main_bytes(0x80000, 0);
    if (!load_interleaved(loader, kMainRoms, main_bytes, error)) return false;
    for (size_t i = 0; i < rom_.size(); i++) {
        rom_[i] = uint16_t((uint16_t(main_bytes[i * 2]) << 8) | main_bytes[i * 2 + 1]);
    }

    std::vector<uint8_t> sound(0x10000, 0);
    if (!loader.load(kSoundCpu, sound, error)) return false;
    std::copy(sound.begin(), sound.end(), sound_rom_.begin());

    sprite_rom_.assign(0x190000, 0);
    if (!loader.load(kSpriteRoms, sprite_rom_, error)) return false;

    playfield_rom_.assign(0xa0000, 0);
    if (!loader.load(kPlayfieldRoms, playfield_rom_, error)) return false;
    for (uint8_t& b : playfield_rom_) b = uint8_t(~b);

    char_rom_.assign(0x8000, 0);
    if (!loader.load(kCharRoms, char_rom_, error)) return false;

    oki_rom_.assign(0x40000, 0);
    if (!loader.load(kOkiRoms, oki_rom_, error)) return false;
    oki_.set_rom(oki_rom_);

    warnings_ = loader.warnings();
    return true;
}

void SkullXbo::decode_graphics() {
    {
        GfxLayout layout;
        layout.width = 8;
        layout.height = 8;
        layout.total = 0x8000;
        layout.planes = 4;
        layout.char_increment = 16 * 8;
        const int frac = 0x50000 * 8;
        layout.plane_offsets = {0, 1, 2, 3};
        layout.x_offsets = {frac + 0, frac + 4, 0, 4, frac + 8, frac + 12, 8, 12};
        layout.y_offsets = {0, 16, 32, 48, 64, 80, 96, 112};
        playfield_gfx_.decode(layout, playfield_rom_);
    }
    {
        GfxLayout layout;
        layout.width = 8;
        layout.height = 8;
        layout.total = 0x800;
        layout.planes = 2;
        layout.char_increment = 8 * 16;
        layout.plane_offsets = {0, 1};
        layout.x_offsets = {0, 2, 4, 6, 8, 10, 12, 14};
        layout.y_offsets = {0, 16, 32, 48, 64, 80, 96, 112};
        alpha_gfx_.decode(layout, char_rom_);
    }
    {
        GfxLayout layout;
        layout.width = 8;
        layout.height = 8;
        layout.total = 0x5000;
        layout.planes = 5;
        layout.char_increment = 16 * 8;
        const int p = 0x50000 * 8;
        layout.plane_offsets = {p * 4, p * 3, p * 2, p, 0};
        layout.x_offsets = {0, 2, 4, 6, 8, 10, 12, 14};
        layout.y_offsets = {0, 16, 32, 48, 64, 80, 96, 112};
        sprite_gfx_.decode(layout, sprite_rom_);
    }
}

void SkullXbo::reset() {
    main_cpu_.reset();
    sound_cpu_.reset();
    ym_.reset();
    oki_.reset();
    oki_.set_rom(oki_rom_);
    in0_ = 0xffff;
    in1_ = 0xffff;
    coin_bits_ = 0;
    service_ = false;
    xscroll_ = 0;
    yscroll_ = 0;
    playfield_latch_ = -1;
    mob_bank_ = 0;
    motion_objects_->set_bank(0);
    eeprom_unlocked_ = false;
    main_to_sound_ready_ = false;
    sound_to_main_ready_ = false;
    timed_int_ = false;
    ym_int_ = false;
    sound_bank_ = 0;
    wrio_ = 0xff;
    mix_ = 0x0e;
    ym_ct_ = 0;
    update_volumes();
    irq1_line_ = -1;
    vblank_ = false;
    audio_accumulator_ = 0;
    oki_accumulator_ = 0;
    last_oki_ = 0;
    audio_.clear();
    sound_cpu_.set_irq(IrqLine::Clear);
    sound_cpu_.set_nmi(IrqLine::Clear);
}

void SkullXbo::update_sound_irq() {
    sound_cpu_.set_irq((timed_int_ || ym_int_) ? IrqLine::Assert : IrqLine::Clear);
}

void SkullXbo::update_volumes() {
    ym_gain_ = float((mix_ >> 1) & 7) / 7.0f;
    oki_gain_ = ((mix_ & 1) ? 1.0f : 0.5f) * float(ym_ct_ & 1);
}

size_t SkullXbo::alpha_index(uint32_t address) const {
    return size_t((address - 0xffc000) >> 1);
}

void SkullXbo::set_palette(int index, uint16_t value) {
    if (index < 0 || size_t(index) >= palette_ram_.size()) return;
    palette_ram_[size_t(index)] = value;
    palette_[size_t(index)] = irgb1555(value);
}

uint32_t SkullXbo::pal_color(int index) const {
    if (index < 0 || size_t(index) >= palette_.size()) return 0xff000000u;
    return palette_[size_t(index)];
}

uint16_t SkullXbo::main_read(uint32_t address) {
    address &= 0xffffff;
    if (address < 0x080000) return rom_[address >> 1];
    if (address >= 0xff5000 && address <= 0xff5001) {
        sound_to_main_ready_ = false;
        main_cpu_.set_irq(4, IrqLine::Clear);
        return uint16_t(0xff00 | sound_to_main_data_);
    }
    if (address >= 0xff5800 && address <= 0xff5801) return in0_;
    if (address >= 0xff5802 && address <= 0xff5803) {
        uint16_t value = in1_;
        value &= ~0x0010;  // HBLANK: CPU runs during active display
        if (vblank_) value |= 0x0020;
        else value &= ~0x0020;
        if (main_to_sound_ready_) value &= ~0x0040;
        else value |= 0x0040;
        if (service_) value &= ~0x0080;
        else value |= 0x0080;
        return value;
    }
    if (address >= 0xff6000 && address <= 0xff6fff) {
        const size_t index = size_t((address - 0xff6000) >> 1);
        if (index < eeprom_.size()) return uint16_t(0xff00 | eeprom_[index]);
    }
    if (address >= 0xff2000 && address <= 0xff2fff) return palette_ram_[(address >> 1) & 0x7ff];
    if (address >= 0xff8000 && address <= 0xff9fff) return playfield_[(address >> 1) & 0x7ff];
    if (address >= 0xffa000 && address <= 0xffbfff) return playfield_ext_[(address >> 1) & 0x7ff];
    if (address >= 0xffc000 && address <= 0xffcf7f) {
        const size_t index = alpha_index(address);
        if (index < alpha_.size()) return alpha_[index];
    }
    if (address >= 0xffcf80 && address <= 0xffcfff) return slip_ram_[(address >> 1) & 0x3f];
    if (address >= 0xffd000 && address <= 0xffdfff) return sprite_ram_[(address >> 1) & 0x7ff];
    if (address >= 0xffe000) return work_ram_[(address >> 1) & 0xfff];
    return 0xffff;
}

void SkullXbo::main_write(uint32_t address, uint16_t value) {
    address &= 0xffffff;
    if (address < 0x080000) return;
    if (address >= 0xff0000 && address <= 0xff07ff) {
        mob_bank_ = (address >> 9) & 1;
        motion_objects_->set_bank(uint32_t(mob_bank_));
        return;
    }
    if (address >= 0xff0800 && address <= 0xff0bff) return;  // halt until HBLANK
    if (address >= 0xff0c00 && address <= 0xff0fff) {
        eeprom_unlocked_ = true;
        return;
    }
    if (address >= 0xff1000 && address <= 0xff13ff) {
        main_cpu_.set_irq(2, IrqLine::Clear);
        return;
    }
    if (address >= 0xff1400 && address <= 0xff17ff) {
        main_to_sound_data_ = uint8_t(value);
        main_to_sound_ready_ = true;
        sound_cpu_.set_nmi(IrqLine::Assert);
        return;
    }
    if (address >= 0xff1800 && address <= 0xff1bff) {
        sound_cpu_.reset();
        ym_.reset();
        oki_.reset();
        oki_.set_rom(oki_rom_);
        timed_int_ = false;
        ym_int_ = false;
        update_sound_irq();
        return;
    }
    if ((address >= 0xff1c00 && address <= 0xff1c7f) ||
        (address >= 0xff1e00 && address <= 0xff1e7f)) {
        playfield_latch_ = int(value);
        return;
    }
    if ((address >= 0xff1c80 && address <= 0xff1cff) ||
        (address >= 0xff1e80 && address <= 0xff1eff)) {
        xscroll_ = value;
        return;
    }
    if ((address >= 0xff1d00 && address <= 0xff1d7f) ||
        (address >= 0xff1f00 && address <= 0xff1f7f)) {
        main_cpu_.set_irq(1, IrqLine::Clear);
        return;
    }
    if ((address >= 0xff1d80 && address <= 0xff1dff) ||
        (address >= 0xff1f80 && address <= 0xff1fff)) {
        return;  // watchdog
    }
    if (address >= 0xff2000 && address <= 0xff2fff) {
        set_palette(int((address >> 1) & 0x7ff), value);
        return;
    }
    if (address >= 0xff4000 && address <= 0xff47ff) {
        yscroll_ = value;
        return;
    }
    if (address >= 0xff4800 && address <= 0xff4fff) return;  // mobwr unknown
    if (address >= 0xff6000 && address <= 0xff6fff) {
        if (eeprom_unlocked_) {
            const size_t index = size_t((address - 0xff6000) >> 1);
            if (index < eeprom_.size()) eeprom_[index] = uint8_t(value);
            eeprom_unlocked_ = false;
        }
        return;
    }
    if (address >= 0xff8000 && address <= 0xff9fff) {
        const size_t index = size_t((address >> 1) & 0x7ff);
        playfield_[index] = value;
        if (playfield_latch_ != -1) {
            playfield_ext_[index] =
                uint16_t((playfield_ext_[index] & 0xff00) | (playfield_latch_ & 0x00ff));
        }
        return;
    }
    if (address >= 0xffa000 && address <= 0xffbfff) {
        playfield_ext_[(address >> 1) & 0x7ff] = value;
        return;
    }
    if (address >= 0xffc000 && address <= 0xffcf7f) {
        const size_t index = alpha_index(address);
        if (index < alpha_.size()) alpha_[index] = value;
        return;
    }
    if (address >= 0xffcf80 && address <= 0xffcfff) {
        slip_ram_[(address >> 1) & 0x3f] = value;
        return;
    }
    if (address >= 0xffd000 && address <= 0xffdfff) {
        sprite_ram_[(address >> 1) & 0x7ff] = value;
        return;
    }
    if (address >= 0xffe000) work_ram_[(address >> 1) & 0xfff] = value;
}

uint8_t SkullXbo::sound_read(uint16_t address) {
    if (address <= 0x1fff) return sound_ram_[address];
    if (address >= 0x2000 && address <= 0x2001) {
        if ((address & 1) == 0) return ym_.status();
        return 0;
    }
    const uint16_t io = address & ~0x01f9;
    if (io == 0x2800) return oki_.read();
    if (io == 0x2802) {
        main_to_sound_ready_ = false;
        sound_cpu_.set_nmi(IrqLine::Clear);
        return main_to_sound_data_;
    }
    if (io == 0x2804) {
        uint8_t result = uint8_t(0x1c | coin_bits_);
        if (sound_to_main_ready_) result |= 0x20;
        if (!main_to_sound_ready_) result |= 0x40;
        if (!service_) result |= 0x80;
        return result;
    }
    if (io == 0x2806) {
        timed_int_ = false;
        update_sound_irq();
        return 0;
    }
    if (address >= 0x3000 && address <= 0x3fff) {
        return sound_rom_[uint16_t(sound_bank_) * 0x1000 + (address & 0x0fff)];
    }
    if (address >= 0x4000) return sound_rom_[address];
    return 0xff;
}

void SkullXbo::sound_write(uint16_t address, uint8_t value) {
    if (address <= 0x1fff) {
        sound_ram_[address] = value;
        return;
    }
    if (address == 0x2000) {
        ym_.select_register(value);
        return;
    }
    if (address == 0x2001) {
        ym_.write(value);
        return;
    }
    const uint16_t io = address & ~0x01f9;
    if (io == 0x2a00) {
        oki_.write(value);
        return;
    }
    if (io == 0x2a02) {
        sound_to_main_data_ = value;
        sound_to_main_ready_ = true;
        main_cpu_.set_irq(4, IrqLine::Assert);
        return;
    }
    if (io == 0x2a04) {
        wrio_ = value;
        sound_bank_ = uint8_t((value >> 6) & 3);
        oki_.set_pin7((value & 8) != 0);
        if ((value & 4) == 0) oki_.reset();
        if ((value & 1) == 0) ym_.reset();
        return;
    }
    if (io == 0x2a06) {
        mix_ = value;
        update_volumes();
        return;
    }
    if (io == 0x2806) {
        timed_int_ = false;
        update_sound_irq();
    }
}

void SkullXbo::on_sound_cycles(int cycles) {
    ym_.run_timers(cycles * 2);
    oki_accumulator_ += int64_t(cycles) * int64_t(oki_.sample_frequency());
    while (oki_accumulator_ >= int64_t(kSoundClock)) {
        oki_accumulator_ -= int64_t(kSoundClock);
        last_oki_ = oki_.update();
    }
    audio_accumulator_ += int64_t(cycles) * int64_t(YM2151::kSampleRate);
    while (audio_accumulator_ >= int64_t(kSoundClock)) {
        audio_accumulator_ -= int64_t(kSoundClock);
        const int32_t sample =
            int32_t(float(ym_.update()) * ym_gain_) + int32_t(float(last_oki_) * oki_gain_ * 0.75f);
        audio_.push_back(int16_t(std::clamp(sample, int32_t(-32768), int32_t(32767))));
    }
}

void SkullXbo::set_inputs(const MachineInputs& inputs) {
    uint16_t in0 = 0xffff;
    if (inputs.player1.button1) in0 &= ~0x0100;
    if (inputs.player1.button2) in0 &= ~0x0200;
    if (inputs.player1.right) in0 &= ~0x1000;
    if (inputs.player1.left) in0 &= ~0x2000;
    if (inputs.player1.down) in0 &= ~0x4000;
    if (inputs.player1.up) in0 &= ~0x8000;
    in0_ = in0;

    uint16_t in1 = 0xffff;
    if (inputs.player2.button1) in1 &= ~0x0100;
    if (inputs.player2.button2) in1 &= ~0x0200;
    if (inputs.player2.right) in1 &= ~0x1000;
    if (inputs.player2.left) in1 &= ~0x2000;
    if (inputs.player2.down) in1 &= ~0x4000;
    if (inputs.player2.up) in1 &= ~0x8000;
    in1_ = in1;

    coin_bits_ = 0;
    if (inputs.coin1) coin_bits_ |= 0x01;
    if (inputs.coin2) coin_bits_ |= 0x02;
    service_ = inputs.player1.select;
}

void SkullXbo::set_dip_switch(int, uint8_t) {}

void SkullXbo::scanline_update(int scanline) {
    int offset = (scanline / 8) * 64 + 42;
    if (offset >= 0x7c0) return;
    if (scanline == 0) yscroll_ = uint16_t((yscroll_ & 0x007f) | ((yscroll_ >> 7) << 7));
    for (int x = 42; x < 64 && offset < 0x7c0; x++, offset++) {
        const uint16_t data = alpha_[size_t(offset)];
        if ((data & 0x000f) == 0x0d) yscroll_ = data;
    }
}

void SkullXbo::render_frame() {
    std::fill(pf_index_.begin(), pf_index_.end(), 0);
    std::fill(mo_index_.begin(), mo_index_.end(), 0xffff);

    const int scroll_x = int(xscroll_ >> 7) & 0x1ff;
    const int scroll_y = int(yscroll_ >> 7) & 0x1ff;

    for (int f = 0; f < 0x800; f++) {
        const int tx = f / 64;
        const int ty = f % 64;
        const uint16_t data1 = playfield_[size_t(f)];
        const uint16_t data2 = playfield_ext_[size_t(f)] & 0xff;
        const int code = data1 & 0x7fff;
        const int color = data2 & 0x0f;
        const bool flipx = (data1 & 0x8000) != 0;
        const uint8_t* px = playfield_gfx_.element(code);
        const int pal_base = 0x200 + color * 16;
        for (int y = 0; y < 8; y++) {
            const int fy = (ty * 8 + y - scroll_y) & 0x1ff;
            if (fy < 0 || fy >= kScreenHeight) continue;
            for (int x = 0; x < 8; x++) {
                const int sx = flipx ? 7 - x : x;
                const uint8_t p = px[y * 8 + sx];
                const int fx = (tx * 8 + x - scroll_x) & 0x1ff;
                if (fx < 0 || fx >= kScreenWidth) continue;
                pf_index_[size_t(fy) * kScreenWidth + size_t(fx)] = uint16_t(pal_base + p);
            }
        }
    }

    auto draw_mo = [this](int code, int color, bool hflip, bool vflip, int x, int y, int, int prio) {
        const int sx0 = x / 2;
        const uint8_t* px = sprite_gfx_.element(code);
        const int pal_base = (color >> 4) * 32;
        for (int row = 0; row < 8; row++) {
            const int fy = y + row;
            if (fy < 0 || fy >= kScreenHeight) continue;
            const int sy = vflip ? 7 - row : row;
            for (int col = 0; col < 8; col++) {
                const int fx = sx0 + col;
                if (fx < 0 || fx >= kScreenWidth) continue;
                const int sx = hflip ? 7 - col : col;
                const uint8_t p = px[sy * 8 + sx];
                if (p == 0) continue;
                mo_index_[size_t(fy) * kScreenWidth + size_t(fx)] =
                    uint16_t((prio << 12) | (pal_base + p));
            }
        }
    };
    motion_objects_->draw(scroll_x * 2, scroll_y, -1, draw_mo);

    for (int i = 0; i < kScreenWidth * kScreenHeight; i++) {
        uint16_t pf = pf_index_[size_t(i)];
        const uint16_t mo = mo_index_[size_t(i)];
        if (mo != 0xffff) {
            const int mopriority = mo >> 12;
            const int mopix = mo & 0x1f;
            const int pfcolor = (pf >> 4) & 0x0f;
            const int pfpix = pf & 0x0f;
            const bool o17 = (pf & 0xc8) == 0xc8;
            if ((mopriority == 0 && !o17 && mopix >= 2) ||
                (mopriority == 1 && mopix >= 2 && (pfcolor & 0x08) == 0) ||
                ((mopriority & 2) && mopix >= 2 && (pfcolor & 0x0c) == 0) ||
                ((pfpix & 8) == 0 && mopix >= 2)) {
                pf = uint16_t(mo & 0x0fff);
            }
            if ((mopriority == 0 && !o17 && mopix == 1) ||
                (mopriority == 1 && mopix == 1 && (pfcolor & 0x08) == 0) ||
                ((mopriority & 2) && mopix == 1 && (pfcolor & 0x0c) == 0) ||
                ((pfpix & 8) == 0 && mopix == 1)) {
                pf = uint16_t(pf | 0x400);
            }
        }
        framebuffer_[size_t(i)] = pal_color(pf);
    }

    for (int f = 0; f < 64 * 31; f++) {
        const int tx = f % 64;
        const int ty = f / 64;
        const uint16_t data = alpha_[size_t(f)];
        const int code = (data ^ 0x400) & 0x7ff;
        const int color = (data >> 11) & 0x0f;
        const bool opaque = (data & 0x8000) != 0;
        const uint8_t* px = alpha_gfx_.element(code);
        const int pal_base = 0x300 + color * 16;
        for (int y = 0; y < 8; y++) {
            const int fy = ty * 8 + y;
            if (fy < 0 || fy >= kScreenHeight) continue;
            for (int x = 0; x < 8; x++) {
                const int fx = tx * 8 + x;
                if (fx < 0 || fx >= kScreenWidth) continue;
                const uint8_t p = px[y * 8 + x];
                if (!opaque && p == 0) continue;
                framebuffer_[size_t(fy) * kScreenWidth + size_t(fx)] = pal_color(pal_base + p);
            }
        }
    }
}

void SkullXbo::run_frame() {
    for (int line = 0; line < kScanlines; line++) {
        vblank_ = line >= kVBlankLine;
        if (line == kVBlankLine) main_cpu_.set_irq(2, IrqLine::Assert);
        if ((line % 8) == 0) {
            const int offset = (line / 8) * 64 + 42;
            if (offset < 0x7c0 && (alpha_[size_t(offset)] & 0x8000) != 0) irq1_line_ = line + 6;
            scanline_update(line);
        }
        if (line == irq1_line_) {
            main_cpu_.set_irq(1, IrqLine::Assert);
            irq1_line_ = -1;
        }
        if ((line % sound_irq_lines_) == 0) {
            timed_int_ = true;
            update_sound_irq();
        }
        if (line == kVBlankLine) render_frame();
        main_cpu_.run(main_cycles_per_line_);
        sound_cpu_.run(sound_cycles_per_line_);
    }
}

void SkullXbo::drain_audio(std::vector<int16_t>& out) {
    out.insert(out.end(), audio_.begin(), audio_.end());
    audio_.clear();
}

}  // namespace dsp
