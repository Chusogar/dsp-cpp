#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "cpu/irq_line.h"

namespace dsp {

// Fujitsu MB88xx 4-bit MCU family, ported from MAME mb88xx.cpp (Ernesto Corvi).
// The parent clock is divided by 6 internally. 51xx/52xx/53xx are MB8843;
// 54xx is MB8844.
class Mb88 {
public:
    using ReadNibble = std::function<uint8_t()>;
    using WriteNibble = std::function<void(uint8_t)>;
    using WriteByte = std::function<void(uint8_t)>;
    using ReadLine = std::function<int()>;

    enum class Type { Mb8841, Mb8842, Mb8843, Mb8844 };

    explicit Mb88(Type type, uint32_t clock);

    void set_program_rom(const uint8_t* data, size_t size);
    void set_k_read(ReadNibble handler) { read_k_ = std::move(handler); }
    void set_o_write(WriteByte handler) { write_o_ = std::move(handler); }
    void set_p_write(WriteNibble handler) { write_p_ = std::move(handler); }
    void set_r_read(int port, ReadNibble handler);
    void set_r_write(int port, WriteNibble handler);
    void set_si_read(ReadLine handler) { read_si_ = std::move(handler); }

    void reset();
    int run(int cycles);

    // External IRQ (rising logical edge). The pin is active-low on silicon.
    void set_irq(IrqLine state);
    // /TC timer clock. The timer counts on a falling edge when PIO bit 6 is set.
    void set_tc(bool state);
    // /RESET, active low: true means the MCU is held in reset.
    void set_reset_line(bool asserted);

    Type type() const { return type_; }
    uint16_t pc() const { return uint16_t((uint16_t(pa_) << 6) | pc_); }
    uint32_t clock() const { return clock_; }
    bool reset_asserted() const { return reset_asserted_; }

private:
    static constexpr int kSerialPrescale = 6;
    static constexpr int kTimerPrescale = 32;
    static constexpr int kSerialDisableThresh = 1000;
    static constexpr uint8_t kIntSerial = 0x01;
    static constexpr uint8_t kIntTimer = 0x02;
    static constexpr uint8_t kIntExternal = 0x04;

    uint8_t fetch();
    uint8_t read_data(uint8_t ea) const;
    void write_data(uint8_t ea, uint8_t value);
    void inc_pc();
    void write_pla(uint8_t index);
    void update_pio_enable(uint8_t newpio);
    void increment_timer();
    void update_pio(int cycles);
    void serial_tick();
    void execute_one();

    Type type_;
    uint32_t clock_;
    int program_width_ = 10;
    int data_width_ = 6;
    uint16_t program_mask_ = 0x3ff;
    uint8_t data_mask_ = 0x3f;
    int pla_bits_ = 8;

    std::vector<uint8_t> program_;
    std::vector<uint8_t> ram_;

    uint8_t pc_ = 0;
    uint8_t pa_ = 0;
    uint16_t sp_[4] = {};
    uint8_t si_ = 0;
    uint8_t a_ = 0;
    uint8_t x_ = 0;
    uint8_t y_ = 0;
    uint8_t st_ = 1;
    uint8_t zf_ = 0;
    uint8_t cf_ = 0;
    uint8_t vf_ = 0;
    uint8_t sf_ = 0;
    uint8_t nf_ = 0;
    uint8_t pio_ = 0;
    uint8_t th_ = 0;
    uint8_t tl_ = 0;
    uint8_t tp_ = 0;
    uint8_t sb_ = 0;
    uint16_t sb_count_ = 0;
    uint8_t pending_interrupt_ = 0;
    uint8_t o_output_ = 0;
    bool tc_ = false;
    bool reset_asserted_ = false;
    int icount_ = 0;
    int serial_cycle_acc_ = 0;

    ReadNibble read_k_;
    WriteByte write_o_;
    WriteNibble write_p_;
    ReadNibble read_r_[4];
    WriteNibble write_r_[4];
    ReadLine read_si_;
};

}  // namespace dsp
