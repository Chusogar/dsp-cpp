#include "sound/huc6280_psg.h"
#include <algorithm>

namespace dsp {
namespace {
constexpr int kVolTable[32] = {
    0, 1, 1, 2, 2, 3, 3, 4, 5, 6, 7, 8, 10, 12, 14, 16,
    19, 23, 27, 32, 38, 45, 53, 64, 76, 90, 107, 128, 152, 180, 214, 255
};
}  // namespace

HuC6280Psg::HuC6280Psg(uint32_t clock) : clock_(clock ? clock : 7159090) { reset(); }

void HuC6280Psg::reset() {
    select_ = 0;
    main_balance_ = 0xFF;
    lfo_freq_ = lfo_control_ = 0;
    lfo_phase_ = 0;
    for (auto& c : ch_) {
        c = Channel{};
        c.balance = 0xFF;
        c.noise_lfsr = 1;
    }
    accum_ = 0;
}

void HuC6280Psg::write(uint8_t port, uint8_t value) {
    switch (port & 0x0F) {
        case 0x00: select_ = value & 7; break;
        case 0x01: main_balance_ = value; break;
        case 0x02:
            if (select_ < kChannels)
                ch_[select_].frequency = uint16_t((ch_[select_].frequency & 0x0F00) | value);
            break;
        case 0x03:
            if (select_ < kChannels)
                ch_[select_].frequency = uint16_t((ch_[select_].frequency & 0x00FF) | ((value & 0x0F) << 8));
            break;
        case 0x04:
            if (select_ < kChannels) {
                auto& c = ch_[select_];
                if ((value & 0x40) && !(c.control & 0x40)) c.wave_index = 0;
                c.control = value;
            }
            break;
        case 0x05:
            if (select_ < kChannels) ch_[select_].balance = value;
            break;
        case 0x06:
            if (select_ < kChannels) {
                auto& c = ch_[select_];
                if (c.control & 0x40) c.dda_out = value & 0x1F;
                else {
                    c.wave[c.wave_index & 31] = value & 0x1F;
                    c.wave_index = (c.wave_index + 1) & 31;
                }
            }
            break;
        case 0x07:
            if (select_ < kChannels) ch_[select_].noise_ctrl = value;
            break;
        case 0x08: lfo_freq_ = value; break;
        case 0x09:
            lfo_control_ = value;
            if (value & 0x80) lfo_phase_ = 0;
            break;
        default: break;
    }
}

int16_t HuC6280Psg::volume_scale(int sample, int ch_vol, int bal_nibble) const {
    int v = kVolTable[ch_vol & 31];
    int b = kVolTable[(bal_nibble & 0x0F) * 2 + 1];
    int32_t o = sample * v * b / 64;
    if (o > 32767) o = 32767;
    if (o < -32768) o = -32768;
    return int16_t(o);
}

void HuC6280Psg::update(int cpu_cycles, std::vector<int16_t>& out) {
    if (cpu_cycles <= 0) return;
    accum_ += int64_t(cpu_cycles) * kSampleRate;
    while (accum_ >= int64_t(clock_)) {
        accum_ -= int64_t(clock_);
        int lfo_mod = 0;
        if ((lfo_control_ & 3) != 0) {
            const int shift = (lfo_control_ & 3) == 1 ? 0 : (lfo_control_ & 3) == 2 ? 2 : 4;
            const uint32_t lfo_period = (uint32_t(lfo_freq_) + 1) << 18;
            lfo_phase_ += uint32_t(clock_ / kSampleRate);
            if (lfo_period) lfo_phase_ %= lfo_period;
            const int idx = int((lfo_phase_ * 32) / (lfo_period ? lfo_period : 1)) & 31;
            lfo_mod = (int(ch_[1].wave[idx]) - 16) << shift;
        }
        int32_t left = 0, right = 0;
        for (int i = 0; i < kChannels; ++i) {
            auto& c = ch_[i];
            if (!(c.control & 0x80)) continue;
            const int ch_vol = c.control & 0x1F;
            int sample = 0;
            if (c.control & 0x40) {
                sample = int(c.dda_out) - 16;
            } else if ((c.noise_ctrl & 0x80) && i >= 4) {
                const int nfreq = (c.noise_ctrl & 0x1F) ^ 0x1F;
                c.phase += uint32_t(0x1000 >> std::min(nfreq, 12));
                if (c.phase >= 0x10000) {
                    c.phase &= 0xFFFF;
                    const uint32_t bit = (c.noise_lfsr ^ (c.noise_lfsr >> 1)) & 1;
                    c.noise_lfsr = (c.noise_lfsr >> 1) | (bit << 11);
                }
                sample = (c.noise_lfsr & 1) ? 15 : -16;
            } else {
                int freq = int(c.frequency);
                if (i == 0) freq = std::max(0, freq + lfo_mod);
                if (freq == 0) sample = int(c.wave[0]) - 16;
                else {
                    c.phase += 0x100000u / uint32_t(std::max(1, freq));
                    sample = int(c.wave[(c.phase >> 12) & 31]) - 16;
                }
            }
            const int ml = (main_balance_ >> 4) & 0x0F, mr = main_balance_ & 0x0F;
            const int cl = (c.balance >> 4) & 0x0F, cr = c.balance & 0x0F;
            left += volume_scale(sample, ch_vol, std::min(ml, cl));
            right += volume_scale(sample, ch_vol, std::min(mr, cr));
        }
        if (left > 32767) left = 32767;
        if (left < -32768) left = -32768;
        if (right > 32767) right = 32767;
        if (right < -32768) right = -32768;
        out.push_back(int16_t((left + right) / 2));
    }
}

}  // namespace dsp
