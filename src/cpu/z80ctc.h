#pragma once

#include <array>
#include <cstdint>
#include <functional>

#include "cpu/irq_line.h"

namespace dsp {

// Z80 CTC (Counter/Timer Circuit), ported from z80ctc.pas. Four channels,
// daisy-chain IRQ vectors (base + channel*2), timer vs external-counter
// modes, and a zero-crossing pulse used on MCR to cascade channel 0 into 1.
class Z80Ctc {
public:
    using IrqCallback = std::function<void(IrqLine, uint8_t vector)>;
    using ZcCallback = std::function<void(int channel)>;

    void set_irq_callback(IrqCallback cb) { irq_cb_ = std::move(cb); }
    void set_zc_callback(ZcCallback cb) { zc_cb_ = std::move(cb); }

    void reset();
    uint8_t read(uint8_t channel);
    void write(uint8_t channel, uint8_t value);
    void tick(int cycles);
    // External CLK/TRG pin; MCR pulses this true then false each scanline event.
    void trigger(int channel, bool value);
    void pulse_trigger(int channel) {
        trigger(channel, true);
        trigger(channel, false);
    }

    uint8_t irq_vector() const { return irq_vector_; }

private:
    static constexpr uint8_t kInterrupt = 0x80;
    static constexpr uint8_t kModeCounter = 0x40;
    static constexpr uint8_t kPrescale256 = 0x20;
    static constexpr uint8_t kEdgeRising = 0x10;
    static constexpr uint8_t kTriggerClock = 0x08;
    static constexpr uint8_t kConstantLoad = 0x04;
    static constexpr uint8_t kReset = 0x02;
    static constexpr uint8_t kControl = 0x01;

    struct Channel {
        uint16_t mode = kReset;
        uint16_t tconst = 0x100;
        uint16_t down = 0x100;
        int acc = 0;
        bool extclk = false;
        bool waiting_trig = false;
        bool running = false;
    };

    void timer_callback(int ch);
    int prescale(const Channel& c) const { return (c.mode & kPrescale256) ? 256 : 16; }

    std::array<Channel, 4> ch_{};
    uint8_t irq_vector_ = 0;
    IrqCallback irq_cb_;
    ZcCallback zc_cb_;
};

}  // namespace dsp
