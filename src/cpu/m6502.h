#pragma once

#include <cstdint>
#include <functional>
#include <utility>

#include "cpu/irq_line.h"

namespace dsp {

// MOS 6502, ported from m6502.pas (NMOS variant used by the Atari sound boards).
class M6502 {
public:
    using ReadHandler = std::function<uint8_t(uint16_t)>;
    using WriteHandler = std::function<void(uint16_t, uint8_t)>;
    using CycleHandler = std::function<void(int)>;

    struct Flags {
        bool n = false, v = false, t = false, brk = false;
        bool dec = false, irq_disable = false, z = false, c = false;
    };

    explicit M6502(uint32_t clock);

    void set_memory_handlers(ReadHandler read, WriteHandler write);
    void set_cycle_handler(CycleHandler handler) { cycle_handler_ = std::move(handler); }

    void reset();
    // Runs until at least `cycles` cycles have elapsed, returns the amount executed.
    int run(int cycles);

    void set_irq(IrqLine state) { irq_request_ = state; }
    void set_nmi(IrqLine state);

    uint32_t clock() const { return clock_; }
    uint16_t pc() const { return pc_; }

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
    uint16_t pc_ = 0;
    uint16_t address_ = 0;  // effective address of the current instruction
    int extra_cycles_ = 0;

    IrqLine irq_request_ = IrqLine::Clear;
    IrqLine nmi_request_ = IrqLine::Clear;
    IrqLine nmi_state_ = IrqLine::Clear;
    bool after_ei_ = false;  // cli/sei/plp delay the interrupt one instruction

    ReadHandler read_;
    WriteHandler write_;
    CycleHandler cycle_handler_;
};

}  // namespace dsp
