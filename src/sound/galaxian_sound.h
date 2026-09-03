#pragma once

#include <cstdint>

namespace dsp {

// Discrete Galaxian / Moon Cresta sound board (pitch, 3 background 555s,
// fire and hit/noise). Approximation of MAME galaxian_a.cpp — enough for
// the marching theme, shots and explosions.
class GalaxianSound {
public:
    static constexpr int kSampleRate = 44100;
    static constexpr uint32_t kSoundClock = 96000;

    void reset();

    void pitch_w(uint8_t data);
    void lfo_freq_w(int bit, uint8_t data);
    void sound_w(int offset, uint8_t data);

    int16_t update();

private:
    uint8_t pitch_ = 0xff;
    uint8_t lfo_ = 0;
    bool fs_[3] = {};
    bool hit_ = false;
    bool fire_ = false;
    bool vol1_ = false;
    bool vol2_ = false;

    uint32_t pitch_acc_ = 0;
    uint8_t pitch_div_ = 0;
    uint32_t lfo_acc_[3] = {};
    bool lfo_out_[3] = {};
    uint32_t noise_ = 0x1;
    int32_t hit_env_ = 0;
};

}  // namespace dsp
