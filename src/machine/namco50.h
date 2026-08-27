#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "cpu/mb88xx.h"

namespace dsp {

// Namco 50XX: Fujitsu MB8842 MCU used as a protection / score device.
// Xevious: startup protection handshake.  Bosconian: full score tracking.
//
// Port map (MAME mame0220):
//   K  = CMD[7:4]
//   R0 = CMD[3:0]
//   R2 = R/W (1 = CPU reading)
//   O  = ANS, written a nibble at a time (bit4 selects high half)
class Namco50xx {
public:
    explicit Namco50xx(uint32_t clock);

    bool load_rom(const std::vector<uint8_t>& rom, std::string* error);

    void reset();
    void set_reset(bool running);
    void set_chip_select(bool asserted);
    void set_rw(bool cpu_reading);
    void write(uint8_t data);
    uint8_t read();

    void run(int cycles);

    uint16_t debug_pc() const { return cpu_.pc(); }

private:
    uint8_t k_r() const;
    uint8_t r0_r() const;
    uint8_t r2_r() const;
    void o_w(uint8_t data);
    void pulse_irq();

    Mb88 cpu_;
    uint8_t latched_cmd_ = 0;
    uint8_t port_o_ = 0;
    bool rw_ = true;
    int prot_step_ = 0;
    int prot_read_idx_ = 0;
};

}  // namespace dsp
