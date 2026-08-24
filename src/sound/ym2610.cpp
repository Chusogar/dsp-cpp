#include "sound/ym2610.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace dsp {
namespace {

const std::array<int, 8> kIndexShift = {-1, -1, -1, -1, 2, 4, 6, 8};

const std::array<int, 49 * 16>& diff_table() {
    static const std::array<int, 49 * 16> table = [] {
        static const int nibble_to_bit[16][4] = {
            {1, 0, 0, 0},  {1, 0, 0, 1},  {1, 0, 1, 0},  {1, 0, 1, 1},
            {1, 1, 0, 0},  {1, 1, 0, 1},  {1, 1, 1, 0},  {1, 1, 1, 1},
            {-1, 0, 0, 0}, {-1, 0, 0, 1}, {-1, 0, 1, 0}, {-1, 0, 1, 1},
            {-1, 1, 0, 0}, {-1, 1, 0, 1}, {-1, 1, 1, 0}, {-1, 1, 1, 1}};
        std::array<int, 49 * 16> values{};
        for (int step = 0; step <= 48; step++) {
            int step_value = int(std::floor(16.0 * std::pow(11.0 / 10.0, step)));
            for (int nibble = 0; nibble < 16; nibble++) {
                const int* bits = nibble_to_bit[nibble];
                values[size_t(step * 16 + nibble)] =
                    bits[0] * (step_value * bits[1] + (step_value >> 1) * bits[2] +
                               (step_value >> 2) * bits[3] + (step_value >> 3));
            }
        }
        return values;
    }();
    return table;
}

int volume_from_tl(int tl, int level) {
    int att = (tl & 0x3f) + (level & 0x1f) * 2;
    if (att > 63) att = 63;
    return 63 - att;
}

}  // namespace

YM2610::YM2610(uint32_t clock, float amplitude)
    : opn_(4, clock, kSampleRate), ay_(clock / 4, 0.6f), amplitude_(amplitude), clock_(clock) {
    adpcm_a_step_ = (double(clock) / 216.0) / double(kSampleRate);
    reset();
}

void YM2610::reset() {
    opn_.prescaler_w(0, 1);
    ay_.reset();
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
    for (int ch = 0; ch < 4; ch++) {
        opn_.channel(ch).pan_left = true;
        opn_.channel(ch).pan_right = true;
    }
    address_[0] = address_[1] = 0;
    adpcm_a_ = {};
    adpcm_a_tl_ = 0x3f;
    adpcm_a_flags_ = 0;
    adpcm_a_acc_ = 0;
    adpcm_a_mix_ = 0;
    adpcm_b_playing_ = false;
    adpcm_b_repeat_ = false;
    adpcm_b_start_ = adpcm_b_end_ = adpcm_b_pos_ = 0;
    adpcm_b_delta_ = 0;
    adpcm_b_volume_ = 0;
    adpcm_b_pan_ = 0xc0;
    adpcm_b_signal_ = 0;
    adpcm_b_step_ = 0;
    adpcm_b_nibble_ = 0;
    adpcm_b_status_ = 0;
    adpcm_b_acc_ = 0;
    adpcm_b_mix_ = 0;
}

void YM2610::write(int port, uint8_t value) {
    const int part = (port >> 1) & 1;
    if ((port & 1) == 0) {
        address_[part] = value;
        if (part == 0) opn_.set_address(value);
        return;
    }
    if (part == 0) write_part0(address_[0], value);
    else write_part1(address_[1], value);
}

uint8_t YM2610::read(int port) {
    switch (port & 3) {
        case 0:
            return uint8_t((opn_.status() & 0x83) | adpcm_b_status_);
        case 1:
            return opn_.address() < 0x10 ? ay_.read() : 0;
        case 2:
            return adpcm_a_flags_;
        default:
            return 0;
    }
}

