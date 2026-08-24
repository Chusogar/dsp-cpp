#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <utility>

#include "cpu/irq_line.h"

namespace dsp {

// Hudson HuC6280, ported from hu6280.pas. The 65C02 derivative used by the Data
// East protection MCU and by the Sly Spy sound board: it maps the 16 bit logical
// space through eight 8 KB MPR registers, so the memory handlers see the
// translated 21 bit address.
class HuC6280 {
public:
    using ReadHandler = std::function<uint8_t(uint32_t)>;
    using WriteHandler = std::function<void(uint32_t, uint8_t)>;
    using CycleHandler = std::function<void(int)>;

    struct Flags {
        bool n = false, v = false, t = false, brk = false;
        bool dec = false, irq_disable = false, z = false, c = false;
    };

    explicit HuC6280(uint32_t clock);

    void set_memory_handlers(ReadHandler read, WriteHandler write);
    void set_cycle_handler(CycleHandler handler) { cycle_handler_ = std::move(handler); }

    void reset();
    int run(int cycles);

    // irqline 0 -> $fff8 (IRQ1), 1 -> $fff6 (IRQ2), 2 -> timer, kNmiLine -> NMI.
    static constexpr int kNmiLine = 32;
    void set_irq_line(int irqline, IrqLine state);
    // $1ff400-$1ff403 control block.
    void irq_status_w(uint8_t offset, uint8_t value);
    uint8_t irq_status_r(uint8_t offset) const;
    // $1fec00-$1fec01 timer block.
    void timer_w(uint8_t offset, uint8_t value);
    uint8_t timer_r() const;

    uint32_t clock() const { return clock_; }
    uint16_t pc() const { return pc_; }

    uint8_t a = 0, x = 0, y = 0, sp = 0xff;
    Flags p;
    std::array<uint8_t, 8> mpr{};

private:
    uint32_t translated(uint16_t address) const {
        return (uint32_t(mpr[address >> 13]) << 13) | (address & 0x1fff);
    }
    uint32_t zero_page(uint16_t address) const {
        return (uint32_t(mpr[1]) << 13) | (address & 0x1fff);
    }
    uint8_t read(uint32_t address) { return read_(address); }
    void write(uint32_t address, uint8_t value) { write_(address, value); }
    uint8_t fetch() { return read(translated(pc_++)); }

    uint8_t get_flags() const;
    void set_flags(uint8_t value);
    void push(uint8_t value);
    uint8_t pull();
    void do_interrupt(uint16_t vector);
    void check_and_take_irq_lines();

    void set_nz(uint8_t value);
    void branch(bool condition, uint8_t offset);
    void adc(uint8_t value);
    void sbc(uint8_t value);
    void compare(uint8_t reg, uint8_t value);

    uint32_t clock_;
    uint16_t pc_ = 0;
    uint16_t address_ = 0;  // effective address of the current instruction
    uint8_t operand_ = 0;   // value fetched by the addressing mode
    int extra_cycles_ = 0;
    int clocks_per_cycle_ = 4;

    uint8_t timer_status_ = 0;
    int32_t timer_load_ = 128 * 1024;
    int32_t timer_value_ = 0;
    uint8_t irq_pending_ = 0;
    uint8_t irq_mask_ = 0;
    uint8_t io_buffer_ = 0;
    std::array<IrqLine, 3> irq_state_{IrqLine::Clear, IrqLine::Clear, IrqLine::Clear};
    IrqLine nmi_state_ = IrqLine::Clear;

    ReadHandler read_;
    WriteHandler write_;
    CycleHandler cycle_handler_;
};

}  // namespace dsp
