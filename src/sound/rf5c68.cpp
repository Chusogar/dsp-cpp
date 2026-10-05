#include "sound/rf5c68.h"

#include <algorithm>
#include <cmath>

namespace dsp {

Rf5c68::Rf5c68(uint32_t clock, float amplitude) : clock_(clock), amplitude_(amplitude) {
    // Chip sample rate is clock / 384 (MAME).
    sample_step_ = double(clock_) / 384.0 / double(kSampleRate);
    reset();
}

void Rf5c68::reset() {
    for (Channel& ch : chan_) ch = Channel{};
    ram_.fill(0);
    cbank_ = 0;
    wbank_ = 0;
    enable_ = 0;
    sample_pos_ = 0;
    last_left_ = last_right_ = 0;
}

void Rf5c68::write_reg(uint8_t offset, uint8_t data) {
    Channel& chan = chan_[cbank_ & 7];
    switch (offset & 0x0f) {
        case 0x00:
            chan.env = data;
            break;
        case 0x01:
            chan.pan = data;
            break;
        case 0x02:
            chan.step = uint16_t((chan.step & 0xff00) | data);
            break;
        case 0x03:
            chan.step = uint16_t((chan.step & 0x00ff) | (uint16_t(data) << 8));
            break;
        case 0x04:
            chan.loopst = uint16_t((chan.loopst & 0xff00) | data);
            break;
        case 0x05:
            chan.loopst = uint16_t((chan.loopst & 0x00ff) | (uint16_t(data) << 8));
            break;
        case 0x06:
            chan.start = data;
            if (!chan.enable) chan.addr = uint32_t(chan.start) << (8 + 11);
            break;
        case 0x07:
            enable_ = (data >> 7) & 1;
            if (data & 0x40) cbank_ = data & 7;
            else wbank_ = uint16_t((data & 0xf) << 12);
            break;
        case 0x08:
            for (int i = 0; i < kChannels; i++) {
                chan_[size_t(i)].enable = uint8_t((~data >> i) & 1);
                if (!chan_[size_t(i)].enable) {
                    chan_[size_t(i)].addr = uint32_t(chan_[size_t(i)].start) << (8 + 11);
                }
            }
            break;
        default:
            break;
    }
}

uint8_t Rf5c68::read_mem(uint16_t offset) const {
    return ram_[(wbank_ | (offset & 0x0fff)) & (kRamSize - 1)];
}

void Rf5c68::write_mem(uint16_t offset, uint8_t data) {
    ram_[(wbank_ | (offset & 0x0fff)) & (kRamSize - 1)] = data;
}

void Rf5c68::generate_chip_sample(int32_t& left, int32_t& right) {
    left = right = 0;
    if (!enable_) return;
    for (Channel& chan : chan_) {
        if (!chan.enable) continue;
        const int lv = (chan.pan & 0x0f) * chan.env;
        const int rv = ((chan.pan >> 4) & 0x0f) * chan.env;
        int sample = ram_[(chan.addr >> 11) & 0xffff];
        if (sample == 0xff) {
            chan.addr = uint32_t(chan.loopst) << 11;
            sample = ram_[(chan.addr >> 11) & 0xffff];
            if (sample == 0xff) continue;
        }
        chan.addr += chan.step;
        if (sample & 0x80) {
            sample &= 0x7f;
            left += (sample * lv) >> 5;
            right += (sample * rv) >> 5;
        } else {
            left -= (sample * lv) >> 5;
            right -= (sample * rv) >> 5;
        }
    }
    // RF5C68 output is 10-bit; shift up toward 16-bit like MAME's put_int_clamp.
    left <<= 6;
    right <<= 6;
    left = std::clamp(left, int32_t(-32768), int32_t(32767));
    right = std::clamp(right, int32_t(-32768), int32_t(32767));
}

int32_t Rf5c68::update() {
    sample_pos_ += sample_step_;
    while (sample_pos_ >= 1.0) {
        sample_pos_ -= 1.0;
        generate_chip_sample(last_left_, last_right_);
    }
    const double mix = double(last_left_ + last_right_) * 0.5 * double(amplitude_);
    return int32_t(std::lround(mix));
}

}  // namespace dsp
