#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <utility>

#include "cpu/irq_line.h"

namespace dsp {

// NEC µPD7801, ported from leniad/dsp-emulator `src/cpu/upd7810.pas` with
// `cpu_type = CPU_7801` (the Epoch Super Cassette Vision CPU). Crystal is
// 4 MHz, internally divided by 2 like the Pascal core.
class Upd7801 {
public:
    using ReadHandler = std::function<uint8_t(uint16_t)>;
    using WriteHandler = std::function<void(uint16_t, uint8_t)>;
    using PortInHandler = std::function<uint8_t(uint8_t mask)>;
    using PortOutHandler = std::function<void(uint8_t value)>;
    using CycleHandler = std::function<void(int)>;

    static constexpr int kPortA = 0;
    static constexpr int kPortB = 1;
    static constexpr int kPortC = 2;

    // Pascal UPD7810_INTF1 / INTF2 / INTF0.
    static constexpr int kIntf1 = 0;
    static constexpr int kIntf2 = 1;
    static constexpr int kIntf0 = 2;

    explicit Upd7801(uint32_t clock = 4000000);

    void set_memory_handlers(ReadHandler read, WriteHandler write);
    void set_port_in(PortInHandler port_b, PortInHandler port_c);
    void set_port_out(PortOutHandler port_a, PortOutHandler port_c);
    void set_cycle_handler(CycleHandler h) { cycle_handler_ = std::move(h); }

    void reset();
    int run(int cycles);

    // 7801 edge rules from `set_input_line_7801`.
    void set_input_line(int irqline, IrqLine state);

    uint32_t clock() const { return clock_; }
    uint16_t pc() const { return pc_; }
    uint8_t a() const { return a_; }
    uint8_t v() const { return v_; }
    uint8_t b() const { return b_; }
    uint8_t c() const { return c_; }
    bool zf() const { return zf_; }
    bool cy() const { return cy_; }
    bool iff() const { return iff_; }

    // Internal IRAM $FF80-$FFFF (128 bytes on 7801).
    std::array<uint8_t, 0x80> iram{};

private:
    uint8_t rd(uint16_t a) { return read_(a); }
    void wr(uint16_t a, uint8_t v) { write_(a, v); }
    uint8_t fetch() { return rd(pc_++); }
    uint16_t fetch16() {
        const uint8_t lo = fetch();
        return uint16_t(lo | (fetch() << 8));
    }
    uint16_t va() const { return uint16_t(a_ | (v_ << 8)); }
    uint16_t bc() const { return uint16_t(c_ | (b_ << 8)); }
    uint16_t de() const { return uint16_t(e_ | (d_ << 8)); }
    uint16_t hl() const { return uint16_t(l_ | (h_ << 8)); }
    void set_bc(uint16_t w) {
        c_ = uint8_t(w);
        b_ = uint8_t(w >> 8);
    }
    void set_de(uint16_t w) {
        e_ = uint8_t(w);
        d_ = uint8_t(w >> 8);
    }
    void set_hl(uint16_t w) {
        l_ = uint8_t(w);
        h_ = uint8_t(w >> 8);
    }

    uint8_t psw() const;
    void set_psw(uint8_t value);
    void zhc_add(uint16_t after, uint16_t before, bool carry);
    void zhc_sub(uint16_t after, uint16_t before, bool carry);

    uint8_t read_port(int port);
    void write_port(int port, uint8_t value);

    void take_irq();
    void handle_timers(int cycles);
    int execute_one();
    void skip_rest(uint8_t op);
    int prefix_cycles(uint8_t op, uint8_t op2) const;
    int prefix_size(uint8_t op, uint8_t op2) const;

    void op_48();
    void op_4c();
    void op_4d();
    void op_60();
    void op_64();
    void op_70();
    void op_74();

    uint8_t& reg_by_index(int index);
    void ani(uint8_t& r);
    void xri(uint8_t& r);
    void ori(uint8_t& r);
    void adi(uint8_t& r);
    void aci(uint8_t& r);
    void sui(uint8_t& r);
    void sbi(uint8_t& r);
    void adinc(uint8_t& r);
    void suinb(uint8_t& r);
    void eqi(uint8_t& r);
    void nei(uint8_t& r);
    void lti(uint8_t& r);
    void gti(uint8_t& r);
    void oni(uint8_t& r);
    void offi(uint8_t& r);

