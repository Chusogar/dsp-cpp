#pragma once

#include <cstdint>
#include <functional>

#include "cpu/irq_line.h"

namespace dsp {

// MOS 6522 VIA — full port of leniad/dsp-emulator via6522.pas.
//
// Timers T1/T2 (one-shot + T1 continuous, optional PB7/PB6), ports A/B with
// DDR + input latch, PCR handshake / fixed / pulse on CA1/CA2/CB1/CB2,
// shift register (PHI2 / T2 / external), IER/IFR with all sources.
class Via6522 {
public:
    using PortRead = std::function<uint8_t()>;
    using PortWrite = std::function<void(uint8_t)>;
    using LineWrite = std::function<void(bool)>;  // CA2 / CB2 output
    using IrqCallback = std::function<void(IrqLine)>;

    explicit Via6522(uint32_t clock = 1000000);

    void set_port_a(PortRead in, PortWrite out = {});
    void set_port_b(PortRead in, PortWrite out = {});
    void set_ca2_handler(LineWrite h) { ca2_handler_ = std::move(h); }
    void set_cb2_handler(LineWrite h) { cb2_handler_ = std::move(h); }
    void set_irq_callback(IrqCallback cb) { irq_cb_ = std::move(cb); }

    void reset();
    uint8_t read(uint8_t reg);
    void write(uint8_t reg, uint8_t value);

    // Advance by PHI2 cycles.
    void tick(int cycles);

    // External port / control inputs.
    void write_pa(uint8_t value);
    void write_pb(uint8_t value);
    void set_pb_line(int line, bool state);
    void write_ca1(bool state);
    void write_ca2(bool state);
    void write_cb1(bool state);
    void write_cb2(bool state);

    // Combined port levels (outputs forced by DDR, else inputs).
    uint8_t out_a() const;
    uint8_t out_b() const;
    bool ca2() const { return out_ca2_; }
    bool cb2() const { return out_cb2_; }

    uint8_t ddr_b() const { return ddr_b_; }

    uint8_t ifr() const { return ifr_; }
    uint8_t ier() const { return ier_; }
    uint8_t acr() const { return acr_; }
    uint8_t pcr() const { return pcr_; }

private:
    static constexpr uint8_t kIntCA2 = 0x01;
    static constexpr uint8_t kIntCA1 = 0x02;
    static constexpr uint8_t kIntSR  = 0x04;
    static constexpr uint8_t kIntCB2 = 0x08;
    static constexpr uint8_t kIntCB1 = 0x10;
    static constexpr uint8_t kIntT2  = 0x20;
    static constexpr uint8_t kIntT1  = 0x40;
    static constexpr uint8_t kIntAny = 0x80;

    void set_int(uint8_t bits);
    void clear_int(uint8_t bits);
    void output_irq();
    void output_pa();
    void output_pb();
    void output_ca2(bool level);
    void output_cb2(bool level);

    uint8_t input_pa();
    uint8_t input_pb();

    void tick_t1();
    void tick_t2();
    void shift_clock();  // one edge of shift register

    uint32_t clock_;

    // Ports
    uint8_t out_a_ = 0, out_b_ = 0;
    uint8_t in_a_ = 0xFF, in_b_ = 0xFF;
    uint8_t latch_a_ = 0xFF, latch_b_ = 0xFF;
    uint8_t ddr_a_ = 0, ddr_b_ = 0;

    // Timers
    uint8_t t1ll_ = 0xF3, t1lh_ = 0xB5;
    uint8_t t2ll_ = 0xFF, t2lh_ = 0xFF;
    uint16_t t1_ = 0, t2_ = 0;
    bool t1_active_ = false, t2_active_ = false;
    uint8_t t1_pb7_ = 1;
    bool t2_pb6_prev_ = true;

    // Shift register
    uint8_t sr_ = 0;
    uint8_t shift_count_ = 0;  // remaining bits (0 = idle)
    int shift_phase_ = 0;      // cycles until next edge
    bool shift_out_bit_ = true;

    // Control
    uint8_t acr_ = 0, pcr_ = 0, ier_ = 0, ifr_ = 0;

    // Control lines
    bool in_ca1_ = false, in_ca2_ = false;
    bool in_cb1_ = false, in_cb2_ = false;
    bool out_ca2_ = true, out_cb1_ = true, out_cb2_ = true;

    PortRead pa_in_, pb_in_;
    PortWrite pa_out_, pb_out_;
    LineWrite ca2_handler_, cb2_handler_;
    IrqCallback irq_cb_;
};

}  // namespace dsp
