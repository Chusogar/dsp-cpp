#pragma once

#include <array>
#include <cstdint>

namespace dsp {

// Ricoh RP-5C01 RTC, 4-bit registers behind MSX I/O $B4/$B5. Enough for the
// MSX2 BIOS to accept the clock; time is a fixed 2026-08-18 07:00:00.
class Rp5c01 {
public:
    void reset();
    void set_address(uint8_t value) { address_ = value & 0x0f; }
    uint8_t read() const;
    void write(uint8_t value);

private:
    uint8_t address_ = 0;
    std::array<uint8_t, 16> regs_{};
};

}  // namespace dsp
