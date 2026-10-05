#include "machine/mos8722.h"

namespace dsp {
namespace {

constexpr uint16_t kBottomAddress[4] = {0x0400, 0x1000, 0x0400, 0x1000};
constexpr uint16_t kTopAddress[4] = {0xf000, 0xf000, 0xe000, 0xc000};

}  // namespace

void Mos8722::reset() {
    reg_.fill(0);
    reg_[P1L] = 0x01;
    p0h_latch_ = 0;
    p1h_latch_ = 0;
    // Force 8502 from reset: real hardware starts on the Z80 BIOS, which then
    // hands off. With the Z80 stubbed, keep the 8502 unlocked (MCR bit0 = 1).
    reg_[MCR] = 0x01;
}

uint8_t Mos8722::read(uint16_t addr, uint8_t bus) const {
    if (c64_mode()) return bus;

    if ((addr >= 0xd500 && addr < 0xd50c) && io_enabled()) {
        switch (addr & 0x0f) {
            case CR: return uint8_t(reg_[CR] | 0x80);
            case MCR:
                // GAME/EXROM sense high (no cartridge), 40/80 key released.
                return uint8_t((reg_[MCR] | 0x06) | 0x80);
            case VR: return 0x20;  // 128K, MMU version 0
            default: return reg_[addr & 0x0f];
        }
    }
    if (addr >= 0xff00 && addr < 0xff05) {
        switch (addr & 0x0f) {
            case CR: return uint8_t(reg_[CR] | 0x80);
            default: return reg_[addr & 0x0f];
        }
    }
    return bus;
}

void Mos8722::write(uint16_t addr, uint8_t value) {
    if (c64_mode()) return;

    if ((addr >= 0xd500 && addr < 0xd50c) && io_enabled()) {
        switch (addr & 0x0f) {
            case CR:
                reg_[CR] = value & 0x7f;
                break;
            case PCRA:
            case PCRB:
            case PCRC:
            case PCRD:
                reg_[addr & 0x0f] = value & 0x7f;
                break;
            case MCR:
                // Keep bit0 set so the stubbed Z80 never seizes the bus.
                reg_[MCR] = uint8_t((value & ~0x01) | 0x01);
                break;
            case RCR:
                reg_[RCR] = value & 0x4f;
                break;
            case P0L:
                reg_[P0L] = value;
                reg_[P0H] = p0h_latch_;
                break;
            case P0H:
                p0h_latch_ = value & 0x01;
                break;
            case P1L:
                reg_[P1L] = value;
                reg_[P1H] = p1h_latch_;
                break;
            case P1H:
                p1h_latch_ = value & 0x01;
                break;
            default:
                reg_[addr & 0x0f] = value;
                break;
        }
        return;
    }

    if (addr >= 0xff00 && addr < 0xff05) {
        switch (addr & 0x0f) {
            case CR:
                reg_[CR] = value & 0x7f;
                break;
            default:
                // LCRA..LCRD: load matching preconfiguration into CR.
                reg_[CR] = reg_[addr & 0x0f];
                break;
        }
    }
}

uint32_t Mos8722::translate_ram(uint16_t addr) const {
    int bank = ram_bank();
    uint16_t page = uint16_t(addr & 0xff00);

    if (addr < 0x0100) {
        page = uint16_t(reg_[P0L] << 8);
        bank = reg_[P0H] & 0x01;
    } else if (addr < 0x0200) {
        page = uint16_t(reg_[P1L] << 8);
        bank = reg_[P1H] & 0x01;
    } else {
        // Page-pointer mirror: accessing the relocated page reaches ZP/stack.
        if ((addr >> 8) == reg_[P0L]) {
            page = 0x0000;
            bank = reg_[P0H] & 0x01;
        } else if ((addr >> 8) == reg_[P1L]) {
            page = 0x0100;
            bank = reg_[P1H] & 0x01;
        }

        const int share = reg_[RCR] & 0x03;
        const bool bottom = (reg_[RCR] & 0x04) != 0;
        const bool top = (reg_[RCR] & 0x08) != 0;
        if ((bottom && addr < kBottomAddress[share]) ||
            (top && addr >= kTopAddress[share])) {
            bank = 0;
        }
    }

    return uint32_t(bank) * 0x10000u + uint32_t(page | (addr & 0x00ff));
}

}  // namespace dsp
