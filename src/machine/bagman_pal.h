#pragma once

#include <cstdint>

namespace dsp {

// PAL16R6 protection device used by Bagman (bagman_pal.pas / MAME bagmanpal).
class BagmanPal {
public:
    BagmanPal() { reset(); }

    void reset();
    void write(uint8_t position, uint8_t value);
    uint8_t read();
    void update();

private:
    uint8_t and_map_[64] = {};
    uint8_t column_value_[32] = {};
    uint8_t out_value_[8] = {};
};

}  // namespace dsp
