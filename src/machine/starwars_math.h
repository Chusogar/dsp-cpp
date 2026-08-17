#pragma once

#include <array>
#include <cstdint>

namespace dsp {

// Star Wars matrix processor + restoring divider, from MAME starwars_m.cpp.
class StarwarsMath {
public:
    void init(const uint8_t* prom);
    void reset();
    void write(uint8_t offset, uint8_t data);
    uint8_t div_reh() const { return uint8_t(quotient_shift_ >> 8); }
    uint8_t div_rel() const { return uint8_t(quotient_shift_); }
    bool running() const { return run_cycles_ > 0; }
    void tick(int cpu_cycles);

    std::array<uint8_t, 0x1000>& ram() { return ram_; }
    const std::array<uint8_t, 0x1000>& ram() const { return ram_; }

private:
    static constexpr int kNop = 0x00;
    static constexpr int kLac = 0x01;
    static constexpr int kReadAcc = 0x02;
    static constexpr int kHalt = 0x04;
    static constexpr int kIncBic = 0x08;
    static constexpr int kClearAcc = 0x10;
    static constexpr int kLdc = 0x20;
    static constexpr int kLdb = 0x40;
    static constexpr int kLda = 0x80;

    void run_mproc();

    std::array<uint8_t, 0x1000> ram_{};
    std::array<uint8_t, 1024> prom_str_{};
    std::array<uint8_t, 1024> prom_mas_{};
    std::array<uint8_t, 1024> prom_am_{};

    int mpa_ = 0;
    int bic_ = 0;
    int16_t a_ = 0, b_ = 0, c_ = 0;
    int32_t acc_ = 0;
    uint16_t divisor_ = 0, dividend_ = 0, dvd_shift_ = 0, quotient_shift_ = 0;
    int run_cycles_ = 0;
};

}  // namespace dsp
