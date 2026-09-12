#include "sound/saa1099.h"

#include <cmath>

namespace dsp {

Saa1099::Saa1099(uint32_t clock) : clock_(clock) { reset(); }

void Saa1099::reset() {
    addr_ = 0;
    enabled_ = false;
    channels_.fill(Channel{});
    noise_.fill(Noise{});
    for (auto& n : noise_) n.lfsr = 0x1ffff;
    envelope_.fill(Envelope{});
}

double Saa1099::channel_freq_hz(const Channel& ch) const {
    // SAA1099 tone generator: period (in master clocks) = 2 * (256 - freq) *
    // 2^(8-octave); higher octave and higher frequency code both raise
    // pitch. This matches the chip's documented ~30 Hz - 30 kHz range at
    // its usual ~8 MHz clock closely enough for game/BASIC SOUND use, even
    // if not bit-exact to silicon.
    const double period = 2.0 * double(256 - int(ch.frequency)) * double(1u << (8 - ch.octave));
    if (period <= 0.0) return 0.0;
    return double(clock_) / period;
}

void Saa1099::write(uint8_t value) {
    const uint8_t reg = addr_;
    if (reg <= 0x05) {
        channels_[reg].amplitude = value;
    } else if (reg >= 0x08 && reg <= 0x0d) {
        channels_[reg - 0x08].frequency = value;
    } else if (reg >= 0x10 && reg <= 0x12) {
        const int base = (reg - 0x10) * 2;
        channels_[size_t(base)].octave = value & 7;
        channels_[size_t(base + 1)].octave = (value >> 4) & 7;
    } else if (reg == 0x14) {
        for (int i = 0; i < 6; ++i) channels_[size_t(i)].tone_enable = (value >> i) & 1;
    } else if (reg == 0x15) {
        for (int i = 0; i < 6; ++i) channels_[size_t(i)].noise_enable = (value >> i) & 1;
    } else if (reg == 0x16) {
        noise_[0].freq_select = value & 3;
        noise_[1].freq_select = (value >> 4) & 3;
    } else if (reg == 0x18 || reg == 0x19) {
        Envelope& e = envelope_[reg - 0x18];
        e.waveform = (value >> 1) & 7;
        e.resolution4bit = (value >> 4) & 1;
        e.invert_right = (value >> 5) & 1;
        e.enabled = (value >> 7) & 1;
    } else if (reg == 0x1c) {
        enabled_ = (value & 1) != 0;
        if (!enabled_) {
            for (auto& ch : channels_) ch.phase = 0.0;
            for (auto& n : noise_) n.phase = 0.0;
        }
    }
}

void Saa1099::update(int32_t& left, int32_t& right) {
    left = 0;
    right = 0;
    if (!enabled_) return;

    // Advance the two noise generators (17-bit Galois LFSR), each clocked
    // at a rate selected by its freq_select (approximation: fixed divisors
    // of the master clock, matching the chip's four selectable rates).
    static const double kNoiseDivisors[4] = {256.0, 512.0, 1024.0, 2048.0};
    for (auto& n : noise_) {
        const double step = double(clock_) / kNoiseDivisors[n.freq_select] / double(kSampleRate);
        n.phase += step;
        while (n.phase >= 1.0) {
            n.phase -= 1.0;
            const uint32_t bit = ((n.lfsr >> 0) ^ (n.lfsr >> 2)) & 1u;
            n.lfsr = (n.lfsr >> 1) | (bit << 16);
            n.level = (n.lfsr & 1) != 0;
        }
    }

    for (int i = 0; i < 6; ++i) {
        Channel& ch = channels_[size_t(i)];
        if (!ch.tone_enable && !ch.noise_enable) continue;

        bool level = true;
        if (ch.tone_enable) {
            const double freq = channel_freq_hz(ch);
            const double step = freq / double(kSampleRate);
            ch.phase += step;
            while (ch.phase >= 1.0) ch.phase -= 1.0;
            level = ch.phase < 0.5;
        }
        if (ch.noise_enable) level = level && noise_[size_t(i / 3)].level;
        if (!level) continue;

        const int left_amp = (ch.amplitude >> 4) & 0xf;
        const int right_amp = ch.amplitude & 0xf;
        // Each of 16 levels contributes roughly an equal share so six full
        // -volume channels don't clip too aggressively when mixed.
        left += left_amp * (8000 / 15) / 6;
        right += right_amp * (8000 / 15) / 6;
    }
}

}  // namespace dsp
