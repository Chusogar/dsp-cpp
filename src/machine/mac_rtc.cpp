#include "machine/mac_rtc.h"

#include <cstring>

namespace dsp {

void MacRtc::reset() {
    mem_.fill(0);
    // Seconds since 1904-01-01 for 1990-01-01, a plausible classic-Mac clock.
    seconds_ = 2716300800u;
    mem_[0] = uint8_t(seconds_ >> 24);
    mem_[1] = uint8_t(seconds_ >> 16);
    mem_[2] = uint8_t(seconds_ >> 8);
    mem_[3] = uint8_t(seconds_);
    ce_ = false;
    clk_ = false;
    data_in_ = true;
    data_out_ = true;
    bit_ = 0;
    state_ = 0;
    cmd_ = 0;
    data_ = 0;
}

void MacRtc::ce_w(bool level) {
    if (level == ce_) return;
    ce_ = level;
    if (ce_) {
        bit_ = 0;
        state_ = 0;
        cmd_ = 0;
        data_ = 0;
        data_out_ = true;
    } else {
        data_out_ = true;
    }
}

void MacRtc::data_w(bool level) { data_in_ = level; }

bool MacRtc::data_r() const { return ce_ ? data_out_ : true; }

void MacRtc::clk_w(bool level) {
    if (!ce_ || level == clk_) {
        clk_ = level;
        return;
    }
    const bool rise = level && !clk_;
    clk_ = level;
    if (!rise) return;

    if (state_ == 0) {
        cmd_ = uint8_t((cmd_ << 1) | (data_in_ ? 1 : 0));
        if (++bit_ == 8) {
            bit_ = 0;
            if (cmd_ & 0x80) {
                state_ = 1;
                data_ = 0;
            } else {
                state_ = 2;
                data_ = mem_[cmd_ & 0x1f];
                data_out_ = (data_ & 0x80) != 0;
            }
        }
    } else if (state_ == 1) {
        data_ = uint8_t((data_ << 1) | (data_in_ ? 1 : 0));
        if (++bit_ == 8) {
            mem_[cmd_ & 0x1f] = data_;
            if ((cmd_ & 0x1f) <= 3) {
                seconds_ = (uint32_t(mem_[0]) << 24) | (uint32_t(mem_[1]) << 16) |
                           (uint32_t(mem_[2]) << 8) | mem_[3];
            }
            state_ = 0;
            bit_ = 0;
        }
    } else {
        data_ = uint8_t(data_ << 1);
        data_out_ = (data_ & 0x80) != 0;
        if (++bit_ == 8) {
            state_ = 0;
            bit_ = 0;
            data_out_ = true;
        }
    }
}

bool MacRtc::tick_seconds() {
    seconds_++;
    mem_[0] = uint8_t(seconds_ >> 24);
    mem_[1] = uint8_t(seconds_ >> 16);
    mem_[2] = uint8_t(seconds_ >> 8);
    mem_[3] = uint8_t(seconds_);
    return true;
}

}  // namespace dsp
