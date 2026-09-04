#pragma once

#include <cstdint>
#include <functional>

namespace dsp {

// MOS 8520 CIA (Amiga). Same register file as the 6526: PRA/PRB, timers A/B,
// ICR/CRA/CRB. TOD is a 24-bit binary counter (not BCD). Port reads are
// (latch & DDR) | (external & ~DDR) — no C64-style open-collector AND.
class Cia8520 {
public:
    using PortRead = std::function<uint8_t()>;
    using PortWrite = std::function<void(uint8_t)>;
    using IrqHandler = std::function<void(bool)>;

    void set_port_a(PortRead in, PortWrite out = {});
    void set_port_b(PortRead in, PortWrite out = {});
    void set_irq_handler(IrqHandler h) { irq_ = std::move(h); }

    void reset();
    uint8_t read(uint8_t reg);
    void write(uint8_t reg, uint8_t value);
    // Advance by E-clocks (CPU clock / 10 on the Amiga).
    void tick(int eclocks);
    void tod_tick();  // one TOD increment (VBlank on CIA-A, HSync on CIA-B)
    void pulse_flag();

    uint8_t pra() const { return pra_; }
    uint8_t prb() const { return prb_; }
    uint8_t ddra() const { return ddra_; }
    uint8_t ddrb() const { return ddrb_; }
    bool irq() const { return (icr_ & 0x80) != 0; }
    uint8_t cra() const { return cra_; }
    uint8_t crb() const { return crb_; }
    uint8_t imr() const { return imr_; }
    uint8_t icr_peek() const { return icr_; }
    uint16_t ta() const { return ta_; }
    uint16_t tb() const { return tb_; }
    uint32_t tod() const { return tod_; }
    uint32_t alarm() const { return alarm_; }


    // Output pins (DDR=1 bits of the latch; inputs read as 1 here).
    uint8_t pa_out() const { return uint8_t(pra_ | uint8_t(~ddra_)); }
    uint8_t pb_out() const { return uint8_t(prb_ | uint8_t(~ddrb_)); }

private:
    void update_irq();
    uint8_t port_read(uint8_t latch, uint8_t ddr, const PortRead& in) const;

    uint8_t pra_ = 0, prb_ = 0, ddra_ = 0, ddrb_ = 0;
    uint16_t ta_ = 0xFFFF, tb_ = 0xFFFF;
    uint16_t ta_latch_ = 0xFFFF, tb_latch_ = 0xFFFF;
    uint32_t tod_ = 0;
    uint32_t alarm_ = 0;
    uint32_t tod_latch_ = 0;
    uint8_t sdr_ = 0, icr_ = 0, imr_ = 0;
    uint8_t cra_ = 0, crb_ = 0;
    bool tod_run_ = true;
    bool tod_latched_ = false;

    PortRead pa_in_, pb_in_;
    PortWrite pa_out_, pb_out_;
    IrqHandler irq_;
};

}  // namespace dsp
