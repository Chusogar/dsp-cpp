#include "machine/rp5c01.h"

namespace dsp {

void Rp5c01::reset() {
    address_ = 0;
    regs_.fill(0);
    // 2026-08-18 07:00:00, 24-hour mode.
    regs_[0] = 0;              // seconds 1
    regs_[1] = 0;              // seconds 10
    regs_[2] = 0;              // minutes 1
    regs_[3] = 0;              // minutes 10
    regs_[4] = 7;              // hours 1
    regs_[5] = 0;              // hours 10
    regs_[6] = 2;              // weekday (Tue)
    regs_[7] = 8;              // day 1
    regs_[8] = 1;              // day 10
    regs_[9] = 8;              // month 1
    regs_[10] = 0;             // month 10
    regs_[11] = 6;             // year 1
    regs_[12] = 2;             // year 10
    regs_[13] = 0x00;          // mode
}

uint8_t Rp5c01::read() const { return uint8_t(regs_[address_ & 0x0f] & 0x0f); }

void Rp5c01::write(uint8_t value) { regs_[address_ & 0x0f] = uint8_t(value & 0x0f); }

}  // namespace dsp
