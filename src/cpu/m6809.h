#pragma once

#include <cstdint>
#include <functional>
#include <utility>

#include "cpu/irq_line.h"

namespace dsp {

// Motorola 6809, ported from m6809.pas + m6809.inc.
class M6809 {
public:
    using ReadHandler = std::function<uint8_t(uint16_t)>;
    using WriteHandler = std::function<void(uint16_t, uint8_t)>;
    using CycleHandler = std::function<void(int)>;

    struct Flags {
        bool e = false, f = false, h = false, i = false;
        bool n = false, z = false, v = false, c = false;
    };

    explicit M6809(uint32_t clock);

    void set_memory_handlers(ReadHandler read, WriteHandler write);
    // Optional opcode map. Konami-1 boards encrypt instruction fetches but
    // leave operands and data reads alone (m6809.pas `opcode`). When empty,
    // fetch_opcode uses `read`.
    void set_opcode_read(ReadHandler handler) { opcode_read_ = std::move(handler); }
    // Called after every instruction with the number of elapsed cycles.
    void set_cycle_handler(CycleHandler handler) { cycle_handler_ = std::move(handler); }

    void reset();
    // Runs until at least `cycles` cycles have elapsed, returns the amount executed.
    int run(int cycles);

    void set_irq(IrqLine state) { irq_state_ = state; }
    void set_firq(IrqLine state) { firq_state_ = state; }
    void set_nmi(IrqLine state);

    uint32_t clock() const { return clock_; }
    uint16_t pc() const { return pc_; }
    uint8_t debug_cc() const { return get_cc(); }
    IrqLine debug_irq_state() const { return irq_state_; }

    // Registers, public to keep debugging and driver hooks simple.
    uint8_t a = 0, b = 0, dp = 0;
    uint16_t x = 0, y = 0, u = 0, s = 0;
    Flags cc;

private:
    uint16_t d() const { return uint16_t((uint16_t(a) << 8) | b); }
    void set_d(uint16_t value) {
        a = uint8_t(value >> 8);
        b = uint8_t(value);
    }

    uint8_t read(uint16_t address) { return read_(address); }
    void write(uint16_t address, uint8_t value) { write_(address, value); }
    uint16_t read_word(uint16_t address);
    void write_word(uint16_t address, uint16_t value);
    uint8_t fetch();
    uint8_t fetch_opcode();
    uint16_t fetch_word();

    void push_s(uint8_t value);
    uint8_t pop_s();
    void push_sw(uint16_t value);
    uint16_t pop_sw();
    void push_u(uint8_t value);
    uint8_t pop_u();
    void push_uw(uint16_t value);
    uint16_t pop_uw();

    uint8_t get_cc() const;
    void set_cc(uint8_t value);

    int call_nmi();
    int call_irq();
    int call_firq();

    uint16_t get_indexed();
    uint16_t* index_register(uint8_t postbyte);
    uint16_t transfer_source(uint8_t code) const;
    void transfer_target(uint8_t code, uint16_t value);
    void tfr(uint8_t value);
    void exg(uint8_t value);
    void page_10(uint8_t opcode);
    void page_11(uint8_t opcode);

    // ALU helpers (m680x_* in m6809.inc).
    uint8_t op_neg(uint8_t value);
    uint8_t op_com(uint8_t value);
    uint8_t op_lsr(uint8_t value);
    uint8_t op_ror(uint8_t value);
    uint8_t op_asr(uint8_t value);
    uint8_t op_asl(uint8_t value);
    uint8_t op_rol(uint8_t value);
    uint8_t op_dec(uint8_t value);
    uint8_t op_inc(uint8_t value);
    void op_tst(uint8_t value);
    uint8_t op_sub8(uint8_t left, uint8_t right);
    uint16_t op_sub16(uint16_t left, uint16_t right);
    uint8_t op_sbc(uint8_t left, uint8_t right);
    uint8_t op_and(uint8_t left, uint8_t right);
    uint8_t op_eor(uint8_t left, uint8_t right);
    uint8_t op_adc(uint8_t left, uint8_t right);
    uint8_t op_or(uint8_t left, uint8_t right);
    uint8_t op_add8(uint8_t left, uint8_t right);
    uint16_t op_add16(uint16_t left, uint16_t right);
    uint8_t op_ld_st8(uint8_t value);
    uint16_t op_ld_st16(uint16_t value);

    void branch(bool condition, uint8_t offset);
    void long_branch(bool condition, uint16_t offset);

    uint32_t clock_;
    uint16_t pc_ = 0;
    bool cwai_ = false;
    bool stack_init_ = false;  // pila_init: the NMI is masked until S is loaded

    IrqLine irq_state_ = IrqLine::Clear;
    IrqLine firq_state_ = IrqLine::Clear;
    IrqLine nmi_request_ = IrqLine::Clear;
    IrqLine nmi_state_ = IrqLine::Clear;

    int extra_cycles_ = 0;
    // Values decoded by the addressing mode of the current instruction.
    uint16_t address_ = 0;
    uint8_t operand_ = 0;

    ReadHandler read_;
    ReadHandler opcode_read_;
    WriteHandler write_;
    CycleHandler cycle_handler_;
};

}  // namespace dsp
