#include "sound/sn76496.h"

namespace dsp {
namespace {

constexpr int32_t kMaxOutput = 0x7fff;
constexpr int32_t kStep = 0x10000;
constexpr int32_t kWhiteNoise = 0x14002;
constexpr int32_t kPeriodicNoise = 0x8000;
constexpr uint32_t kNoisePreset = 0x0f35;

}  // namespace

SN76496::SN76496(uint32_t clock, float amplitude) : clock_(clock), amplitude_(amplitude) {
    set_gain(0);
    resample();
    reset();
}

void SN76496::set_gain(int gain) {
    // Increase the maximum output based on the gain (0.2 dB per step).
    double out = double(kMaxOutput) / 3.0;
    while (gain > 0) {
        gain--;
        out *= 1.023292992;
    }
    for (int index = 0; index < 15; index++) {  // volume table, 2 dB per step
        volume_table_[index] = float(out > (double(kMaxOutput) / 3.0) ? double(kMaxOutput) / 3.0
                                                                     : out);
        out /= 1.258925412;
    }
    volume_table_[15] = 0.0f;
}

void SN76496::resample() {
    // The tone generators run at clock/16, so this is the number of steps that
    // happen during a single output sample, in 16.16 fixed point.
    uint64_t tmp = uint64_t(kStep) * 16u * uint64_t(kSampleRate);
    update_step_ = uint32_t((tmp + clock_ / 2) / clock_);
}

void SN76496::reset() {
    for (int index = 0; index < 4; index++) {
        volume_[index] = 0.0f;
        registers_[index * 2] = 0;
        registers_[index * 2 + 1] = 0x0f;  // volume off
        output_[index] = 0;
        period_[index] = int32_t(update_step_);
        count_[index] = int32_t(update_step_);
    }
    last_register_ = 0;
    rng_ = kNoisePreset;
    output_[3] = uint8_t(rng_ & 1);
    noise_feedback_ = kPeriodicNoise;
}

void SN76496::write(uint8_t data) {
    int reg;
    if ((data & 0x80) != 0) {
        reg = (data & 0x70) >> 4;
        last_register_ = uint8_t(reg);
        registers_[reg] = uint16_t((registers_[reg] & 0x3f0) | (data & 0x0f));
    } else {
        reg = last_register_;
        if (reg == 0 || reg == 2 || reg == 4) {
            registers_[reg] = uint16_t((registers_[reg] & 0x0f) | ((data & 0x3f) << 4));
        } else {
            registers_[reg] = uint16_t((registers_[reg] & 0x3f0) | (data & 0x0f));
        }
    }

    int channel = reg / 2;
    switch (reg) {
        case 0:
        case 2:
        case 4:
            period_[channel] = int32_t(update_step_ * registers_[reg]);
            if (period_[channel] == 0) period_[channel] = int32_t(update_step_);
            if (reg == 4 && (registers_[6] & 0x03) == 0x03) period_[3] = period_[2] * 2;
            break;
        case 1:
        case 3:
        case 5:
        case 7: volume_[channel] = volume_table_[data & 0x0f]; break;
        case 6: {
            int noise = registers_[6];
            noise_feedback_ = (noise & 4) != 0 ? kWhiteNoise : kPeriodicNoise;
            noise &= 3;
            // N/512, N/1024, N/2048 or the output of tone #3.
            period_[3] = (noise == 3) ? period_[2] * 2 : int32_t(update_step_ << (5 + noise));
            rng_ = kNoisePreset;
            output_[3] = uint8_t(rng_ & 1);
            break;
        }
        default: break;
    }
}

int32_t SN76496::update() {
    // Silent channels only need their counter advanced.
    for (int index = 0; index < 4; index++) {
        if (volume_[index] == 0.0f && count_[index] <= kStep) count_[index] += kStep;
    }

    int32_t vol[4] = {0, 0, 0, 0};
    for (int index = 0; index < 3; index++) {
        if (output_[index] != 0) vol[index] += count_[index];
        count_[index] -= kStep;
        // period_[i] is the half period of the square wave: adding it twice keeps
        // the wave in the same phase, so vol[] accumulates the time spent high.
        while (count_[index] <= 0) {
            count_[index] += period_[index];
            if (count_[index] > 0) {
                output_[index] ^= 1;
                if (output_[index] != 0) vol[index] += period_[index];
                break;
            }
            count_[index] += period_[index];
            vol[index] += period_[index];
        }
        if (output_[index] != 0) vol[index] -= count_[index];
    }

    int32_t left = kStep;
    do {
        int32_t next_event = (count_[3] < left) ? count_[3] : left;
        if (output_[3] != 0) vol[3] += count_[3];
        count_[3] -= next_event;
        if (count_[3] <= 0) {
            if ((rng_ & 1) != 0) rng_ ^= uint32_t(noise_feedback_);
            rng_ >>= 1;
            output_[3] = uint8_t(rng_ & 1);
            count_[3] += period_[3];
            if (output_[3] != 0) vol[3] += period_[3];
        }
        if (output_[3] != 0) vol[3] -= count_[3];
        left -= next_event;
    } while (left != 0);

    double out = double(vol[0]) * volume_[0] + double(vol[1]) * volume_[1] +
                 double(vol[2]) * volume_[2] + double(vol[3]) * volume_[3];
    double limit = double(kMaxOutput) * kStep;
    if (out > limit) out = limit;
    if (out < -limit) out = -limit;
    return int32_t((out / kStep) * amplitude_);
}

}  // namespace dsp
