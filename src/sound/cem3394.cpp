/***************************************************************************

    CEM3394 sound driver.

    Ported from the classic MAME 0.160-era cem3394.c. Very crude.

    Still to do (same as the original):
        - adjust the overall volume when multiple waves are being generated
        - filter internal sound
        - support resonance

***************************************************************************/

#include "sound/cem3394.h"

#include <cmath>
#include <cstring>

namespace dsp {
namespace {

constexpr int kEnablePulse = 1;
constexpr int kEnableTriangle = 1;
constexpr int kEnableSawtooth = 1;

// Pulse width shaping (matches classic MAME limits used by Bally/Sente titles).
constexpr int kLimitWidth = 1;
constexpr double kMinimumWidth = 0.25;
constexpr double kMaximumWidth = 0.75;

constexpr uint8_t kWaveTriangle = 1;
constexpr uint8_t kWaveSawtooth = 2;
constexpr uint8_t kWavePulse = 4;

constexpr int kFractionBits = 28;
constexpr uint32_t kFractionOne = 1u << kFractionBits;
constexpr double kFractionOneD = double(1u << kFractionBits);
constexpr uint32_t kFractionMask = kFractionOne - 1;

}  // namespace

Cem3394::Cem3394(uint32_t /*clock*/, double vco_zero_freq, double filter_zero_freq)
    : vco_zero_freq_(vco_zero_freq), filter_zero_freq_(filter_zero_freq) {
    reset();
}

void Cem3394::reset() {
    std::memset(values_, 0, sizeof(values_));
    wave_select_ = 0;
    volume_ = 0;
    mixer_internal_ = 0;
    mixer_external_ = 0;
    position_ = 0;
    step_ = 0;
    filter_step_ = 0;
    modulation_depth_ = 0;
    pulse_width_ = 0;
}

double Cem3394::compute_db(double voltage) {
    // assumes 0.0 == full off, 4.0 == full on, with linear taper
    if (voltage >= 4.0) return 0.0;
    if (voltage <= 0.0) return 90.0;
    if (voltage >= 2.5) return (4.0 - voltage) * (1.0 / 1.5) * 20.0;
    double temp = 20.0 * std::pow(2.0, 2.5 - voltage);
    if (temp < 90.0) return 90.0;
    return temp;
}

uint32_t Cem3394::compute_db_volume(double voltage) {
    double temp;
    if (voltage >= 4.0) return 256;
    if (voltage <= 0.0) return 0;
    if (voltage >= 2.5) {
        temp = (4.0 - voltage) * (1.0 / 1.5) * 20.0;
    } else {
        temp = 20.0 * std::pow(2.0, 2.5 - voltage);
        if (temp < 50.0) return 0;
    }
    return uint32_t(256.0 * std::pow(0.891251, temp));
}

void Cem3394::set_voltage(int input, double voltage) {
    if (input < 0 || input > FINAL_GAIN) return;
    if (voltage == values_[input]) return;
    values_[input] = voltage;

    switch (input) {
        case VCO_FREQUENCY: {
            // -4.0 .. +4.0, at 0.75 V/octave
            const double temp = vco_zero_freq_ * std::pow(2.0, -voltage * (1.0 / 0.75));
            step_ = uint32_t(temp * inv_sample_rate_ * kFractionOneD);
            break;
        }
        case WAVE_SELECT:
            wave_select_ &= uint8_t(~(kWaveTriangle | kWaveSawtooth));
            if (voltage >= -0.5 && voltage <= -0.2)
                wave_select_ |= kWaveTriangle;
            else if (voltage >= 0.9 && voltage <= 1.5)
                wave_select_ |= uint8_t(kWaveTriangle | kWaveSawtooth);
            else if (voltage >= 2.3 && voltage <= 3.9)
                wave_select_ |= kWaveSawtooth;
            break;
        case PULSE_WIDTH:
            // 0.0 == 0% duty, 2.0 == 100% duty
            if (voltage < 0.0) {
                pulse_width_ = 0;
                wave_select_ &= uint8_t(~kWavePulse);
            } else {
                double temp = voltage * 0.5;
                if (kLimitWidth) temp = kMinimumWidth + (kMaximumWidth - kMinimumWidth) * temp;
                pulse_width_ = uint32_t(temp * kFractionOneD);
                wave_select_ |= kWavePulse;
            }
            break;
        case FINAL_GAIN:
            volume_ = compute_db_volume(voltage);
            break;
        case MIXER_BALANCE:
            // 0.0 equal parts; positive favors external, negative internal
            if (voltage >= 0.0) {
                mixer_internal_ = compute_db_volume(3.55 - voltage);
                mixer_external_ = compute_db_volume(3.55 + 0.45 * (voltage * 0.25));
            } else {
                mixer_internal_ = compute_db_volume(3.55 - 0.45 * (voltage * 0.25));
                mixer_external_ = compute_db_volume(3.55 + voltage);
            }
            break;
        case FILTER_FREQUENCY: {
            // -3.0 .. +4.0, at 0.375 V/octave
            const double temp = filter_zero_freq_ * std::pow(2.0, -voltage * (1.0 / 0.375));
            filter_step_ = uint32_t(temp * inv_sample_rate_ * kFractionOneD);
            break;
        }
        case MODULATION_AMOUNT:
            // 0.01 at 0 V, 2.0 at 3.5 V
            if (voltage < 0.0)
                modulation_depth_ = uint32_t(0.01 * kFractionOneD);
            else if (voltage > 3.5)
                modulation_depth_ = uint32_t(2.00 * kFractionOneD);
            else
                modulation_depth_ =
                    uint32_t(((voltage * (1.0 / 3.5)) * 1.99 + 0.01) * kFractionOneD);
            break;
        case FILTER_RESONANCE:
            // Not implemented in the classic core.
            break;
        default:
            break;
    }
}

double Cem3394::get_parameter(int input) const {
    if (input < 0 || input > FINAL_GAIN) return 0.0;
    const double voltage = values_[input];

    switch (input) {
        case VCO_FREQUENCY:
            return vco_zero_freq_ * std::pow(2.0, -voltage * (1.0 / 0.75));
        case WAVE_SELECT:
            return voltage;
        case PULSE_WIDTH:
            if (voltage <= 0.0) return 0.0;
            if (voltage >= 2.0) return 1.0;
            return voltage * 0.5;
        case FINAL_GAIN:
            return compute_db(voltage);
        case MIXER_BALANCE:
            return voltage * 0.25;
        case MODULATION_AMOUNT:
            if (voltage < 0.0) return 0.01;
            if (voltage > 3.5) return 2.0;
            return (voltage * (1.0 / 3.5)) * 1.99 + 0.01;
        case FILTER_RESONANCE:
            if (voltage < 0.0) return 0.0;
            if (voltage > 2.5) return 1.0;
            return voltage * (1.0 / 2.5);
        case FILTER_FREQUENCY:
            return filter_zero_freq_ * std::pow(2.0, -voltage * (1.0 / 0.375));
        default:
            return 0.0;
    }
}

int16_t Cem3394::update() {
    int int_volume = int((volume_ * mixer_internal_) / 256);
    // External input is not wired in this port; keep the mixer math but silence it.
    const int ext_volume = 0;
    const uint32_t step = step_;

    // Crude "filter" volume tweak from the classic driver.
    if (step > filter_step_ && (step - filter_step_) != 0)
        int_volume /= int(step - filter_step_);

    if (int_volume == 0 && ext_volume == 0) return 0;

    int16_t mix = 0;
    // All enabled waveforms sample from the same starting phase, then the
    // phase advances once (matches the classic MAME batch generator).
    const uint32_t position = position_;
    uint32_t end_position = position_;

    if (int_volume != 0) {
        if (kEnablePulse && (wave_select_ & kWavePulse)) {
            const uint32_t pulse_width = pulse_width_;
            if (pulse_width >= step) {
                mix = (position < pulse_width) ? int16_t(0x1932) : int16_t(0);
                end_position = (position + step) & kFractionMask;
            } else {
                // Narrow pulse: emit a scaled blip only on cycle crossings.
                const int16_t volume =
                    step != 0 ? int16_t(int32_t(0x1932) * int32_t(pulse_width) / int32_t(step))
                              : int16_t(0);
                const uint32_t newposition = position + step;
                mix = ((newposition ^ position) & ~kFractionMask) ? volume : int16_t(0);
                end_position = newposition & kFractionMask;
            }
        }

        if (kEnableSawtooth && (wave_select_ & kWaveSawtooth)) {
            mix = int16_t(mix + int16_t(((position >> (kFractionBits - 14)) & 0x3fff) - 0x2000));
            end_position = (position + step) & kFractionMask;
        }

        if (kEnableTriangle && (wave_select_ & kWaveTriangle)) {
            int16_t value;
            if (position & (1u << (kFractionBits - 1)))
                value = int16_t(0x2000 - ((position >> (kFractionBits - 14)) & 0x1fff));
            else
                value = int16_t((position >> (kFractionBits - 14)) & 0x1fff);
            mix = int16_t(mix + value + (value >> 2));
            end_position = (position + step) & kFractionMask;
        }

        position_ = end_position;
    }

    return int16_t(int32_t(mix) * int_volume / 128);
}

}  // namespace dsp
