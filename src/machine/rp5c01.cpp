#include "machine/rp5c01.h"

namespace dsp {

void Rp5c01::reset() {
    address_ = 0;
    mode_ = 0;
    for (auto& bank : bank_) bank.fill(0);
    // Bank 0: 2026-08-18 07:00:00, 24-hour mode.
    bank_[0][0] = 0;   // seconds 1
    bank_[0][1] = 0;   // seconds 10
    bank_[0][2] = 0;   // minutes 1
    bank_[0][3] = 0;   // minutes 10
    bank_[0][4] = 7;   // hours 1
    bank_[0][5] = 0;   // hours 10
    bank_[0][6] = 2;   // weekday (Tue)
    bank_[0][7] = 8;   // day 1
    bank_[0][8] = 1;   // day 10
    bank_[0][9] = 8;   // month 1
    bank_[0][10] = 0;  // month 10 / 24h
    bank_[0][11] = 6;  // year 1
    bank_[0][12] = 2;  // year 10
}

uint8_t Rp5c01::read() const {
    if (address_ == 13) return uint8_t(mode_ & 0x0f);
    if (address_ >= 13) return 0x0f;
    int bank = mode_ & 3;
    if (bank == 3) return 0;
    return uint8_t(bank_[bank][address_] & 0x0f);
}

void Rp5c01::write(uint8_t value) {
    value &= 0x0f;
    if (address_ == 13) {
        mode_ = value;
        return;
    }
    if (address_ >= 13) return;
    int bank = mode_ & 3;
    if (bank == 3) return;  // timer-reset bank: ignore so the clock/RAM survive
    bank_[bank][address_] = value;
}

}  // namespace dsp
