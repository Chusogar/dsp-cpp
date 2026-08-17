#include "machine/sega_315_5195.h"

namespace dsp {
namespace {

constexpr uint32_t kRegionSize[4] = {0x10000, 0x20000, 0x80000, 0x200000};

}  // namespace

void Sega3155195::reset() {
    regs_.fill(0);
    from_sound_ = 0;
    rebuild_map();
}

void Sega3155195::rebuild_map() {
    for (int region = 0; region < 8; region++) {
        const uint32_t size = kRegionSize[regs_[size_t(0x10 + region * 2)] & 3];
        dirs_start_[size_t(region)] = uint32_t(regs_[size_t(0x11 + region * 2)]) << 16;
        dirs_end_[size_t(region)] = dirs_start_[size_t(region)] + size;
    }
}

uint8_t Sega3155195::read_reg(uint8_t address) {
    address &= 0x1f;
    switch (address) {
        case 0:
        case 1:
            return regs_[address];
        case 2:
            return ((regs_[2] & 3) == 3) ? 0 : 0x0f;
        case 3:
            return from_sound_;
        default:
            return open_bus_ ? open_bus_() : 0xff;
    }
}

void Sega3155195::write_reg(uint8_t address, uint8_t value) {
    address &= 0x1f;
    const uint8_t old = regs_[address];
    regs_[address] = value;
    switch (address) {
        case 2:
            if (((old ^ value) & 3) != 0) {
                if (reset_line_) {
                    reset_line_(((value & 3) == 3) ? IrqLine::Assert : IrqLine::Clear);
                }
            }
            break;
        case 3:
            if (sound_latch_) sound_latch_(value);
            break;
        case 4:
            if (value > 0 && value < 0x0f && irq_line_) {
                irq_line_((~value) & 7, IrqLine::Hold);
            }
            break;
        case 5:
            if (value == 1 && bus_write_) {
                const uint32_t addr = (uint32_t(regs_[0x0a]) << 17) | (uint32_t(regs_[0x0b]) << 9) |
                                      (uint32_t(regs_[0x0c]) << 1);
                const uint16_t word = uint16_t((regs_[0] << 8) | regs_[1]);
                bus_write_(addr, word);
            } else if (value == 2 && bus_read_) {
                const uint32_t addr = (uint32_t(regs_[7]) << 17) | (uint32_t(regs_[8]) << 9) |
                                      (uint32_t(regs_[9]) << 1);
                const uint16_t word = bus_read_(addr);
                regs_[0] = uint8_t(word >> 8);
                regs_[1] = uint8_t(word);
            }
            break;
        default:
            if (address >= 0x10) {
                if (old != value) rebuild_map();
            }
            break;
    }
}

}  // namespace dsp