    void add_a(uint8_t r);
    void adc_a(uint8_t r);
    void sub_a(uint8_t r);
    void sbb_a(uint8_t r);
    void addnc_a(uint8_t r);
    void subnb_a(uint8_t r);
    void eqa_a(uint8_t r);
    void nea_a(uint8_t r);
    void lta_a(uint8_t r);
    void gta_a(uint8_t r);
    void ona_a(uint8_t r);
    void offa_a(uint8_t r);
    void ora_a(uint8_t r);
    void xra_a(uint8_t r);

    void add_x_a(uint8_t& r);
    void adc_x_a(uint8_t& r);
    void sub_x_a(uint8_t& r);
    void sbb_x_a(uint8_t& r);
    void addnc_x_a(uint8_t& r);
    void subnb_x_a(uint8_t& r);
    void eqa_x_a(uint8_t& r);
    void nea_x_a(uint8_t& r);
    void lta_x_a(uint8_t& r);
    void gta_x_a(uint8_t& r);
    void ana_x_a(uint8_t& r);
    void ora_x_a(uint8_t& r);
    void xra_x_a(uint8_t& r);

    static constexpr uint16_t kIntf0Bit = 0x2000;
    static constexpr uint16_t kIntFt0Bit = 0x0002;
    static constexpr uint16_t kIntf1Bit = 0x0008;
    static constexpr uint16_t kIntf2Bit = 0x0010;
    static constexpr uint16_t kIntfStBit = 0x0400;

    uint32_t clock_;
    uint16_t pc_ = 0, sp_ = 0, ppc_ = 0;
    uint8_t v_ = 0, a_ = 0, b_ = 0, c_ = 0, d_ = 0, e_ = 0, h_ = 0, l_ = 0;
    uint8_t v2_ = 0, a2_ = 0, b2_ = 0, c2_ = 0, d2_ = 0, e2_ = 0, h2_ = 0, l2_ = 0;
    uint16_t ea_ = 0, ea2_ = 0;
    bool zf_ = false, cy_ = false, sk_ = false, hc_ = false;
    bool l0_ = false, l1_ = false, f1_ = false, f7_ = false;
    bool iff_ = false, iff_pending_ = false;
    uint8_t int1_ = 0, int2_ = 0;
    uint16_t irr_ = 0;
    uint8_t mkl_ = 0xFF;
    uint16_t tm_ = 0;
    int ovc0_ = 0;
    uint8_t to_ = 0;
    uint8_t ma_ = 0, mb_ = 0xFF, mc_ = 0xFF, mcc_ = 0;
    uint8_t pa_in_ = 0, pb_in_ = 0, pc_in_ = 0;
    uint8_t pa_out_ = 0, pb_out_ = 0, pc_out_ = 0;
    uint8_t txd_ = 0, rdx_ = 0, sck_ = 0, ci_ = 0, co0_ = 0, co1_ = 0;

    ReadHandler read_;
    WriteHandler write_;
    PortInHandler port_b_in_, port_c_in_;
    PortOutHandler port_a_out_, port_c_out_;
    CycleHandler cycle_handler_;

    static const uint8_t kOpSize[256];
    static const uint8_t kOpCycles[256];
    static const uint8_t kOp48Size[256];
    static const uint8_t kOp48Cycles[256];
    static const uint8_t kOp4cSize[256];
    static const uint8_t kOp4cCycles[256];
    static const uint8_t kOp4dSize[256];
    static const uint8_t kOp4dCycles[256];
    static const uint8_t kOp64Size[256];
    static const uint8_t kOp64Cycles[256];
    static const uint8_t kOp70Size[256];
    static const uint8_t kOp70Cycles[256];
    static const uint8_t kOp74Size[256];
    static const uint8_t kOp74Cycles[256];
};

}  // namespace dsp
