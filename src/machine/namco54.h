#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "cpu/mb88xx.h"

namespace dsp {

// Namco 54xx: MB8844 noise/tone generator with three 4-bit DACs.
class Namco54xx {
public:
    explicit Namco54xx(uint32_t clock);

    bool load_rom(const std::vector<uint8_t>& rom, std::string* error);

    void reset();
    void set_reset(bool running);
    void set_chip_select(bool asserted);
    void write(uint8_t data);
    void run(int cycles);

    uint8_t chanl1() const { return chanl1_; }  // R4-R7
    uint8_t chanl2() const { return chanl2_; }  // O4-O7
    uint8_t chanl3() const { return chanl3_; }  // O0-O3
    uint16_t debug_pc() const { return cpu_.pc(); }

private:
    uint8_t k_r() const { return uint8_t(latched_cmd_ >> 4); }
    uint8_t r0_r() const { return uint8_t(latched_cmd_ & 0x0f); }
    void o_w(uint8_t data);
    void r1_w(uint8_t data);

    Mb88 cpu_;
    uint8_t latched_cmd_ = 0;
    uint8_t chanl1_ = 0;
    uint8_t chanl2_ = 0;
    uint8_t chanl3_ = 0;
};

}  // namespace dsp
