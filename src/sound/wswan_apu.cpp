#include "sound/wswan_apu.h"

#include <algorithm>

namespace dsp {
namespace {

constexpr uint8_t kNoiseTaps[8] = {14, 10, 13, 4, 8, 6, 9, 11};

int16_t clamp16(int32_t value) {
    return int16_t(std::clamp(value, int32_t(-32768), int32_t(32767)));
}

}  // namespace

void WswanApu::reset(bool color) {
    color_ = color;
    period_.fill(0);
    volume_.fill(0);
    period_counter_.fill(1);
    sample_pos_.fill(0x1f);
    last_left_.fill(0);
    last_right_.fill(0);
    sweep_value_ = 0;
    sweep_step_ = 0;
    sweep_counter_ = 1;
    sweep_divider_ = 8192;
    noise_control_ = 0;
    noise_lfsr_ = 0;
    voice_volume_ = 0;
    control_ = 0;
    output_control_ = 0x80;
    wave_base_ = 0;
    master_volume_ = color ? 3 : 2;
    hyper_ctrl_ = 0;
    hyper_chan_ctrl_ = 0;
    hyper_voice_ = 0;
    hyper_left_ = hyper_right_ = 0;
    dma_source_ = dma_length_ = 0;
    dma_source_saved_ = dma_length_saved_ = 0;
    dma_control_ = 0;
    dma_timer_ = 0;
    cycle_error_ = 0;
}

uint8_t WswanApu::wave_sample(int ch, uint8_t pos) const {
    if (!read_mem_) return 0;
    uint32_t address = (uint32_t(wave_base_) << 6) + (uint32_t(ch) << 4) + (pos >> 1);
    uint8_t data = read_mem_(address);
    return uint8_t((pos & 1) ? ((data >> 4) & 0x0f) : (data & 0x0f));
}

void WswanApu::mix_channel(int ch, int32_t& left, int32_t& right) {
    if (!(control_ & (1 << ch))) {
        last_left_[ch] = last_right_[ch] = 0;
        return;
    }

    int32_t sample_l = 0;
    int32_t sample_r = 0;

    if (ch == 1 && (control_ & 0x20)) {
        // Direct D/A (voice) mode on channel 2.
        int sample = volume_[ch];
        int half = sample >> 1;
        sample_l = (voice_volume_ & 4) ? sample : (voice_volume_ & 8) ? half : 0;
        sample_r = (voice_volume_ & 1) ? sample : (voice_volume_ & 2) ? half : 0;
    } else if (ch == 3 && (control_ & 0x80) && (noise_control_ & 0x10)) {
        int sample = (noise_lfsr_ & 1) ? 0x0f : 0x00;
        sample_l = sample * ((volume_[ch] >> 4) & 0x0f);
        sample_r = sample * (volume_[ch] & 0x0f);
    } else {
        int sample = wave_sample(ch, sample_pos_[ch]);
        sample_l = sample * ((volume_[ch] >> 4) & 0x0f);
        sample_r = sample * (volume_[ch] & 0x0f);
    }

    last_left_[ch] = sample_l;
    last_right_[ch] = sample_r;
    left += sample_l;
    right += sample_r;
}

void WswanApu::advance(int cycles) {
    if (cycles <= 0) return;

    for (int ch = 0; ch < 4; ++ch) {
        if (!(control_ & (1 << ch))) continue;

        if (ch == 1 && (control_ & 0x20)) {
            // Voice mode holds a constant DAC level; nothing to clock.
            continue;
        }

        if (ch == 2 && (control_ & 0x40) && sweep_value_) {
            int remaining = cycles;
            while (remaining > 0) {
                int slice = remaining;
                if (slice > sweep_divider_) slice = sweep_divider_;
                sweep_divider_ -= slice;
                if (sweep_divider_ <= 0) {
                    sweep_divider_ += 8192;
                    --sweep_counter_;
                    if (sweep_counter_ <= 0) {
                        sweep_counter_ = uint8_t(sweep_step_ + 1);
                        period_[ch] = uint16_t((period_[ch] + int8_t(sweep_value_)) & 0x7ff);
                    }
                }

                uint32_t step = 2048u - period_[ch];
                if (step > 4) {
                    period_counter_[ch] -= slice;
                    while (period_counter_[ch] <= 0) {
                        sample_pos_[ch] = uint8_t((sample_pos_[ch] + 1) & 0x1f);
                        period_counter_[ch] += int32_t(step);
                    }
                }
                remaining -= slice;
            }
            continue;
        }

        uint32_t step = 2048u - period_[ch];
        if (step <= 4) continue;

        period_counter_[ch] -= cycles;
        while (period_counter_[ch] <= 0) {
            if (ch == 3 && (control_ & 0x80) && (noise_control_ & 0x10)) {
                uint8_t tap = kNoiseTaps[noise_control_ & 7];
                uint16_t bit =
                    uint16_t((1 ^ (noise_lfsr_ >> 7) ^ (noise_lfsr_ >> tap)) & 1);
                noise_lfsr_ = uint16_t(((noise_lfsr_ << 1) | bit) & 0x7fff);
            } else {
                sample_pos_[ch] = uint8_t((sample_pos_[ch] + 1) & 0x1f);
            }
            period_counter_[ch] += int32_t(step);
        }
    }

    // Hyper Voice holds last written sample (manual / DMA feed).
    if (hyper_ctrl_ & 0x80) {
        int16_t sample = int16_t(uint8_t(hyper_voice_));
        switch (hyper_ctrl_ & 0x0c) {
            case 0x0: sample = int16_t(uint16_t(sample) << (8 - (hyper_ctrl_ & 3))); break;
            case 0x4: sample = int16_t(uint16_t(sample | -0x100) << (8 - (hyper_ctrl_ & 3))); break;
            case 0x8: sample = int16_t(int16_t(int8_t(sample)) << (8 - (hyper_ctrl_ & 3))); break;
            case 0xc: sample = int16_t(uint16_t(sample) << 8); break;
            default: break;
        }
        sample = int16_t(sample >> 5);
        hyper_left_ = (hyper_chan_ctrl_ & 0x40) ? sample : 0;
        hyper_right_ = (hyper_chan_ctrl_ & 0x20) ? sample : 0;
    } else {
        hyper_left_ = hyper_right_ = 0;
    }
}

void WswanApu::run_dma_byte() {
    if (!read_mem_ || !(dma_control_ & 0x80) || dma_length_ == 0) return;
    uint8_t value = read_mem_(dma_source_ & 0xfffff);
    if (dma_control_ & 0x10) {
        // Target Hyper Voice port $95 equivalent.
        hyper_voice_ = value;
    } else {
        // Target channel 2 volume / voice DAC ($89).
        volume_[1] = value;
    }
    if (dma_control_ & 0x40) --dma_source_;
    else ++dma_source_;
    dma_source_ &= 0xfffff;
    --dma_length_;
    dma_length_ &= 0xfffff;
    if (dma_length_ == 0) {
        if (dma_control_ & 0x08) {
            dma_length_ = dma_length_saved_;
            dma_source_ = dma_source_saved_;
        } else {
            dma_control_ &= uint8_t(~0x80);
        }
    }
}

void WswanApu::step_dma(int cycles) {
    if (!(dma_control_ & 0x80) || !color_) return;
    for (int i = 0; i < cycles; ++i) {
        if (dma_timer_ <= 0) {
            run_dma_byte();
            switch (dma_control_ & 3) {
                case 0: dma_timer_ = 5; break;
                case 1: dma_timer_ = 3; break;
                case 2: dma_timer_ = 1; break;
                default: dma_timer_ = 0; break;
            }
        } else {
            --dma_timer_;
        }
        if (!(dma_control_ & 0x80)) break;
    }
}

int16_t WswanApu::update() {
    // One output sample = kClock / kSampleRate CPU clocks (with error diffusion).
    constexpr int kBase = int(kClock / kSampleRate);  // 128
    cycle_error_ += int(kClock % kSampleRate);
    int cycles = kBase;
    if (cycle_error_ >= int(kSampleRate)) {
        cycle_error_ -= int(kSampleRate);
        ++cycles;
    }

    step_dma(cycles);
    advance(cycles);

    int32_t left = 0;
    int32_t right = 0;
    for (int ch = 0; ch < 4; ++ch) mix_channel(ch, left, right);

    bool headphones = (output_control_ & 0x80) != 0;
    bool enabled = headphones ? ((output_control_ & 0x08) != 0)
                              : ((output_control_ & 0x01) != 0);
    // Mednafen always mixes; some titles leave $91 partially clear. If either
    // speaker or headphones enable is set, produce sound.
    if (!enabled && (output_control_ & 0x09) == 0) return 0;

    left += hyper_left_;
    right += hyper_right_;

    int32_t mono;
    if (headphones || (output_control_ & 0x08)) {
        mono = (left + right) << 4;
    } else {
        int shift = (output_control_ >> 1) & 3;
        mono = ((left + right) >> shift) << 7;
    }

    // Master volume: ASWAN 0-2, SPHINX 0-3 (0 = mute).
    int max_vol = color_ ? 3 : 2;
    int vol = master_volume_ & max_vol;
    if (max_vol > 0) mono = (mono * vol) / max_vol;

    return clamp16(mono);
}

uint8_t WswanApu::read(uint16_t port) {
    port &= 0xff;
    if (port >= 0x80 && port <= 0x87) {
        int ch = (port - 0x80) >> 1;
        return (port & 1) ? uint8_t(period_[ch] >> 8) : uint8_t(period_[ch]);
    }
    if (port >= 0x88 && port <= 0x8b) return volume_[port - 0x88];
    switch (port) {
        case 0x4a: return uint8_t(dma_source_);
        case 0x4b: return uint8_t(dma_source_ >> 8);
        case 0x4c: return uint8_t(dma_source_ >> 16);
        case 0x4e: return uint8_t(dma_length_);
        case 0x4f: return uint8_t(dma_length_ >> 8);
        case 0x50: return uint8_t(dma_length_ >> 16);
        case 0x52: return dma_control_;
        case 0x6a: return hyper_ctrl_;
        case 0x6b: return hyper_chan_ctrl_;
        case 0x8c: return sweep_value_;
        case 0x8d: return sweep_step_;
        case 0x8e: return noise_control_;
        case 0x8f: return wave_base_;
        case 0x90: return control_;
        case 0x91: return uint8_t(output_control_ | 0x80);
        case 0x92: return uint8_t(noise_lfsr_);
        case 0x93: return uint8_t(noise_lfsr_ >> 8);
        case 0x94: return voice_volume_;
        case 0x9e: return color_ ? uint8_t(master_volume_ & 3) : 0;
        default: return 0;
    }
}

void WswanApu::write(uint16_t port, uint8_t value) {
    port &= 0xff;
    if (port >= 0x80 && port <= 0x87) {
        int ch = (port - 0x80) >> 1;
        if (port & 1) period_[ch] = uint16_t((period_[ch] & 0x00ff) | ((value & 7) << 8));
        else period_[ch] = uint16_t((period_[ch] & 0x0700) | value);
        return;
    }
    if (port >= 0x88 && port <= 0x8b) {
        volume_[port - 0x88] = value;
        return;
    }
    switch (port) {
        case 0x4a:
            dma_source_ = (dma_source_ & 0xffff00) | value;
            dma_source_saved_ = dma_source_;
            break;
        case 0x4b:
            dma_source_ = (dma_source_ & 0xff00ff) | (uint32_t(value) << 8);
            dma_source_saved_ = dma_source_;
            break;
        case 0x4c:
            dma_source_ = (dma_source_ & 0x00ffff) | ((uint32_t(value) & 0xf) << 16);
            dma_source_saved_ = dma_source_;
            break;
        case 0x4e:
            dma_length_ = (dma_length_ & 0xffff00) | value;
            dma_length_saved_ = dma_length_;
            break;
        case 0x4f:
            dma_length_ = (dma_length_ & 0xff00ff) | (uint32_t(value) << 8);
            dma_length_saved_ = dma_length_;
            break;
        case 0x50:
            dma_length_ = (dma_length_ & 0x00ffff) | ((uint32_t(value) & 0xf) << 16);
            dma_length_saved_ = dma_length_;
            break;
        case 0x52:
            dma_control_ = value & ~0x20;
            break;
        case 0x6a:
            hyper_ctrl_ = value;
            break;
        case 0x6b:
            hyper_chan_ctrl_ = value & 0x6f;
            break;
        case 0x8c:
            sweep_value_ = value;
            break;
        case 0x8d:
            sweep_step_ = value;
            sweep_counter_ = uint8_t(sweep_step_ + 1);
            sweep_divider_ = 8192;
            break;
        case 0x8e:
            if (value & 0x08) noise_lfsr_ = 0;
            noise_control_ = value & 0x17;
            break;
        case 0x8f:
            wave_base_ = value;
            break;
        case 0x90:
            for (int n = 0; n < 4; ++n) {
                if (!(control_ & (1 << n)) && (value & (1 << n))) {
                    period_counter_[n] = 1;
                    sample_pos_[n] = 0x1f;
                }
            }
            control_ = value;
            break;
        case 0x91:
            output_control_ = uint8_t((value & 0x0f) | (output_control_ & 0x80));
            // Writing bit7 is ignored on hardware for connected sense; keep headphones on.
            output_control_ |= 0x80;
            break;
        case 0x92:
            noise_lfsr_ = uint16_t((noise_lfsr_ & 0xff00) | value);
            break;
        case 0x93:
            noise_lfsr_ = uint16_t((noise_lfsr_ & 0x00ff) | ((value & 0x7f) << 8));
            break;
        case 0x94:
            voice_volume_ = value & 0x0f;
            break;
        case 0x95:
            hyper_voice_ = value;
            break;
        case 0x9e:
            if (color_) master_volume_ = value & 3;
            break;
        default:
            break;
    }
}

}  // namespace dsp
