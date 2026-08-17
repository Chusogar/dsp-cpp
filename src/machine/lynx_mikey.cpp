#include "machine/lynx_mikey.h"

#include <algorithm>

namespace dsp {
namespace {

int clamp16(int value) {
    if (value > 32767) return 32767;
    if (value < -32768) return -32768;
    return value;
}

}  // namespace

LynxMikey::LynxMikey() { reset(); }

void LynxMikey::reset() {
    data_.fill(0);
    data_[0x88] = 0x01;
    timers_ = {};
    audio_ = {};
    for (auto& channel : audio_) channel.shifter = 1;
    interrupt_ = 0;
    disp_addr_ = 0;
    vb_rest_ = false;
    update_irq();
}

int LynxMikey::prescale(int clock_sel) {
    static const int kScale[7] = {4, 8, 16, 32, 64, 128, 256};
    if (clock_sel < 0 || clock_sel > 6) return 4;
    return kScale[clock_sel];
}

void LynxMikey::update_irq() {
    if (irq_cb_) irq_cb_(interrupt_ != 0);
}

void LynxMikey::signal_irq(int which) {
    if (timers_[size_t(which)].int_en() && which != 4) {
        interrupt_ = uint8_t(interrupt_ | (1 << which));
        if (wake_cb_) wake_cb_();
        update_irq();
    }
    switch (which) {
        case 0:
            if (scanline_cb_) scanline_cb_(timers_[2].counter);
            if (timers_[2].counter == 103) vb_rest_ = true;
            if (timers_[2].counter == 102) {
                disp_addr_ = uint16_t(data_[0x94] | (data_[0x95] << 8));
            }
            if (timers_[2].counter == 101) vb_rest_ = false;
            count_down_linked(2);
            break;
        case 2:
            count_down_linked(4);
            break;
        case 1:
            count_down_linked(3);
            break;
        case 3:
            count_down_linked(5);
            break;
        case 5:
            count_down_linked(7);
            break;
        case 7:
            if (audio_[0].linked() && audio_[0].count_en() && !audio_[0].done()) {
                if (audio_[0].counter > 0) {
                    audio_[0].counter -= 1;
                } else {
                    clock_audio(0);
                }
            }
            break;
        default:
            break;
    }
}

void LynxMikey::count_down_linked(int which) {
    Timer& timer = timers_[size_t(which)];
    if (!timer.count_en() || !timer.linked() || timer.done()) return;
    if (timer.counter > 0) {
        timer.counter -= 1;
        return;
    }
    expire_timer(which);
}

void LynxMikey::expire_timer(int which) {
    signal_irq(which);
    Timer& timer = timers_[size_t(which)];
    if (timer.reload_en()) {
        timer.counter = timer.bakup;
    } else {
        timer.set_done(true);
    }
}

uint8_t LynxMikey::timer_read(int which, int offset) const {
    const Timer& timer = timers_[size_t(which)];
    switch (offset) {
        case 0:
            return timer.bakup;
        case 1:
            return timer.cntrl1;
        case 2:
            return timer.counter;
        case 3:
            return timer.cntrl2;
        default:
            return 0;
    }
}

void LynxMikey::timer_write(int which, int offset, uint8_t value) {
    Timer& timer = timers_[size_t(which)];
    switch (offset) {
        case 0:
            timer.bakup = value;
            break;
        case 1:
            timer.cntrl1 = value;
            if (value & 0x40) timer.set_done(false);
            if (timer.count_en() && timer.counter == 0) timer.counter = timer.bakup;
            timer.accum = 0;
            break;
        case 2:
            timer.counter = value;
            break;
        case 3:
            timer.cntrl2 = uint8_t((timer.cntrl2 & ~0x08) | (value & 0x08));
            break;
    }
}

void LynxMikey::clock_audio(int channel) {
    Audio& audio = audio_[size_t(channel)];
    uint16_t shifter = uint16_t(audio.shifter & 0x0fff);
    int taps = 0;
    const uint16_t taps_mask =
        uint16_t(audio.feedback | (uint16_t(audio.other & 0x0f) << 8));
    for (int bit = 0; bit < 12; bit++) {
        if ((taps_mask & (1 << bit)) && (shifter & (1 << bit))) taps ^= 1;
    }
    const int out_bit = shifter & 1;
    shifter = uint16_t((shifter >> 1) | (taps ? 0x800 : 0));
    audio.shifter = shifter;
    audio.output = out_bit ? audio.volume : int8_t(-audio.volume);
    if (audio.reload_en()) {
        audio.counter = audio.bakup;
    } else {
        audio.other |= 0x08;
    }
    if (channel + 1 < kAudioCount) {
        Audio& next = audio_[size_t(channel + 1)];
        if (next.linked() && next.count_en() && !next.done()) {
            if (next.counter > 0) {
                next.counter -= 1;
            } else {
                clock_audio(channel + 1);
            }
        }
    }
}

uint8_t LynxMikey::audio_read(int channel, int offset) const {
    const Audio& audio = audio_[size_t(channel)];
    switch (offset) {
        case 0:
            return uint8_t(audio.volume);
        case 1:
            return audio.feedback;
        case 2:
            return uint8_t(audio.output);
        case 3:
            return uint8_t(audio.shifter);
        case 4:
            return audio.bakup;
        case 5:
            return audio.control;
        case 6:
            return audio.counter;
        case 7:
            return uint8_t((audio.other & 0xf0) | ((audio.shifter >> 8) & 0x0f));
        default:
            return 0;
    }
}

void LynxMikey::audio_write(int channel, int offset, uint8_t value) {
    Audio& audio = audio_[size_t(channel)];
    switch (offset) {
        case 0:
            audio.volume = int8_t(value);
            break;
        case 1:
            audio.feedback = value;
            break;
        case 2:
            audio.output = int8_t(value);
            break;
        case 3:
            audio.shifter = uint16_t((audio.shifter & 0x0f00) | value);
            break;
        case 4:
            audio.bakup = value;
            break;
        case 5:
            audio.control = value;
            if (value & 0x40) audio.other &= uint8_t(~0x08);
            audio.accum = 0;
            break;
        case 6:
            audio.counter = value;
            break;
        case 7:
            audio.other = value;
            audio.shifter = uint16_t((audio.shifter & 0x00ff) | (uint16_t(value & 0x0f) << 8));
            break;
    }
}

void LynxMikey::tick(int cpu_cycles) {
    for (int which = 0; which < kTimerCount; which++) {
        Timer& timer = timers_[size_t(which)];
        if (!timer.count_en() || timer.linked() || timer.done()) continue;
        const int scale = prescale(timer.clock_sel());
        timer.accum += cpu_cycles;
        while (timer.accum >= scale) {
            timer.accum -= scale;
            if (timer.counter > 0) {
                timer.counter -= 1;
            } else {
                expire_timer(which);
            }
        }
    }
    for (int channel = 0; channel < kAudioCount; channel++) {
        Audio& audio = audio_[size_t(channel)];
        if (!audio.count_en() || audio.linked() || audio.done()) continue;
        const int scale = prescale(audio.clock_sel());
        audio.accum += cpu_cycles;
        while (audio.accum >= scale) {
            audio.accum -= scale;
            if (audio.counter > 0) {
                audio.counter -= 1;
            } else {
                clock_audio(channel);
            }
        }
    }
}

uint32_t LynxMikey::pal_argb(int index) const {
    const int pen = index & 0x0f;
    const uint8_t green = uint8_t(data_[0xa0 + pen] & 0x0f);
    const uint8_t red = uint8_t(data_[0xb0 + pen] & 0x0f);
    const uint8_t blue = uint8_t(data_[0xb0 + pen] >> 4);
    const uint32_t r = uint32_t(red) * 17;
    const uint32_t g = uint32_t(green) * 17;
    const uint32_t b = uint32_t(blue) * 17;
    return 0xff000000u | (r << 16) | (g << 8) | b;
}

int16_t LynxMikey::mix_sample() const {
    int mix = 0;
    for (const Audio& audio : audio_) mix += audio.output * 64;
    return int16_t(clamp16(mix));
}

uint8_t LynxMikey::read(uint8_t offset) {
    if (offset < 0x20) return timer_read(offset >> 2, offset & 3);
    if (offset >= 0x20 && offset < 0x40) {
        return audio_read((offset - 0x20) >> 3, offset & 7);
    }
    switch (offset) {
        case 0x80:
        case 0x81:
            return interrupt_;
        case 0x84:
        case 0x85:
            return 0x00;
        case 0x86:
            return 0x80;
        case 0x88:
            return 0x01;
        case 0x8b: {
            const uint8_t direction = data_[0x8a];
            uint8_t value = 0;
            value |= (direction & 0x01) ? (data_[0x8b] & 0x01) : 0x01;
            value |= (direction & 0x02) ? (data_[0x8b] & 0x02) : 0x00;
            value |= 0x04;  // no expansion / ComLynx disabled
            if (direction & 0x08) {
                value |= ((data_[0x8b] & 0x08) && vb_rest_) ? 0x00 : 0x08;
            }
            value |= (direction & 0x10) ? (data_[0x8b] & 0x10) : 0x10;
            return value;
        }
        case 0x8c:
            return 0xa0;  // TXRDY | TXEMPTY, UART idle
        case 0x8d:
            return 0x00;
        default:
            return data_[offset];
    }
}

void LynxMikey::write(uint8_t offset, uint8_t value) {
    if (offset < 0x20) {
        timer_write(offset >> 2, offset & 3, value);
        return;
    }
    if (offset >= 0x20 && offset < 0x40) {
        audio_write((offset - 0x20) >> 3, offset & 7, value);
        data_[offset] = value;
        return;
    }
    switch (offset) {
        case 0x80:
            interrupt_ = uint8_t(interrupt_ & ~value);
            update_irq();
            break;
        case 0x81:
            interrupt_ = uint8_t(interrupt_ | value);
            if (value && wake_cb_) wake_cb_();
            update_irq();
            break;
        case 0x87: {
            data_[offset] = value;
            if (sysctl_cb_) sysctl_cb_(value, data_[0x8b]);
            break;
        }
        case 0x8b:
            data_[offset] = value;
            break;
        case 0x91:
            data_[offset] = value;
            break;
        case 0x94:
        case 0x95:
            data_[offset] = value;
            break;
        default:
            data_[offset] = value;
            break;
    }
}

}  // namespace dsp
