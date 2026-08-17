#pragma once

#include <cstdint>
#include <vector>

namespace dsp {

// Pole Position engine sample player with the three analog filters from MAME
// src/mame/audio/polepos.cpp.
class PolePosEngine {
public:
    static constexpr int kSampleRate = 44100;

    explicit PolePosEngine(uint32_t clock);

    void set_samples(const uint8_t* data, size_t size);
    void reset();
    void clson(bool enabled);
    void lsb_w(uint8_t data);
    void msb_w(uint8_t data);

    int32_t update();

private:
    struct Filter2 {
        double x0 = 0, x1 = 0, x2 = 0;
        double y0 = 0, y1 = 0, y2 = 0;
        double a1 = 0, a2 = 0;
        double b0 = 0, b1 = 0, b2 = 0;

        void setup(int type, double fc, double d, double gain);
        void opamp_m_bandpass(double r1, double r2, double r3, double c1, double c2);
        void reset_state();
        void step();
    };

    uint32_t clock_;
    uint32_t position_ = 0;
    int sample_msb_ = 0;
    int sample_lsb_ = 0;
    bool sample_enable_ = false;
    std::vector<uint8_t> samples_;
    Filter2 filter_[3];
};

}  // namespace dsp
