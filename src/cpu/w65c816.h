#pragma once

#include <cstdint>
#include <functional>

#include "cpu/irq_line.h"

namespace dsp {

// WDC 65C816, as used by the Apple IIGS. A superset of the 65C02: on reset it
// starts in 6502-compatible "emulation" mode (E=1, 8-bit A/X/Y, page-1 stack,
// same instruction set as a 65C02 plus a handful of new one-byte opcodes),
// and software switches to 16-bit "native" mode via XCE once it's ready to
// use the wider registers, the 24-bit address space (a separate 8-bit bank
// register for code and for data), the movable Direct Page, and the extra
// addressing modes (stack-relative, 24-bit long, indirect-long).
//
// This isn't a port of an existing Pascal core (the reference project this
// codebase otherwise migrates from has no 65C816 support); it's written
// directly from the published 65C816 instruction set, cross-checked against
// MAME's g65816 core for cycle counts and edge cases (page-boundary and
// emulation-mode quirks, e.g. JSR/JSL stack behavior and the forced
// bank-wrap of 16-bit indexed addressing within a bank).
class W65C816 {
public:
    using ReadHandler = std::function<uint8_t(uint32_t)>;
    using WriteHandler = std::function<void(uint32_t, uint8_t)>;
    using CycleHandler = std::function<void(int)>;

    struct Flags {
        // Always meaningful.
        bool n = false, v = false, z = false, c = false, i = true, d = false;
        // Native mode only: m=1 selects 8-bit A/memory, x=1 selects 8-bit X/Y.
        // In emulation mode these read back as 1 (m) and the "B" break flag
        // occupies the same bit position as x.
        bool m = true, x = true;
    };

    explicit W65C816(uint32_t clock);

    void set_memory_handlers(ReadHandler read, WriteHandler write);
    void set_cycle_handler(CycleHandler handler) { cycle_handler_ = std::move(handler); }
    void set_fetch_hook(std::function<void(uint32_t)> hook) { fetch_hook_ = std::move(hook); }

    void reset();
    // Runs until at least `cycles` cycles have elapsed, returns the amount executed.
    int run(int cycles);

    void set_irq(IrqLine state) { irq_request_ = state; }
    void set_nmi(IrqLine state);

    uint32_t clock() const { return clock_; }
    uint32_t pc() const { return (uint32_t(pbr) << 16) | pc_; }
    bool emulation() const { return e_; }

    // Registers, public for debugging/driver convenience. a/x/y are always
    // stored full-width; the high byte is ignored (and forced to 0 on write)
    // whenever the corresponding m/x flag selects 8-bit mode.
    uint16_t a = 0, x = 0, y = 0;
    uint16_t sp = 0x01ff;
    uint16_t d = 0;       // Direct Page register
    uint8_t pbr = 0;      // Program Bank Register (K)
    uint8_t dbr = 0;      // Data Bank Register (B)
    Flags p;

private:
    bool e_ = true;  // emulation-mode flag (not part of P; set/cleared by XCE)

    uint16_t pc_ = 0;
    uint32_t clock_;

    IrqLine irq_request_ = IrqLine::Clear;
    IrqLine irq_state_ = IrqLine::Clear;
    IrqLine nmi_request_ = IrqLine::Clear;
    IrqLine nmi_state_ = IrqLine::Clear;
    bool stopped_ = false;   // STP
    bool waiting_ = false;   // WAI

    ReadHandler read_;
    WriteHandler write_;
    CycleHandler cycle_handler_;
    std::function<void(uint32_t)> fetch_hook_;

    uint8_t read(uint32_t address) { return read_ ? read_(address & 0xffffff) : 0xff; }
    void write(uint32_t address, uint8_t value) {
        if (write_) write_(address & 0xffffff, value);
    }
    uint8_t fetch8();
    uint16_t fetch16();
    uint32_t fetch24();

    uint8_t get_p() const;
    void set_p(uint8_t value);
    void set_nz8(uint8_t value);
    void set_nz16(uint16_t value);

    void push8(uint8_t value);
    uint8_t pop8();
    void push16(uint16_t value);
    uint16_t pop16();

    int take_irq(uint32_t vector_native, uint32_t vector_emulated);

    // Addressing mode resolvers: each returns the effective 24-bit address
    // and advances pc_ past the operand bytes. `extra` accumulates any
    // page-crossing/index penalty cycles for modes where that matters.
    uint32_t addr_direct(int& extra);
    uint32_t addr_direct_x(int& extra);
    uint32_t addr_direct_y(int& extra);
    uint32_t addr_direct_indirect(int& extra);
    uint32_t addr_direct_indirect_x(int& extra);
    uint32_t addr_direct_indirect_y(int& extra);
    uint32_t addr_direct_indirect_long(int& extra);
    uint32_t addr_direct_indirect_long_y(int& extra);
    uint32_t addr_absolute();
    uint32_t addr_absolute_x(int& extra);
    uint32_t addr_absolute_y(int& extra);
    uint32_t addr_absolute_long();
    uint32_t addr_absolute_long_x();
    uint32_t addr_stack_relative();
    uint32_t addr_stack_relative_indirect_y();

    // ALU / RMW helpers, operating in either 8 or 16-bit width depending on
    // the m (for A/memory ops) or x (for X/Y ops) flag.
    void op_adc(uint32_t address);
    void op_sbc(uint32_t address);
    void op_and(uint32_t address);
    void op_ora(uint32_t address);
    void op_eor(uint32_t address);
    void op_bit(uint32_t address, bool immediate);
    void op_cmp(uint32_t address);
    void op_cpx(uint32_t address);
    void op_cpy(uint32_t address);
    void op_lda(uint32_t address);
    void op_ldx(uint32_t address);
    void op_ldy(uint32_t address);
    void op_sta(uint32_t address);
    void op_stx(uint32_t address);
    void op_sty(uint32_t address);
    void op_stz(uint32_t address);
    void op_asl_mem(uint32_t address);
    void op_lsr_mem(uint32_t address);
    void op_rol_mem(uint32_t address);
    void op_ror_mem(uint32_t address);
    void op_inc_mem(uint32_t address);
    void op_dec_mem(uint32_t address);
    void op_trb(uint32_t address);
    void op_tsb(uint32_t address);
    void branch(bool condition);

    int extra_cycles_ = 0;
};

}  // namespace dsp
