#include "cpu/z80ctc.h"

namespace dsp {

void Z80Ctc::reset() {
    for (auto& c : ch_) c = Channel{};
    irq_vector_ = 0;
    if (irq_cb_) irq_cb_(IrqLine::Clear, 0);
}

void Z80Ctc::timer_callback(int ch) {
    Channel& c = ch_[ch & 3];
    if ((c.mode & kInterrupt) != 0 && irq_cb_) {
        irq_cb_(IrqLine::Hold, uint8_t(irq_vector_ + (ch & 3) * 2));
    }
    if (zc_cb_) zc_cb_(ch & 3);
    c.down = c.tconst;
}

uint8_t Z80Ctc::read(uint8_t channel) { return uint8_t(ch_[channel & 3].down); }

void Z80Ctc::write(uint8_t channel, uint8_t value) {
    Channel& c = ch_[channel & 3];
    if ((c.mode & kConstantLoad) != 0) {
        c.tconst = value ? value : 0x100;
        c.mode = uint16_t(c.mode & ~(kConstantLoad | kReset));
        c.down = c.tconst;
        c.acc = 0;
        if ((c.mode & kModeCounter) == 0) {
            if ((c.mode & kTriggerClock) == 0) {
                c.running = true;
                c.waiting_trig = false;
            } else {
                c.running = false;
                c.waiting_trig = true;
            }
        } else {
            c.running = false;
        }
        return;
    }
    if ((value & kControl) == 0 && (channel & 3) == 0) {
        irq_vector_ = uint8_t(value & 0xf8);
        return;
    }
    if (value & kControl) {
        c.mode = value;
        c.waiting_trig = false;
        if (value & kReset) {
            c.running = false;
            c.acc = 0;
        }
    }
}

void Z80Ctc::trigger(int channel, bool value) {
    Channel& c = ch_[channel & 3];
    if (value == c.extclk) return;
    c.extclk = value;
    const bool active = ((c.mode & kEdgeRising) != 0) ? value : !value;
    if (!active) return;

    if (c.waiting_trig && (c.mode & kModeCounter) == 0) {
        c.running = true;
        c.waiting_trig = false;
        c.acc = 0;
        c.down = c.tconst;
    }
    c.waiting_trig = false;

    if ((c.mode & kModeCounter) != 0 && (c.mode & kReset) == 0) {
        if (c.down <= 1) timer_callback(channel);
        else c.down--;
    }
}

void Z80Ctc::tick(int cycles) {
    if (cycles <= 0) return;
    for (int i = 0; i < 4; i++) {
        Channel& c = ch_[i];
        if (!c.running || (c.mode & kModeCounter) != 0 || (c.mode & kReset) != 0) continue;
        c.acc += cycles;
        const int step = prescale(c);
        while (c.acc >= step) {
            c.acc -= step;
            if (c.down <= 1) timer_callback(i);
            else c.down--;
        }
    }
}

}  // namespace dsp
