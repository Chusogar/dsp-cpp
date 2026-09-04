#pragma once

#include <array>
#include <cstdint>
#include <functional>

#include "cpu/irq_line.h"

namespace dsp {

// Intel MCS-51 (8051/8751) microcontroller, ported from mcs51.pas. Only the
// features the arcade MCUs need are modelled: internal RAM and SFRs, timers 0
// and 1, the two external interrupt lines and the four I/O ports.
class Mcs51 {
public:
    using PortReadHandler = std::function<uint8_t()>;
    using PortWriteHandler = std::function<void(uint8_t)>;
    using ReadHandler = std::function<uint8_t(uint16_t)>;
    using WriteHandler = std::function<void(uint16_t, uint8_t)>;

    static constexpr size_t kRomSize = 0x2000;

    explicit Mcs51(uint32_t clock);

    void set_port_read_handler(int port, PortReadHandler handler);
    void set_port_write_handler(int port, PortWriteHandler handler);
    void set_external_handlers(ReadHandler read, WriteHandler write);
    void set_port_forced_input(int port, uint8_t value) { forced_input_[size_t(port)] = value; }

    uint8_t* rom() { return rom_.data(); }

    void reset();
    // Runs at least the requested number of machine cycles and returns how many
    // were consumed.
    int run(int cycles);

    void set_irq0_line(IrqLine state);
    void set_irq1_line(IrqLine state);

    uint32_t clock() const { return clock_; }
    uint16_t pc() const { return pc_; }
    uint8_t debug_sfr(uint8_t address) const { return sfr_[address]; }

private:
    struct Psw {
        bool p = false, o = false, bank0 = false, bank1 = false, u = false, ac = false, c = false;
    };

    uint8_t get_psw();
    void set_psw(uint8_t value);
    void add_flags(uint8_t a, uint8_t data, uint8_t carry);
    void sub_flags(uint8_t a, uint8_t data, uint8_t carry);
    void update_irq_prio(uint8_t ipl, uint8_t iph);

    void iram_w(uint8_t pos, uint8_t value);
    uint8_t iram_r(uint8_t pos);
    void iram_iw(uint8_t pos, uint8_t value);
    uint8_t iram_ir(uint8_t pos) const;

    uint8_t bit_address_r(uint8_t pos);
    void bit_address_w(uint8_t pos, uint8_t bit);

    void push_pc();
    void pop_pc();
    void clear_irqs();
    int evaluate_irq();

    uint8_t r_reg(uint8_t addr) const { return ram_[addr | (sfr_[0xd0] & 0x18)]; }
    void set_reg(uint8_t addr, uint8_t value) { ram_[addr | (sfr_[0xd0] & 0x18)] = value; }

    uint8_t fetch() { return rom_[pc_++ & (kRomSize - 1)]; }
    uint8_t peek(uint16_t address) const { return rom_[address & (kRomSize - 1)]; }

    uint8_t acc() const { return sfr_[0xe0]; }
    void set_acc(uint8_t value) {
        sfr_[0xe0] = value;
        calc_parity_ = true;
    }

    void update_timer_t0(int cycles);
    void update_timer_t1(int cycles);

    uint32_t clock_;
    uint16_t pc_ = 0;
    Psw psw_;
    std::array<uint8_t, 0x100> ram_{};
    std::array<uint8_t, 0x100> sfr_{};
    std::array<uint8_t, kRomSize> rom_{};
    std::array<uint8_t, 8> irq_prio_{};
    std::array<uint8_t, 4> forced_input_{};
    std::array<PortReadHandler, 4> port_read_{};
    std::array<PortWriteHandler, 4> port_write_{};
    ReadHandler read_external_;
    WriteHandler write_external_;
    IrqLine irq0_ = IrqLine::Clear;
    IrqLine irq1_ = IrqLine::Clear;
    uint32_t last_line_state_ = 0;
    uint16_t t0_count_ = 0;
    uint16_t t1_count_ = 0;
    uint8_t num_interrupts_ = 5;
    uint8_t irq_active_ = 0;
    int cur_irq_prio_ = -1;
    bool calc_parity_ = false;
    bool rwm_ = false;
};

}  // namespace dsp
