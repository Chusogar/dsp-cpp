#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

#include "cpu/irq_line.h"

namespace dsp {

// Texas Instruments TMS7000 family (8-bit microcontroller).
// Instruction behaviour follows the published TMS7000 ISA and MAME's tms7000
// core (hap / Tim Lindner). EXL-100 / EXELTEL replace SWAP R (opcode $D7)
// with the custom LVDP micro-op that reads a byte from the TMS3556 VRAM port.
class Tms7000 {
public:
    using ReadHandler = std::function<uint8_t(uint16_t)>;
    using WriteHandler = std::function<void(uint16_t, uint8_t)>;
    using PortInHandler = std::function<uint8_t()>;
    using PortOutHandler = std::function<void(uint8_t)>;
    using CycleHandler = std::function<void(int)>;

    enum class Chip {
        Tms7000,   // 128 B RAM, no internal ROM
        Tms7020,   // 128 B RAM, 2 KiB ROM at $F800
        Tms7040,   // 128 B RAM, 4 KiB ROM at $F000
        Tms7041,   // 128 B RAM, 4 KiB ROM, extra PF (70x1)
        Tms7042    // 256 B RAM, 4 KiB ROM, extra PF (70x2)
    };

    static constexpr int kInt1 = 0;
    static constexpr int kInt3 = 1;

    static constexpr int kPortA = 0;
    static constexpr int kPortB = 1;
    static constexpr int kPortC = 2;
    static constexpr int kPortD = 3;

    static constexpr uint8_t kSrC = 0x80;
    static constexpr uint8_t kSrN = 0x40;
    static constexpr uint8_t kSrZ = 0x20;
    static constexpr uint8_t kSrI = 0x10;

    explicit Tms7000(uint32_t clock, Chip chip = Chip::Tms7000, unsigned divider = 2);

    void set_memory_handlers(ReadHandler read, WriteHandler write);
    void set_port_in(int port, PortInHandler handler);
    void set_port_out(int port, PortOutHandler handler);
    void set_cycle_handler(CycleHandler handler) { cycle_handler_ = std::move(handler); }

    // Internal mask ROM. Size must match the chip (2 KiB / 4 KiB).
    void set_internal_rom(const uint8_t* data, size_t size);
    void set_exl_lvdp(bool enabled) { exl_lvdp_ = enabled; }

    void reset();
    int run(int cycles);
    void set_input_line(int irqline, IrqLine state);

    uint32_t clock() const { return clock_; }
    unsigned divider() const { return divider_; }
    uint32_t cpu_clock() const { return clock_ / std::max(1u, divider_); }

    uint16_t pc() const { return pc_; }
    uint8_t sp() const { return sp_; }
    uint8_t sr() const { return sr_; }
    uint8_t a() const { return ram_at(0); }
    uint8_t b() const { return ram_at(1); }
    uint8_t iocnt0() const { return io_control_[0]; }
    bool idle() const { return idle_state_; }

    void set_pc(uint16_t value) { pc_ = value; }
    void set_sp(uint8_t value) { sp_ = value; }
    void set_sr(uint8_t value) { sr_ = value & 0xf0; }
    void set_a(uint8_t value) { write_r8(0, value); }
    void set_b(uint8_t value) { write_r8(1, value); }

    uint8_t ram_at(uint8_t address) const {
        return address < ram_.size() ? ram_[address] : 0;
    }

private:
    using OpFunc = int (Tms7000::*)(uint8_t, uint8_t);

    static constexpr int kWbNo = -1;

    uint8_t read_mem8(uint16_t address);
    void write_mem8(uint16_t address, uint8_t data);
    uint16_t read_mem16(uint16_t address);
    void write_mem16(uint16_t address, uint16_t data);

    uint8_t read_r8(uint8_t address) { return read_mem8(address); }
    void write_r8(uint8_t address, uint8_t data) { write_mem8(address, data); }
    uint16_t read_r16(uint8_t address);
    void write_r16(uint8_t address, uint16_t data);

    uint8_t read_p(uint8_t address) { return read_mem8(uint16_t(0x100 + address)); }
    void write_p(uint8_t address, uint8_t data) { write_mem8(uint16_t(0x100 + address), data); }

    uint8_t imm8();
    uint16_t imm16();
    uint8_t pull8();
    void push8(uint8_t data);
    uint16_t pull16();
    void push16(uint16_t data);

    uint8_t pf_read(uint8_t offset);
    void pf_write(uint8_t offset, uint8_t data);
    uint8_t port_in(int port);
    void port_out(int port, uint8_t data);

    void set_nz(uint16_t value);
    void set_c(uint16_t value);
    void set_cnz(uint16_t value);
    uint8_t get_c() const { return uint8_t(sr_ >> 7) & 1; }

