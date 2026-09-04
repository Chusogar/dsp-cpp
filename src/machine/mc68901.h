#pragma once

#include <cstdint>
#include <functional>

namespace dsp {

// Motorola MC68901 Multi-Function Peripheral, enough for the Atari ST:
// GPIP, four timers in delay mode, and the interrupt controller.
class Mc68901 {
public:
    using IrqCallback = std::function<void(bool)>;

    static constexpr int kClock = 2457600;

    void reset();
    void set_irq_callback(IrqCallback cb) { irq_cb_ = std::move(cb); }

    uint8_t read(int offset);
    void write(int offset, uint8_t value);

    // Advance by `ticks` of the 2.4576 MHz MFP clock.
    void tick(int ticks);

    void set_gpip_bit(int bit, int value);
    uint8_t gpip() const { return gpip_; }

    // 68000 interrupt acknowledge: vector number, or -1 if nothing pending.
    int irq_ack();
    bool irq_pending() const { return irq_; }

private:
    void update_irq();
    void raise(int channel);
    void update_irq();

    struct Timer {
        uint8_t control = 0;
        uint8_t data = 0;
        uint8_t count = 0;
        int prescale_acc = 0;
        int channel = 0;
    };

    void tick_timer(Timer& t, int ticks);

    uint8_t gpip_ = 0xff;
    uint8_t aer_ = 0;
    uint8_t ddr_ = 0;
    uint8_t iera_ = 0, ierb_ = 0;
    uint8_t ipra_ = 0, iprb_ = 0;
    uint8_t isra_ = 0, isrb_ = 0;
    uint8_t imra_ = 0, imrb_ = 0;
    uint8_t vr_ = 0x40;
    uint8_t tacr_ = 0, tbcr_ = 0, tcdcr_ = 0;
    Timer ta_{}, tb_{}, tc_{}, td_{};
    bool irq_ = false;
    IrqCallback irq_cb_;
};

}  // namespace dsp
