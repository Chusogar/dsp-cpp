#pragma once

#include <cstdint>

namespace dsp {

// 8-bit unsigned DAC used by the Sega N7751 speech path.
class Dac {
public:
    explicit Dac(float amp = 1.0f) : amp_(amp) {}

    void reset() { sample_ = 0; }
    void data8_w(uint8_t value) {
        sample_ = int32_t((int(value) - 0x80) * int(256.0f * amp_));
    }
    int32_t update() const { return sample_; }

private:
    float amp_ = 1.0f;
    int32_t sample_ = 0;
};

}  // namespace dsp