void YM2610::write_part0(uint8_t address, uint8_t value) {
    if (address < 0x10) {
        ay_.control(address);
        ay_.write(value);
        return;
    }
    if (address < 0x1d) {
        switch (address) {
            case 0x10:
                if (value & 0x01) {
                    adpcm_b_playing_ = false;
                    adpcm_b_signal_ = 0;
                    adpcm_b_step_ = 0;
                    adpcm_b_status_ &= ~0x20;
                    adpcm_b_mix_ = 0;
                }
                adpcm_b_repeat_ = (value & 0x10) != 0;
                if (value & 0x80) {
                    adpcm_b_pos_ = adpcm_b_start_;
                    adpcm_b_nibble_ = 0;
                    adpcm_b_signal_ = 0;
                    adpcm_b_step_ = 0;
                    adpcm_b_playing_ = true;
                    adpcm_b_status_ &= ~0x20;
                }
                break;
            case 0x11:
                adpcm_b_pan_ = value;
                break;
            case 0x12:
                adpcm_b_start_hi_ = value;
                adpcm_b_start_ = (uint32_t(adpcm_b_start_hi_) << 16) | (uint32_t(adpcm_b_start_lo_) << 8);
                break;
            case 0x13:
                adpcm_b_start_lo_ = value;
                adpcm_b_start_ = (uint32_t(adpcm_b_start_hi_) << 16) | (uint32_t(adpcm_b_start_lo_) << 8);
                break;
            case 0x14:
                adpcm_b_end_hi_ = value;
                adpcm_b_end_ = (uint32_t(adpcm_b_end_hi_) << 16) | (uint32_t(adpcm_b_end_lo_) << 8);
                break;
            case 0x15:
                adpcm_b_end_lo_ = value;
                adpcm_b_end_ = (uint32_t(adpcm_b_end_hi_) << 16) | (uint32_t(adpcm_b_end_lo_) << 8);
                break;
            case 0x19:
                adpcm_b_delta_hi_ = value;
                adpcm_b_delta_ = uint16_t((uint16_t(adpcm_b_delta_hi_) << 8) | adpcm_b_delta_lo_);
                break;
            case 0x1a:
                adpcm_b_delta_lo_ = value;
                adpcm_b_delta_ = uint16_t((uint16_t(adpcm_b_delta_hi_) << 8) | adpcm_b_delta_lo_);
                break;
            case 0x1b:
                adpcm_b_volume_ = value;
                break;
            case 0x1c:
                adpcm_b_status_ &= uint8_t(~(value & 0xc0));
                break;
            default:
                break;
        }
        return;
    }
    if (address < 0x30) {
        opn_.write_mode(address, value);
        return;
    }
    opn_.write_reg(address, value, 0);
}

void YM2610::write_part1(uint8_t address, uint8_t value) {
    if (address < 0x30) {
        switch (address) {
            case 0x00:
                adpcm_a_key(value);
                break;
            case 0x01:
                adpcm_a_tl_ = value;
                break;
            default:
                if (address >= 0x08 && address <= 0x0d) {
                    adpcm_a_[size_t(address - 0x08)].pan_level = value;
                } else if (address >= 0x10 && address <= 0x15) {
                    adpcm_a_[size_t(address - 0x10)].start_hi = value;
                } else if (address >= 0x18 && address <= 0x1d) {
                    adpcm_a_[size_t(address - 0x18)].start_lo = value;
                } else if (address >= 0x20 && address <= 0x25) {
                    adpcm_a_[size_t(address - 0x20)].end_hi = value;
                } else if (address >= 0x28 && address <= 0x2d) {
                    adpcm_a_[size_t(address - 0x28)].end_lo = value;
                }
                break;
        }
        return;
    }
    opn_.write_reg(address, value, 3);
}

void YM2610::adpcm_a_key(uint8_t value) {
    for (int ch = 0; ch < 6; ch++) {
        const uint8_t bit = uint8_t(1u << ch);
        AdpcmA& a = adpcm_a_[size_t(ch)];
        if ((value & 0x80) != 0) {
            if ((value & bit) != 0) {
                a.playing = false;
                a.signal = 0;
                a.step = 0;
                adpcm_a_flags_ &= uint8_t(~bit);
            }
            continue;
        }
        if ((value & bit) == 0) continue;
        a.start = (uint32_t(a.start_hi) << 16) | (uint32_t(a.start_lo) << 8);
        a.end = (uint32_t(a.end_hi) << 16) | (uint32_t(a.end_lo) << 8);
        a.pos = a.start;
        a.nibble = 0;
        a.signal = 0;
        a.step = 0;
        a.playing = true;
        adpcm_a_flags_ &= uint8_t(~bit);
    }
}

