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
// Written directly from the published 65C816 instruction set. Register and
// memory results are validated against the SingleStepTests 65816 vectors
// (emulation and native mode, every opcode), including the emulation-mode
// direct-page and stack wrap quirks and decimal-mode arithmetic.
class W65C816 {
public:
    using ReadHandler = std::function<uint8_t(uint32_t)>;
    using WriteHandler = std::function<void(uint32_t, uint8_t)>;
    using CycleHandler = std::function<void(int)>;
    // Called for WDM ($42 xx). Returning true means the host handled it.
    using WdmHandler = std::function<void(uint8_t)>;
    // Lets the host redirect interrupt/reset vector fetches (the IIGS pulls
    // them from ROM bank $FF). Receives the bank-0 vector address.
    using VectorHandler = std::function<uint32_t(uint32_t)>;

    struct Flags {
        bool n = false, v = false, z = false, c = false, i = true, d = false;
        // Native mode only: m=1 selects 8-bit A/memory, x=1 selects 8-bit X/Y.
        bool m = true, x = true;
    };

    explicit W65C816(uint32_t clock);

    void set_memory_handlers(ReadHandler read, WriteHandler write);
    void set_cycle_handler(CycleHandler handler) { cycle_handler_ = std::move(handler); }
    void set_fetch_hook(std::function<void(uint32_t)> hook) { fetch_hook_ = std::move(hook); }
    void set_wdm_handler(WdmHandler handler) { wdm_handler_ = std::move(handler); }
    void set_vector_handler(VectorHandler handler) { vector_handler_ = std::move(handler); }

    void reset();
    // Runs until at least `cycles` cycles have elapsed, returns the amount executed.
    int run(int cycles);
    // Executes exactly one instruction (or services one interrupt), returns cycles.
    int step();
    // Asks run() to return after the current instruction.
    void end_timeslice() { end_slice_ = true; }

    void set_irq(IrqLine state) { irq_request_ = state; }
    void set_nmi(IrqLine state);

    uint32_t clock() const { return clock_; }
    uint32_t pc() const { return (uint32_t(pbr) << 16) | pc_; }
    void set_pc(uint32_t v) { pbr = uint8_t(v >> 16); pc_ = uint16_t(v); }
    bool emulation() const { return e_; }
    void set_emulation(bool e);
    uint8_t get_p() const;
    void set_p(uint8_t value);
    bool waiting() const { return waiting_; }
    bool stopped() const { return stopped_; }
    void clear_halt() { stopped_ = waiting_ = false; }

    // Registers, public for debugging/driver convenience. a/x/y are always
    // stored full-width; the high byte of X/Y is forced to 0 in 8-bit index
    // mode, the high byte of A ("B") is preserved in 8-bit accumulator mode.
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
    IrqLine nmi_request_ = IrqLine::Clear;
    bool nmi_latched_ = false;
    bool stopped_ = false;   // STP
    bool waiting_ = false;   // WAI
    bool end_slice_ = false;

    ReadHandler read_;
    WriteHandler write_;
    CycleHandler cycle_handler_;
    std::function<void(uint32_t)> fetch_hook_;
    WdmHandler wdm_handler_;
    VectorHandler vector_handler_;

    int cycles_ = 0;  // cycles of the instruction being executed

    uint8_t rd(uint32_t address) { return read_ ? read_(address & 0xffffff) : 0xff; }
    void wr(uint32_t address, uint8_t value) {
        if (write_) write_(address & 0xffffff, value);
    }
    uint8_t fetch8();
    uint16_t fetch16();
    uint32_t fetch24();

    // Data access helpers. "Long" addresses are 24-bit and the second byte
    // of a 16-bit access carries into the next bank. Direct-page and stack
    // accesses always live in bank 0 and wrap at 64K.
    uint16_t rd16_long(uint32_t address) { return uint16_t(rd(address) | (rd(address + 1) << 8)); }
    void wr16_long(uint32_t address, uint16_t v) { wr(address, uint8_t(v)); wr(address + 1, uint8_t(v >> 8)); }
    uint16_t rd16_bank0(uint16_t address) { return uint16_t(rd(address) | (rd(uint16_t(address + 1)) << 8)); }
    // Direct page read of byte `offset` (offset may exceed 255 when indexed).
    // In emulation mode with DL=0 the access wraps inside the direct page.
    uint32_t dp_addr(uint32_t offset) const;

    void set_nz8(uint8_t value) { p.z = value == 0; p.n = (value & 0x80) != 0; }
    void set_nz16(uint16_t value) { p.z = value == 0; p.n = (value & 0x8000) != 0; }

    // Stack: push/pull wrap inside page 1 in emulation mode; the *_n variants
    // are used by the 65816-only instructions, which may leave page 1
    // temporarily (SP's high byte is forced back to $01 afterwards).
    void push8(uint8_t value);
    uint8_t pull8();
    void push8n(uint8_t value) { wr(sp, value); sp = uint16_t(sp - 1); }
    uint8_t pull8n() { sp = uint16_t(sp + 1); return rd(sp); }
    void fix_sp() { if (e_) sp = uint16_t(0x100 | (sp & 0xff)); }

    void interrupt(uint16_t vector_native, uint16_t vector_emulated, bool brk);
    uint16_t read_vector(uint16_t vector);

    // Effective-address resolvers (advance pc_ past operands).
    enum class Mode {
        Imm, Dp, DpX, DpY, DpInd, DpIndX, DpIndY, DpIndLong, DpIndLongY,
        Abs, AbsX, AbsY, Long, LongX, Sr, SrIndY
    };
    uint32_t ea(Mode mode, bool wide, bool is_write);

    // Memory operand access respecting the addressing mode's wrap rules.
    bool bank0_mode_ = false;  // last ea() was a bank-0 (dp/stack) access
    uint16_t load(uint32_t address, bool wide);
    void store(uint32_t address, uint16_t value, bool wide);

    void op_adc(uint16_t value);
    void op_sbc(uint16_t value);
    void op_cmp(uint16_t reg, uint16_t value, bool wide);
    void branch(bool condition);
    void rmw(uint32_t address, int op);
};

}  // namespace dsp
