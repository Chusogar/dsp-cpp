#include "drivers/arcade/taitosj.h"

#include <algorithm>

#include "core/rom_loader.h"
#include "video/gfx.h"

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


// Character RAM banks at $9000 and $a800: three bit planes of $800 bytes
// ($1000: pen bit 2, $800: bit 1, $000: bit 0), the leftmost pixel in bit 0.
// 8x8 characters take 8 bytes (one per row); 16x16 sprites take 32 bytes,
// rows 0-7 of the left half, then the right half, then rows 8-15 likewise.
constexpr size_t kRamBase = 0x8000;
constexpr size_t kRamSize = 0xd200 - kRamBase;
constexpr size_t kCharBanks[2] = {0x9000 - kRamBase, 0xa800 - kRamBase};
constexpr int kScreenTop = 16;  // first of the 224 lines shown, of 256

// Bit n of a nibble moved to bit 0 of byte n.
constexpr uint32_t kSpread[16] = {
    0x00000000, 0x00000001, 0x00000100, 0x00000101, 0x00010000, 0x00010001, 0x00010100, 0x00010101,
    0x01000000, 0x01000001, 0x01000100, 0x01000101, 0x01010000, 0x01010001, 0x01010100, 0x01010101,
};

inline uint8_t char_pen(const uint8_t* bank, size_t byte, int bit) {
    return uint8_t((((bank[byte + 0x1000] >> bit) & 1) << 2) | (((bank[byte + 0x800] >> bit) & 1) << 1) |
                   ((bank[byte] >> bit) & 1));
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
    ram_.assign(kRamSize, 0);
    screen_.assign(size_t(kScreenWidth) * kScreenHeight, 0);

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
    // Only as much as each set has: RAM is short on the small boards.
    if (!loader.load_pages(elevator ? kElevatorMain : kJunglekMain, elevator ? 0x8000 : 0x9000,
                     main_rom_, main_pages_.data(), main_pages_.size(), error) ||
        !loader.load_pages(elevator ? kElevatorSound : kJunglekSound, elevator ? 0x2000 : 0x3000,
                     sound_rom_, sound_pages_.data(), sound_pages_.size(), error) ||
        !loader.load_pages(elevator ? kElevatorChars : kJunglekChars, 0x8000, gfx_rom_,
                     gfx_pages_.data(), gfx_pages_.size(), error)) {
        return false;
    }

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
    dac_mixed_ = 0;
    sound_latch_ = 0;
    sound_semaphore_ = false;
    sound_nmi_ = {false, false};

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
    std::fill(screen_.begin(), screen_.end(), uint8_t(0));
}

