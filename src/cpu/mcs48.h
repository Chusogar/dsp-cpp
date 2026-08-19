#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <utility>

#include "cpu/irq_line.h"
#include "machine/i8243.h"

namespace dsp {

constexpr uint16_t MCS48_PORT_P0 = 0x100;
constexpr uint16_t MCS48_PORT_P1 = 0x101;
constexpr uint16_t MCS48_PORT_P2 = 0x102;
constexpr uint16_t MCS48_PORT_T0 = 0x110;
constexpr uint16_t MCS48_PORT_T1 = 0x111;
constexpr uint16_t MCS48_PORT_BUS = 0x120;
constexpr uint16_t MCS48_PORT_PROG = 0x121;

class Mcs48 {
public:
    using PortInHandler = std::function<uint8_t(uint16_t)>;
    using PortOutHandler = std::function<void(uint16_t, uint8_t)>;
    using ReadHandler = std::function<uint8_t(uint16_t)>;
    using WriteHandler = std::function<void(uint16_t, uint8_t)>;

    enum class Chip : uint8_t { I8039 = 0, I8035 = 1, N7751 = 2, I8042 = 3 };

    static constexpr size_t kRomSize = 0x800;

    Mcs48(uint32_t clock, Chip chip);

    void set_io_handlers(PortInHandler in_port, PortOutHandler out_port);
    void set_external_handlers(ReadHandler read, WriteHandler write);

    uint8_t* rom() { return rom_.data(); }

    void reset();
    int run(int cycles);

    void set_irq(IrqLine state) { irq_ = state; }
    void set_reset_line(IrqLine state) { reset_ = state; }

    I8243& i8243() { return i8243_; }
    const I8243& i8243() const { return i8243_; }

    uint8_t upi41_master_r(uint8_t address);
    void upi41_master_w(uint8_t address, uint8_t value);

    uint32_t clock() const { return clock_; }
    uint16_t pc() const { return pc_; }
    Chip chip() const { return chip_; }

private:
    static constexpr uint8_t kStsIbf = 0x02;
    static constexpr uint8_t kStsObf = 0x01;
    static constexpr uint8_t kTimerEnabled = 0x01;
    static constexpr uint8_t kCounterEnabled = 0x02;
    static constexpr uint8_t kP2Obf = 0x10;
    static constexpr uint8_t kP2Nibf = 0x20;
    static constexpr uint8_t kP2Drq = 0x40;
    static constexpr uint8_t kP2Ndack = 0x80;
    static constexpr uint8_t kMcs48Feature = 0x01;
    static constexpr uint8_t kUpi41Feature = 0x02;

    uint8_t psw() const { return psw_; }
    void set_psw(uint8_t value);
    void update_regptr();

    uint8_t r(int n) const { return ram_[size_t(bank_base_ + n)]; }
    void set_r(int n, uint8_t value) { ram_[size_t(bank_base_ + n)] = value; }

    uint8_t ram_indir(int n) const { return ram_[r(n) & ram_mask_]; }
    void set_ram_indir(int n, uint8_t value) { ram_[r(n) & ram_mask_] = value; }

    uint8_t read_program(uint16_t address) const;
    uint8_t read_rom();
    uint8_t read_byte(uint16_t address) const;

    uint8_t test_r(uint8_t which) const;
    uint8_t bus_r() const;
    void bus_w(uint8_t value);
    uint8_t port_r(uint8_t port) const;
    void port_w(uint8_t port, uint8_t value);

    void expander_operation(int operation, uint8_t port);
    void push_pc_psw();
    int check_irqs();
    void burn_cycles(uint8_t count);
    uint8_t p2_mask() const;
    void add(uint8_t value);
    void addc(uint8_t value);
    void cond(bool test);

    uint32_t clock_;
    Chip chip_;
    uint16_t rom_mask_ = 0;
    uint8_t ram_mask_ = 0;
    uint8_t feature_mask_ = 0;

    uint16_t pc_ = 0;
    uint16_t old_pc_ = 0;
    uint16_t a11_ = 0;
    uint8_t a_ = 0;
    uint8_t p1_ = 0xff;
    uint8_t p2_ = 0xff;
    uint8_t psw_ = 0;
    uint8_t bank_base_ = 0;
    bool f1_ = false;

    uint8_t timer_ = 0;
    uint8_t prescaler_ = 0;
    uint8_t t1_history_ = 0;
    uint8_t sts_ = 0;
    uint8_t dbbi_ = 0;
    uint8_t dbbo_ = 0xff;
    uint8_t timecount_enabled_ = 0;

    bool irq_polled_ = false;
    bool tirq_enabled_ = false;
    bool xirq_enabled_ = false;
    bool timer_flag_ = false;
    bool flags_enabled_ = false;
    bool dma_enabled_ = false;
    bool irq_in_progress_ = false;
    bool timer_overflow_ = false;

    IrqLine irq_ = IrqLine::Clear;
    IrqLine reset_ = IrqLine::Clear;

    std::array<uint8_t, kRomSize> rom_{};
    std::array<uint8_t, 0x100> ram_{};

    PortInHandler in_port_;
    PortOutHandler out_port_;
    ReadHandler ext_in_;
    WriteHandler ext_out_;
    I8243 i8243_;
};

}  // namespace dsp
