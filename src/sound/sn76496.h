#pragma once

#include <cstdint>

namespace dsp {

// Texas Instruments SN76496 PSG, ported from sn_76496.pas.
class SN76496 {
public:
    static constexpr int kSampleRate = 44100;

    explicit SN76496(uint32_t clock, float amplitude = 1.0f);

    void reset();
    void write(uint8_t data);

    // Generates the next sample (sample rate is kSampleRate).
    int32_t update();

private:
    void set_gain(int gain);
    void resample();

    uint32_t clock_;
    float amplitude_;
    uint32_t update_step_ = 0;
    float volume_table_[16] = {};
    uint16_t registers_[8] = {};
    uint8_t last_register_ = 0;
    float volume_[4] = {};
    int32_t period_[4] = {};
    int32_t count_[4] = {};
    uint8_t output_[4] = {};
    uint32_t rng_ = 0;
    int32_t noise_feedback_ = 0;
};

}  // namespace dsp