uint8_t TaitoSJ::main_read(uint16_t address) {
    if (address < 0x6000) return main_rom(address);
    if (address < 0x8000) {
        // Bank 0: $6000-$7fff of the image; bank 1: $6000-$6fff then $8000-$8fff.
        const size_t offset = address & 0x1fff;
        if (rom_bank_ == 0 || offset < 0x1000) return main_rom(0x6000 + offset);
        return main_rom(0x8000 + offset - 0x1000);
    }
    if (address < 0x8800) return ram_[address - kRamBase];
    if (address < 0x9000) {
        if (!uses_mcu()) return 0;  // sets without MCU read an empty bus
        if ((address & 1) == 0) {
            mcu_zaccept_ = true;
            return mcu_to_z80_;
        }
        return uint8_t(~((mcu_zready_ ? 1 : 0) | (mcu_zaccept_ ? 2 : 0)));
    }
    if (address < 0xd000) return ram_[address - kRamBase];
    if (address < 0xd060) return uint8_t(scroll_y_[address & 0x7f]);
    if (address >= 0xd200 && address < 0xd300) return palette_ram_[address & 0x7f];
    if (address >= 0xd400 && address < 0xd500) {
        switch (address & 0x0f) {
            case 0x0: case 0x1: case 0x2: case 0x3:
                return collision_[address & 3];
            case 0x4: case 0x5: case 0x6: case 0x7: {
                const uint8_t value = gfx_pos_ < 0x8000 ? gfx_pages_[gfx_pos_ >> 12][gfx_pos_ & 0xfff] : uint8_t(0);
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
    return 0;
}

uint8_t TaitoSJ::bus_peek(uint16_t address) const {
    if (address < 0x6000) return main_rom(address);
    if (address >= kRamBase && address < kRamBase + kRamSize) return ram_[address - kRamBase];
    return 0;
}

void TaitoSJ::main_write(uint16_t address, uint8_t value) {
    if (address < 0x8000) return;  // ROM
    if (address < 0x8800) {
        ram_[address - kRamBase] = value;
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
    if (address < 0xd000) {  // character RAM banks and the three tile maps
        ram_[address - kRamBase] = value;
        return;
    }
    if (address < 0xd060) {
        scroll_y_[address & 0x7f] = value;
        return;
    }
    if (address >= 0xd100 && address < 0xd200) {  // sprite RAM
        ram_[address - kRamBase] = value;
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
    if (address < 0x4000) return sound_pages_[address >> 12][address & 0xfff];
    if (address <= 0x43ff) return sound_ram_[address & 0x3ff];
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
        sound_ram_[address & 0x3ff] = value;
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
            if (mcu_address_ >= kRamBase && mcu_address_ < kRamBase + kRamSize)
                ram_[mcu_address_ - kRamBase] = mcu_port_a_out_;
            // The low byte of the latched address increments, for burst writes.
            mcu_address_ = uint16_t((mcu_address_ & 0xff00) | ((mcu_address_ + 1) & 0xff));
        }
        if ((~value & 0x20) != 0) mcu_port_a_in_ = bus_peek(mcu_address_);
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
    dac_mixed_ = int32_t(dac_sample_ * 0.30f);  // here, not per sample: no FPU on small boards
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
        sample += dac_mixed_;
        audio_.push_back(int16_t(std::clamp<int32_t>(sample, -32768, 32767)));
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

// The picture is composed one row at a time into a line buffer, then copied
// into screen_ in one go: boards that show screen_ while the next frame is
// being drawn (the RP2040 builds its HDMI lines straight from it) never catch
// a row with only the background in it. Per pixel the result is the same as
// drawing each layer and the sprites over the whole screen in turn.

// Tile layer parameters for one frame: pen 0 transparent; each 8-pixel column
// of the screen has its own vertical scroll (from the scroll_y_ table).
TaitoSJ::LayerFrame TaitoSJ::layer_frame(int layer, uint16_t scroll_x, uint8_t scroll_y,
                                         int column_base) const {
    static constexpr size_t kMaps[3] = {0xc400 - kRamBase, 0xc800 - kRamBase, 0xcc00 - kRamBase};
    LayerFrame f;
    f.color = layer == 2   ? uint8_t((colorbank_[1] & 0x07) << 3)
              : layer == 1 ? uint8_t(((colorbank_[0] >> 4) & 0x07) << 3)
                           : uint8_t((colorbank_[0] & 0x07) << 3);
    const int bank = layer == 2   ? (colorbank_[1] & 0x08) >> 3
                     : layer == 1 ? (colorbank_[0] & 0x80) >> 7
                                  : (colorbank_[0] & 0x08) >> 3;
    f.chars = ram_.data() + kCharBanks[bank];
    f.map = ram_.data() + kMaps[layer];
    f.scroll_x = scroll_x;
    for (int group = 0; group < 32; group++) {
        const int column = (group + scroll_x / 8) & 0x1f;
        f.offset_y[size_t(group)] = scroll_y_[size_t(column_base + column)] + scroll_y;
    }
    return f;
}

// Eight screen pixels share a column (so a vertical scroll); they span at
// most two tiles, whose plane bytes are joined and shifted into place.
void TaitoSJ::blit_layer_row(const LayerFrame& f, int y, uint8_t* line) {
    for (int x0 = 0; x0 < 256; x0 += 8) {
        const int first_x = (x0 + f.scroll_x) & 0xff;
        const int tile0 = first_x >> 3, tile1 = (tile0 + 1) & 0x1f, shift = first_x & 7;
        const int source_y = (y + kScreenTop + f.offset_y[size_t(x0 >> 3)]) & 0xff;
        const uint8_t* row = f.map + (source_y >> 3) * 32;
        const size_t a = size_t(row[tile0]) * 8 + size_t(source_y & 7);
        const size_t b = size_t(row[tile1]) * 8 + size_t(source_y & 7);
        const uint8_t* chars = f.chars;
        const unsigned p2 = ((chars[a + 0x1000] | (chars[b + 0x1000] << 8)) >> shift) & 0xff;
        const unsigned p1 = ((chars[a + 0x800] | (chars[b + 0x800] << 8)) >> shift) & 0xff;
        const unsigned p0 = ((chars[a] | (chars[b] << 8)) >> shift) & 0xff;
        const unsigned any = p0 | p1 | p2;
        if (any == 0) continue;  // all transparent
        // Four pixels at a time: kSpread puts bit n of a nibble in byte n.
        for (int half = 0; half < 2; half++) {
            const int s = half * 4;
            const uint32_t pens = kSpread[(p0 >> s) & 15] | (kSpread[(p1 >> s) & 15] << 1) |
                                  (kSpread[(p2 >> s) & 15] << 2);
            uint8_t* dst = line + x0 + s;
            if (((any >> s) & 15) == 15) {  // all four opaque
                const uint32_t v = pens + f.color * 0x01010101u;
                dst[0] = uint8_t(v), dst[1] = uint8_t(v >> 8), dst[2] = uint8_t(v >> 16),
                dst[3] = uint8_t(v >> 24);
            } else {
                for (int x = 0; x < 4; x++) {
                    const uint8_t pen = uint8_t(pens >> (8 * x));
                    if (pen != 0) dst[x] = uint8_t(f.color + pen);
                }
            }
        }
    }
}

// The sprites of one frame, in drawing order.
int TaitoSJ::sprite_frame(std::array<SpriteFrame, 32>& out) const {
    const size_t base = 0xd100 - kRamBase + size_t((video_mode_ & 0x04) << 5);
    int count = 0;
    for (int index = 0x1f; index >= 0; index--) {
        const int which = (index - 1) & 0x1f;
        if (which >= 0x10 && which <= 0x17) continue;  // no sprites here
        const size_t offset = base + size_t(which) * 4;
        SpriteFrame& sp = out[size_t(count)];
        sp.sx = uint8_t(ram_[offset + 0] + pos_x_[3]);
        sp.sy = uint8_t(240 - ram_[offset + 1] + pos_x_[4]);
        if (sp.sy >= 240) continue;
        const uint8_t attrib = ram_[offset + 2];
        const size_t code = ram_[offset + 3] & 0x3f;
        const int bank = (ram_[offset + 3] & 0x40) != 0 ? 1 : 0;
        sp.color = uint8_t((2 * ((colorbank_[1] >> 4) & 0x03) + ((attrib >> 2) & 0x01)) << 3);
        sp.flip_x = (attrib & 0x01) != 0;
        sp.flip_y = (attrib & 0x02) != 0;
        sp.pixels = ram_.data() + kCharBanks[bank] + code * 32;
        count++;
    }
    return count;
}

void TaitoSJ::draw_sprites_row(const SpriteFrame* sprites, int count, int y, uint8_t* line) {
    const int screen_y = y + kScreenTop;
    for (int i = 0; i < count; i++) {
        const SpriteFrame& sp = sprites[i];
        const int sprite_y = screen_y - sp.sy;
        if (sprite_y < 0 || sprite_y >= 16) continue;
        const int source_y = sp.flip_y ? 15 - sprite_y : sprite_y;
        const uint8_t* row = sp.pixels + (source_y < 8 ? source_y : source_y + 8);
        for (int x = 0; x < 16; x++) {
            const int target_x = sp.sx + x;
            if (target_x >= 256) continue;
            const int source_x = sp.flip_x ? 15 - x : x;
            const uint8_t pen = char_pen(row, size_t(source_x < 8 ? 0 : 8), source_x & 7);
            if (pen != 0) line[target_x] = uint8_t(sp.color + pen);
        }
    }
}

void TaitoSJ::update_video() {
    // What each of the four priority slots draws this frame.
    LayerFrame layers[3];
    bool layer_on[3] = {false, false, false};
    if ((video_mode_ & 0x10) != 0) {
        uint16_t x = scroll_[0];
        x = uint16_t((x & 0xf8) + ((x + 3) & 7) + pos_x_[0]);
        layers[0] = layer_frame(0, x, scroll_[1], 0);
        layer_on[0] = true;
    }
    if ((video_mode_ & 0x20) != 0) {
        uint16_t x = scroll_[2];
        x = uint16_t((x & 0xf8) + ((x + 1) & 7) + pos_x_[1]);
        layers[1] = layer_frame(1, x, scroll_[3], 32);
        layer_on[1] = true;
    }
    if ((video_mode_ & 0x40) != 0) {
        uint16_t x = scroll_[4];
        x = uint16_t((x & 0xf8) + ((x - 1) & 7) + pos_x_[2]);
        layers[2] = layer_frame(2, x, scroll_[5], 64);
        layer_on[2] = true;
    }
    std::array<SpriteFrame, 32> sprites;
    const int sprite_count = (video_mode_ & 0x80) != 0 ? sprite_frame(sprites) : 0;
    const auto& order = draw_order_[size_t(video_priority_ & 0x1f)];
    const uint8_t background = uint8_t((colorbank_[1] & 0x07) << 3);

    // Jungle King runs its monitor rotated 180 degrees.
    const bool rotate = variant_ == Variant::JungleKing;
    const bool flip_x = ((video_mode_ & 0x01) != 0) != rotate;
    const bool flip_y = ((video_mode_ & 0x02) != 0) != rotate;
    uint8_t line[256];
    for (int y = 0; y < kScreenHeight; y++) {
        std::fill(line, line + 256, background);
        for (int slot = 0; slot < 4; slot++) {
            const int what = order[size_t(slot)];
            if (what == 0) draw_sprites_row(sprites.data(), sprite_count, y, line);
            else if (what <= 3 && layer_on[what - 1]) blit_layer_row(layers[what - 1], y, line);
        }
        if (flip_x) std::reverse(line, line + 256);
        const int target_y = flip_y ? kScreenHeight - 1 - y : y;
        std::copy(line, line + 256, screen_.begin() + long(target_y) * kScreenWidth);
    }
}

const uint32_t* TaitoSJ::framebuffer() const {
    framebuffer_.resize(screen_.size());
    for (size_t i = 0; i < screen_.size(); ++i) framebuffer_[i] = palette_[screen_[i] & 0x3f];
    return framebuffer_.data();
}

void TaitoSJ::run_frame() {
    const int main_cycles = int(kMainClock / kFramesPerSecond / kScanlines);
    const int sound_cycles = int(kSoundClock / kFramesPerSecond / kScanlines);
    const int mcu_cycles = int(mcu_.clock() / kFramesPerSecond / kScanlines);
    for (int line = 0; line < kScanlines; line++) {
        if (line == 240) {
            main_cpu_.set_irq(IrqLine::Hold);
            if (draw_enabled_) update_video();
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
