#pragma once

#include <cstdint>
#include <functional>
#include <utility>

#include "cpu/irq_line.h"

namespace dsp {

// Motorola 6805 family, ported from m6805.pas. The M68705 variant is the one
// used as a protection MCU by Taito SJ and many other boards.
class M6805 {
public:
    using ReadHandler = std::function<uint8_t(uint16_t)>;
    using WriteHandler = std::function<void(uint16_t, uint8_t)>;
    using CycleHandler = std::function<void(int)>;

    enum class Type { M6805, M68705, HD63705 };

    struct Flags {
        bool h = false, i = true, n = false, z = false, c = false;
    };

    M6805(uint32_t clock, Type type);

    void set_memory_handlers(ReadHandler read, WriteHandler write);
    void set_cycle_handler(CycleHandler handler) { cycle_handler_ = std::move(handler); }

    void reset();
    // Runs until at least `cycles` cycles have elapsed, returns the amount executed.
    int run(int cycles);

    // The external interrupt pin, read back by the BIL/BIH instructions.
    void set_irq(IrqLine state);

    uint32_t clock() const { return clock_; }
    uint16_t pc() const { return pc_; }

    uint8_t a = 0, x = 0;
    uint16_t sp = 0x7f;
    Flags cc;

private:
    uint8_t read(uint16_t address) { return read_(address); }
    void write(uint16_t address, uint8_t value) { write_(address, value); }
    uint16_t read_word(uint16_t address);
    uint8_t fetch();
    uint16_t fetch_word();

    uint8_t get_cc() const;
    void set_cc(uint8_t value);
    void push(uint8_t value);
    void push_word(uint16_t value);
    uint8_t pull();
    uint16_t pull_word();

    void branch(bool taken, uint8_t offset);
    int execute();

    ReadHandler read_;
    WriteHandler write_;
    CycleHandler cycle_handler_;

    uint32_t clock_;
    Type type_;
    uint16_t pc_ = 0;
    uint16_t sp_mask_ = 0x7f;
    uint16_t sp_low_ = 0x60;
    IrqLine irq_state_ = IrqLine::Clear;
    bool irq_pending_ = false;
};

}  // namespace dsp
