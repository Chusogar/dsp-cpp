#include "sound/huc6280_psg.h"

#include <algorithm>
#include <cmath>

namespace dsp {
namespace {

// The channel volume steps in 1.5 dB, the balance nibbles in 3 dB.
double attenuation(int steps, double db_per_step) {
    return std::pow(10.0, -db_per_step * double(steps) / 20.0);
}

}  // namespace

HuC6280Psg::HuC6280Psg(uint32_t clock) : clock_(clock) {
    reset();
}

void HuC6280Psg::reset() {
    for (Channel& channel : channels_) channel = Channel{};
    selected_ = 0;
    balance_ = 0xff;
    lfo_frequency_ = 0;
    lfo_control_ = 0;
    cycles_per_sample_ = int(clock_ / uint32_t(kSampleRate));
    cycle_error_ = 0;
}

void HuC6280Psg::write(uint8_t offset, uint8_t value) {
    if ((offset & 0x0f) == 0) {
        selected_ = value & 0x07;
        return;
    }
    if ((offset & 0x0f) == 1) {
        balance_ = value;
        return;
    }
    if ((offset & 0x0f) == 8) {
        lfo_frequency_ = value;
        return;
    }
    if ((offset & 0x0f) == 9) {
        lfo_control_ = value;
        return;
    }
    if (selected_ >= kChannels) return;
    Channel& channel = channels_[selected_];
    switch (offset & 0x0f) {
        case 2:
            channel.frequency = uint16_t((channel.frequency & 0xf00) | value);
            break;
        case 3:
            channel.frequency = uint16_t((channel.frequency & 0x0ff) | (uint16_t(value & 0x0f) << 8));
            break;
        case 4:
            // Enabling a silent channel restarts the waveform write pointer.
            if ((channel.control & 0x80) == 0 && (value & 0x80) != 0) channel.wave_index = 0;
            if ((value & 0x80) == 0) channel.write_index = 0;
            channel.control = value;
            break;
        case 5:
            channel.balance = value;
            break;
        case 6:
            if ((channel.control & 0xc0) == 0xc0) {
                channel.dda = value & 0x1f;
            } else {
                channel.wave[channel.write_index & 0x1f] = value & 0x1f;
                channel.write_index = uint8_t((channel.write_index + 1) & 0x1f);
            }
            break;
        case 7:
            channel.noise_control = value;
            break;
        default:
            break;
    }
}

double HuC6280Psg::gain(const Channel& channel) const {
    const int volume = channel.control & 0x1f;
    const int channel_left = (channel.balance >> 4) & 0x0f;
    const int channel_right = channel.balance & 0x0f;
    const int global_left = (balance_ >> 4) & 0x0f;
    const int global_right = balance_ & 0x0f;
    const double level = attenuation(31 - volume, 1.5);
    const double left = attenuation((15 - channel_left) + (15 - global_left), 3.0);
    const double right = attenuation((15 - channel_right) + (15 - global_right), 3.0);
    return level * (left + right) * 0.5;
}

void HuC6280Psg::step(Channel& channel, int cycles, bool has_noise) {
    if (has_noise && (channel.noise_control & 0x80) != 0) {
        const int period = (32 - (channel.noise_control & 0x1f)) * 512;
        channel.noise_counter -= cycles;
        while (channel.noise_counter <= 0) {
            channel.noise_counter += period;
            const uint32_t feedback = ((channel.noise_shift >> 1) ^ channel.noise_shift) & 1;
            channel.noise_shift = (channel.noise_shift >> 1) | (feedback << 16);
            channel.noise_level = uint8_t((channel.noise_shift & 1) != 0 ? 0x1f : 0x00);
        }
        return;
    }
    // A zero frequency latches the current sample, like the real divider does.
    const int period = channel.frequency != 0 ? int(channel.frequency) : 0x1000;
    channel.counter -= cycles;
    while (channel.counter <= 0) {
        channel.counter += period;
        channel.wave_index = uint8_t((channel.wave_index + 1) & 0x1f);
    }
}

int16_t HuC6280Psg::update() {
    cycle_error_ += int(clock_) - cycles_per_sample_ * kSampleRate;
    int cycles = cycles_per_sample_;
    if (cycle_error_ >= kSampleRate) {
        cycle_error_ -= kSampleRate;
        cycles++;
    }
    double mix = 0.0;
    for (int i = 0; i < kChannels; i++) {
        Channel& channel = channels_[size_t(i)];
        const bool has_noise = i >= 4;
        step(channel, cycles, has_noise);
        if ((channel.control & 0x80) == 0) continue;
        uint8_t sample;
        if ((channel.control & 0x40) != 0) {
            sample = channel.dda;
        } else if (has_noise && (channel.noise_control & 0x80) != 0) {
            sample = channel.noise_level;
        } else {
            sample = channel.wave[channel.wave_index & 0x1f];
        }
        mix += (double(sample) - 16.0) * gain(channel);
    }
    const double scaled = mix * (32767.0 / (16.0 * double(kChannels)));
    return int16_t(std::clamp(scaled, -32768.0, 32767.0));
}

}  // namespace dsp
