#pragma once

#include <array>
#include <cstdint>
#include <functional>

#include "cpu/irq_line.h"

namespace dsp {

// Z80 CTC (4 channels), subset for Midway MCR.
class Z80Ctc {
public:
    using IrqCallback = std::function<void(IrqLine)>;
    using ZcCallback = std::function<void(int channel)>;

    void set_irq_callback(IrqCallback cb) { irq_cb_ = std::move(cb); }
    void set_zc_callback(ZcCallback cb) { zc_cb_ = std::move(cb); }

    void reset();
    uint8_t read(uint8_t channel);
    void write(uint8_t channel, uint8_t value);
    void tick(int cycles);
    void trigger(int channel);

private:
    struct Channel {
        uint8_t control = 0;
        uint8_t time_const = 0;
        uint16_t down = 0;
        bool waiting_const = false;
        bool counting = false;
        bool irq_enabled = false;
    };

    void channel_tick(int ch, int cycles);
    void raise_irq();

    std::array<Channel, 4> ch_{};
    uint8_t irq_vector_ = 0;
    IrqCallback irq_cb_;
    ZcCallback zc_cb_;
};

}  // namespace dsp
