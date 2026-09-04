#include "sound/ym2203.h"

#include <algorithm>

namespace dsp {

YM2203::YM2203(uint32_t clock, float amplitude, float ay_amplitude)
    : opn_(3, clock, kSampleRate), ay_(clock, ay_amplitude), amplitude_(amplitude) {
    opn_.set_ssg_clock_handler([this](uint32_t ay_clock) { ay_.set_clock(ay_clock); });
    reset();
}

void YM2203::set_irq_handler(IrqHandler handler) { opn_.set_irq_handler(std::move(handler)); }

void YM2203::set_port_handlers(AY8910::PortRead port_a_read, AY8910::PortRead port_b_read,
                               AY8910::PortWrite port_a_write, AY8910::PortWrite port_b_write) {
    ay_.set_port_handlers(std::move(port_a_read), std::move(port_b_read), std::move(port_a_write),
                          std::move(port_b_write));
}

void YM2203::reset() {
    external_timers_ = false;
    forced_tb_ = 0;
    tb_latched_ = false;
    opn_.prescaler_w(0, 1);
    ay_.reset();
    opn_.irq_mask_set(0x03);
    opn_.write_mode(0x27, 0x30);  // mode 0, timer reset
    opn_.reset_eg_timer();
    opn_.status_reset(0xff);
    opn_.set_mode(0);
    opn_.reset_timers();
    opn_.reset_channels();
    for (int reg = 0xb2; reg >= 0x30; reg--) opn_.write_reg(reg, 0);
    for (int reg = 0x26; reg >= 0x20; reg--) opn_.write_reg(reg, 0);
}

void YM2203::write_port(int port, uint8_t value) {
    if ((port & 1) == 0) {
        opn_.set_address(value);
        if (value < 0x10) ay_.control(value);
        if (value >= 0x2d && value <= 0x2f) opn_.prescaler_w(value, 1);
        return;
    }
    const uint8_t address = opn_.address();
    regs_[address] = value;
    if (address == 0x27 && (value & 0x02) == 0) tb_latched_ = false;
    if ((address & 0xf0) == 0x00) {
        ay_.write(value);
    } else if ((address & 0xf0) == 0x20) {
        opn_.write_mode(address, value);
    } else {
        opn_.write_reg(address, value);
    }
}

uint8_t YM2203::read() { return opn_.address() < 16 ? ay_.read() : 0; }

int32_t YM2203::update() {
    OpnCore::Channel& ch0 = opn_.channel(0);
    OpnCore::Channel& ch1 = opn_.channel(1);
    OpnCore::Channel& ch2 = opn_.channel(2);

    opn_.refresh_fc_eg_chan(ch0);
    opn_.refresh_fc_eg_chan(ch1);
    if ((opn_.mode() & 0xc0) != 0) {
        // 3 slot mode
        if (ch2.slot[OpnCore::kSlot1].incr == -1) {
            OpnCore::ThreeSlot& sl3 = opn_.three_slot();
            opn_.refresh_fc_eg_slot(ch2.slot[OpnCore::kSlot1], int(sl3.fc[1]), sl3.kcode[1]);
            opn_.refresh_fc_eg_slot(ch2.slot[OpnCore::kSlot2], int(sl3.fc[2]), sl3.kcode[2]);
            opn_.refresh_fc_eg_slot(ch2.slot[OpnCore::kSlot3], int(sl3.fc[0]), sl3.kcode[0]);
            opn_.refresh_fc_eg_slot(ch2.slot[OpnCore::kSlot4], int(ch2.fc), ch2.kcode);
        }
    } else {
        opn_.refresh_fc_eg_chan(ch2);
    }

    opn_.clear_bus();
    opn_.advance_envelopes();
    opn_.chan_calc(ch0);
    opn_.chan_calc(ch1);
    opn_.chan_calc(ch2);

    const int32_t fm =
        opn_.channel_output(0) + opn_.channel_output(1) + opn_.channel_output(2);
    int32_t out = ay_.update() + int32_t(float(fm) * amplitude_);
    out = std::max(-0x7fff, std::min(0x7fff, out));

    if (!external_timers_) {
        opn_.internal_timer_a(ch2);
        opn_.internal_timer_b();
    }
    return out;
}

void YM2203::run_timers(int cycles) {
    external_timers_ = true;
    if (cycles > 0 && (regs_[0x27] & 0x02) != 0) {
        // Integer backup for timer B: Space Harrier's $0D33 poll
        // can miss a floating-point countdown that never quite
        // underflows when the Z80 feeds 20-cycle slices.
        forced_tb_ += cycles;
        const int period = std::max(1, (256 - int(regs_[0x26])) * 16) * 72;
        if (forced_tb_ >= period) {
            forced_tb_ -= period;
            tb_latched_ = true;
            opn_.timer_b_over();
        }
    }
    opn_.advance_timers(cycles, opn_.channel(2));
}

}  // namespace dsp
