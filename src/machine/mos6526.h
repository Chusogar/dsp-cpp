#pragma once

#include <cstdint>
#include <functional>

#include "cpu/irq_line.h"

namespace dsp {

// MOS 6526 CIA, ported from mos6526_old.pas (the chip commodore64.pas actually
// instantiates under {$DEFINE CIA_OLD}). Two independent instances: CIA1 (IRQ)
// and CIA2 (NMI).
class Mos6526 {
public:
    using PortRead = std::function<uint8_t()>;
    using PortWrite = std::function<void(uint8_t)>;
    using IrqCallback = std::function<void(IrqLine)>;

    explicit Mos6526(uint32_t clock);

    void set_calls(PortRead pa_read, PortRead pb_read, PortWrite pa_write, PortWrite pb_write,
                   IrqCallback irq);

    void reset();
    uint8_t read(uint8_t address);
    void write(uint8_t address, uint8_t value);

    // Advance `cycles` PHI2 ticks (one per CPU cycle on the C64).
    void sync(int cycles);
    void flag_w(uint8_t value);
    void clock_tod();

    uint8_t pa() const { return pa_; }
    uint8_t pb() const { return pb_; }
    uint8_t pra() const { return pra_; }
    uint8_t prb() const { return prb_; }

    // Active-low joystick bits (up/down/left/right/fire). Wired by the driver.
    uint8_t joystick1 = 0xff;
    uint8_t joystick2 = 0xff;

private:
    static constexpr uint8_t kIcrTa = 0x01;
    static constexpr uint8_t kIcrTb = 0x02;
    static constexpr uint8_t kIcrAlarm = 0x04;
    static constexpr uint8_t kIcrSp = 0x08;
    static constexpr uint8_t kIcrFlag = 0x10;

    void write_tod(int offset, uint8_t data);
    void set_cra(uint8_t data);
    void set_crb(uint8_t data);
    void update_pa();
    void update_pb();
    void clock_ta();
    void clock_tb();
    void update_interrupt();
    void clock_pipeline();

    uint32_t clock_ = 0;
    bool tod_stopped_ = true;
    bool irq_ = false;
    bool icr_read_ = false;
    uint8_t flag_ = 1;
    uint8_t ta_pb6_ = 0;
    uint8_t tb_pb7_ = 0;
    uint8_t cra_ = 0;
    uint8_t crb_ = 0;
    uint8_t ddra_ = 0xff;
    uint8_t ddrb_ = 0;
    uint8_t pa_in_ = 0;
    uint8_t pb_in_ = 0;
    uint8_t imr_ = 0;
    uint8_t icr_ = 0;
    uint8_t pra_ = 0;
    uint8_t prb_ = 0;
    uint8_t pa_ = 0xff;
    uint8_t pb_ = 0xff;
    uint32_t alarm_ = 0;
    uint32_t tod_ = 0x01000000;
    uint16_t ta_latch_ = 0xffff;
    uint16_t tb_latch_ = 0xffff;
    uint16_t ta_ = 0;
    uint16_t tb_ = 0;
    int tod_count_ = 0;
    int bits_ = 0;
    int ta_out_ = 0;
    int tb_out_ = 0;
    int ir0_ = 0;
    int ir1_ = 0;
    int load_a0_ = 0, load_a1_ = 0, load_a2_ = 0;
    int load_b0_ = 0, load_b1_ = 0, load_b2_ = 0;
    int pc_ = 1;
    int count_a0_ = 0, count_a1_ = 0, count_a2_ = 0, count_a3_ = 0, oneshot_a0_ = 0;
    int count_b0_ = 0, count_b1_ = 0, count_b2_ = 0, count_b3_ = 0, oneshot_b0_ = 0;
    int cnt_ = 1;

    PortRead pa_read_;
    PortRead pb_read_;
    PortWrite pa_write_;
    PortWrite pb_write_;
    IrqCallback irq_call_;
};

}  // namespace dsp
