#include "drivers/arcade/xboard.h"

#include <algorithm>
#include <cmath>

#include "core/rom_loader.h"
#include "video/sega16.h"

namespace dsp {
namespace {

// After Burner II (VER 2.00), MAME set "aburner2".
const std::vector<RomEntry> kMainRom = {
    {"epr-11107.58", 0x20000, 0x00000, 0x6d87bab7},
    {"epr-11108.63", 0x20000, 0x00001, 0x202a3e1d},
};
const std::vector<RomEntry> kSubRom = {
    {"epr-11109.20", 0x20000, 0x00000, 0x85a0fe07},
    {"epr-11110.29", 0x20000, 0x00001, 0xf3d6797c},
};
const std::vector<RomEntry> kTileRom = {
    {"epr-11115.154", 0x10000, 0x00000, 0xe8e32921},
    {"epr-11114.153", 0x10000, 0x10000, 0x2e97f633},
    {"epr-11113.152", 0x10000, 0x20000, 0x36058c8c},
};
const std::vector<RomEntry> kSpriteRom = {
    {"mpr-10932.90", 0x20000, 0x000000, 0xcc0821d6},
    {"mpr-10934.94", 0x20000, 0x000001, 0x4a51b1fa},
    {"mpr-10936.98", 0x20000, 0x000002, 0xada70d64},
    {"mpr-10938.102", 0x20000, 0x000003, 0xe7675baf},
    {"mpr-10933.91", 0x20000, 0x080000, 0xc8efb2c3},
    {"mpr-10935.95", 0x20000, 0x080001, 0xc1e23521},
    {"mpr-10937.99", 0x20000, 0x080002, 0xf0199658},
    {"mpr-10939.103", 0x20000, 0x080003, 0xa0d49480},
    {"epr-11103.92", 0x20000, 0x100000, 0xbdd60da2},
    {"epr-11104.96", 0x20000, 0x100001, 0x06a35fce},
    {"epr-11105.100", 0x20000, 0x100002, 0x027b0689},
    {"epr-11106.104", 0x20000, 0x100003, 0x9e1fec09},
    {"epr-11116.93", 0x20000, 0x180000, 0x49b4c1ba},
    {"epr-11117.97", 0x20000, 0x180001, 0x821fbb71},
    {"epr-11118.101", 0x20000, 0x180002, 0x8f38540b},
    {"epr-11119.105", 0x20000, 0x180003, 0xd0343a8e},
};
const std::vector<RomEntry> kRoadRom = {{"epr-10922.40", 0x10000, 0, 0xb49183d4}};
const std::vector<RomEntry> kSoundRom = {{"epr-11112.17", 0x10000, 0, 0xd777fc6d}};
const std::vector<RomEntry> kPcmRom = {
    {"mpr-10931.11", 0x20000, 0x00000, 0x9209068f},
    {"mpr-10930.12", 0x20000, 0x20000, 0x6493368b},
    {"epr-11102.13", 0x20000, 0x40000, 0x6c07c78d},
};

// Road colour bases and horizontal offset (MAME segaic16_road_init for
// ROAD_XBOARD) and the tilemap palette base.
constexpr uint16_t kRoadColorBase1 = 0x1700;
constexpr uint16_t kRoadColorBase2 = 0x1720;
constexpr uint16_t kRoadColorBase3 = 0x1780;
constexpr int kRoadXOffset = -166;
constexpr uint16_t kTileColorBase = 0x1c00;
// X-Board sprite origin (sega_xboard_sprite_device: set_local_origin(190, 0)).
constexpr int kSpriteXOrigin = 190;

uint16_t combine(uint16_t old, uint16_t data, uint16_t mask) {
    return uint16_t((old & ~mask) | (data & mask));
}

int ramp(int value, int target, int delta) {
    if (value < target) return std::min(target, value + delta);
    return std::max(target, value - delta);
}

}  // namespace

// ---------------------------------------------------------------------------
// 315-5248 multiplier / 315-5249 divider / 315-5250 compare-timer
// (MAME segaic16_m.cpp).

uint16_t XBoard::Multiplier::read(int offset) const {
    switch (offset & 3) {
        case 0: return regs[0];
        case 1: return regs[1];
        case 2: return uint16_t((int32_t(int16_t(regs[0])) * int16_t(regs[1])) >> 16);
        default: return uint16_t((int32_t(int16_t(regs[0])) * int16_t(regs[1])) & 0xffff);
    }
}

void XBoard::Multiplier::write(int offset, uint16_t data, uint16_t mask) {
    regs[size_t(offset & 1)] = combine(regs[size_t(offset & 1)], data, mask);
}

uint16_t XBoard::Divider::read(int offset) const {
    switch (offset & 7) {
        case 0: return regs[0];
        case 1: return regs[1];
        case 2: return regs[2];
        case 4: return regs[4];
        case 5: return regs[5];
        case 6: return regs[6];
        default: return 0xffff;
    }
}

void XBoard::Divider::write(int offset, uint16_t data, uint16_t mask) {
    switch (offset & 3) {
        case 0: regs[0] = combine(regs[0], data, mask); break;
        case 1: regs[1] = combine(regs[1], data, mask); break;
        case 2: regs[2] = combine(regs[2], data, mask); break;
        default: break;
    }
    if (offset & 8) execute(offset & 4);
}

void XBoard::Divider::execute(int mode) {
    regs[6] = 0;
    if (mode == 0) {
        const int32_t dividend = int32_t((uint32_t(regs[0]) << 16) | regs[1]);
        const int32_t divisor = int16_t(regs[2]);
        int64_t quotient;
        if (divisor == 0) {
            quotient = dividend;
            regs[6] |= 0x4000;
        } else {
            quotient = int64_t(dividend) / divisor;
        }
        if (quotient < -32768) {
            quotient = -32768;
            regs[6] |= 0x8000;
        } else if (quotient > 32767) {
            quotient = 32767;
            regs[6] |= 0x8000;
        }
        regs[4] = uint16_t(int16_t(quotient));
        regs[5] = uint16_t(int16_t(int64_t(dividend) - quotient * divisor));
    } else {
        const uint32_t dividend = (uint32_t(regs[0]) << 16) | regs[1];
        const uint32_t divisor = regs[2];
        uint32_t quotient;
        if (divisor == 0) {
            quotient = dividend;
            regs[6] |= 0x4000;
        } else {
            quotient = dividend / divisor;
        }
        regs[4] = uint16_t(quotient >> 16);
        regs[5] = uint16_t(quotient);
    }
}

void XBoard::CompareTimer::reset() {
    regs.fill(0);
    counter = 0;
    bit = 0;
    irq = false;
    zint = false;
}

void XBoard::CompareTimer::execute(bool update_history) {
    const int16_t bound1 = int16_t(regs[0]);
    const int16_t bound2 = int16_t(regs[1]);
    const int16_t value = int16_t(regs[2]);
    const int16_t lo = std::min(bound1, bound2);
    const int16_t hi = std::max(bound1, bound2);
    if (value < lo) {
        regs[7] = uint16_t(lo);
        regs[3] = 0x8000;
    } else if (value > hi) {
        regs[7] = uint16_t(hi);
        regs[3] = 0x4000;
    } else {
        regs[7] = uint16_t(value);
        regs[3] = 0x0000;
    }
    if (update_history) {
        if (bit < 16) regs[4] = uint16_t(regs[4] | ((regs[3] == 0 ? 1u : 0u) << bit));
        bit++;
    }
}

uint16_t XBoard::CompareTimer::read(int offset) {
    switch (offset & 15) {
        case 0x0: return regs[0];
        case 0x1: return regs[1];
        case 0x2: return regs[2];
        case 0x3: return regs[3];
        case 0x4: return regs[4];
        case 0x5: return regs[1];
        case 0x6: return regs[2];
        case 0x7: return regs[7];
        case 0x9:
        case 0xd: irq = false; break;
        default: break;
    }
    return 0xffff;
}

void XBoard::CompareTimer::write(int offset, uint16_t data, uint16_t mask) {
    switch (offset & 15) {
        case 0x0: regs[0] = combine(regs[0], data, mask); execute(); break;
        case 0x1: regs[1] = combine(regs[1], data, mask); execute(); break;
        case 0x2: regs[2] = combine(regs[2], data, mask); execute(true); break;
        case 0x4: regs[4] = 0; bit = 0; break;
        case 0x6: regs[2] = combine(regs[2], data, mask); execute(); break;
        case 0x8:
        case 0xc: regs[8] = combine(regs[8], data, mask); break;
        case 0x9:
        case 0xd: irq = false; break;
        case 0xa:
        case 0xe: regs[10] = combine(regs[10], data, mask); break;
        case 0xb:
        case 0xf:
            // Sound latch: the Z80 gets an NMI until it reads port 0x40.
            regs[11] = uint16_t(data & 0xff);
            zint = true;
            break;
        default: break;
    }
}

void XBoard::CompareTimer::clock(bool state) {
    if (exck == state) return;
    exck = state;
    if (!exck) return;
    const uint16_t old = counter;
    if (regs[10] & 1) counter = uint16_t((counter + 1) & 0xffff);
    if (old == 0xfff) {
        irq = true;
        counter = uint16_t(regs[8] & 0xfff);
    }
}

void XBoard::IoChip::reset() {
    dir.fill(0xff);
}

// ---------------------------------------------------------------------------

XBoard::XBoard(Game game)
    : game_(game),
      main_cpu_(kMainClock),
      sub_cpu_(kMainClock),
      sound_cpu_(kSoundClock),
      ym_(kSoundClock, 0.5f),  // update() sums L+R: halve it for a mono mix
      pcm_(kSoundClock, 1.0f, SegaPcm::Variant::Sega315_5218),
      bitmap_(kScreenWidth * kScreenHeight, 0),
      priority_(kScreenWidth * kScreenHeight, 0),
      sprite_bitmap_(kScreenWidth * kScreenHeight, 0xffff),
      framebuffer_(kScreenWidth * kScreenHeight, 0xff000000u) {
    main_cpu_.set_address_mask(0x3fffff);
    sub_cpu_.set_address_mask(0xfffff);
    main_cpu_.set_memory_handlers([this](uint32_t a) { return main_read(a); },
                                  [this](uint32_t a, uint16_t v) { main_write(a, v, 0xffff); });
    main_cpu_.set_byte_handlers(
        [this](uint32_t a) {
            const uint16_t w = main_read(a & ~1u);
            return uint8_t((a & 1) ? w : (w >> 8));
        },
        [this](uint32_t a, uint8_t v) {
            if (a & 1) main_write(a & ~1u, v, 0x00ff);
            else main_write(a & ~1u, uint16_t(v << 8), 0xff00);
        });
    sub_cpu_.set_memory_handlers([this](uint32_t a) { return sub_read(a); },
                                 [this](uint32_t a, uint16_t v) { sub_write(a, v, 0xffff); });
    sub_cpu_.set_byte_handlers(
        [this](uint32_t a) {
            const uint16_t w = sub_read(a & ~1u);
            return uint8_t((a & 1) ? w : (w >> 8));
        },
        [this](uint32_t a, uint8_t v) {
            if (a & 1) sub_write(a & ~1u, v, 0x00ff);
            else sub_write(a & ~1u, uint16_t(v << 8), 0xff00);
        });
    // The main CPU's RESET instruction resets the sub CPU.
    main_cpu_.set_reset_instruction_handler([this]() { sub_cpu_.set_reset_line(IrqLine::Pulse); });

    sound_cpu_.set_memory_handlers([this](uint16_t a) { return sound_read(a); },
                                   [this](uint16_t a, uint8_t v) { sound_write(a, v); });
    sound_cpu_.set_io_handlers([this](uint16_t p) { return sound_in(p); },
                               [this](uint16_t p, uint8_t v) { sound_out(p, v); });
    sound_cpu_.set_cycle_handler([this](int cycles) { on_sound_cycles(cycles); });
    ym_.set_irq_handler(
        [this](bool state) { sound_cpu_.set_irq(state ? IrqLine::Assert : IrqLine::Clear); });
    pcm_.set_bank(SegaPcm::kBank512);
    pcm_.set_read_rom([this](uint32_t address) -> uint8_t {
        return address < pcm_rom_.size() ? pcm_rom_[address] : 0xff;
    });
    build_palette_luts();
    (void)game_;
}

bool XBoard::init(const std::string& rom_path, std::string* error) {
    if (!load_roms(rom_path, error)) return false;
    reset();
    return true;
}

bool XBoard::load_roms(const std::string& rom_path, std::string* error) {
    RomLoader loader;
    if (!loader.open(rom_path, error)) return false;
    if (!load_roms16w(loader, kMainRom, main_rom_, error)) return false;
    if (!load_roms16w(loader, kSubRom, sub_rom_, error)) return false;
    std::vector<uint8_t> tiles;
    if (!load_rom_bytes(loader, kTileRom, tiles, error)) return false;
    decode_s16_tiles(tiles_, tiles, 2);
    if (!load_roms32dw(loader, kSpriteRom, sprite_rom_, error)) return false;
    std::vector<uint8_t> road;
    if (!load_rom_bytes(loader, kRoadRom, road, error)) return false;
    decode_outrun_road(road_gfx_, road);
    if (!load_rom_bytes(loader, kSoundRom, sound_rom_, error)) return false;
    sound_rom_.resize(0x10000, 0xff);
    if (!load_rom_bytes(loader, kPcmRom, pcm_rom_, error)) return false;
    pcm_rom_.resize(0x80000, 0xff);  // ROMREGION_ERASEFF
    return true;
}

void XBoard::build_palette_luts() {
    uint8_t normal[32], shadow[32], hilight[32];
    build_s16_palette_luts(normal, shadow, hilight);
    for (int i = 0; i < 32; i++) {
        pal_normal_[size_t(i)] = normal[i];
        pal_shadow_[size_t(i)] = shadow[i];
        pal_hilight_[size_t(i)] = hilight[i];
    }
}

void XBoard::palette_write(int index, uint16_t value) {
    palette_ram_[size_t(index)] = value;
    //  sBGR BBBB GGGG RRRR
    const int r = ((value >> 12) & 0x01) | ((value << 1) & 0x1e);
    const int g = ((value >> 13) & 0x01) | ((value >> 3) & 0x1e);
    const int b = ((value >> 14) & 0x01) | ((value >> 7) & 0x1e);
    palette_[size_t(index)] =
        s16_argb(pal_normal_[size_t(r)], pal_normal_[size_t(g)], pal_normal_[size_t(b)]);
    const auto& fx = (value & 0x8000) ? pal_hilight_ : pal_shadow_;
    palette_[size_t(index + kPaletteEntries)] =
        s16_argb(fx[size_t(r)], fx[size_t(g)], fx[size_t(b)]);
}

void XBoard::reset() {
    main_cpu_.reset();
    sub_cpu_.reset();
    sound_cpu_.reset();
    ym_.reset();
    pcm_.reset();
    backup1_.fill(0);
    backup2_.fill(0);
    subram0_.fill(0);
    subram1_.fill(0);
    tile_ram_.fill(0);
    text_ram_.fill(0);
    sprite_ram_.fill(0);
    sprite_buffer_.fill(0);
    road_ram_.fill(0);
    road_buffer_.fill(0);
    sound_ram_.fill(0);
    for (int i = 0; i < kPaletteEntries; i++) palette_write(i, 0);
    mult_main_ = {};
    mult_sub_ = {};
    div_main_ = {};
    div_sub_ = {};
    timer_main_.reset();
    timer_sub_.reset();
    for (auto& chip : iochip_) {
        chip.latch.fill(0);
        chip.reset();
    }
    latched_pages_.fill(0);
    latched_xscroll_.fill(0);
    latched_yscroll_.fill(0);
    road_control_ = 0;
    display_enable_ = false;
    pc0_ = 0;
    sound_reset_ = true;
    sound_mute_ = false;
    vblank_irq_ = false;
    adc_latch_ = 0;
    sound_commands_ = 0;
    stick_x_ = stick_y_ = throttle_ = 0x80;
    main_debt_ = sub_debt_ = sound_debt_ = 0;
    pcm_acc_ = audio_acc_ = pcm_sum_ = 0;
    pcm_count_ = 0;
    pcm_last_ = 0;
    audio_.clear();
    update_main_irqs();
}

void XBoard::set_inputs(const MachineInputs& inputs) {
    // IO1 port A (MAME xboard_generic / aburner): D1 test, D2 service,
    // D3 start, D4 Vulcan cannon, D5 missile, D6 coin 1, D7 coin 2.
    const InputState& p = inputs.player1;
    io1_porta_ = 0xff;
    if (inputs.service) io1_porta_ &= ~0x02;
    if (p.start) io1_porta_ &= ~0x08;
    if (p.button1) io1_porta_ &= ~0x10;
    if (p.button2) io1_porta_ &= ~0x20;
    if (inputs.coin1) io1_porta_ &= ~0x40;
    if (inputs.coin2) io1_porta_ &= ~0x80;
    // ADC0 stick X 0x20-0xe0, ADC1 stick Y 0x40-0xc0 reversed (up = 0xc0),
    // ADC2 throttle (centres at 0x80): ramped like MAME's key deltas.
    stick_x_ = ramp(stick_x_, p.left ? 0x20 : p.right ? 0xe0 : 0x80, 8);
    stick_y_ = ramp(stick_y_, p.up ? 0xc0 : p.down ? 0x40 : 0x80, 6);
    throttle_ = ramp(throttle_, p.button3 ? 0xff : p.button4 ? 0x00 : 0x80, 40);
}

void XBoard::set_dip_switch(int bank, uint8_t value) {
    if (bank == 0) dsw_a_ = value;
    if (bank == 1) dsw_b_ = value;
}

void XBoard::drain_audio(std::vector<int16_t>& out) {
    out.swap(audio_);
    audio_.clear();
}

uint8_t XBoard::adc_value(int channel) const {
    switch (channel & 7) {
        case 0: return uint8_t(stick_x_);
        case 1: return uint8_t(stick_y_);
        case 2: return uint8_t(throttle_);
        case 3:
        case 4: return uint8_t((0xb0 + 0x50) / 2);  // motor position (upright: centred)
        default: return 0x10;
    }
}

// ---------------------------------------------------------------------------
// CXD1095 I/O chips

uint8_t XBoard::iochip_input(int chip, int port) {
    if (chip == 0) {
        switch (port) {
            case 0:
                // aburner2_motor_r: D7 unused (1), D6 = ADC /INTR (conversion
                // done), D5-D0 motor limit switches (open).
                return 0xbf;
            case 1: return 0xff;
            default: return 0xff;
        }
    }
    switch (port) {
        case 0: return io1_porta_;
        case 1: return 0xff;
        case 2: return dsw_a_;
        case 3: return dsw_b_;
        default: return 0xff;
    }
}

void XBoard::iochip_output(int chip, int port, uint8_t value) {
    if (chip != 0) return;
    if (port == 2) {
        // D6 watchdog, D5 display enable, D4-D2 ADC select, D0 sound reset.
        pc0_ = value;
        display_enable_ = (value & 0x20) != 0;
        const bool reset = (value & 0x01) == 0;
        if (reset && !sound_reset_) sound_cpu_.reset();
        sound_reset_ = reset;
    } else if (port == 3) {
        // D7 amplifier mute (1 = sound on), lamps and coin counter.
        sound_mute_ = (value & 0x80) == 0;
    }
}

uint8_t XBoard::iochip_read(int chip, int offset) {
    IoChip& io = iochip_[size_t(chip)];
    if (offset < 5) {
        uint8_t mask = io.dir[size_t(offset)];
        if (offset == 4) mask &= 0x0f;
        const uint8_t in = mask ? uint8_t(iochip_input(chip, offset) & mask) : 0;
        return uint8_t(in | (io.latch[size_t(offset)] & ~io.dir[size_t(offset)]));
    }
    return 0;
}

void XBoard::iochip_write(int chip, int offset, uint8_t value) {
    IoChip& io = iochip_[size_t(chip)];
    if (offset < 5) {
        if (offset == 4) value &= 0x0f;
        io.latch[size_t(offset)] = value;
        iochip_output(chip, offset, uint8_t(value & ~io.dir[size_t(offset)]));
    } else if (offset == 6) {
        uint8_t data = value;
        for (int port = 0; port < 4; port++) {
            io.dir[size_t(port)] = uint8_t(((data & 1) ? 0x0f : 0) | ((data & 2) ? 0xf0 : 0));
            data >>= 2;
        }
    } else if (offset == 7) {
        io.dir[4] = uint8_t((value & 0x0f) | 0xf0);
    }
}

// ---------------------------------------------------------------------------
// Main CPU (MAME segaxbd_state::main_map)

uint16_t XBoard::main_read(uint32_t address) {
    address &= 0x3fffff;
    if (address < 0x080000) return main_rom_[(address >> 1) % main_rom_.size()];
    if (address < 0x0a0000) return backup1_[(address & 0x3fff) >> 1];
    if (address < 0x0c0000) return backup2_[(address & 0x3fff) >> 1];
    if (address < 0x0d0000) return tile_ram_[(address & 0xffff) >> 1];
    if (address < 0x0e0000) return text_ram_[(address & 0xfff) >> 1];
    if (address < 0x0e4000) return mult_main_.read(int((address >> 1) & 3));
    if (address < 0x0e8000) return div_main_.read(int((address >> 1) & 0xf));
    if (address < 0x0ec000) {
        const uint16_t value = timer_main_.read(int((address >> 1) & 0xf));
        update_main_irqs();
        return value;
    }
    if (address < 0x100000) return 0xffff;
    if (address < 0x110000) return sprite_ram_[(address & 0xfff) >> 1];
    if (address < 0x120000) return 0xffff;
    if (address < 0x130000) return palette_ram_[(address & 0x3fff) >> 1];
    if (address < 0x140000) return uint16_t(0xff00 | adc_latch_);
    if (address < 0x150000) return uint16_t(0xff00 | iochip_read(0, int((address >> 1) & 7)));
    if (address < 0x160000) return uint16_t(0xff00 | iochip_read(1, int((address >> 1) & 7)));
    if (address < 0x200000) return 0xffff;
    if (address < 0x280000) return sub_rom_[((address - 0x200000) >> 1) % sub_rom_.size()];
    if (address < 0x2a0000) return subram0_[(address & 0x3fff) >> 1];
    if (address < 0x2c0000) return subram1_[(address & 0x3fff) >> 1];
    if (address < 0x2e0000) return 0xffff;
    if (address < 0x2e4000) return mult_sub_.read(int((address >> 1) & 3));
    if (address < 0x2e8000) return div_sub_.read(int((address >> 1) & 0xf));
    if (address < 0x2ec000) return timer_sub_.read(int((address >> 1) & 7));
    if (address < 0x2ee000) return road_ram_[(address & 0xfff) >> 1];
    if (address < 0x2f0000) {
        road_swap();
        return 0xffff;
    }
    if (address >= 0x3f8000 && address < 0x3fc000) return backup1_[(address & 0x3fff) >> 1];
    if (address >= 0x3fc000) return backup2_[(address & 0x3fff) >> 1];
    return 0xffff;
}

void XBoard::main_write(uint32_t address, uint16_t value, uint16_t mask) {
    address &= 0x3fffff;
    auto store = [&](uint16_t& cell) { cell = combine(cell, value, mask); };
    if (address < 0x080000) return;
    if (address < 0x0a0000) return store(backup1_[(address & 0x3fff) >> 1]);
    if (address < 0x0c0000) return store(backup2_[(address & 0x3fff) >> 1]);
    if (address < 0x0d0000) return store(tile_ram_[(address & 0xffff) >> 1]);
    if (address < 0x0e0000) return store(text_ram_[(address & 0xfff) >> 1]);
    if (address < 0x0e4000) return mult_main_.write(int((address >> 1) & 3), value, mask);
    if (address < 0x0e8000) return div_main_.write(int((address >> 1) & 0xf), value, mask);
    if (address < 0x0ec000) {
        const int reg = int((address >> 1) & 0xf);
        timer_main_.write(reg, value, mask);
        if ((reg & 0xb) == 0xb) {
            sound_commands_++;
            sound_cpu_.set_nmi(IrqLine::Assert);
        }
        update_main_irqs();
        return;
    }
    if (address < 0x100000) return;
    if (address < 0x110000) return store(sprite_ram_[(address & 0xfff) >> 1]);
    if (address < 0x120000) return sprite_swap();
    if (address < 0x130000) {
        const int index = int((address & 0x3fff) >> 1);
        return palette_write(index, combine(palette_ram_[size_t(index)], value, mask));
    }
    if (address < 0x140000) {
        // ADC0804: a write starts a conversion of the channel selected by
        // I/O chip 0 port C D4-D2; it is ready by the time it is read.
        adc_latch_ = adc_value((pc0_ >> 2) & 7);
        return;
    }
    if (address < 0x150000) {
        if (mask & 0x00ff) iochip_write(0, int((address >> 1) & 7), uint8_t(value));
        return;
    }
    if (address < 0x160000) {
        if (mask & 0x00ff) iochip_write(1, int((address >> 1) & 7), uint8_t(value));
        return;
    }
    if (address < 0x280000) return;  // 0x160000 I/O control (ignored as in MAME), sub ROM
    if (address < 0x2a0000) return store(subram0_[(address & 0x3fff) >> 1]);
    if (address < 0x2c0000) return store(subram1_[(address & 0x3fff) >> 1]);
    if (address < 0x2e0000) return;
    if (address < 0x2e4000) return mult_sub_.write(int((address >> 1) & 3), value, mask);
    if (address < 0x2e8000) return div_sub_.write(int((address >> 1) & 0xf), value, mask);
    if (address < 0x2ec000) return timer_sub_.write(int((address >> 1) & 7), value, mask);
    if (address < 0x2ee000) return store(road_ram_[(address & 0xfff) >> 1]);
    if (address < 0x2f0000) {
        if (mask & 0x00ff) road_control_ = uint8_t(value & 7);
        return;
    }
    if (address >= 0x3f8000 && address < 0x3fc000) return store(backup1_[(address & 0x3fff) >> 1]);
    if (address >= 0x3fc000) return store(backup2_[(address & 0x3fff) >> 1]);
}

// ---------------------------------------------------------------------------
// Sub CPU (MAME segaxbd_state::sub_map)

uint16_t XBoard::sub_read(uint32_t address) {
    address &= 0xfffff;
    if (address < 0x080000) return sub_rom_[(address >> 1) % sub_rom_.size()];
    if (address < 0x0a0000) return subram0_[(address & 0x3fff) >> 1];
    if (address < 0x0c0000) return subram1_[(address & 0x3fff) >> 1];
    if (address < 0x0e0000) return 0xffff;
    if (address < 0x0e4000) return mult_sub_.read(int((address >> 1) & 3));
    if (address < 0x0e8000) return div_sub_.read(int((address >> 1) & 0xf));
    if (address < 0x0ec000) return timer_sub_.read(int((address >> 1) & 7));
    if (address < 0x0ee000) return road_ram_[(address & 0xfff) >> 1];
    if (address < 0x0f0000) {
        road_swap();
        return 0xffff;
    }
    return 0xffff;
}

void XBoard::sub_write(uint32_t address, uint16_t value, uint16_t mask) {
    address &= 0xfffff;
    auto store = [&](uint16_t& cell) { cell = combine(cell, value, mask); };
    if (address < 0x080000) return;
    if (address < 0x0a0000) return store(subram0_[(address & 0x3fff) >> 1]);
    if (address < 0x0c0000) return store(subram1_[(address & 0x3fff) >> 1]);
    if (address < 0x0e0000) return;
    if (address < 0x0e4000) return mult_sub_.write(int((address >> 1) & 3), value, mask);
    if (address < 0x0e8000) return div_sub_.write(int((address >> 1) & 0xf), value, mask);
    if (address < 0x0ec000) return timer_sub_.write(int((address >> 1) & 7), value, mask);
    if (address < 0x0ee000) return store(road_ram_[(address & 0xfff) >> 1]);
    if (address < 0x0f0000) {
        if (mask & 0x00ff) road_control_ = uint8_t(value & 7);
    }
}

void XBoard::road_swap() { std::swap(road_ram_, road_buffer_); }

void XBoard::sprite_swap() {
    std::swap(sprite_ram_, sprite_buffer_);
    sprite_ram_[0] = 0xffff;  // MAME: "hack for thunderblade"
}

void XBoard::update_main_irqs() {
    // MAME segaxbd_state::update_main_irqs: the timer (level 2) and VBLANK
    // (level 4) lines are encoded on the IPL pins, so both give level 6.
    int irq = 0;
    if (timer_main_.irq) irq |= 2;
    else main_cpu_.set_irq(2, IrqLine::Clear);
    if (vblank_irq_) irq |= 4;
    else main_cpu_.set_irq(4, IrqLine::Clear);
    if (irq != 6) main_cpu_.set_irq(6, IrqLine::Clear);
    if (irq) main_cpu_.set_irq(irq, IrqLine::Assert);
}

// ---------------------------------------------------------------------------
// Sound (MAME segaxbd_state::sound_map / sound_portmap)

uint8_t XBoard::sound_read(uint16_t address) {
    if (address < 0xf000) return sound_rom_[address];
    if (address < 0xf800) return pcm_.read(address);
    return sound_ram_[address & 0x7ff];
}

void XBoard::sound_write(uint16_t address, uint8_t value) {
    if (address < 0xf000) return;
    if (address < 0xf800) pcm_.write(address, value);
    else sound_ram_[address & 0x7ff] = value;
}

uint8_t XBoard::sound_in(uint16_t port) {
    const uint8_t p = uint8_t(port);
    if (p < 0x40) return (p & 1) ? ym_.status() : 0xff;
    if (p < 0x80) {
        timer_main_.zint = false;
        sound_cpu_.set_nmi(IrqLine::Clear);
        return uint8_t(timer_main_.regs[11]);
    }
    return 0xff;
}

void XBoard::sound_out(uint16_t port, uint8_t value) {
    const uint8_t p = uint8_t(port);
    if (p < 0x40) {
        if ((p & 1) == 0) ym_.select_register(value);
        else ym_.write(value);
    }
}

void XBoard::on_sound_cycles(int cycles) {
    ym_.run_timers(cycles);
    pcm_acc_ += int64_t(cycles) * pcm_.tick_rate();
    while (pcm_acc_ >= kSoundClock) {
        pcm_acc_ -= kSoundClock;
        pcm_.clock();
        pcm_sum_ += pcm_.last_sample();
        pcm_count_++;
    }
    audio_acc_ += int64_t(cycles) * kSampleRate;
    while (audio_acc_ >= kSoundClock) {
        audio_acc_ -= kSoundClock;
        if (pcm_count_ > 0) {
            pcm_last_ = int32_t(pcm_sum_ / pcm_count_);
            pcm_sum_ = 0;
            pcm_count_ = 0;
        }
        // MAME segaxbd: YM2151 0.15, Sega PCM 0.35.
        double mix = double(ym_.update()) * 0.15 + double(pcm_last_) * 0.35;
        if (sound_mute_) mix = 0;
        const int32_t sample = int32_t(std::lround(mix * 3.5));
        audio_.push_back(int16_t(std::clamp(sample, int32_t(-32768), int32_t(32767))));
    }
}

// ---------------------------------------------------------------------------
// Video (MAME segaxbd_state::screen_update)

void XBoard::latch_tilemaps() {
    for (int i = 0; i < 4; i++) {
        latched_pages_[size_t(i)] = text_ram_[size_t(0xe80 / 2 + i)];
        latched_yscroll_[size_t(i)] = text_ram_[size_t(0xe90 / 2 + i)];
        latched_xscroll_[size_t(i)] = text_ram_[size_t(0xe98 / 2 + i)];
    }
}

void XBoard::draw_tile_layer(int which, int category, uint8_t mark) {
    // System 16B tilemap (segaic16 tilemap_16b_draw_layer): a 2x2 page
    // virtual map of 1024x512, per-row X scroll, per-16-pixel column Y
    // scroll and per-row alternate page sets.
    const uint16_t xscroll = latched_xscroll_[size_t(which)];
    const uint16_t yscroll = latched_yscroll_[size_t(which)];
    const uint16_t pages = latched_pages_[size_t(which)];
    const bool colscroll = (yscroll & 0x8000) != 0;
    for (int y = 0; y < kScreenHeight; y++) {
        const uint16_t rowscroll = text_ram_[size_t(0xf80 / 2 + 0x20 * which + y / 8)];
        for (int x0 = colscroll ? -8 : 0; x0 < kScreenWidth; x0 += colscroll ? 16 : kScreenWidth) {
            uint16_t effx = (xscroll & 0x8000) ? rowscroll : xscroll;
            uint16_t effy = colscroll
                                ? text_ram_[size_t(0xf16 / 2 + 0x20 * which + (x0 + 8) / 16)]
                                : yscroll;
            uint16_t effpages = pages;
            if (rowscroll & 0x8000) {
                effx = latched_xscroll_[size_t(which + 2)];
                effy = latched_yscroll_[size_t(which + 2)];
                effpages = latched_pages_[size_t(which + 2)];
            }
            effx = uint16_t((0xc0 - effx) & 0x3ff);
            effy &= 0x1ff;
            const int vy = (y + effy) & 0x1ff;
            const int xs = std::max(x0, 0);
            const int xe = std::min(colscroll ? x0 + 16 : kScreenWidth, kScreenWidth);
            uint16_t* dest = &bitmap_[size_t(y * kScreenWidth)];
            uint8_t* pri = &priority_[size_t(y * kScreenWidth)];
            for (int x = xs; x < xe; x++) {
                const int vx = (x + effx) & 0x3ff;
                const int page = (effpages >> (((vy >> 8) << 3) | ((vx >> 9) << 2))) & 0xf;
                const uint16_t data =
                    tile_ram_[size_t(page * 0x800 + ((vy & 0xff) >> 3) * 64 + ((vx & 0x1ff) >> 3))];
                if (int(data >> 15) != category) continue;
                const uint8_t pen = tiles_.element(data & 0x1fff)[(vy & 7) * 8 + (vx & 7)];
                if (pen == 0) continue;
                dest[x] = uint16_t(kTileColorBase + ((data >> 6) & 0x7f) * 8 + pen);
                pri[x] |= mark;
            }
        }
    }
}

void XBoard::draw_text_layer(int category, uint8_t mark) {
    for (int y = 0; y < kScreenHeight; y++) {
        uint16_t* dest = &bitmap_[size_t(y * kScreenWidth)];
        uint8_t* pri = &priority_[size_t(y * kScreenWidth)];
        for (int x = 0; x < kScreenWidth; x++) {
            const int tx = x + 192;
            const uint16_t data = text_ram_[size_t((y >> 3) * 64 + (tx >> 3))];
            if (int(data >> 15) != category) continue;
            const uint8_t pen = tiles_.element(data & 0x1ff)[(y & 7) * 8 + (tx & 7)];
            if (pen == 0) continue;
            dest[x] = uint16_t(kTileColorBase + ((data >> 9) & 7) * 8 + pen);
            pri[x] |= mark;
        }
    }
}

void XBoard::draw_road(bool background) {
    // segaic16_road_outrun_draw with the X-Board parameters.
    static const uint8_t kPriorityMap[2][8] = {{0x80, 0x81, 0x81, 0x87, 0, 0, 0, 0x00},
                                               {0x81, 0x81, 0x81, 0x8f, 0, 0, 0, 0x80}};
    const uint16_t* roadram = road_buffer_.data();
    const uint8_t* gfx = road_gfx_.data();
    for (int y = 0; y < kScreenHeight; y++) {
        uint16_t* dest = &bitmap_[size_t(y * kScreenWidth)];
        const uint16_t data0 = roadram[0x000 + y];
        const uint16_t data1 = roadram[0x100 + y];
        if (background) {
            int color = -1;
            switch (road_control_ & 3) {
                case 0:
                    if (data0 & 0x800) color = data0 & 0x7f;
                    break;
                case 1:
                    if (data0 & 0x800) color = data0 & 0x7f;
                    else if (data1 & 0x800) color = data1 & 0x7f;
                    break;
                case 2:
                    if (data1 & 0x800) color = data1 & 0x7f;
                    else if (data0 & 0x800) color = data0 & 0x7f;
                    break;
                case 3:
                    if (data1 & 0x800) color = data1 & 0x7f;
                    break;
            }
            if (color != -1) std::fill(dest, dest + kScreenWidth, uint16_t(color | kRoadColorBase3));
            continue;
        }
        if ((data0 & 0x800) && (data1 & 0x800)) continue;
        const uint8_t control = road_control_ & 3;
        const bool per_line = (road_control_ & 4) != 0;
        const uint8_t* src0 =
            (data0 & 0x800) ? gfx + 256 * 2 * 512 : gfx + (0x000 + ((data0 >> 1) & 0xff)) * 512;
        int hpos0 = roadram[0x200 + (per_line ? y : (data0 & 0x1ff))] & 0xfff;
        const uint16_t color0 = roadram[0x600 + (per_line ? y : (data0 & 0x1ff))];
        const uint8_t* src1 =
            (data1 & 0x800) ? gfx + 256 * 2 * 512 : gfx + (0x100 + ((data1 >> 1) & 0xff)) * 512;
        int hpos1 = roadram[0x400 + (per_line ? (0x100 + y) : (data1 & 0x1ff))] & 0xfff;
        const uint16_t color1 = roadram[0x600 + (per_line ? (0x100 + y) : (data1 & 0x1ff))];
        uint16_t table[32] = {};
        table[0x00] = uint16_t(kRoadColorBase1 ^ 0x00 ^ ((color0 >> 0) & 1));
        table[0x01] = uint16_t(kRoadColorBase1 ^ 0x02 ^ ((color0 >> 1) & 1));
        table[0x02] = uint16_t(kRoadColorBase1 ^ 0x04 ^ ((color0 >> 2) & 1));
        table[0x03] = (data0 & 0x200) ? table[0x00]
                                      : uint16_t(kRoadColorBase2 ^ 0x00 ^ ((color0 >> 8) & 0xf));
        table[0x07] = uint16_t(kRoadColorBase1 ^ 0x06 ^ ((color0 >> 3) & 1));
        table[0x10] = uint16_t(kRoadColorBase1 ^ 0x08 ^ ((color1 >> 4) & 1));
        table[0x11] = uint16_t(kRoadColorBase1 ^ 0x0a ^ ((color1 >> 5) & 1));
        table[0x12] = uint16_t(kRoadColorBase1 ^ 0x0c ^ ((color1 >> 6) & 1));
        table[0x13] = (data1 & 0x200) ? table[0x10]
                                      : uint16_t(kRoadColorBase2 ^ 0x10 ^ ((color1 >> 8) & 0xf));
        table[0x17] = uint16_t(kRoadColorBase1 ^ 0x0e ^ ((color1 >> 7) & 1));
        const int shift = 0x5f8 + kRoadXOffset;
        hpos0 = (hpos0 - shift) & 0xfff;
        hpos1 = (hpos1 - shift) & 0xfff;
        switch (control) {
            case 0:
                if (data0 & 0x800) break;
                for (int x = 0; x < kScreenWidth; x++) {
                    dest[x] = table[(hpos0 < 0x200) ? src0[hpos0] : 3];
                    hpos0 = (hpos0 + 1) & 0xfff;
                }
                break;
            case 1:
            case 2:
                for (int x = 0; x < kScreenWidth; x++) {
                    const int pix0 = (hpos0 < 0x200) ? src0[hpos0] : 3;
                    const int pix1 = (hpos1 < 0x200) ? src1[hpos1] : 3;
                    if ((kPriorityMap[control - 1][pix0] >> pix1) & 1) dest[x] = table[0x10 + pix1];
                    else dest[x] = table[pix0];
                    hpos0 = (hpos0 + 1) & 0xfff;
                    hpos1 = (hpos1 + 1) & 0xfff;
                }
                break;
            case 3:
                if (data1 & 0x800) break;
                for (int x = 0; x < kScreenWidth; x++) {
                    dest[x] = table[0x10 + ((hpos1 < 0x200) ? src1[hpos1] : 3)];
                    hpos1 = (hpos1 + 1) & 0xfff;
                }
                break;
        }
    }
}

void XBoard::draw_sprites() {
    // sega_outrun_sprite_device::draw, X-Board variant.
    std::fill(sprite_bitmap_.begin(), sprite_bitmap_.end(), uint16_t(0xffff));
    sprites_drawn_ = 0;
    if (sprite_rom_.empty()) return;
    const int numbanks = int(sprite_rom_.size() / 0x10000);
    const int min_x = kSpriteXOrigin;
    const int max_x = kSpriteXOrigin + kScreenWidth - 1;
    for (size_t index = 0; index + 8 <= sprite_buffer_.size(); index += 8) {
        uint16_t* data = &sprite_buffer_[index];
        if (data[0] & 0x8000) break;
        const int hide = data[0] & 0x5000;
        int bank = (data[0] >> 9) & 7;
        const int top = int(data[0] & 0x1ff) - 0x100;
        uint16_t addr = data[1];
        const int pitch = int16_t((data[2] >> 1) | ((data[4] & 0x1000) << 3)) >> 8;
        int xpos = data[2] & 0x1ff;
        int vzoom = data[3] & 0x7ff;
        const int ydelta = (data[4] & 0x8000) ? 1 : -1;
        const bool flip = ((~data[4] >> 14) & 1) != 0;
        const int xdelta = (data[4] & 0x2000) ? 1 : -1;
        int hzoom = data[4] & 0x7ff;
        const int height = (data[5] & 0xfff) + 1;
        const uint16_t colpri = uint16_t(((data[6] & 0xff) << 4) | (((data[3] >> 12) & 7) << 12));
        if (xpos < 0x80 && xdelta < 0) xpos += 0x200;
        data[7] = addr;
        if (hide) continue;
        if (numbanks) bank %= numbanks;
        const uint32_t* spritedata = sprite_rom_.data() + size_t(0x10000) * size_t(bank);
        if (vzoom < 0x40) vzoom = 0x40;
        if (hzoom < 0x40) hzoom = 0x40;
        sprites_drawn_++;
        int yacc = 0;
        const int ytarget = top + ydelta * height;
        for (int y = top; y != ytarget; y += ydelta) {
            if (y >= 0 && y < kScreenHeight) {
                uint16_t* dest = &sprite_bitmap_[size_t(y * kScreenWidth)];
                int xacc = 0;
                data[7] = addr;
                for (int x = xpos; (xdelta > 0 && x <= max_x) || (xdelta < 0 && x >= min_x);) {
                    uint32_t pixels = spritedata[data[7]];
                    if (flip) {
                        data[7]--;
                    } else {
                        data[7]++;
                        pixels = ((pixels << 28) & 0xf0000000u) | ((pixels << 20) & 0x0f000000u) |
                                 ((pixels << 12) & 0x00f00000u) | ((pixels << 4) & 0x000f0000u) |
                                 ((pixels >> 4) & 0x0000f000u) | ((pixels >> 12) & 0x00000f00u) |
                                 ((pixels >> 20) & 0x000000f0u) | ((pixels >> 28) & 0x0000000fu);
                    }
                    const bool last = (pixels & 0x0f000000u) == 0x0f000000u;
                    for (int k = 0; k < 8; k++) {
                        const int pix = int(pixels & 0xf);
                        while (xacc < 0x200) {
                            if (x >= min_x && x <= max_x && pix != 0 && pix != 15) {
                                dest[x - kSpriteXOrigin] = uint16_t(colpri | pix);
                            }
                            x += xdelta;
                            xacc += hzoom;
                        }
                        xacc -= 0x200;
                        pixels >>= 4;
                    }
                    if (last) break;
                }
            }
            yacc += vzoom;
            addr = uint16_t(addr + pitch * (yacc >> 9));
            yacc &= 0x1ff;
        }
    }
}

void XBoard::render() {
    if (!display_enable_) {
        std::fill(framebuffer_.begin(), framebuffer_.end(), 0xff000000u);
        return;
    }
    draw_sprites();
    std::fill(priority_.begin(), priority_.end(), uint8_t(0));
    std::fill(bitmap_.begin(), bitmap_.end(), uint16_t(0));
    // After Burner II: road_priority = 0, so both road layers go first.
    draw_road(true);
    draw_road(false);
    draw_tile_layer(1, 0, 0x01);  // background
    draw_tile_layer(1, 1, 0x02);
    draw_tile_layer(0, 0, 0x02);  // foreground
    draw_tile_layer(0, 1, 0x04);
    draw_text_layer(0, 0x04);
    draw_text_layer(1, 0x08);
    for (size_t i = 0; i < bitmap_.size(); i++) {
        uint16_t index = bitmap_[i];
        const uint16_t pix = sprite_bitmap_[i];
        if (pix != 0xffff) {
            const int prio = (pix >> 12) & 3;
            if ((1 << prio) > priority_[i]) {
                if ((pix & 0x400f) == 0x400a) index = uint16_t((index & 0x1fff) + kPaletteEntries);
                else index = uint16_t(pix & 0xfff);
            }
        }
        framebuffer_[i] = palette_[index % palette_.size()];
    }
}

void XBoard::run_frame() {
    constexpr int kSlices = 2;
    const double lines = double(kScanlines) * kSlices;
    const double main_cycles = double(kMainClock) / kFramesPerSecond / lines;
    const double sound_cycles = double(kSoundClock) / kFramesPerSecond / lines;
    for (int line = 0; line < kScanlines; line++) {
        // The main compare/timer counts V0 edges.
        timer_main_.clock((line & 1) != 0);
        if (line == 223) {
            vblank_irq_ = true;
            sub_cpu_.set_irq(4, IrqLine::Assert);
            render();
        } else if (line == 224) {
            vblank_irq_ = false;
            sub_cpu_.set_irq(4, IrqLine::Clear);
        } else if (line == 261) {
            latch_tilemaps();
        }
        update_main_irqs();
        for (int slice = 0; slice < kSlices; slice++) {
            main_debt_ += main_cycles;
            main_debt_ -= main_cpu_.run(int(main_debt_));
            sub_debt_ += main_cycles;
            sub_debt_ -= sub_cpu_.run(int(sub_debt_));
            sound_debt_ += sound_cycles;
            if (sound_reset_) {
                const int idle = int(sound_debt_);
                on_sound_cycles(idle);
                sound_debt_ -= idle;
            } else {
                sound_debt_ -= sound_cpu_.run(int(sound_debt_));
            }
        }
    }
}

}  // namespace dsp
