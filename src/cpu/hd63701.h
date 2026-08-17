#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <utility>

#include "cpu/irq_line.h"

namespace dsp {

// 6800-family MCU, ported from m680x.pas.
//   HD63701Y: 256 bytes of internal RAM, 16 KB of internal ROM at $c000,
//             six I/O ports and the output compare timer (Double Dragon).
//   M6803:    128 bytes of internal RAM at $40, no internal ROM, ports 1-4
//             only (Irem M62 sound CPU).
class HD63701 {
public:
    using ReadHandler = std::function<uint8_t(uint16_t)>;
    using WriteHandler = std::function<void(uint16_t, uint8_t)>;
    using PortReadHandler = std::function<uint8_t()>;
    using PortWriteHandler = std::function<void(uint8_t)>;
    using CycleHandler = std::function<void(int)>;

    enum class Type { HD63701Y, M6803 };

    struct Flags {
        bool h = false, i = true, n = false, z = false, v = false, c = false;
    };

    explicit HD63701(uint32_t clock, Type type = Type::HD63701Y);

    void set_memory_handlers(ReadHandler read, WriteHandler write);
    // Ports 1 to 4 (index 0 to 3).
    void set_port_read(int port, PortReadHandler handler);
    void set_port_write(int port, PortWriteHandler handler);
    // Ports 5 and 6 (index 0 and 1).
    void set_portx_read(int port, PortReadHandler handler);
    void set_portx_write(int port, PortWriteHandler handler);
    void set_cycle_handler(CycleHandler handler) { cycle_handler_ = std::move(handler); }

    std::array<uint8_t, 0x4000>& internal_rom() { return rom_; }
    Type type() const { return type_; }

    void reset();
    // Runs until at least `cycles` cycles have elapsed, returns the amount executed.
    int run(int cycles);

    void set_irq(IrqLine state) { irq_state_ = state; }
    void set_nmi(IrqLine state);
    void set_halt(IrqLine state) { halt_state_ = state; }
    void set_reset(IrqLine state) { reset_state_ = state; }
    IrqLine halt_line() const { return halt_state_; }
    IrqLine reset_line() const { return reset_state_; }

    uint32_t clock() const { return clock_; }
    uint16_t pc() const { return pc_; }

    // Registers, public to keep debugging and driver hooks simple.
    uint8_t a = 0, b = 0;
    uint16_t x = 0, sp = 0;
    Flags cc;

private:
    uint16_t d() const { return uint16_t((uint16_t(a) << 8) | b); }
    void set_d(uint16_t value) {
        a = uint8_t(value >> 8);
        b = uint8_t(value);
    }

    uint8_t read(uint16_t address);
    void write(uint16_t address, uint8_t value);
    uint16_t read_word(uint16_t address);
    void write_word(uint16_t address, uint16_t value);

    uint8_t read_io(uint8_t reg);
    void write_io(uint8_t reg, uint8_t value);

    void push(uint8_t value);
    uint8_t pop();
    void push_word(uint16_t value);
    uint16_t pop_word();

    uint8_t get_cc() const;
    void set_cc(uint8_t value);

    int call_int(uint16_t vector);
    void modified_counters();
    void check_timer_event();

    // ALU helpers, shared with the 6800 core in m680x.pas.
    uint8_t op_neg(uint8_t value);
    uint8_t op_com(uint8_t value);
    uint8_t op_lsr(uint8_t value);
    uint8_t op_asl(uint8_t value);
    uint8_t op_ror(uint8_t value);
    uint8_t op_rol(uint8_t value);
    uint8_t op_asr(uint8_t value);
    uint8_t op_dec(uint8_t value);
    uint8_t op_inc(uint8_t value);
    void op_tst(uint8_t value);
    uint8_t op_sub(uint8_t left, uint8_t right);
    uint8_t op_sbc(uint8_t left, uint8_t right);
    uint8_t op_and(uint8_t left, uint8_t right);
    uint8_t op_eor(uint8_t left, uint8_t right);
    uint8_t op_adc(uint8_t left, uint8_t right);
    uint8_t op_or(uint8_t left, uint8_t right);
    uint8_t op_add(uint8_t left, uint8_t right);

    uint32_t clock_;
    uint16_t pc_ = 0;

    std::array<uint8_t, 0x4000> rom_{};
    std::array<uint8_t, 0x200> internal_ram_{};

    std::array<PortReadHandler, 4> port_read_{};
    std::array<PortWriteHandler, 4> port_write_{};
    std::array<PortReadHandler, 2> portx_read_{};
    std::array<PortWriteHandler, 2> portx_write_{};
    std::array<uint8_t, 4> port_ddr_{};
    std::array<uint8_t, 4> port_in_{};
    std::array<uint8_t, 4> port_out_{};
    std::array<uint8_t, 2> portx_ddr_{};
    std::array<uint8_t, 2> portx_in_{};
    std::array<uint8_t, 2> portx_out_{};

    uint8_t ram_control_ = 0x40;
    uint8_t trcsr_ = 0;
    uint8_t tdr_ = 0;
    uint8_t tcsr_ = 0;
    uint8_t latch09_ = 0;
    uint8_t pending_tcsr_ = 0;
    bool trcsr_read_ = false;
    uint32_t counter_ = 0;          // free running counter (CTD)
    uint32_t output_compare_ = 0;   // output compare register (OCD)
    uint32_t timer_next_ = 0;

    IrqLine irq_state_ = IrqLine::Clear;
    IrqLine nmi_request_ = IrqLine::Clear;
    IrqLine nmi_state_ = IrqLine::Clear;
    IrqLine halt_state_ = IrqLine::Clear;
    IrqLine reset_state_ = IrqLine::Clear;

    int extra_cycles_ = 0;
    uint16_t address_ = 0;
    uint16_t operand_word_ = 0;
    uint8_t operand_ = 0;

    ReadHandler read_;
    WriteHandler write_;
    CycleHandler cycle_handler_;
    Type type_ = Type::HD63701Y;
};

}  // namespace dsp
