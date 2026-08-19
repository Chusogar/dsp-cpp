#include "sound/polepos_engine.h"

#include <algorithm>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace dsp {
namespace {

constexpr double kR166 = 1000.0;
constexpr double kR167 = 2200.0;
constexpr double kR168 = 4700.0;
constexpr double kR166Shunt = 1.0 / (1.0 / kR166 + 1.0 / 250.0);
constexpr double kR167Shunt = 1.0 / (1.0 / kR166 + 1.0 / 250.0);
constexpr double kR168Shunt = 1.0 / (1.0 / kR166 + 1.0 / 250.0);

const double kVolumeTable[8] = {
    (kR168Shunt + kR167Shunt + kR166Shunt + 2200) / 10000,
    (kR168Shunt + kR167Shunt + kR166 + 2200) / 10000,
    (kR168Shunt + kR167 + kR166Shunt + 2200) / 10000,
    (kR168Shunt + kR167 + kR166 + 2200) / 10000,
    (kR168 + kR167Shunt + kR166Shunt + 2200) / 10000,
    (kR168 + kR167Shunt + kR166 + 2200) / 10000,
    (kR168 + kR167 + kR166Shunt + 2200) / 10000,
    (kR168 + kR167 + kR166 + 2200) / 10000,
};

constexpr double kRFiltOut[3] = {4700.0, 7500.0, 10000.0};
constexpr double kRFiltTotal = 1.0 / (1.0 / 4700.0 + 1.0 / 7500.0 + 1.0 / 10000.0);

constexpr int kFilterLowpass = 0;
constexpr int kFilterHighpass = 1;
constexpr int kFilterBandpass = 2;

double res_k(double x) { return x * 1000.0; }
double cap_u(double x) { return x * 1.0e-6; }

}  // namespace

void PolePosEngine::Filter2::setup(int type, double fc, double d, double gain) {
    const double sample_rate = double(kSampleRate);
    const double two_over_t = 2 * sample_rate;
    const double two_over_t_sq = two_over_t * two_over_t;
    const double w = sample_rate * 2.0 * std::tan(M_PI * fc / sample_rate);
    const double w_sq = w * w;
    const double den = two_over_t_sq + d * w * two_over_t + w_sq;
    a1 = 2.0 * (-two_over_t_sq + w_sq) / den;
    a2 = (two_over_t_sq - d * w * two_over_t + w_sq) / den;
    switch (type) {
        case kFilterLowpass:
            b0 = b2 = w_sq / den;
            b1 = 2.0 * b0;
            break;
        case kFilterBandpass:
            b0 = d * w * two_over_t / den;
            b1 = 0.0;
            b2 = -b0;
            break;
        case kFilterHighpass:
        default:
            b0 = b2 = two_over_t_sq / den;
            b1 = -2.0 * b0;
            break;
    }
    b0 *= gain;
    b1 *= gain;
    b2 *= gain;
}

void PolePosEngine::Filter2::opamp_m_bandpass(double r1, double r2, double r3, double c1,
                                              double c2) {
    if (r1 == 0) return;
    double r_in, gain;
    if (r2 == 0) {
        gain = 1;
        r_in = r1;
    } else {
        gain = r2 / (r1 + r2);
        r_in = 1.0 / (1.0 / r1 + 1.0 / r2);
    }
    const double fc = 1.0 / (2 * M_PI * std::sqrt(r_in * r3 * c1 * c2));
    const double d = (c1 + c2) / std::sqrt(r3 / r_in * c1 * c2);
    gain *= -r3 / r_in * c2 / (c1 + c2);
    setup(kFilterBandpass, fc, d, gain);
}

void PolePosEngine::Filter2::reset_state() {
    x0 = x1 = x2 = 0;
    y0 = y1 = y2 = 0;
}

void PolePosEngine::Filter2::step() {
    y0 = -a1 * y1 - a2 * y2 + b0 * x0 + b1 * x1 + b2 * x2;
    x2 = x1;
    x1 = x0;
    y2 = y1;
    y1 = y0;
}

PolePosEngine::PolePosEngine(uint32_t clock) : clock_(clock) {
    filter_[0].opamp_m_bandpass(res_k(220), res_k(33), res_k(390), cap_u(0.01), cap_u(0.01));
    filter_[1].opamp_m_bandpass(res_k(150), res_k(22), res_k(330), cap_u(0.0047), cap_u(0.0047));
    filter_[2].setup(kFilterHighpass, 950, 1.0 / 0.707, 1);
    reset();
}

void PolePosEngine::set_samples(const uint8_t* data, size_t size) {
    samples_.assign(data, data + size);
}

void PolePosEngine::reset() {
    position_ = 0;
    sample_msb_ = 0;
    sample_lsb_ = 0;
    sample_enable_ = false;
    for (auto& f : filter_) f.reset_state();
}

void PolePosEngine::clson(bool enabled) {
    if (!enabled) {
        lsb_w(0);
        msb_w(0);
    }
}

void PolePosEngine::lsb_w(uint8_t data) {
    sample_lsb_ = data & 62;
    sample_enable_ = (data & 1) != 0;
}

void PolePosEngine::msb_w(uint8_t data) { sample_msb_ = data & 63; }

int32_t PolePosEngine::update() {
    if (!sample_enable_ || samples_.empty()) return 0;
    const uint32_t clock =
        uint32_t((uint64_t(clock_ / 16) * uint64_t((sample_msb_ + 1) * 64 + sample_lsb_ + 1)) /
                 (64u * 64u));
    const uint32_t step = uint32_t((uint64_t(clock) << 12) / uint32_t(kSampleRate));
    const int slot = (sample_msb_ >> 3) & 7;
    const double volume = kVolumeTable[slot];
    const size_t base = size_t(slot) * 0x800;
    uint8_t sample = 0x80;
    if (base < samples_.size()) {
        const size_t index = base + ((position_ >> 12) & 0x7ff);
        if (index < samples_.size()) sample = samples_[index];
    }
    const double x = (3.4 / 255.0 * double(sample) - 2.0) * volume;
    double i_total = 0;
    for (int i = 0; i < 3; i++) {
        filter_[i].x0 = x;
        filter_[i].step();
        if (filter_[i].y0 > 1.5) filter_[i].y0 = 1.5;
        if (filter_[i].y0 < -2) filter_[i].y0 = -2;
        i_total += filter_[i].y0 / kRFiltOut[i];
    }
    i_total *= kRFiltTotal * 32000.0 / 2.0;
    position_ += step;
    return int32_t(i_total);
}

}  // namespace dsp
