#pragma once

#include <cstdint>
#include <functional>

#include "cpu/irq_line.h"

namespace dsp {

// MOS 6522 VIA, ported from via6522.pas (subset: ports, T1/T2, IER/IFR, SR basic).
class Via6522 {
public:
    using PortRead = std::function<uint8_t()>;
    using PortWrite = std::function<void(uint8_t)>;
    using IrqCallback = std::function<void(IrqLine)>;

    explicit Via6522(uint32_t clock);

    void set_port_a(PortRead in, PortWrite out = nullptr);
    void set_port_b(PortRead in, PortWrite out = nullptr);
    void set_irq_callback(IrqCallback cb) { irq_cb_ = std::move(cb); }

    void reset();
    uint8_t read(uint8_t reg);
    void write(uint8_t reg, uint8_t value);

    // Advance by PHI2 cycles (same domain as the attached CPU).
    void tick(int cycles);

    void write_pa(uint8_t value) { in_a_ = value; }
    void write_pb(uint8_t value) { in_b_ = value; }
    void set_pb_line(int line, bool state);
    void write_cb1(bool state);

    uint8_t out_a() const { return uint8_t((out_a_ & ddr_a_) | (in_a_ & ~ddr_a_)); }
    uint8_t out_b() const { return uint8_t((out_b_ & ddr_b_) | (in_b_ & ~ddr_b_)); }

private:
    void set_int(uint8_t bit);
    void clear_int(uint8_t bit);
    void output_irq();
    void output_pa();
    void output_pb();
    uint16_t t1_counter() const;

    uint32_t clock_;
    uint8_t out_a_ = 0, out_b_ = 0, in_a_ = 0xff, in_b_ = 0xff;
    uint8_t ddr_a_ = 0, ddr_b_ = 0;
    uint8_t t1ll_ = 0xf3, t1lh_ = 0xb5, t2ll_ = 0xff, t2lh_ = 0xff;
    uint16_t t1_ = 0, t2_ = 0;
    bool t1_active_ = false, t2_active_ = false;
    uint8_t t1_pb7_ = 1;
    uint8_t sr_ = 0, acr_ = 0, pcr_ = 0, ier_ = 0, ifr_ = 0;
    bool in_cb1_ = false;

    PortRead pa_in_, pb_in_;
    PortWrite pa_out_, pb_out_;
    IrqCallback irq_cb_;

    static constexpr uint8_t kIntT2 = 0x20;
    static constexpr uint8_t kIntT1 = 0x40;
    static constexpr uint8_t kIntAny = 0x80;
};

}  // namespace dsp