int YM2610::decode_nibble(int& signal, int& step, uint8_t nibble) {
    signal += diff_table()[size_t(step * 16 + (nibble & 0x0f))];
    if (signal > 2047) signal = 2047;
    else if (signal < -2048) signal = -2048;
    step += kIndexShift[size_t(nibble & 0x07)];
    if (step > 48) step = 48;
    else if (step < 0) step = 0;
    return signal;
}

void YM2610::clock_adpcm_a() {
    int32_t mix = 0;
    for (int ch = 0; ch < 6; ch++) {
        AdpcmA& a = adpcm_a_[size_t(ch)];
        if (!a.playing) continue;
        if (adpcm_a_rom_.empty() || a.pos >= adpcm_a_rom_.size() || a.pos > a.end) {
            a.playing = false;
            adpcm_a_flags_ |= uint8_t(1u << ch);
            continue;
        }
        const uint8_t byte = adpcm_a_rom_[a.pos];
        const uint8_t nibble = a.nibble == 0 ? uint8_t(byte >> 4) : uint8_t(byte & 0x0f);
        a.nibble ^= 1;
        if (a.nibble == 0) {
            a.pos++;
            if (a.pos > a.end) {
                a.playing = false;
                adpcm_a_flags_ |= uint8_t(1u << ch);
            }
        }
        const int sample = decode_nibble(a.signal, a.step, nibble);
        const int vol = volume_from_tl(adpcm_a_tl_, a.pan_level);
        if ((a.pan_level & 0xc0) != 0) mix += (sample * vol) / 8;
    }
    adpcm_a_mix_ = mix;
}

void YM2610::clock_adpcm_b() {
    if (!adpcm_b_playing_) {
        adpcm_b_mix_ = 0;
        return;
    }
    if (adpcm_b_rom_.empty() || adpcm_b_pos_ >= adpcm_b_rom_.size() || adpcm_b_pos_ > adpcm_b_end_) {
        if (adpcm_b_repeat_) {
            adpcm_b_pos_ = adpcm_b_start_;
            adpcm_b_nibble_ = 0;
            adpcm_b_signal_ = 0;
            adpcm_b_step_ = 0;
        } else {
            adpcm_b_playing_ = false;
            adpcm_b_status_ |= 0x20;
            adpcm_b_mix_ = 0;
            return;
        }
    }
    const uint8_t byte = adpcm_b_rom_[adpcm_b_pos_];
    const uint8_t nibble = adpcm_b_nibble_ == 0 ? uint8_t(byte >> 4) : uint8_t(byte & 0x0f);
    adpcm_b_nibble_ ^= 1;
    if (adpcm_b_nibble_ == 0) adpcm_b_pos_++;
    const int sample = decode_nibble(adpcm_b_signal_, adpcm_b_step_, nibble);
    if ((adpcm_b_pan_ & 0xc0) != 0) {
        adpcm_b_mix_ = (sample * int(adpcm_b_volume_ & 0xff)) / 16;
    } else {
        adpcm_b_mix_ = 0;
    }
}

int32_t YM2610::update() {
    for (int ch = 0; ch < 4; ch++) {
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
    for (int ch = 0; ch < 4; ch++) {
        opn_.chan_calc(opn_.channel(ch));
        const OpnCore::Channel& channel = opn_.channel(ch);
        if (channel.pan_left || channel.pan_right) mix += opn_.channel_output(ch);
    }

    adpcm_a_acc_ += adpcm_a_step_;
    while (adpcm_a_acc_ >= 1.0) {
        adpcm_a_acc_ -= 1.0;
        clock_adpcm_a();
    }

    const double b_rate =
        adpcm_b_delta_ == 0 ? 0.0
                            : (double(clock_) * double(adpcm_b_delta_) / 72.0 / 65536.0) /
                                  double(kSampleRate);
    adpcm_b_acc_ += b_rate;
    while (adpcm_b_acc_ >= 1.0) {
        adpcm_b_acc_ -= 1.0;
        clock_adpcm_b();
    }

    mix = int32_t(float(mix) * amplitude_);
    mix += ay_.update();
    mix += adpcm_a_mix_ * 2;
    mix += adpcm_b_mix_;
    mix = std::max(-0x7fff, std::min(0x7fff, mix));

    opn_.internal_timer_a(opn_.channel(2));
    opn_.internal_timer_b();
    return mix;
}

}  // namespace dsp
