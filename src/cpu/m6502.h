#pragma once

#include <cstdint>
#include <functional>
#include <utility>

#include "cpu/irq_line.h"

namespace dsp {

// MOS 6502, ported from m6502.pas. Type::Nmos is the arcade variant (BCD on);
// Type::Nes is the 2A03 (decimal mode ignored, same undocumented opcodes).
// Optional G65SC02/65C02 mode covers the extra Lynx opcodes (BRA, STZ, PHX/PHY,
// TRB/TSB, BIT imm, INC/DEC A, JMP (abs,X), (zp)).
class M6502 {
public:
    using ReadHandler = std::function<uint8_t(uint16_t)>;
    using WriteHandler = std::function<void(uint16_t, uint8_t)>;
    using CycleHandler = std::function<void(int)>;

    struct Flags {
        bool n = false, v = false, t = false, brk = false;
        bool dec = false, irq_disable = false, z = false, c = false;
    };

    enum class Type { Nmos, Nes };

    explicit M6502(uint32_t clock, Type type = Type::Nmos);

    void set_memory_handlers(ReadHandler read, WriteHandler write);
    void set_cycle_handler(CycleHandler handler) { cycle_handler_ = std::move(handler); }

    // WDC/GTE 65C02 (and the Lynx G65SC02) extra opcodes: BRA, STZ, PHX/PHY,
    // TRB/TSB, BIT imm/zp,x/abs,x, INC/DEC A, JMP (abs,X), (zp) and the CMOS
    // decimal N/Z behaviour. NMOS remains the default so Gauntlet is unchanged.
    void set_cmos(bool enabled) { cmos_ = enabled; }
    bool cmos() const { return cmos_; }

    void set_halted(bool halted) { halted_ = halted; }
    bool halted() const { return halted_; }

    void reset();
    // Runs until at least `cycles` cycles have elapsed, returns the amount executed.
    int run(int cycles);

    void set_irq(IrqLine state) { irq_request_ = state; }
    void set_nmi(IrqLine state);
    // NES PPU NMI: one-instruction delay, same as n2a03 `after_ei:=true`.
    void delay_interrupts() { after_ei_ = true; }
    // OAM DMA steals 513 (+1 if odd) cycles from the current timeslice.
    void steal_cycles(int cycles);

    uint32_t clock() const { return clock_; }
    uint16_t pc() const { return pc_; }
    void set_pc(uint16_t value) { pc_ = value; }

    uint8_t a = 0, x = 0, y = 0, sp = 0xfd;
    Flags p;

private:
    uint8_t read(uint16_t address) { return read_(address); }
    void write(uint16_t address, uint8_t value) { write_(address, value); }
    uint8_t fetch() { return read(pc_++); }

    uint8_t get_flags() const;
    void set_flags(uint8_t value);
    void push(uint8_t value);
    uint8_t pop();

    int call_nmi();
    int call_irq();

    void set_nz(uint8_t value);
    void adc(uint8_t value);
    void sbc(uint8_t value);
    void compare(uint8_t reg, uint8_t value);
    void branch(bool condition, uint8_t offset);

    uint32_t clock_;
    Type type_ = Type::Nmos;
    uint16_t pc_ = 0;
    uint16_t address_ = 0;  // effective address of the current instruction
    int extra_cycles_ = 0;
    int stolen_cycles_ = 0;

    IrqLine irq_request_ = IrqLine::Clear;
    IrqLine nmi_request_ = IrqLine::Clear;
    IrqLine nmi_state_ = IrqLine::Clear;
    bool after_ei_ = false;  // cli/sei/plp delay the interrupt one instruction
    bool cmos_ = false;
    bool halted_ = false;

    ReadHandler read_;
    WriteHandler write_;
    CycleHandler cycle_handler_;
};

}  // namespace dsp
