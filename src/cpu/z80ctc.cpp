#include "cpu/z80ctc.h"

namespace dsp {

void Z80Ctc::reset() {
    for (auto& c : ch_) c = Channel{};
    irq_vector_ = 0;
    if (irq_cb_) irq_cb_(IrqLine::Clear);
}

void Z80Ctc::raise_irq() {
    if (irq_cb_) irq_cb_(IrqLine::Hold);
}

void Z80Ctc::write(uint8_t channel, uint8_t value) {
    Channel& c = ch_[channel & 3];
    if (c.waiting_const) {
        c.time_const = value ? value : 0x100;
        c.down = c.time_const;
        c.waiting_const = false;
        c.counting = true;
        return;
    }
    if (value & 1) {
        c.control = value;
        c.irq_enabled = (value & 0x80) != 0;
        if (value & 0x02) {
            c.counting = false;
            c.down = 0;
        }
        if (value & 0x04) c.waiting_const = true;
    } else if ((channel & 3) == 0) {
        irq_vector_ = uint8_t(value & 0xf8);
    }
}

uint8_t Z80Ctc::read(uint8_t channel) { return uint8_t(ch_[channel & 3].down); }

void Z80Ctc::trigger(int channel) {
    Channel& c = ch_[channel & 3];
    if (!c.counting && c.time_const) {
        c.down = c.time_const;
        c.counting = true;
    }
}

void Z80Ctc::channel_tick(int idx, int cycles) {
    Channel& c = ch_[idx];
    if (!c.counting || !c.time_const) return;
    const int prescale = (c.control & 0x20) ? 256 : 16;
    int steps = cycles / prescale;
    if (steps <= 0 && cycles > 0) steps = 1;  // ensure progress on slow rates
    for (int i = 0; i < steps; ++i) {
        if (c.down <= 1) {
            c.down = c.time_const;
            if (c.irq_enabled) raise_irq();
            if (zc_cb_) zc_cb_(idx);
            if (!(c.control & 0x40)) c.counting = false;
        } else {
            --c.down;
        }
    }
}

void Z80Ctc::tick(int cycles) {
    if (cycles <= 0) return;
    for (int i = 0; i < 4; ++i) channel_tick(i, cycles);
}

}  // namespace dsp
