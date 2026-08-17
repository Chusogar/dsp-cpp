#include "machine/starwars_math.h"

namespace dsp {

void StarwarsMath::init(const uint8_t* prom) {
    for (int cnt = 0; cnt < 1024; cnt++) {
        int val = prom[0x0c00 + cnt] & 0x0f;
        val |= (prom[0x0800 + cnt] << 4) & 0x00f0;
        val |= (prom[0x0400 + cnt] << 8) & 0x0f00;
        val |= (prom[0x0000 + cnt] << 12) & 0xf000;
        prom_str_[size_t(cnt)] = uint8_t((val >> 8) & 0xff);
        prom_mas_[size_t(cnt)] = uint8_t(val & 0x7f);
        prom_am_[size_t(cnt)] = uint8_t((val >> 7) & 1);
    }
}

void StarwarsMath::reset() {
    mpa_ = bic_ = 0;
    a_ = b_ = c_ = 0;
    acc_ = 0;
    divisor_ = dividend_ = dvd_shift_ = quotient_shift_ = 0;
    run_cycles_ = 0;
}

void StarwarsMath::tick(int cpu_cycles) {
    if (run_cycles_ > 0) {
        run_cycles_ -= cpu_cycles;
        if (run_cycles_ < 0) run_cycles_ = 0;
    }
}

void StarwarsMath::run_mproc() {
    int mptime = 0;
    int stop = 100000;
    run_cycles_ = 1;

    while (stop > 0) {
        mptime += 5;
        const int ip15_8 = prom_str_[size_t(mpa_)];
        const int ip7 = prom_am_[size_t(mpa_)];
        const int ip6_0 = prom_mas_[size_t(mpa_)];

        int ma = (ip7 == 0) ? ((ip6_0 & 3) | ((bic_ & 0x1ff) << 2)) : ip6_0;
        const int ma_byte = ma << 1;
        const int ramword =
            (ram_[size_t(ma_byte + 1)] & 0xff) | ((ram_[size_t(ma_byte)] & 0xff) << 8);

        if (ip15_8 & kClearAcc) acc_ = 0;
        if (ip15_8 & kLac) acc_ = int32_t(uint32_t(ramword) << 16);
        if (ip15_8 & kReadAcc) {
            ram_[size_t(ma_byte + 1)] = uint8_t((acc_ >> 16) & 0xff);
            ram_[size_t(ma_byte)] = uint8_t((acc_ >> 24) & 0xff);
        }
        if (ip15_8 & kHalt) stop = 0;
        if (ip15_8 & kIncBic) bic_ = (bic_ + 1) & 0x1ff;
        if (ip15_8 & kLdc) {
            c_ = int16_t(ramword);
            acc_ += (((int32_t(a_ - b_) << 1) * c_) << 1);
            a_ = (a_ & 0x8000) ? int16_t(0xffff) : 0;
            b_ = (b_ & 0x8000) ? int16_t(0xffff) : 0;
            mptime += 33;
        }
        if (ip15_8 & kLdb) b_ = int16_t(ramword);
        if (ip15_8 & kLda) a_ = int16_t(ramword);

        const int tmp = mpa_ + 1;
        mpa_ = (mpa_ & 0x0300) | (tmp & 0x00ff);
        stop--;
    }

    // Mathbox clock is 12.096 MHz; the 6809 runs at 1/8 of that.
    run_cycles_ = (mptime + 7) / 8;
    if (run_cycles_ < 1) run_cycles_ = 1;
}

void StarwarsMath::write(uint8_t offset, uint8_t data) {
    switch (offset & 7) {
        case 0:
            mpa_ = int(data) << 2;
            run_mproc();
            break;
        case 1:
            bic_ = (bic_ & 0x00ff) | ((int(data) & 1) << 8);
            break;
        case 2:
            bic_ = (bic_ & 0x0100) | data;
            break;
        case 4:
            divisor_ = uint16_t((divisor_ & 0x00ff) | (uint16_t(data) << 8));
            dvd_shift_ = dividend_;
            quotient_shift_ = 0;
            break;
        case 5:
            divisor_ = uint16_t((divisor_ & 0xff00) | data);
            for (int i = 1; i < 16; i++) {
                quotient_shift_ = uint16_t(quotient_shift_ << 1);
                if ((int32_t(dvd_shift_) + int32_t(divisor_ ^ 0xffff) + 1) & 0x10000) {
                    quotient_shift_ |= 1;
                    dvd_shift_ = uint16_t((dvd_shift_ + (divisor_ ^ 0xffff) + 1) << 1);
                } else {
                    dvd_shift_ = uint16_t(dvd_shift_ << 1);
                }
            }
            break;
        case 6:
            dividend_ = uint16_t((dividend_ & 0x00ff) | (uint16_t(data) << 8));
            break;
        case 7:
            dividend_ = uint16_t((dividend_ & 0xff00) | data);
            break;
        default:
            break;
    }
}

}  // namespace dsp
