#pragma once

#include <cstdint>
#include <functional>

#include "cpu/irq_line.h"

namespace dsp {

// MOS 6526 CIA (old-style single-chip instance used by C64).
// Ported from leniad/dsp-emulator src/ordenadores/misc/mos6526_old.pas.
class Mos6526 {
public:
    using PortRead = std::function<uint8_t()>;
    using PortWrite = std::function<void(uint8_t)>;
    using IrqHandler = std::function<void(IrqLine)>;

    explicit Mos6526(uint32_t clock = 985248);

    void set_port_a(PortRead in, PortWrite out = {});
    void set_port_b(PortRead in, PortWrite out = {});
    void set_irq_handler(IrqHandler h) { irq_ = std::move(h); }

    void reset();
    uint8_t read(uint8_t reg);
    void write(uint8_t reg, uint8_t value);
    // Advance by PHI2 cycles.
    void tick(int cycles);

    // Live port output (after DDR).
    uint8_t pa() const { return uint8_t((pra_ & ddra_) | (ina_ & ~ddra_)); }
    uint8_t pb() const { return uint8_t((prb_ & ddrb_) | (inb_ & ~ddrb_)); }

    // Joystick bits mixed into port A/B (active low), C64 style.
    uint8_t joystick1 = 0xFF;
    uint8_t joystick2 = 0xFF;

    void set_flag(bool state);  // FLAG input (cassette sense etc.)

private:
    void update_irq();
    void clock_ta();
    void clock_tb();

    uint32_t clock_;
    uint8_t pra_ = 0, prb_ = 0, ddra_ = 0, ddrb_ = 0;
    uint8_t ina_ = 0xFF, inb_ = 0xFF;
    uint16_t ta_ = 0xFFFF, tb_ = 0xFFFF;
    uint16_t ta_latch_ = 0xFFFF, tb_latch_ = 0xFFFF;
    uint8_t tod_t_ = 0, tod_s_ = 0, tod_m_ = 0, tod_h_ = 0;
    uint8_t sdr_ = 0, icr_ = 0, imr_ = 0;
    uint8_t cra_ = 0, crb_ = 0;
    bool ta_under_ = false, tb_under_ = false;
    bool flag_ = false;

    PortRead pa_in_, pb_in_;
    PortWrite pa_out_, pb_out_;
    IrqHandler irq_;
};

}  // namespace dsp
