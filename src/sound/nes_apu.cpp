#include "sound/nes_apu.h"

#include <algorithm>
#include <cmath>

namespace dsp {
namespace {

constexpr int kVblLength[32] = {
    10, 254, 20, 2,  40, 4,  80, 6,  160, 8,  60, 10, 14, 12, 26, 14,
    12, 16,  24, 18, 48, 20, 96, 22, 192, 24, 72, 26, 16, 28, 32, 30};

constexpr uint16_t kFreqLimit[8] = {0x3ff, 0x555, 0x666, 0x71c, 0x787, 0x7c1, 0x7e0, 0x7f0};

constexpr uint16_t kNoiseFreq[16] = {4,   8,   16,  32,   64,   96,   128,  160,
                                     202, 254, 380, 508,  762,  1016, 2034, 4068};

constexpr uint16_t kDpcmClocks[16] = {428, 380, 340, 320, 286, 254, 226, 214,
                                      190, 160, 142, 128, 106, 85,  72,  54};

constexpr uint8_t kDutyLut[4] = {0x40, 0x60, 0x78, 0x9f};

constexpr double kNtscRefresh = 60.0988;

}  // namespace

NesApu::NesApu(uint32_t clock) : clock_(clock) {
    build_tables();
    reset();
}

void NesApu::build_tables() {
    const double val = double(clock_) / kNtscRefresh / 4.0;
    samps_per_sync_ = int(std::round(double(clock_) / 4.0 / kNtscRefresh));
    for (int t = 0; t < 32; ++t) {
        vbl_times_[size_t(t)] = uint32_t(kVblLength[t] * val / 2.0);
        sync_times1_[size_t(t)] = uint32_t(val * (t + 1));
    }
    for (int t = 0; t < 128; ++t) {
        sync_times2_[size_t(t)] = uint32_t(val * t) >> 2;
    }
    square_lut_[0] = 0;
    for (int t = 1; t < 32; ++t) {
        square_lut_[size_t(t)] = float(95.88 / ((8128.0 / t) + 100.0));
    }
    for (int t = 0; t < 16; ++t) {
        for (int n = 0; n < 16; ++n) {
            for (int d = 0; d < 128; ++d) {
                float tnd_out = float(t) / 8227.0f + float(n) / 12241.0f + float(d) / 22638.0f;
                if (tnd_out != 0) tnd_out = 159.79f / ((1.0f / tnd_out) + 100.0f);
                tnd_lut_[t][n][d] = tnd_out;
            }
        }
    }
}

void NesApu::reset() {
    squ_[0] = Square{};
    squ_[1] = Square{};
    tri_ = Triangle{};
    noi_ = Noise{};
    dpcm_ = Dpcm{};
    regs_.fill(0);
    noi_.lfsr = 1;
    apu_dpcm_reset();
    dpcm_.enabled = false;
    step_mode_ = 4;
    buffer_.fill(0);
    buffer_pos_ = 1;
    frame_irq_ = true;
    frame_irq_timer_enabled_ = false;
    old_res_ = 0;
    write(0x15, 0);
}

void NesApu::apu_dpcm_reset() {
    dpcm_.address = 0xc000 + (uint32_t(dpcm_.regs[2]) << 6);
    dpcm_.length = uint16_t((dpcm_.regs[3] << 4) + 1);
    dpcm_.bits_left = 8;
    dpcm_.irq = (dpcm_.regs[0] & 0x80) != 0;
    dpcm_.enabled = true;
}

void NesApu::apu_regwrite(uint8_t address, uint8_t value) {
    const int chan = (address & 4) ? 1 : 0;
    switch (address) {
        case 0x00:
        case 0x04:
            squ_[chan].regs[0] = value;
            break;
        case 0x01:
        case 0x05:
            squ_[chan].regs[1] = value;
            break;
        case 0x02:
        case 0x06:
            squ_[chan].regs[2] = value;
            if (squ_[chan].enabled) {
                squ_[chan].freq = int(((((squ_[chan].regs[3] & 7) << 8) + value) + 1) << 16);
            }
            break;
        case 0x03:
        case 0x07:
            squ_[chan].regs[3] = value;
            if (squ_[chan].enabled) {
                squ_[chan].vbl_length = int(vbl_times_[value >> 3]);
                squ_[chan].env_vol = 0;
                squ_[chan].freq = int(((((value & 7) << 8) + squ_[chan].regs[2]) + 1) << 16);
            }
            break;
        case 0x08:
            tri_.regs[0] = value;
            if (tri_.enabled && !tri_.counter_started) {
                tri_.linear_length = int(sync_times2_[value & 0x7f]);
            }
            break;
        case 0x09:
            tri_.regs[1] = value;
            break;
        case 0x0a:
            tri_.regs[2] = value;
            break;
        case 0x0b:
            tri_.regs[3] = value;
            tri_.write_latency = int((samps_per_sync_ + 239) / 240);
            if (tri_.enabled) {
                tri_.counter_started = false;
                tri_.vbl_length = int(vbl_times_[value >> 3]);
                tri_.linear_length = int(sync_times2_[tri_.regs[0] & 0x7f]);
                tri_.linear_reload = true;
            }
            break;
        case 0x0c:
            noi_.regs[0] = value;
            break;
        case 0x0d:
            noi_.regs[1] = value;
            break;
        case 0x0e:
            noi_.regs[2] = value;
            break;
        case 0x0f:
            noi_.regs[3] = value;
            if (noi_.enabled) {
                noi_.vbl_length = int(vbl_times_[value >> 3]);
                noi_.env_vol = 0;
            }
            break;
        case 0x10:
            dpcm_.regs[0] = value;
            dpcm_.irq = (value & 0x80) != 0;
            break;
        case 0x11:
            dpcm_.regs[1] = value & 0x7f;
            dpcm_.vol = dpcm_.regs[1];
            break;
        case 0x12:
            dpcm_.regs[2] = value;
            dpcm_.address = 0xc000 + (uint32_t(value) << 6);
            break;
        case 0x13:
            dpcm_.regs[3] = value;
            break;
        case 0x17:
            if (value & 0x80) {
                step_mode_ = 5;
                frame_irq_timer_enabled_ = false;
            } else {
                step_mode_ = 4;
                frame_irq_timer_enabled_ = true;
                frame_irq_ = (value & 0x40) == 0;
            }
            break;
        case 0x15:
            dpcm_.irq = false;
            if (value & 0x01) {
                squ_[0].enabled = true;
            } else {
                squ_[0].enabled = false;
                squ_[0].vbl_length = 0;
            }
            if (value & 0x02) {
                squ_[1].enabled = true;
            } else {
                squ_[1].enabled = false;
                squ_[1].vbl_length = 0;
            }
            if (value & 0x04) {
                tri_.enabled = true;
            } else {
                tri_.enabled = false;
                tri_.vbl_length = 0;
                tri_.linear_length = 0;
                tri_.counter_started = false;
                tri_.write_latency = 0;
            }
            if (value & 0x08) {
                noi_.enabled = true;
            } else {
                noi_.enabled = false;
                noi_.vbl_length = 0;
            }
            if (value & 0x10) {
                if (!dpcm_.enabled) apu_dpcm_reset();
            } else {
                dpcm_.enabled = false;
            }
            break;
        default:
            break;
    }
}

uint8_t NesApu::read(uint16_t address) {
    const uint8_t addr = uint8_t(address & 0xff);
    if (addr == 0x15) {
        uint8_t readval = 0;
        if (squ_[0].vbl_length != 0) readval |= 0x01;
        if (squ_[1].vbl_length != 0) readval |= 0x02;
        if (tri_.vbl_length != 0) readval |= 0x04;
        if (noi_.vbl_length != 0) readval |= 0x08;
        if (dpcm_.length != 0) readval |= 0x10;
        if (frame_irq_) readval |= 0x40;
        if (dpcm_.irq) readval |= 0x80;
        frame_irq_ = false;
        return readval;
    }
    return regs_[addr];
}

void NesApu::write(uint16_t address, uint8_t value) {
    const uint8_t addr = uint8_t(address & 0xff);
    if (addr < regs_.size()) regs_[addr] = value;
    apu_regwrite(addr, value);
}

void NesApu::apu_square(int chan) {
    Square& s = squ_[chan];
    if (!s.enabled) {
        s.output = 0;
        return;
    }
    const int env_delay = int(sync_times1_[s.regs[0] & 0x0f]);
    s.env_phase -= 4;
    while (s.env_phase < 0) {
        s.env_phase += env_delay;
        if (s.regs[0] & 0x20) {
            s.env_vol = uint8_t((s.env_vol + 1) & 0x0f);
        } else if (s.env_vol < 15) {
            s.env_vol = uint8_t(s.env_vol + 1);
        }
    }
    if (s.vbl_length > 0 && (s.regs[0] & 0x20) == 0) --s.vbl_length;
    if (s.vbl_length == 0) {
        s.output = 0;
        return;
    }
    if ((s.regs[1] & 0x80) != 0 && (s.regs[1] & 7) != 0) {
        const int sweep_delay = int(sync_times1_[(s.regs[1] >> 4) & 7]);
        s.sweep_phase -= 2;
        while (s.sweep_phase < 0) {
            s.sweep_phase += sweep_delay;
            if (s.regs[1] & 8) {
                s.freq -= s.freq >> (s.regs[1] & 7);
            } else {
                s.freq += s.freq >> (s.regs[1] & 7);
            }
        }
    }
    const int period = s.freq >> 16;
    if (((s.regs[1] & 8) == 0 && period > int(kFreqLimit[s.regs[1] & 7])) || period < 4) {
        s.output = 0;
        return;
    }
    s.phaseacc -= 4;
    while (s.phaseacc < 0) {
        s.phaseacc += period;
        s.adder = uint8_t((s.adder + 1) & 0x0f);
    }
    const uint8_t vol = (s.regs[0] & 0x10) ? uint8_t(s.regs[0] & 0x0f) : uint8_t(0x0f - s.env_vol);
    const uint8_t duty = kDutyLut[s.regs[0] >> 6];
    const uint8_t bit = uint8_t((s.adder >> 1) & 7);
    s.output = uint8_t(vol * ((duty >> (7 - bit)) & 1));
}

void NesApu::apu_triangle() {
    if (!tri_.enabled) return;
    const bool not_held = (tri_.regs[0] & 0x80) == 0;
    if (!tri_.counter_started && not_held) {
        if (tri_.write_latency != 0) --tri_.write_latency;
        if (tri_.write_latency == 0) tri_.counter_started = true;
    }
    if (tri_.counter_started) {
        if (tri_.linear_reload) {
            tri_.linear_length = int(sync_times2_[tri_.regs[0] & 0x7f]);
        } else if (tri_.linear_length > 0) {
            --tri_.linear_length;
        }
        if (not_held) tri_.linear_reload = false;
        if (tri_.vbl_length != 0 && not_held) --tri_.vbl_length;
    }
    if (!(tri_.linear_length != 0 && tri_.vbl_length != 0)) return;
    const int freq = ((tri_.regs[3] & 7) << 8) + tri_.regs[2] + 1;
    if (freq < 2) return;
    tri_.phaseacc -= 4;
    while (tri_.phaseacc < 0) {
        tri_.phaseacc += freq;
        tri_.adder = uint8_t(tri_.adder + 1);
        tri_.output = tri_.adder & 0x0f;
        if ((tri_.adder & 0x10) == 0) tri_.output ^= 0x0f;
    }
}

void NesApu::apu_noise() {
    if (!noi_.enabled) {
        noi_.output = 0;
        return;
    }
    const int env_delay = int(sync_times1_[noi_.regs[0] & 0x0f]);
    noi_.env_phase -= 4;
    while (noi_.env_phase < 0) {
        noi_.env_phase += env_delay;
        if (noi_.regs[0] & 0x20) {
            noi_.env_vol = uint8_t((noi_.env_vol + 1) & 15);
        } else if (noi_.env_vol < 15) {
            noi_.env_vol = uint8_t(noi_.env_vol + 1);
        }
    }
    if ((noi_.regs[0] & 0x20) == 0 && noi_.vbl_length > 0) --noi_.vbl_length;
    if (noi_.vbl_length == 0) {
        noi_.output = 0;
        return;
    }
    const int freq = kNoiseFreq[noi_.regs[2] & 0x0f];
    noi_.phaseacc -= 4;
    while (noi_.phaseacc < 0) {
        noi_.phaseacc += freq;
        const uint32_t bit = 1;
        const uint32_t feedback = ((noi_.lfsr & 1) ^ ((noi_.lfsr >> bit) & 1)) << 15;
        noi_.lfsr = (noi_.lfsr | feedback) >> 1;
    }
    if (noi_.lfsr & 1) {
        noi_.output = 0;
    } else if (noi_.regs[0] & 0x10) {
        noi_.output = uint8_t(noi_.regs[0] & 0x0f);
    } else {
        noi_.output = uint8_t(0x0f - noi_.env_vol);
    }
}

void NesApu::apu_dpcm() {
    if (dpcm_.enabled) {
        const int freq = kDpcmClocks[dpcm_.regs[0] & 0x0f];
        dpcm_.phaseacc -= 4;
        while (dpcm_.phaseacc < 0) {
            dpcm_.phaseacc += freq;
            if (dpcm_.length == 0) {
                dpcm_.enabled = false;
                if (dpcm_.regs[0] & 0x40) {
                    apu_dpcm_reset();
                } else {
                    if (dpcm_.irq && irq_) irq_();
                    dpcm_.vol = 0;
                    break;
                }
            }
            dpcm_.bits_left = uint8_t(dpcm_.bits_left - 1);
            const uint8_t bit_pos = uint8_t(7 - (dpcm_.bits_left & 7));
            if (bit_pos == 7) {
                dpcm_.cur_byte = dpcm_read_ ? dpcm_read_(uint16_t(dpcm_.address)) : 0;
                dpcm_.address += 1;
                if (dpcm_.address == 0x10000) dpcm_.address = 0x8000;
                dpcm_.length = uint16_t(dpcm_.length - 1);
                dpcm_.bits_left = 8;
            }
            if ((dpcm_.cur_byte & (1 << bit_pos)) != 0 && dpcm_.vol < 126) {
                dpcm_.vol = uint8_t(dpcm_.vol + 2);
            } else if (dpcm_.vol > 1) {
                dpcm_.vol = uint8_t(dpcm_.vol - 2);
            }
        }
    }
    dpcm_.output = dpcm_.vol;
}

void NesApu::advance() {
    apu_square(0);
    apu_square(1);
    apu_triangle();
    apu_noise();
    apu_dpcm();
    const int pulse = std::min(31, int(squ_[0].output) + int(squ_[1].output));
    const int tri = std::clamp(tri_.output, 0, 15);
    const int noi = std::min(15, int(noi_.output));
    const int dmc = std::min(127, int(dpcm_.output));
    if (buffer_pos_ > kBufferSize) buffer_pos_ = kBufferSize;
    buffer_[size_t(buffer_pos_)] = square_lut_[size_t(pulse)] + tnd_lut_[tri][noi][dmc];
    ++buffer_pos_;
}

int16_t NesApu::update() {
    float res = old_res_;
    if (buffer_pos_ > 11) {
        res = 0;
        for (int f = 1; f <= 11; ++f) res += buffer_[size_t(f)];
        res = (res / 11.0f) * 32767.0f;
        for (int f = 12; f < buffer_pos_; ++f) buffer_[size_t(f - 11)] = buffer_[size_t(f)];
        buffer_pos_ -= 11;
        old_res_ = res;
    }
    return int16_t(std::clamp(int(res), -32767, 32767));
}

}  // namespace dsp
