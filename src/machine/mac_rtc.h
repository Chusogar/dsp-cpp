#pragma once

#include <array>
#include <cstdint>

namespace dsp {

// Apple 343-0042 1-bit RTC / PRAM hanging off VIA PB0/PB1/PB2.
class MacRtc {
public:
    void reset();
    void ce_w(bool level);
    void clk_w(bool level);
    void data_w(bool level);
    bool data_r() const;

    // Advance the one-second clock. Returns true if the 1 Hz line should toggle.
    bool tick_seconds();

    uint8_t peek(int address) const { return mem_[std::size_t(address) & 31]; }

private:
    std::array<uint8_t, 32> mem_{};
    uint32_t seconds_ = 0;
    bool ce_ = false;
    bool clk_ = false;
    bool data_in_ = true;
    bool data_out_ = true;
    int bit_ = 0;
    int state_ = 0;  // 0 command, 1 write data, 2 read data
    uint8_t cmd_ = 0;
    uint8_t data_ = 0;
};

}  // namespace dsp