    void flag_ext_interrupt(int extline);
    void check_interrupts();
    void do_interrupt(int irqline);

    void timer_run(int tmr);
    void timer_reload(int tmr);
    void timer_tick_low(int tmr);
    void tick_timers(int cpu_cycles);

    void execute_one(uint8_t op);
    void lvdp();
    void consume(int cycles) { icount_ -= cycles; }

    void am_a(OpFunc op);
    void am_b(OpFunc op);
    void am_r(OpFunc op);
    void am_a2a(OpFunc op);
    void am_a2b(OpFunc op);
    void am_a2r(OpFunc op);
    void am_a2p(OpFunc op);
    void am_b2a(OpFunc op);
    void am_b2b(OpFunc op);
    void am_b2r(OpFunc op);
    void am_b2p(OpFunc op);
    void am_r2a(OpFunc op);
    void am_r2b(OpFunc op);
    void am_r2r(OpFunc op);
    void am_i2a(OpFunc op);
    void am_i2b(OpFunc op);
    void am_i2r(OpFunc op);
    void am_i2p(OpFunc op);
    void am_p2a(OpFunc op);
    void am_p2b(OpFunc op);

    int op_clr(uint8_t param1, uint8_t param2);
    int op_dec(uint8_t param1, uint8_t param2);
    int op_inc(uint8_t param1, uint8_t param2);
    int op_inv(uint8_t param1, uint8_t param2);
    int op_rl(uint8_t param1, uint8_t param2);
    int op_rlc(uint8_t param1, uint8_t param2);
    int op_rr(uint8_t param1, uint8_t param2);
    int op_rrc(uint8_t param1, uint8_t param2);
    int op_swap(uint8_t param1, uint8_t param2);
    int op_xchb(uint8_t param1, uint8_t param2);
    int op_adc(uint8_t param1, uint8_t param2);
    int op_add(uint8_t param1, uint8_t param2);
    int op_and(uint8_t param1, uint8_t param2);
    int op_cmp(uint8_t param1, uint8_t param2);
    int op_dac(uint8_t param1, uint8_t param2);
    int op_dsb(uint8_t param1, uint8_t param2);
    int op_mpy(uint8_t param1, uint8_t param2);
    int op_mov(uint8_t param1, uint8_t param2);
    int op_or(uint8_t param1, uint8_t param2);
    int op_sbb(uint8_t param1, uint8_t param2);
    int op_sub(uint8_t param1, uint8_t param2);
    int op_xor(uint8_t param1, uint8_t param2);
    int op_djnz(uint8_t param1, uint8_t param2);
    int op_btjo(uint8_t param1, uint8_t param2);
    int op_btjz(uint8_t param1, uint8_t param2);

    void shortbranch(bool check);
    void jmp(bool check);

    void br_dir();
    void br_inx();
    void br_ind();
    void call_dir();
    void call_inx();
    void call_ind();
    void cmpa_dir();
    void cmpa_inx();
    void cmpa_ind();
    void decd_a();
    void decd_b();
    void decd_r();
    void dint();
    void eint();
    void idle_op();
    void lda_dir();
    void lda_inx();
    void lda_ind();
    void ldsp();
    void movd_dir();
    void movd_inx();
    void movd_ind();
    void nop();
    void pop_a();
    void pop_b();
    void pop_r();
    void pop_st();
    void push_a();
    void push_b();
    void push_r();
    void push_st();
    void reti();
    void rets();
    void setc();
    void sta_dir();
    void sta_inx();
    void sta_ind();
    void stsp();
    void trap(uint8_t op);
    void illegal();

    void apply_write(uint16_t address, int result, bool to_p);

    ReadHandler read_;
    WriteHandler write_;
    std::array<PortInHandler, 4> port_in_{};
    std::array<PortOutHandler, 4> port_out_{};
    CycleHandler cycle_handler_;

    uint32_t clock_ = 0;
    unsigned divider_ = 2;
    Chip chip_ = Chip::Tms7000;
    bool family_70x2_ = false;
    bool exl_lvdp_ = false;
    uint16_t rom_base_ = 0;
    std::vector<uint8_t> ram_;
    std::vector<uint8_t> rom_;

    int icount_ = 0;
    uint16_t pc_ = 0;
    uint8_t sp_ = 0;
    uint8_t sr_ = 0;
    bool idle_state_ = false;
    bool irq_state_[2] = {false, false};
    uint8_t io_control_[3] = {};
    uint8_t port_latch_[4] = {};
    uint8_t port_ddr_[4] = {};

    uint8_t timer_data_[2] = {};
    uint8_t timer_control_[2] = {};
    int timer_decrementer_[2] = {};
    int timer_prescaler_[2] = {};
    uint16_t timer_capture_latch_[2] = {};
    int timer_crystal_acc_[2] = {};
};

}  // namespace dsp
