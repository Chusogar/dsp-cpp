#include "sound/ym2612.h"

#include <algorithm>

namespace dsp {

YM2612::YM2612(uint32_t clock, float amplitude)
    : opn_(6, clock, kSampleRate), amplitude_(amplitude) {
    reset();
}

void YM2612::reset() {
    // OPN2 has a fixed /72 FM prescaler (same default as YM2203 after reset).
    opn_.prescaler_w(0, 1);
    opn_.irq_mask_set(0x03);
    opn_.write_mode(0x27, 0x30);
    opn_.reset_eg_timer();
    opn_.status_reset(0xff);
    opn_.set_mode(0);
    opn_.reset_timers();
    opn_.reset_channels();
    for (int part = 0; part < 2; part++) {
        const int base = part * 3;
        for (int reg = 0xb6; reg >= 0x30; reg--) opn_.write_reg(reg, 0, base);
    }
    for (int reg = 0x26; reg >= 0x20; reg--) opn_.write_mode(reg, 0);
    address_[0] = address_[1] = 0;
    dac_data_ = 0x80;
    dac_enable_ = false;
    // Default both speakers on for every channel (matches a hardware reset).
    for (int ch = 0; ch < 6; ch++) {
        opn_.channel(ch).pan_left = true;
        opn_.channel(ch).pan_right = true;
    }
}

void YM2612::write(int port, uint8_t value) {
    const int part = (port >> 1) & 1;
    if ((port & 1) == 0) {
        address_[part] = value;
        opn_.set_address(value);
        return;
    }
    const uint8_t address = address_[part];
    if (part == 0) {
        if (address == 0x2a) {
            dac_data_ = value;
            return;
        }
        if (address == 0x2b) {
            dac_enable_ = (value & 0x80) != 0;
            return;
        }
        if (address < 0x30) {
            opn_.write_mode(address, value);
            return;
        }
        opn_.write_reg(address, value, 0);
        return;
    }
    if (address >= 0x30) opn_.write_reg(address, value, 3);
}

uint8_t YM2612::read(int port) const {
    (void)port;
    // Bit 7 is the busy flag; we complete writes instantly.
    return uint8_t(opn_.status() & 0x83);
}

int32_t YM2612::update() {
    for (int ch = 0; ch < 6; ch++) {
        if (ch == 2 && (opn_.mode() & 0xc0) != 0) {
            OpnCore::Channel& ch2 = opn_.channel(2);
            if (ch2.slot[OpnCore::kSlot1].incr == -1) {
                OpnCore::ThreeSlot& sl3 = opn_.three_slot();
                opn_.refresh_fc_eg_slot(ch2.slot[OpnCore::kSlot1], int(sl3.fc[1]), sl3.kcode[1]);
                opn_.refresh_fc_eg_slot(ch2.slot[OpnCore::kSlot2], int(sl3.fc[2]), sl3.kcode[2]);
                opn_.refresh_fc_eg_slot(ch2.slot[OpnCore::kSlot3], int(sl3.fc[0]), sl3.kcode[0]);
                opn_.refresh_fc_eg_slot(ch2.slot[OpnCore::kSlot4], int(ch2.fc), ch2.kcode);
            }
        } else {
            opn_.refresh_fc_eg_chan(opn_.channel(ch));
        }
    }

    opn_.clear_bus();
    opn_.advance_envelopes();

    int32_t mix = 0;
    for (int ch = 0; ch < 6; ch++) {
        if (ch == 5 && dac_enable_) {
            // 8-bit unsigned PCM, centred on 0x80.
            const int32_t dac = (int32_t(dac_data_) - 0x80) << 6;
            mix += dac;
            continue;
        }
        opn_.chan_calc(opn_.channel(ch));
        const int32_t sample = opn_.channel_output(ch);
        const OpnCore::Channel& channel = opn_.channel(ch);
        if (channel.pan_left || channel.pan_right) mix += sample;
    }

    int32_t out = int32_t(float(mix) * amplitude_);
    out = std::max(-0x7fff, std::min(0x7fff, out));

    opn_.internal_timer_a(opn_.channel(2));
    opn_.internal_timer_b();
    return out;
}

}  // namespace dsp
