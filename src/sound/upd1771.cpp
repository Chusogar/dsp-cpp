#include "sound/upd1771.h"

#include <algorithm>
#include <cstring>

namespace dsp {

const int8_t Upd1771::kWaveforms[8][32] = {
    {0, 0, -123, -123, -61, -23, 125, 107, 94, 83, -128, -128, -128, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, -128, -128, -128, 0, 0, 0, 0, 0, 0},
    {37, 16, 32, -21, 32, 52, 4, 4, 33, 18, 60, 56, 0, 8, 5, 16, 65, 19, 69, 16, -2, 19, 37, 16, 97, 19, 0, 87, 127, -3, 1, 2},
    {0, 8, 1, 52, 4, 0, 0, 77, 81, -109, 47, 97, -83, -109, 38, 97, 0, 52, 4, 0, 1, 4, 1, 22, 2, -46, 33, 97, 0, 8, -85, -99},
    {47, 97, 40, 97, -3, 25, 64, 17, 0, 52, 12, 5, 12, 5, 12, 5, 12, 5, 12, 5, 8, 4, -114, 19, 0, 52, -122, 21, 2, 5, 0, 8},
    {-52, -96, -118, -128, -111, -74, -37, -5, 31, 62, 89, 112, 127, 125, 115, 93, 57, 23, 0, -16, -8, 15, 37, 54, 65, 70, 62, 54, 43, 31, 19, 0},
    {-81, -128, -61, 13, 65, 93, 127, 47, 41, 44, 52, 55, 56, 58, 58, 34, 0, 68, 76, 72, 61, 108, 55, 29, 32, 39, 43, 49, 50, 51, 51, 0},
    {-21, -45, -67, -88, -105, -114, -122, -128, -123, -116, -103, -87, -70, -53, -28, -9, 22, 46, 67, 86, 102, 114, 123, 125, 127, 117, 104, 91, 72, 51, 28, 0},
    {-78, -118, -128, -102, -54, -3, 40, 65, 84, 88, 84, 80, 82, 88, 94, 103, 110, 119, 122, 125, 122, 122, 121, 123, 125, 126, 127, 127, 125, 118, 82, 0}
};

const uint8_t Upd1771::kNoiseTbl[256] = {
    28, 134, 138, 143, 152, 161, 173, 190, 217, 138, 102, 77, 64, 51, 43, 35,
    30, 138, 144, 151, 164, 174, 184, 214, 236, 233, 105, 74, 62, 52, 45, 39,
    36, 36, 137, 142, 147, 156, 165, 176, 193, 221, 64, 54, 48, 41, 39, 36,
    139, 144, 150, 158, 167, 179, 196, 225, 37, 33, 138, 143, 147, 157, 165, 178,
    194, 221, 221, 152, 162, 175, 191, 216, 253, 101, 74, 60, 49, 43, 36, 34,
    30, 135, 140, 145, 154, 163, 175, 192, 219, 190, 217, 140, 102, 77, 64, 52,
    44, 36, 31, 136, 144, 154, 164, 178, 194, 218, 255, 103, 77, 61, 52, 45,
    38, 36, 32, 137, 142, 147, 156, 165, 177, 194, 222, 193, 218, 255, 103, 77,
    61, 51, 45, 38, 36, 32, 137, 142, 147, 156, 165, 177, 194, 221, 163, 176,
    192, 217, 254, 102, 75, 60, 50, 43, 36, 35, 30, 136, 141, 146, 155, 164,
    176, 193, 220, 173, 190, 218, 34, 32, 28, 133, 138, 143, 152, 161, 173, 190,
    218, 32, 27, 133, 141, 151, 161, 175, 191, 216, 253, 100, 73, 58, 48, 42,
    35, 33, 29, 134, 139, 145, 154, 162, 174, 192, 219, 51, 43, 36, 31, 136,
    144, 154, 164, 178, 194, 218, 255, 103, 76, 62, 51, 45, 37, 36, 31, 137,
    142, 147, 156, 165, 177, 194, 222, 133, 142, 152, 162, 176, 192, 217, 254, 100,
    75, 59, 49, 42, 35, 34, 30, 136, 140, 145, 155, 163, 175, 193, 220, 220,
};

Upd1771::Upd1771(uint32_t clock, float amp) : clock_(clock), amp_(amp) {
    reset();
}

void Upd1771::reset() {
    state_ = Silence;
    index_ = 0;
    pc3_ = 0;
    packet_.fill(0);
    t_timbre_ = t_offset_ = t_volume_ = t_tpos_ = 0;
    t_period_ = t_ppos_ = 0;
    nw_timbre_ = nw_volume_ = nw_tpos_ = 0;
    nw_period_ = nw_ppos_ = 0;
    n_value_.fill(0);
    n_ppos_.fill(0);
    n_period_.fill(0);
    n_volume_.fill(0);
    salida_ = 0;
    ack_timer_ = -1;
    sample_acc_ = 0;
    chip_phase_ = 0;
    samples_.clear();
}

void Upd1771::schedule_ack() {
    // ~512 chip ticks until ACK (matches Pascal timer)
    ack_timer_ = 512;
}

void Upd1771::write(uint8_t value) {
    if (ack_) ack_(false);
    if (index_ < packet_.size()) {
        packet_[index_] = value;
        index_++;
    } else return;

    switch (packet_[0]) {
        case 0:
            state_ = Silence;
            index_ = 0;
            break;
        case 1:
            if (index_ == 10) {
                state_ = Noise;
                index_ = 0;
                nw_timbre_ = uint8_t((packet_[1] & 0xE0) >> 5);
                nw_period_ = (uint32_t(packet_[2]) + 1) << 7;
                nw_volume_ = packet_[3] & 0x1F;
                n_period_[0] = (uint32_t(packet_[4]) + 1) << 7;
                n_period_[1] = (uint32_t(packet_[5]) + 1) << 7;
                n_period_[2] = (uint32_t(packet_[6]) + 1) << 7;
                n_volume_[0] = packet_[7] & 0x1F;
                n_volume_[1] = packet_[8] & 0x1F;
                n_volume_[2] = packet_[9] & 0x1F;
            } else {
                schedule_ack();
            }
            break;
        case 2:
            if (index_ == 4) {
                t_timbre_ = uint8_t((packet_[1] & 0xE0) >> 5);
                t_offset_ = packet_[1] & 0x1F;
                t_period_ = packet_[2];
                if (t_period_ < 0x20) t_period_ = 0x20;
                t_volume_ = packet_[3] & 0x1F;
                state_ = Tone;
                index_ = 0;
            } else {
                schedule_ack();
            }
            break;
        case 0x1F:
            if (index_ >= 2 && packet_[index_ - 2] == 0xFE && packet_[index_ - 1] == 0) {
                index_ = 0;
                packet_[0] = 0;
                state_ = Adpcm;
            } else {
                schedule_ack();
            }
            break;
        default:
            state_ = Silence;
            index_ = 0;
            break;
    }
}

void Upd1771::pcm_write(uint8_t state) {
    if (state != pc3_) {
        index_ = 0;
        packet_[0] = 0;
    }
    pc3_ = state;
}

void Upd1771::internal_tick() {
    salida_ = 0;
    if (state_ == Tone) {
        int temp = int(kWaveforms[t_timbre_ & 7][t_tpos_ & 31]) * int(t_volume_);
        if (temp > 16384) temp = 16384;
        if (temp < -16384) temp = -16384;
        salida_ = int16_t(temp);
        t_ppos_++;
        if (t_ppos_ >= t_period_) {
            t_tpos_++;
            if (t_tpos_ == 32) t_tpos_ = t_offset_;
            t_ppos_ = 0;
        }
    } else if (state_ == Noise) {
        int wlfsr_val = int(kNoiseTbl[nw_tpos_]) - 127;
        nw_ppos_++;
        if (nw_ppos_ >= nw_period_) {
            nw_tpos_++;
            nw_ppos_ = 0;
        }
        int res[3];
        for (int f = 0; f < 3; f++) {
            res[f] = int(n_value_[f]) * 127;
            n_ppos_[f]++;
            if (n_ppos_[f] >= n_period_[f]) {
                n_ppos_[f] = 0;
                n_value_[f] = uint8_t(~n_value_[f]);
            }
        }
        int temp = (wlfsr_val * int(nw_volume_)) |
                   (res[0] * int(n_volume_[0])) |
                   (res[1] * int(n_volume_[1])) |
                   (res[2] * int(n_volume_[2]));
        if (temp > 32767) temp = 32767;
        if (temp < -32768) temp = -32768;
        salida_ = int16_t(temp);
    }

    if (ack_timer_ > 0) {
        if (--ack_timer_ == 0 && ack_) ack_(true);
    }
}

void Upd1771::run_cycles(int cpu_cycles, uint32_t cpu_clock) {
    // Chip internal rate = clock/4
    const double chip_hz = double(clock_) / 4.0;
    chip_phase_ += double(cpu_cycles) * chip_hz / double(cpu_clock);
    while (chip_phase_ >= 1.0) {
        chip_phase_ -= 1.0;
        internal_tick();
    }
    sample_acc_ += double(cpu_cycles) * double(kSampleRate);
    while (sample_acc_ >= double(cpu_clock)) {
        sample_acc_ -= double(cpu_clock);
        int v = int(float(salida_) * amp_);
        if (v > 32767) v = 32767;
        if (v < -32768) v = -32768;
        samples_.push_back(int16_t(v));
    }
}

void Upd1771::take_samples(std::vector<int16_t>& out) {
    out.insert(out.end(), samples_.begin(), samples_.end());
    samples_.clear();
}

}  // namespace dsp
