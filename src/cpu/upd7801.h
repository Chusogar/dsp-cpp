#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <utility>

#include "cpu/irq_line.h"

namespace dsp {

// NEC µPD7801 (SCV CPU), API shaped after leniad/dsp-emulator upd7810.pas
// with cpu_type = CPU_7801.
//
// NOTE: The full opcode interpreter is ~3300 lines of Pascal
// (upd7810.pas + upd7810_tables.pas). This C++ core exposes the same
// memory / port / IRQ interface the Super Cassette Vision driver needs.
// Opcode execution is implemented for the common subset required to boot
// the SCV BIOS; remaining opcodes advance PC using the official size table
// so the machine does not hang, but full accuracy requires completing the
// body from the Pascal source.
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

    // 7801 interrupt lines (from Pascal UPD7810_INTF1 / INTF2).
    static constexpr int kIntf1 = 1;
    static constexpr int kIntf2 = 2;

    explicit Upd7801(uint32_t clock = 4000000);

    void set_memory_handlers(ReadHandler read, WriteHandler write);
    void set_port_in(PortInHandler port_b, PortInHandler port_c);
    void set_port_out(PortOutHandler port_a, PortOutHandler port_c);
    void set_cycle_handler(CycleHandler h) { cycle_handler_ = std::move(h); }

    void reset();
    // Run until at least `cycles` have elapsed; returns cycles executed.
    int run(int cycles);

    void set_input_line(int irqline, IrqLine state);

    uint32_t clock() const { return clock_; }
    uint16_t pc() const { return pc_; }

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

    void take_irq();
    int execute_one();

    uint32_t clock_;
    uint16_t pc_ = 0, sp_ = 0, ppc_ = 0;
    // Registers: VA (V=high, A=low), BC, DE, HL + alternates, EA
    uint8_t v_ = 0, a_ = 0, b_ = 0, c_ = 0, d_ = 0, e_ = 0, h_ = 0, l_ = 0;
    uint8_t v2_ = 0, a2_ = 0, b2_ = 0, c2_ = 0, d2_ = 0, e2_ = 0, h2_ = 0, l2_ = 0;
    uint16_t ea_ = 0;
    bool zf_ = false, cy_ = false, sk_ = false, hc_ = false;
    bool iff_ = false, iff_pending_ = false;
    uint8_t int1_ = 0, int2_ = 0;  // 0 clear, 1 assert
    uint8_t mkl_ = 0xFF;           // interrupt mask

    uint8_t pa_out_ = 0, pc_out_ = 0;

    ReadHandler read_;
    WriteHandler write_;
    PortInHandler port_b_in_, port_c_in_;
    PortOutHandler port_a_out_, port_c_out_;
    CycleHandler cycle_handler_;

    static const uint8_t kOpSize[256];
    static const uint8_t kOpCycles[256];
};

}  // namespace dsp
