#include "machine/mos6532.h"

namespace dsp {
namespace {

constexpr uint8_t kIrqTimer = 0x80;

}  // namespace

void Mos6532::set_pa(PortRead in, PortWrite out) {
    pa_in_cb_ = std::move(in);
    pa_out_cb_ = std::move(out);
}

void Mos6532::set_pb(PortRead in, PortWrite out) {
    pb_in_cb_ = std::move(in);
    pb_out_cb_ = std::move(out);
}

void Mos6532::reset() {
    ram_.fill(0);
    pa_out_ = pb_out_ = 0;
    pa_ddr_ = pb_ddr_ = 0;
    pa_in_ = pb_in_ = 0xff;
    timer_count_ = 256 << 10;
    timer_shift_ = 10;
    timer_counting_ = true;
    ie_timer_ = false;
    irq_timer_ = false;
    update_pa();
    update_pb();
    update_irq();
}

void Mos6532::set_pa_in_bit(int bit, bool level) {
    const uint8_t mask = uint8_t(1u << bit);
    if (level) pa_in_ |= mask;
    else pa_in_ = uint8_t(pa_in_ & ~mask);
}

void Mos6532::update_pa() {
    if (pa_out_cb_) pa_out_cb_(uint8_t((pa_out_ & pa_ddr_) | (uint8_t(~pa_ddr_))));
}

void Mos6532::update_pb() {
    if (pb_out_cb_) pb_out_cb_(uint8_t((pb_out_ & pb_ddr_) | (uint8_t(~pb_ddr_))));
}

void Mos6532::update_irq() {
    if (irq_cb_) irq_cb_((ie_timer_ && irq_timer_) ? IrqLine::Assert : IrqLine::Clear);
}

uint8_t Mos6532::timer_value() const {
    const int shift = timer_counting_ ? timer_shift_ : 0;
    int val = timer_count_ >> shift;
    if (val < 0) val = 0;
    if (val > 255) val = 255;
    return uint8_t(val);
}

void Mos6532::timer_start(uint8_t data) {
    timer_counting_ = true;
    timer_count_ = (int(data) + 1) << timer_shift_;
}

void Mos6532::tick(int cycles) {
    if (cycles <= 0) return;
    timer_count_ -= cycles;
    if (timer_count_ <= 0) {
        if (timer_counting_) {
            irq_timer_ = true;
            update_irq();
            timer_counting_ = false;
        }
        // Keep spinning in 1-cycle mode after timeout, as the real RIOT does.
        while (timer_count_ <= 0) timer_count_ += 256;
    }
}

uint8_t Mos6532::io_read(uint8_t offset) {
    offset &= 0x1f;
    switch (offset & 0x07) {
        case 0x00:
            if ((offset & 0x18) == 0 || (offset & 0x18) == 0x18) {
                uint8_t in = pa_in_cb_ ? pa_in_cb_() : pa_in_;
                in = uint8_t((in & ~pa_ddr_) | (pa_out_ & pa_ddr_));
                return in;
            }
            break;
        case 0x01:
            return pa_ddr_;
        case 0x02: {
            uint8_t in = pb_in_cb_ ? pb_in_cb_() : pb_in_;
            return uint8_t((in & ~pb_ddr_) | (pb_out_ & pb_ddr_));
        }
        case 0x03:
            return pb_ddr_;
        default:
            break;
    }
    if ((offset & 0x04) && ((offset & 0x03) == 0)) {
        const bool ie = (offset & 0x08) != 0;
        const uint8_t value = timer_value();
        irq_timer_ = false;
        ie_timer_ = ie;
        if (!timer_counting_) timer_start(value);
        update_irq();
        return value;
    }
    if ((offset & 0x05) == 0x05) return irq_timer_ ? kIrqTimer : 0;
    return 0;
}

void Mos6532::io_write(uint8_t offset, uint8_t value) {
    offset &= 0x1f;
    if ((offset & 0x14) == 0x14) {
        static const int kShift[4] = {0, 3, 6, 10};
        timer_shift_ = kShift[offset & 3];
        timer_start(value);
        irq_timer_ = false;
        ie_timer_ = (offset & 0x08) != 0;
        update_irq();
        return;
    }
    switch (offset & 0x03) {
        case 0:
            pa_out_ = value;
            update_pa();
            break;
        case 1:
            pa_ddr_ = value;
            update_pa();
            break;
        case 2:
            pb_out_ = value;
            update_pb();
            break;
        case 3:
            pb_ddr_ = value;
            update_pb();
            break;
    }
}

}  // namespace dsp
