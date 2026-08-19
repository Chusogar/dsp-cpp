#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "cpu/mb88xx.h"

namespace dsp {

// Namco 52xx: MB8843 sample player. P0-P3 is a 4-bit DAC.
class Namco52xx {
public:
    using RomRead = std::function<uint8_t(uint16_t)>;
    using SiRead = std::function<int()>;

    explicit Namco52xx(uint32_t clock);

    bool load_rom(const std::vector<uint8_t>& rom, std::string* error);
    void set_rom_read(RomRead handler) { rom_read_ = std::move(handler); }
    void set_si_read(SiRead handler) { si_read_ = std::move(handler); }

    void reset();
    void set_reset(bool running);
    void set_chip_select(bool asserted);
    void write(uint8_t data);
    void run(int cycles);

    uint8_t dac() const { return dac_; }
    uint16_t debug_pc() const { return cpu_.pc(); }

private:
    uint8_t k_r() const { return uint8_t(latched_cmd_ & 0x0f); }
    int si_r() const { return si_read_ ? si_read_() : 1; }
    uint8_t r0_r() const;
    uint8_t r1_r() const;
    void p_w(uint8_t data) { dac_ = uint8_t(data & 0x0f); }
    void r2_w(uint8_t data);
    void r3_w(uint8_t data);
    void o_w(uint8_t data);

    Mb88 cpu_;
    uint8_t latched_cmd_ = 0;
    uint16_t address_ = 0;
    uint8_t dac_ = 0;
    RomRead rom_read_;
    SiRead si_read_;
};

}  // namespace dsp
