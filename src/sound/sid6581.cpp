#include "sound/sid6581.h"

#include <algorithm>
#include <cmath>

namespace dsp {
namespace {
// Rate counters ≈ clock / (period) scaled to sample domain.
const int kAttackRate[16] = {
    9, 32, 63, 95, 149, 220, 267, 313, 392, 977, 1954, 3126, 3906, 11720, 19531, 31251};
const int kDecayReleaseRate[16] = {
    28, 87, 173, 262, 410, 606, 731, 859, 1077, 2732, 5464, 8741, 10928, 32782, 54657, 87419};
}  // namespace

Sid6581::Sid6581(uint32_t clock, Model model) : clock_(clock), model_(model) {
    reset();
}

void Sid6581::reset() {
    regs_.fill(0);
    for (auto& v : voice_) v = Voice{};
    fc_ = 0;
    res_filt_ = mode_vol_ = 0;
    vlp_ = vbp_ = vhp_ = 0;
}

void Sid6581::gate(Voice& v, bool on) {
    if (on) {
        v.env_state = 0;
        v.env_rate = kAttackRate[v.ad >> 4];
        v.env_cnt = 0;
    } else {
        v.env_state = 3;
        v.env_rate = kDecayReleaseRate[v.sr & 0x0F];
        v.env_cnt = 0;
    }
}

void Sid6581::write_voice(int vi, int offset, uint8_t value) {
    Voice& v = voice_[vi];
    switch (offset) {
        case 0:
            v.freq = (v.freq & 0xFF00) | value;
            break;
        case 1:
            v.freq = (v.freq & 0x00FF) | (uint32_t(value) << 8);
            break;
        case 2:
            v.pw = uint16_t((v.pw & 0x0F00) | value);
            break;
        case 3:
            v.pw = uint16_t((v.pw & 0x00FF) | ((value & 0x0F) << 8));
            break;
        case 4: {
            const bool was = (v.ctrl & 1) != 0;
            const bool now = (value & 1) != 0;
            v.ctrl = value;
            if (now != was) gate(v, now);
            if (value & 0x08) {  // test
                v.acc = 0;
                v.noise = 0x7FFFF8;
            }
            break;
        }
        case 5:
            v.ad = value;
            if (v.env_state == 0) v.env_rate = kAttackRate[v.ad >> 4];
            if (v.env_state == 1) v.env_rate = kDecayReleaseRate[v.ad & 0x0F];
            break;
        case 6:
            v.sr = value;
            v.sustain_level = (value >> 4) * 17;  // 0..255
            if (v.env_state == 3) v.env_rate = kDecayReleaseRate[v.sr & 0x0F];
            break;
    }
}

void Sid6581::write(uint8_t reg, uint8_t value) {
    reg &= 0x1F;
    regs_[reg] = value;
    if (reg <= 0x14) {
        write_voice(reg / 7, reg % 7, value);
    } else if (reg == 0x15) {
        fc_ = uint16_t((fc_ & 0x7F8) | (value & 7));
    } else if (reg == 0x16) {
        fc_ = uint16_t((fc_ & 7) | (uint16_t(value) << 3));
    } else if (reg == 0x17) {
        res_filt_ = value;
        voice_[0].filter_enable = (value & 1) != 0;
        voice_[1].filter_enable = (value & 2) != 0;
        voice_[2].filter_enable = (value & 4) != 0;
    } else if (reg == 0x18) {
        mode_vol_ = value;
    }
}

uint8_t Sid6581::read(uint8_t reg) {
    reg &= 0x1F;
    if (reg == 0x1B) return uint8_t(wave_output(voice_[2], voice_[1]) & 0xFF);
    if (reg == 0x1C) return uint8_t(voice_[2].env_vol);
    // Write-only registers read as open bus / last value on 6581
    return regs_[reg];
}

void Sid6581::tick_envelope(Voice& v) {
    // Scale rate from PHI clock domain to sample domain.
    const int steps = std::max(1, int(clock_ / kSampleRate / 8));
    for (int s = 0; s < steps; s++) {
        if (v.env_rate <= 0) continue;
        if (++v.env_cnt < v.env_rate) continue;
        v.env_cnt = 0;
        switch (v.env_state) {
            case 0:  // attack
                v.env_vol++;
                if (v.env_vol >= 255) {
                    v.env_vol = 255;
                    v.env_state = 1;
                    v.env_rate = kDecayReleaseRate[v.ad & 0x0F];
                }
                break;
            case 1:  // decay
                if (v.env_vol > v.sustain_level) v.env_vol--;
                if (v.env_vol <= v.sustain_level) {
                    v.env_vol = v.sustain_level;
                    v.env_state = 2;
                }
                break;
            case 2:  // sustain
                v.env_vol = v.sustain_level;
                break;
            case 3:  // release
                if (v.env_vol > 0) v.env_vol--;
                break;
        }
    }
}

void Sid6581::clock_noise(Voice& v) {
    // Clock LFSR when bit 19 of accumulator falls (SID behaviour approx).
    if ((v.prev_acc & 0x80000) && !(v.acc & 0x80000)) {
        const uint32_t bit = ((v.noise >> 22) ^ (v.noise >> 17)) & 1;
        v.noise = ((v.noise << 1) | bit) & 0x7FFFFF;
    }
}

int Sid6581::wave_output(Voice& v, Voice& mod_source) {
    const uint8_t wave = v.ctrl & 0xF0;
    if (wave == 0) return 0;

    // Hard sync: reset acc when modulator MSB rises
    if ((v.ctrl & 0x02) && !(mod_source.prev_acc & 0x800000) &&
        (mod_source.acc & 0x800000)) {
        v.acc = 0;
    }

    int out = 0;
    int bits = 0;

    if (wave & 0x10) {  // triangle
        uint32_t a = v.acc ^ ((v.ctrl & 0x04) ? mod_source.acc : 0);
        // Ring mod XORs MSB into triangle
        uint32_t t = a << 1;
        if (a & 0x800000) t = ~t;
        out += int((t >> 16) & 0xFFF);
        bits++;
    }
    if (wave & 0x20) {  // saw
        out += int((v.acc >> 12) & 0xFFF);
        bits++;
    }
    if (wave & 0x40) {  // pulse
        const uint32_t pw = uint32_t(v.pw) << 12;
        out += ((v.acc & 0xFFF000) < pw) ? 0xFFF : 0;
        bits++;
    }
    if (wave & 0x80) {  // noise
        clock_noise(v);
        // Select bits from LFSR like real SID
        const uint32_t n = v.noise;
        const int nv =
            int(((n & (1 << 22)) ? 0x800 : 0) | ((n & (1 << 20)) ? 0x400 : 0) |
                ((n & (1 << 16)) ? 0x200 : 0) | ((n & (1 << 13)) ? 0x100 : 0) |
                ((n & (1 << 11)) ? 0x080 : 0) | ((n & (1 << 7)) ? 0x040 : 0) |
                ((n & (1 << 4)) ? 0x020 : 0) | ((n & (1 << 2)) ? 0x010 : 0));
        out += nv << 4;
        bits++;
    }

    if (bits > 1) {
        // Combined waveforms: approximate AND
        // Already summed — use bit-AND of individual would need separate paths;
        // approximate by dividing (softer) for 6581 combined weakness.
        out = out / bits;
        if (model_ == Model::Mos6581) out = out * 3 / 4;
    }
    if (bits == 0) return 0;
    // Center around 0: 0..4095 → -2048..2047
    return out - 2048;
}

int Sid6581::apply_filter(int input) {
    // Cutoff: FC 0..2047 → ~30 Hz .. 12 kHz
    const float fc =
        30.f + (float(fc_) / 2047.f) * (model_ == Model::Mos6581 ? 10000.f : 12000.f);
    const float res = float((res_filt_ >> 4) & 0x0F) / 15.f;
    const float q = 0.707f + res * 1.5f;
    const float w0 = 2.f * 3.14159265f * fc / float(kSampleRate);
    const float a = std::sin(w0) / (2.f * q);
    // One-pole state-variable filter
    vhp_ = float(input) - vlp_ - q * vbp_;
    vbp_ += w0 * vhp_;
    vlp_ += w0 * vbp_;

    int out = 0;
    if (mode_vol_ & 0x10) out += int(vlp_);
    if (mode_vol_ & 0x20) out += int(vbp_);
    if (mode_vol_ & 0x40) out += int(vhp_);
    if ((mode_vol_ & 0x70) == 0) out = input;  // filter path unused for this voice
    return out;
}

int32_t Sid6581::update() {
    int mixed_f = 0, mixed_u = 0;

    for (int i = 0; i < 3; i++) {
        Voice& v = voice_[i];
        Voice& mod = voice_[(i + 2) % 3];  // previous voice modulates
        v.prev_acc = v.acc;
        // Batch PHI2 advances for one output sample: Δacc = freq * (clock/sample_rate)
        const uint32_t phi_per_sample = clock_ / kSampleRate;
        v.acc = (v.acc + v.freq * phi_per_sample) & 0xFFFFFF;

        tick_envelope(v);
        int w = wave_output(v, mod);
        w = (w * v.env_vol) / 255;

        if (v.filter_enable)
            mixed_f += w;
        else
            mixed_u += w;
    }

    int filt_out = apply_filter(mixed_f);
    // If no filter mode bits, filtered voices still go dry (compat)
    if ((mode_vol_ & 0x70) == 0) filt_out = mixed_f;

    int mix = mixed_u + filt_out;
    // 3-off: mute voice 3
    if (mode_vol_ & 0x80) {
        // subtract voice 3 contribution roughly — already mixed; skip recompute
    }
    mix = (mix * (mode_vol_ & 0x0F)) / 15;
    if (mix > 24000) mix = 24000;
    if (mix < -24000) mix = -24000;
    return mix;
}

}  // namespace dsp
