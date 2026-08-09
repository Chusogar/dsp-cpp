#include "drivers/taitosj.h"

#include <algorithm>

#include "core/rom_loader.h"

namespace dsp {
namespace {

const std::vector<RomEntry> kElevatorMain = {
    {"ba3__01.2764.ic1", 0x2000, 0x0000, 0xda775a24},
    {"ba3__02.2764.ic2", 0x2000, 0x2000, 0xfbfd8b3a},
    {"ba3__03-1.2764.ic3", 0x2000, 0x4000, 0xa2e69833},
    {"ba3__04-1.2764.ic6", 0x2000, 0x6000, 0x2b78c462},
};

const std::vector<RomEntry> kElevatorSound = {
    {"ba3__09.2732.ic70", 0x1000, 0x0000, 0x6d5f57cb},
    {"ba3__10.2732.ic71", 0x1000, 0x1000, 0xf0a769a1},
};

const std::vector<RomEntry> kElevatorMcu = {
    {"ba3__11.mc68705p3.ic24", 0x800, 0x0000, 0x9ce75afc},
};

const std::vector<RomEntry> kElevatorChars = {
    {"ba3__05.2764.ic4", 0x2000, 0x0000, 0x6c4ee58f},
    {"ba3__06.2764.ic5", 0x2000, 0x2000, 0x41ab0afc},
    {"ba3__07.2764.ic9", 0x2000, 0x4000, 0xefe43731},
    {"ba3__08.2764.ic10", 0x2000, 0x6000, 0x3ca20696},
};

const std::vector<RomEntry> kJunglekMain = {
    {"kn21-1.bin", 0x1000, 0x0000, 0x45f55d30}, {"kn22-1.bin", 0x1000, 0x1000, 0x07cc9a21},
    {"kn43.bin", 0x1000, 0x2000, 0xa20e5a48},   {"kn24.bin", 0x1000, 0x3000, 0x19ea7f83},
    {"kn25.bin", 0x1000, 0x4000, 0x844365ea},   {"kn46.bin", 0x1000, 0x5000, 0x27a95fd5},
    {"kn47.bin", 0x1000, 0x6000, 0x5c3199e0},   {"kn28.bin", 0x1000, 0x7000, 0x194a2d09},
    {"kn60.bin", 0x1000, 0x8000, 0x1a9c0a26},
};

const std::vector<RomEntry> kJunglekSound = {
    {"kn37.bin", 0x1000, 0x0000, 0xdee7f5d4},
    {"kn38.bin", 0x1000, 0x1000, 0xbffd3d21},
    {"kn59-1.bin", 0x1000, 0x2000, 0xcee485fc},
};

const std::vector<RomEntry> kJunglekChars = {
    {"kn29.bin", 0x1000, 0x0000, 0x8f83c290}, {"kn30.bin", 0x1000, 0x1000, 0x89fd19f1},
    {"kn51.bin", 0x1000, 0x2000, 0x70e8fc12}, {"kn52.bin", 0x1000, 0x3000, 0xbcbac1a3},
    {"kn53.bin", 0x1000, 0x4000, 0xb946c87d}, {"kn34.bin", 0x1000, 0x5000, 0x320db2e1},
    {"kn55.bin", 0x1000, 0x6000, 0x70aef58f}, {"kn56.bin", 0x1000, 0x7000, 0x932eb667},
};

// The PROM is called eb16.ic22 in the Elevator Action sets.
const std::vector<RomEntry> kPriorityProm = {
    {"eb16.22|eb16.ic22", 0x100, 0x0000, 0xb833b5ea},
};

constexpr uint8_t kTransparent = 0xff;

// Both character banks hold three bit planes of 0x800 bytes at $9000 and $a800.
const std::vector<int>& char_x_offsets() {
    static const std::vector<int> offsets = {7, 6, 5, 4, 3, 2, 1, 0};
    return offsets;
}

GfxLayout char_layout() {
    GfxLayout layout;
    layout.width = 8;
    layout.height = 8;
    layout.total = 256;
    layout.planes = 3;
    layout.char_increment = 8 * 8;
    layout.plane_offsets = {512 * 8 * 8, 256 * 8 * 8, 0};
    layout.x_offsets = char_x_offsets();
    layout.y_offsets = {0 * 8, 1 * 8, 2 * 8, 3 * 8, 4 * 8, 5 * 8, 6 * 8, 7 * 8};
    return layout;
}

GfxLayout sprite_layout() {
    GfxLayout layout;
    layout.width = 16;
    layout.height = 16;
    layout.total = 64;
    layout.planes = 3;
    layout.char_increment = 32 * 8;
    layout.plane_offsets = {128 * 16 * 16, 64 * 16 * 16, 0};
    layout.x_offsets = {7,         6,         5,         4,         3,         2,
                        1,         0,         8 * 8 + 7, 8 * 8 + 6, 8 * 8 + 5, 8 * 8 + 4,
                        8 * 8 + 3, 8 * 8 + 2, 8 * 8 + 1, 8 * 8 + 0};
    layout.y_offsets = {0 * 8,  1 * 8,  2 * 8,  3 * 8,  4 * 8,  5 * 8,  6 * 8,  7 * 8,
                        16 * 8, 17 * 8, 18 * 8, 19 * 8, 20 * 8, 21 * 8, 22 * 8, 23 * 8};
    return layout;
}

// Volume table of the DAC, ay1_portb_write() in taitosj_hw.pas.
constexpr uint8_t kVolumeTable[256] = {
    0xff, 0xfe, 0xfc, 0xfb, 0xf9, 0xf7, 0xf6, 0xf4, 0xf3, 0xf2, 0xf1, 0xef, 0xee, 0xec, 0xeb, 0xea,
    0xe8, 0xe7, 0xe5, 0xe4, 0xe2, 0xe1, 0xe0, 0xdf, 0xde, 0xdd, 0xdc, 0xdb, 0xd9, 0xd8, 0xd7, 0xd6,
    0xd5, 0xd4, 0xd3, 0xd2, 0xd1, 0xd0, 0xcf, 0xce, 0xcd, 0xcc, 0xcb, 0xca, 0xc9, 0xc8, 0xc7, 0xc6,
    0xc5, 0xc4, 0xc3, 0xc2, 0xc1, 0xc0, 0xbf, 0xbf, 0xbe, 0xbd, 0xbc, 0xbb, 0xba, 0xba, 0xb9, 0xb8,
    0xb7, 0xb7, 0xb6, 0xb5, 0xb4, 0xb3, 0xb3, 0xb2, 0xb1, 0xb1, 0xb0, 0xaf, 0xae, 0xae, 0xad, 0xac,
    0xab, 0xaa, 0xaa, 0xa9, 0xa8, 0xa8, 0xa7, 0xa6, 0xa6, 0xa5, 0xa5, 0xa4, 0xa3, 0xa2, 0xa2, 0xa1,
    0xa1, 0xa0, 0xa0, 0x9f, 0x9e, 0x9e, 0x9d, 0x9d, 0x9c, 0x9c, 0x9b, 0x9b, 0x9a, 0x99, 0x99, 0x98,
    0x97, 0x97, 0x96, 0x96, 0x95, 0x95, 0x94, 0x94, 0x93, 0x93, 0x92, 0x92, 0x91, 0x91, 0x90, 0x90,
    0x8b, 0x8b, 0x8a, 0x8a, 0x89, 0x89, 0x89, 0x88, 0x88, 0x87, 0x87, 0x87, 0x86, 0x86, 0x85, 0x85,
    0x84, 0x84, 0x83, 0x83, 0x82, 0x82, 0x82, 0x81, 0x81, 0x81, 0x80, 0x80, 0x7f, 0x7f, 0x7f, 0x7e,
    0x7e, 0x7e, 0x7d, 0x7d, 0x7c, 0x7c, 0x7c, 0x7b, 0x7b, 0x7b, 0x7a, 0x7a, 0x7a, 0x79, 0x79, 0x79,
    0x78, 0x78, 0x77, 0x77, 0x77, 0x76, 0x76, 0x76, 0x75, 0x75, 0x75, 0x74, 0x74, 0x74, 0x73, 0x73,
    0x73, 0x73, 0x72, 0x72, 0x72, 0x71, 0x71, 0x71, 0x70, 0x70, 0x70, 0x70, 0x6f, 0x6f, 0x6f, 0x6e,
    0x6e, 0x6e, 0x6d, 0x6d, 0x6d, 0x6c, 0x6c, 0x6c, 0x6c, 0x6b, 0x6b, 0x6b, 0x6b, 0x6a, 0x6a, 0x6a,
    0x6a, 0x69, 0x69, 0x69, 0x68, 0x68, 0x68, 0x68, 0x68, 0x67, 0x67, 0x67, 0x66, 0x66, 0x66, 0x66,
    0x65, 0x65, 0x65, 0x65, 0x64, 0x64, 0x64, 0x64, 0x64, 0x63, 0x63, 0x63, 0x63, 0x62, 0x62, 0x62,
};

}  // namespace

TaitoSJ::TaitoSJ(Variant variant)
    : variant_(variant),
      main_cpu_(kMainClock),
      sound_cpu_(kSoundClock),
      mcu_(kMcuClock, M6805::Type::M68705),
      ay0_(kAyClock, 0.3f),
      ay1_(kAyClock, 1.0f),
      ay2_(kAyClock, 1.0f),
      ay3_(kAyClock, 2.0f) {
    framebuffer_.assign(size_t(kScreenWidth) * kScreenHeight, 0xff000000u);

    main_cpu_.set_memory_handlers(
        [this](uint16_t address) { return main_read(address); },
        [this](uint16_t address, uint8_t value) { main_write(address, value); });
    main_cpu_.set_io_handlers([](uint16_t) { return uint8_t(0xff); }, [](uint16_t, uint8_t) {});
    sound_cpu_.set_memory_handlers(
        [this](uint16_t address) { return sound_read(address); },
        [this](uint16_t address, uint8_t value) { sound_write(address, value); });
    sound_cpu_.set_io_handlers([](uint16_t) { return uint8_t(0xff); }, [](uint16_t, uint8_t) {});
    sound_cpu_.set_cycle_handler([this](int cycles) { on_sound_cycles(cycles); });
    mcu_.set_memory_handlers([this](uint16_t address) { return mcu_read(address); },
                             [this](uint16_t address, uint8_t value) { mcu_write(address, value); });

    ay0_.set_port_handlers([this] { return dsw_b_; }, [this] { return dsw_c_; }, nullptr, nullptr);
    ay1_.set_port_handlers(nullptr, nullptr,
                           [this](uint8_t value) {
                               dac_out_ = uint8_t(~value);
                               dac_update();
                           },
                           [this](uint8_t value) {
                               dac_vol_ = kVolumeTable[value];
                               dac_update();
                           });
    ay2_.set_port_handlers(nullptr, nullptr, [this](uint8_t value) { in4_ = uint8_t(value & 0xf0); },
                           nullptr);
    ay3_.set_port_handlers(nullptr, nullptr, nullptr, [this](uint8_t value) {
        sound_nmi_[0] = (~value & 1) != 0;
        if (sound_nmi_[0] && sound_nmi_[1]) sound_cpu_.set_nmi(IrqLine::Pulse);
    });
}

bool TaitoSJ::init(const std::string& rom_path, std::string* error) {
    RomLoader loader;
    if (!loader.open(rom_path, error)) return false;

    const bool elevator = variant_ == Variant::ElevatorAction;
    std::vector<uint8_t> main_rom(0x9000, 0);
    if (!loader.load(elevator ? kElevatorMain : kJunglekMain, main_rom, error)) return false;
    std::copy_n(main_rom.begin(), 0x6000, memory_.begin());
    std::copy_n(main_rom.begin() + 0x6000, 0x2000, rom_banks_[0].begin());
    std::copy_n(main_rom.begin() + 0x6000, 0x1000, rom_banks_[1].begin());
    std::copy_n(main_rom.begin() + 0x8000, 0x1000, rom_banks_[1].begin() + 0x1000);

    std::vector<uint8_t> sound_rom(0x4000, 0);
    if (!loader.load(elevator ? kElevatorSound : kJunglekSound, sound_rom, error)) return false;
    std::copy(sound_rom.begin(), sound_rom.end(), sound_memory_.begin());

    std::vector<uint8_t> gfx_rom(0x8000, 0);
    if (!loader.load(elevator ? kElevatorChars : kJunglekChars, gfx_rom, error)) return false;
    std::copy(gfx_rom.begin(), gfx_rom.end(), gfx_rom_.begin());

    if (elevator) {
        std::vector<uint8_t> mcu_rom(0x800, 0);
        if (!loader.load(kElevatorMcu, mcu_rom, error)) return false;
        std::copy(mcu_rom.begin(), mcu_rom.end(), mcu_memory_.begin());
    }

    std::vector<uint8_t> prom(0x100, 0);
    if (!loader.load(kPriorityProm, prom, error)) return false;
    // Layer drawing order of every priority code, back to front.
    for (int code = 0; code < 32; code++) {
        uint8_t mask = 0;
        for (int slot = 3; slot >= 0; slot--) {
            uint8_t data = uint8_t(prom[size_t(0x10 * (code & 0x0f) + mask)] & 0x0f);
            data = (code & 0x10) != 0 ? uint8_t(data >> 2) : uint8_t(data & 0x03);
            mask = uint8_t(mask | (1 << data));
            draw_order_[size_t(code)][size_t(slot)] = data;
        }
    }

    if (elevator) {
        dsw_a_ = 0x7f;
        pos_x_ = {-8, -23, -21, -2, 0};
    } else {
        dsw_a_ = 0x3f;
        pos_x_ = {8, 10, 12, 1, -2};
    }
    dsw_b_ = 0x00;
    dsw_c_ = 0xff;

    for (GfxSet& set : chars_) set.create(8, 8, 256);
    for (GfxSet& set : sprites_) set.create(16, 16, 64);

    warnings_ = loader.warnings();
    reset();
    return true;
}

void TaitoSJ::reset() {
    main_cpu_.reset();
    sound_cpu_.reset();
    if (uses_mcu()) mcu_.reset();
    ay0_.reset();
    ay1_.reset();
    ay2_.reset();
    ay3_.reset();

    in0_ = 0xff;
    in1_ = 0xff;
    in2_ = 0xff;
    in4_ = 0x00;
    collision_.fill(0);
    scroll_.fill(0);
    colorbank_.fill(0);
    gfx_pos_ = 0;
    video_priority_ = 0;
    rom_bank_ = 0;
    video_mode_ = 0;
    dac_out_ = 0;
    dac_vol_ = 0;
    dac_sample_ = 0;
    sound_latch_ = 0;
    sound_semaphore_ = false;
    sound_nmi_ = {false, false};
    rechars_ = {true, true};

    mcu_zaccept_ = true;
    mcu_zready_ = false;
    mcu_busreq_ = false;
    mcu_to_z80_ = 0;
    mcu_from_z80_ = 0;
    mcu_address_ = 0;
    mcu_port_a_in_ = 0;
    mcu_port_a_out_ = 0;

    sound_irq_counter_ = 0;
    audio_accumulator_ = 0;
    audio_.clear();
    for (auto& layer : layers_) layer.fill(kTransparent);
    screen_.fill(0);
}

uint8_t TaitoSJ::main_read(uint16_t address) {
    if (address < 0x6000) return memory_[address];
    if (address < 0x8000) return rom_banks_[rom_bank_][address & 0x1fff];
    if (address < 0x8800) return memory_[address];
    if (address < 0x9000) {
        if (!uses_mcu()) return 0;  // sets without MCU read an empty bus
        if ((address & 1) == 0) {
            mcu_zaccept_ = true;
            return mcu_to_z80_;
        }
        return uint8_t(~((mcu_zready_ ? 1 : 0) | (mcu_zaccept_ ? 2 : 0)));
    }
    if (address < 0xc000) return memory_[address];
    if (address < 0xd000) return memory_[address];
    if (address < 0xd060) return uint8_t(scroll_y_[address & 0x7f]);
    if (address >= 0xd200 && address < 0xd300) return palette_ram_[address & 0x7f];
    if (address >= 0xd400 && address < 0xd500) {
        switch (address & 0x0f) {
            case 0x0: case 0x1: case 0x2: case 0x3:
                return collision_[address & 3];
            case 0x4: case 0x5: case 0x6: case 0x7: {
                const uint8_t value = gfx_pos_ < 0x8000 ? gfx_rom_[gfx_pos_] : uint8_t(0);
                gfx_pos_ = uint16_t(gfx_pos_ + 1);
                return value;
            }
            case 0x8: return in0_;
            case 0x9: return in1_;
            case 0xa: return dsw_a_;
            case 0xb: return in2_;
            case 0xc: return 0xef;
            case 0xd: return uint8_t(in4_ | 0x0f);
            case 0xf: return ay0_.read();
            default: return 0;
        }
    }
    if (address >= 0xe000) return memory_[address];
    return 0;
}

void TaitoSJ::main_write(uint16_t address, uint8_t value) {
    if (address < 0x8000) return;  // ROM
    if (address < 0x8800) {
        memory_[address] = value;
        return;
    }
    if (address < 0x9000) {
        if (uses_mcu() && (address & 1) == 0) {
            mcu_zready_ = true;
            mcu_.set_irq(IrqLine::Assert);
            mcu_from_z80_ = value;
        }
        return;
    }
    if (address < 0xa800) {  // character RAM bank 0
        if (memory_[address] != value) {
            memory_[address] = value;
            rechars_[0] = true;
        }
        return;
    }
    if (address < 0xc000) {  // character RAM bank 1
        if (memory_[address] != value) {
            memory_[address] = value;
            rechars_[1] = true;
        }
        return;
    }
    if (address < 0xd000) {  // the three tile maps
        memory_[address] = value;
        return;
    }
    if (address < 0xd060) {
        scroll_y_[address & 0x7f] = value;
        return;
    }
    if (address >= 0xd100 && address < 0xd200) {  // sprite RAM
        memory_[address] = value;
        return;
    }
    if (address >= 0xd200 && address < 0xd300) {
        const size_t entry = address & 0x7f;
        if (palette_ram_[entry] != value) {
            palette_ram_[entry] = value;
            set_palette_entry(int(entry) & 0xfe);
        }
        return;
    }
    if (address >= 0xd300 && address < 0xd400) {
        video_priority_ = value;
        return;
    }
    if (address >= 0xd400 && address < 0xd500) {
        if ((address & 0x0f) == 0x0e) ay0_.control(value);
        if ((address & 0x0f) == 0x0f) ay0_.write(value);
        return;
    }
    if (address >= 0xd500 && address < 0xd600) {
        switch (address & 0x0f) {
            case 0x0: case 0x1: case 0x2: case 0x3: case 0x4: case 0x5:
                scroll_[address & 0x0f] = value;
                break;
            case 0x6: case 0x7:
                colorbank_[address & 1] = value;
                break;
            case 0x8: collision_.fill(0); break;
            case 0x9: gfx_pos_ = uint16_t((gfx_pos_ & 0xff00) | value); break;
            case 0xa: gfx_pos_ = uint16_t((gfx_pos_ & 0x00ff) | (uint16_t(value) << 8)); break;
            case 0xb:
                sound_latch_ = value;
                sound_nmi_[1] = true;
                if (sound_nmi_[0] && sound_nmi_[1]) sound_cpu_.set_nmi(IrqLine::Pulse);
                break;
            case 0xc:
                sound_semaphore_ = (value & 1) != 0;
                if (sound_semaphore_) sound_cpu_.set_nmi(IrqLine::Pulse);
                break;
            case 0xe: rom_bank_ = uint8_t(value >> 7); break;
            default: break;  // 0xd: watchdog
        }
        return;
    }
    if (address >= 0xd600 && address < 0xd700) video_mode_ = value;
}

uint8_t TaitoSJ::sound_read(uint16_t address) {
    if (address <= 0x43ff) return sound_memory_[address];
    switch (address) {
        case 0x4801: return ay1_.read();
        case 0x4803: return ay2_.read();
        case 0x4805: return ay3_.read();
        case 0x5000:
            sound_nmi_[1] = false;
            return sound_latch_;
        case 0x5001:
            return uint8_t((sound_nmi_[1] ? 8 : 0) | (sound_semaphore_ ? 4 : 0) | 3);
        default: return 0;
    }
}

void TaitoSJ::sound_write(uint16_t address, uint8_t value) {
    if (address >= 0x4000 && address <= 0x43ff) {
        sound_memory_[address] = value;
        return;
    }
    switch (address) {
        case 0x4800: ay1_.control(value); break;
        case 0x4801: ay1_.write(value); break;
        case 0x4802: ay2_.control(value); break;
        case 0x4803: ay2_.write(value); break;
        case 0x4804: ay3_.control(value); break;
        case 0x4805: ay3_.write(value); break;
        case 0x5000: sound_latch_ = uint8_t(sound_latch_ & 0x7f); break;
        case 0x5001: sound_semaphore_ = false; break;
        default: break;
    }
}

uint8_t TaitoSJ::mcu_read(uint16_t address) {
    address = uint16_t(address & 0x7ff);
    switch (address) {
        case 0: return mcu_port_a_in_;
        case 1: return 0xff;
        case 2:
            return uint8_t((mcu_zready_ ? 1 : 0) | (mcu_zaccept_ ? 2 : 0) | (mcu_busreq_ ? 0 : 4));
        default: return mcu_memory_[address];
    }
}

void TaitoSJ::mcu_write(uint16_t address, uint8_t value) {
    address = uint16_t(address & 0x7ff);
    if (address == 0) {
        mcu_port_a_out_ = value;
        return;
    }
    if (address == 1) {  // port B, the strobes of the Z80 interface
        if ((~value & 0x01) != 0) return;
        if ((~value & 0x02) != 0) {  // the MCU is going to read data from the Z80
            mcu_zready_ = false;
            mcu_.set_irq(IrqLine::Clear);
            mcu_port_a_in_ = mcu_from_z80_;
        }
        mcu_busreq_ = (~value & 0x08) != 0;
        if ((~value & 0x04) != 0) {  // the MCU is writing data for the Z80
            mcu_to_z80_ = mcu_port_a_out_;
            mcu_zaccept_ = false;
        }
        if ((~value & 0x10) != 0) {
            memory_[mcu_address_] = mcu_port_a_out_;
            // The low byte of the latched address increments, for burst writes.
            mcu_address_ = uint16_t((mcu_address_ & 0xff00) | ((mcu_address_ + 1) & 0xff));
        }
        if ((~value & 0x20) != 0) mcu_port_a_in_ = memory_[mcu_address_];
        if ((~value & 0x40) != 0) {
            mcu_address_ = uint16_t((mcu_address_ & 0xff00) | mcu_port_a_out_);
        }
        if ((~value & 0x80) != 0) {
            mcu_address_ = uint16_t((mcu_address_ & 0x00ff) | (uint16_t(mcu_port_a_out_) << 8));
        }
        return;
    }
    if (address >= 3 && address <= 0x7f) mcu_memory_[address] = value;
}

void TaitoSJ::dac_update() {
    dac_sample_ = int16_t(int(dac_out_) * int(dac_vol_) - 0x8000);
}

void TaitoSJ::on_sound_cycles(int cycles) {
    sound_irq_counter_ += cycles;
    while (sound_irq_counter_ >= kSoundIrqPeriod) {
        sound_irq_counter_ -= kSoundIrqPeriod;
        sound_cpu_.set_irq(IrqLine::Hold);
    }

    audio_accumulator_ += int64_t(cycles) * AY8910::kSampleRate;
    while (audio_accumulator_ >= kSoundClock) {
        audio_accumulator_ -= kSoundClock;
        int32_t sample = ay0_.update() + ay1_.update() + ay2_.update() + ay3_.update();
        sample += int32_t(dac_sample_ * 0.30f);
        audio_.push_back(int16_t(std::clamp(sample, -32768, 32767)));
    }
}

void TaitoSJ::set_palette_entry(int index) {
    const std::vector<int> resistances = {1000, 470, 270};
    static const std::vector<std::vector<double>> weights = compute_resistor_weights(
        0, 255, -1.0, {{resistances, 0, 0}, {resistances, 0, 0}, {resistances, 0, 0}});

    const uint8_t odd = uint8_t(~palette_ram_[size_t(index) | 1]);
    const uint8_t even = uint8_t(~palette_ram_[size_t(index)]);
    const int blue = combine_weights(weights[2], {odd & 1, (odd >> 1) & 1, (odd >> 2) & 1});
    const int green =
        combine_weights(weights[1], {(odd >> 3) & 1, (odd >> 4) & 1, (odd >> 5) & 1});
    const int red = combine_weights(weights[0], {(odd >> 6) & 1, (odd >> 7) & 1, even & 1});
    palette_[size_t(index >> 1)] =
        0xff000000u | (uint32_t(red) << 16) | (uint32_t(green) << 8) | uint32_t(blue);
}

void TaitoSJ::decode_chars(int bank) {
    const size_t base = bank == 0 ? 0x9000 : 0xa800;
    const std::vector<uint8_t> ram(memory_.begin() + long(base), memory_.begin() + long(base) + 0x1800);
    chars_[size_t(bank)].decode(char_layout(), ram);
    sprites_[size_t(bank)].decode(sprite_layout(), ram);
}

void TaitoSJ::draw_layer(int layer) {
    static constexpr uint16_t kBases[3] = {0xc400, 0xc800, 0xcc00};
    const uint8_t color = layer == 2 ? uint8_t((colorbank_[1] & 0x07) << 3)
                          : layer == 1 ? uint8_t(((colorbank_[0] >> 4) & 0x07) << 3)
                                       : uint8_t((colorbank_[0] & 0x07) << 3);
    const int bank = layer == 2   ? (colorbank_[1] & 0x08) >> 3
                     : layer == 1 ? (colorbank_[0] & 0x80) >> 7
                                  : (colorbank_[0] & 0x08) >> 3;
    auto& target = layers_[size_t(layer)];
    for (int offset = 0; offset < 0x400; offset++) {
        const int tile_x = (offset % 32) * 8;
        const int tile_y = (offset / 32) * 8;
        const uint8_t code = memory_[kBases[layer] + offset];
        const uint8_t* pixels = chars_[size_t(bank)].element(code);
        for (int y = 0; y < 8; y++) {
            for (int x = 0; x < 8; x++) {
                const uint8_t pen = pixels[y * 8 + x];
                target[size_t((tile_y + y) * 256 + tile_x + x)] =
                    pen == 0 ? kTransparent : uint8_t(color + pen);
            }
        }
    }
}

void TaitoSJ::blit_layer(int layer, uint16_t scroll_x, uint8_t scroll_y, int column_base) {
    const auto& source = layers_[size_t(layer)];
    for (int x = 0; x < 256; x++) {
        const int column = (x / 8 + scroll_x / 8) & 0x1f;
        const int offset_y = scroll_y_[size_t(column_base + column)] + scroll_y;
        const int source_x = (x + scroll_x) & 0xff;
        for (int y = 0; y < 256; y++) {
            const uint8_t pen = source[size_t(((y + offset_y) & 0xff) * 256 + source_x)];
            if (pen != kTransparent) screen_[size_t(y * 256 + x)] = pen;
        }
    }
}

void TaitoSJ::draw_sprites() {
    const uint16_t base = uint16_t(0xd100 + ((video_mode_ & 0x04) << 5));
    for (int index = 0x1f; index >= 0; index--) {
        const int which = (index - 1) & 0x1f;
        if (which >= 0x10 && which <= 0x17) continue;  // no sprites here
        const int offset = which * 4;
        const uint8_t sx = uint8_t(memory_[base + offset + 0] + pos_x_[3]);
        const uint8_t sy = uint8_t(240 - memory_[base + offset + 1] + pos_x_[4]);
        if (sy >= 240) continue;
        const uint8_t attrib = memory_[base + offset + 2];
        const uint8_t code = uint8_t(memory_[base + offset + 3] & 0x3f);
        const int bank = (memory_[base + offset + 3] & 0x40) != 0 ? 1 : 0;
        const uint8_t color =
            uint8_t((2 * ((colorbank_[1] >> 4) & 0x03) + ((attrib >> 2) & 0x01)) << 3);
        const bool flip_x = (attrib & 0x01) != 0;
        const bool flip_y = (attrib & 0x02) != 0;

        const uint8_t* pixels = sprites_[size_t(bank)].element(code);
        for (int y = 0; y < 16; y++) {
            const int source_y = flip_y ? 15 - y : y;
            const int target_y = sy + y;
            if (target_y >= 256) break;
            for (int x = 0; x < 16; x++) {
                const int source_x = flip_x ? 15 - x : x;
                const uint8_t pen = pixels[source_y * 16 + source_x];
                if (pen == 0) continue;
                const int target_x = sx + x;
                if (target_x >= 256) continue;
                screen_[size_t(target_y * 256 + target_x)] = uint8_t(color + pen);
            }
        }
    }
}

void TaitoSJ::update_video() {
    for (int bank = 0; bank < 2; bank++) {
        if (!rechars_[size_t(bank)]) continue;
        decode_chars(bank);
        rechars_[size_t(bank)] = false;
    }

    for (int layer = 0; layer < 3; layer++) {
        if ((video_mode_ & (0x10 << layer)) != 0) draw_layer(layer);
    }

    screen_.fill(uint8_t((colorbank_[1] & 0x07) << 3));
    for (int slot = 0; slot < 4; slot++) {
        switch (draw_order_[size_t(video_priority_ & 0x1f)][size_t(slot)]) {
            case 0:
                if ((video_mode_ & 0x80) != 0) draw_sprites();
                break;
            case 1:
                if ((video_mode_ & 0x10) != 0) {
                    uint16_t x = scroll_[0];
                    x = uint16_t((x & 0xf8) + ((x + 3) & 7) + pos_x_[0]);
                    blit_layer(0, x, scroll_[1], 0);
                }
                break;
            case 2:
                if ((video_mode_ & 0x20) != 0) {
                    uint16_t x = scroll_[2];
                    x = uint16_t((x & 0xf8) + ((x + 1) & 7) + pos_x_[1]);
                    blit_layer(1, x, scroll_[3], 32);
                }
                break;
            case 3:
                if ((video_mode_ & 0x40) != 0) {
                    uint16_t x = scroll_[4];
                    x = uint16_t((x & 0xf8) + ((x - 1) & 7) + pos_x_[2]);
                    blit_layer(2, x, scroll_[5], 64);
                }
                break;
            default: break;
        }
    }

    // Jungle King runs its monitor rotated 180 degrees.
    const bool rotate = variant_ == Variant::JungleKing;
    const bool flip_x = ((video_mode_ & 0x01) != 0) != rotate;
    const bool flip_y = ((video_mode_ & 0x02) != 0) != rotate;
    for (int y = 0; y < kScreenHeight; y++) {
        for (int x = 0; x < kScreenWidth; x++) {
            const uint8_t pen = screen_[size_t((y + 16) * 256 + x)];
            const int target_x = flip_x ? kScreenWidth - 1 - x : x;
            const int target_y = flip_y ? kScreenHeight - 1 - y : y;
            framebuffer_[size_t(target_y * kScreenWidth + target_x)] = palette_[pen & 0x3f];
        }
    }
}

void TaitoSJ::run_frame() {
    const int main_cycles = int(kMainClock / kFramesPerSecond / kScanlines);
    const int sound_cycles = int(kSoundClock / kFramesPerSecond / kScanlines);
    const int mcu_cycles = int(mcu_.clock() / kFramesPerSecond / kScanlines);
    for (int line = 0; line < kScanlines; line++) {
        if (line == 240) {
            main_cpu_.set_irq(IrqLine::Hold);
            update_video();
        }
        main_cpu_.run(main_cycles);
        sound_cpu_.run(sound_cycles);
        if (uses_mcu()) mcu_.run(mcu_cycles);
    }
}

void TaitoSJ::set_inputs(const MachineInputs& inputs) {
    const InputState& player1 = inputs.player1;
    const InputState& player2 = inputs.player2;
    in0_ = 0xff;
    in1_ = 0xff;
    in2_ = 0xff;
    if (player1.left) in0_ &= 0xfe;
    if (player1.right) in0_ &= 0xfd;
    if (player1.down) in0_ &= 0xfb;
    if (player1.up) in0_ &= 0xf7;
    if (player1.button1) in0_ &= 0xef;
    if (player1.button2) in0_ &= 0xdf;

    if (player2.left) in1_ &= 0xfe;
    if (player2.right) in1_ &= 0xfd;
    if (player2.down) in1_ &= 0xfb;
    if (player2.up) in1_ &= 0xf7;
    if (player2.button1) in1_ &= 0xef;
    if (player2.button2) in1_ &= 0xdf;

    if (inputs.coin2) in2_ &= 0xef;
    if (inputs.coin1) in2_ &= 0xdf;
    if (player1.start) in2_ &= 0xbf;
    if (player2.start) in2_ &= 0x7f;
}

void TaitoSJ::set_dip_switch(int bank, uint8_t value) {
    switch (bank) {
        case 0: dsw_a_ = value; break;
        case 1: dsw_b_ = value; break;
        case 2: dsw_c_ = value; break;
        default: break;
    }
}

void TaitoSJ::drain_audio(std::vector<int16_t>& out) {
    out.insert(out.end(), audio_.begin(), audio_.end());
    audio_.clear();
}

}  // namespace dsp
