#include "sound/msm5205.h"

#include <array>
#include <cmath>

namespace dsp {
namespace {

const std::array<int, 8> kIndexShift = {-1, -1, -1, -1, 2, 4, 6, 8};

// diff_lookup in msm5205.pas: amplitude change for every (step, nibble) pair.
const std::array<int, 49 * 16>& diff_table() {
    static const std::array<int, 49 * 16> table = [] {
        static const int nibble_to_bit[16][4] = {
            {1, 0, 0, 0},  {1, 0, 0, 1},  {1, 0, 1, 0},  {1, 0, 1, 1},
            {1, 1, 0, 0},  {1, 1, 0, 1},  {1, 1, 1, 0},  {1, 1, 1, 1},
            {-1, 0, 0, 0}, {-1, 0, 0, 1}, {-1, 0, 1, 0}, {-1, 0, 1, 1},
            {-1, 1, 0, 0}, {-1, 1, 0, 1}, {-1, 1, 1, 0}, {-1, 1, 1, 1}};
        std::array<int, 49 * 16> values{};
        for (int step = 0; step <= 48; step++) {
            int step_value = int(std::floor(16.0 * std::pow(11.0 / 10.0, step)));
            for (int nibble = 0; nibble < 16; nibble++) {
                const int* bits = nibble_to_bit[nibble];
                values[size_t(step * 16 + nibble)] =
                    bits[0] * (step_value * bits[1] + (step_value >> 1) * bits[2] +
                               (step_value >> 2) * bits[3] + (step_value >> 3));
            }
        }
        return values;
    }();
    return table;
}

}  // namespace

MSM5205::MSM5205(uint32_t clock, int prescaler, int bits)
    : clock_(clock), prescaler_(prescaler), bits_(bits) {
    reset();
}

void MSM5205::reset() {
    position_ = 0;
    end_ = 0;
    data_value_ = -1;
    reset_ = true;
    idle_ = true;
    signal_ = 0;
    step_ = 0;
}

void MSM5205::set_reset(bool state) {
    reset_ = state;
    idle_ = state;
    // The chip zeroes its accumulator on the first clock with reset held, and
    // idle chips are not clocked any more, so do it right away.
    if (state) {
        signal_ = 0;
        step_ = 0;
        data_value_ = -1;
    }
}

void MSM5205::decode(uint8_t nibble) {
    uint8_t data = bits_ == 4 ? uint8_t(nibble & 0x0f) : uint8_t((nibble & 0x07) << 1);
    if (reset_) {
        signal_ = 0;
        step_ = 0;
        data_value_ = -1;
        return;
    }
    signal_ += diff_table()[size_t(step_ * 16 + (data & 0x0f))];
    if (signal_ > 2047) signal_ = 2047;
    else if (signal_ < -2048) signal_ = -2048;
    step_ += kIndexShift[size_t(data & 0x07)];
    if (step_ > 48) step_ = 48;
    else if (step_ < 0) step_ = 0;
}

void MSM5205::vclk() {
    if (idle_) return;
    if (data_value_ != -1) {
        decode(uint8_t(data_value_ & 0x0f));
        data_value_ = -1;
        position_++;
        if (position_ >= end_ || position_ >= rom_.size()) set_reset(true);
    } else {
        data_value_ = position_ < rom_.size() ? rom_[position_] : 0;
        decode(uint8_t(data_value_ >> 4));
    }
}

}  // namespace dsp
