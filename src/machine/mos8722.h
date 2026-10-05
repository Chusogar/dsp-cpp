#pragma once

#include <array>
#include <cstdint>

namespace dsp {

// Simplified MOS 8722 MMU for Commodore 128 mode.
// Enough of CR / PCR / MCR / RCR / page pointers for C128 BASIC to boot.
class Mos8722 {
public:
    enum Reg : int {
        CR = 0,
        PCRA = 1,
        PCRB = 2,
        PCRC = 3,
        PCRD = 4,
        MCR = 5,
        RCR = 6,
        P0L = 7,
        P0H = 8,
        P1L = 9,
        P1H = 10,
        VR = 11,
    };

    void reset();

    // Overlay read/write. Pass-through `bus` when the MMU is not selected.
    uint8_t read(uint16_t addr, uint8_t bus) const;
    void write(uint16_t addr, uint8_t value);

    uint8_t cr() const { return reg_[CR]; }
    uint8_t mcr() const { return reg_[MCR]; }
    uint8_t rcr() const { return reg_[RCR]; }

    bool c64_mode() const { return (reg_[MCR] & 0x40) != 0; }
    bool cpu_8502() const { return (reg_[MCR] & 0x01) != 0; }
    bool io_enabled() const { return (reg_[CR] & 0x01) == 0; }
    bool rom_lo() const { return (reg_[CR] & 0x02) == 0; }          // $4000 system ROM
    uint8_t rom_mid() const { return (reg_[CR] >> 2) & 0x03; }       // $8000
    uint8_t rom_hi() const { return (reg_[CR] >> 4) & 0x03; }        // $C000/$E000
    int ram_bank() const { return (reg_[CR] >> 6) & 0x01; }          // A16

    // Translate CPU address to RAM physical address (bank*64K + offset).
    // Applies page-0/1 pointers and shared-RAM rules.
    uint32_t translate_ram(uint16_t addr) const;

private:
    std::array<uint8_t, 16> reg_{};
    uint8_t p0h_latch_ = 0;
    uint8_t p1h_latch_ = 0;
};

}  // namespace dsp
