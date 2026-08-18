#pragma once

#include <array>
#include <cstdint>

namespace dsp {

// Ricoh RP-5C01 RTC, 4-bit registers behind MSX I/O $B4/$B5. Register 13
// (mode) bits 0-1 select one of four banks: 0 = clock, 1 = alarm, 2 = RAM,
// 3 = timer-reset. Time is a fixed 2026-08-18 07:00:00 in bank 0.
class Rp5c01 {
public:
    void reset();
    void set_address(uint8_t value) { address_ = value & 0x0f; }
    uint8_t read() const;
    void write(uint8_t value);

private:
    uint8_t address_ = 0;
    uint8_t mode_ = 0;
    std::array<std::array<uint8_t, 13>, 3> bank_{};
};

}  // namespace dsp
