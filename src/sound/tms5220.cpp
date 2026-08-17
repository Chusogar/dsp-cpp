#include "sound/tms5220.h"

#include <algorithm>
#include <cstring>

namespace dsp {
namespace {

// TMS5220 coefficient tables (from MAME tms5110r.hxx / digshadow decap).
const int kEnergyBits = 4;
const int kPitchBits = 6;
const int kKBits[10] = {5, 5, 4, 4, 4, 4, 4, 3, 3, 3};

const uint16_t kEnergyTable[16] = {
    0, 1, 2, 3, 4, 6, 8, 11, 16, 23, 33, 47, 63, 85, 114, 0
};

const uint16_t kPitchTable[64] = {
    0, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29,
    30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 44, 46, 48,
    50, 52, 53, 56, 58, 60, 62, 65, 68, 70, 72, 76, 78, 80, 84, 86,
    91, 94, 98, 101, 105, 109, 114, 118, 122, 127, 132, 137, 142, 148, 153, 159
};

const int16_t kK1[32] = {
    -501, -498, -497, -495, -493, -491, -488, -482, -478, -474, -469, -464, -459, -452, -445, -437,
    -412, -380, -339, -288, -227, -158, -81, -1, 80, 157, 226, 287, 337, 379, 411, 436
};
const int16_t kK2[32] = {
    -328, -303, -274, -244, -211, -175, -138, -99, -59, -18, 24, 64, 105, 143, 180, 215,
    248, 278, 306, 331, 354, 374, 392, 408, 422, 435, 445, 455, 463, 470, 476, 506
};
const int16_t kK3[16] = {
    -441, -387, -333, -279, -225, -171, -117, -63, -9, 45, 98, 152, 206, 260, 314, 368
};
const int16_t kK4[16] = {
    -328, -273, -217, -161, -106, -50, 5, 61, 116, 172, 228, 283, 339, 394, 450, 506
};
const int16_t kK5[16] = {
    -328, -282, -235, -189, -142, -96, -50, -3, 43, 90, 136, 182, 229, 275, 322, 368
};
const int16_t kK6[16] = {
    -256, -212, -168, -123, -79, -35, 10, 54, 98, 143, 187, 232, 276, 320, 365, 409
};
const int16_t kK7[16] = {
    -308, -260, -212, -164, -117, -69, -21, 27, 75, 122, 170, 218, 266, 314, 361, 409
};
const int16_t kK8[8] = {-256, -161, -66, 29, 124, 219, 314, 409};
const int16_t kK9[8] = {-256, -176, -96, -15, 65, 146, 226, 307};
const int16_t kK10[8] = {-205, -132, -59, 14, 87, 160, 234, 307};

const int16_t* const kKTables[10] = {kK1, kK2, kK3, kK4, kK5, kK6, kK7, kK8, kK9, kK10};

// Chirp excitation (TI_LATER_CHIRP)
const int8_t kChirp[52] = {
    0x00, 0x03, 0x0f, 0x28, 0x4c, 0x6c, 0x71, 0x50, 0x25, 0x26, 0x4c, 0x44, 0x1a, 0x32, 0x3b, 0x13,
    0x37, 0x1a, 0x25, 0x1f, 0x1d, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00
};

// Interpolation shift coefficients
const int kInterpShift[8] = {0, 3, 3, 3, 2, 2, 1, 1};

int k_value(int which, int idx) {
    const int max = (1 << kKBits[which]) - 1;
    idx = std::clamp(idx, 0, max);
    return kKTables[which][idx];
}

}  // namespace

Tms5220::Tms5220(uint32_t clock) : clock_(clock) { reset(); }

void Tms5220::raise_irq(bool on) {
    if (irq_cb_) irq_cb_(on);
}

void Tms5220::reset() {
    fifo_.fill(0);
    fifo_head_ = fifo_tail_ = fifo_count_ = 0;
    bit_buffer_ = 0;
    bits_left_ = 0;
    speak_external_ = false;
    talk_status_ = false;
    old_energy_idx_ = new_energy_idx_ = 0;
    old_pitch_idx_ = new_pitch_idx_ = 0;
    old_k_idx_.fill(0);
    new_k_idx_.fill(0);
    current_energy_ = current_pitch_ = 0;
    current_k_.fill(0);
    interp_step_ = 0;
    sample_in_subframe_ = 0;
    pitch_count_ = 0;
    rng_ = 1;
    u_.fill(0);
    x_.fill(0);
    out_sample_ = 0;
    cycle_acc_ = 0;
    data_latch_ = 0;
    wsq_ = rsq_ = rs_read_ = true;
    ready_delay_ = 0;
    volume_ = 1.0f;
    raise_irq(false);
}

void Tms5220::fifo_push(uint8_t v) {
    if (fifo_count_ >= 16) return;
    fifo_[fifo_tail_] = v;
    fifo_tail_ = (fifo_tail_ + 1) & 15;
    ++fifo_count_;
}

uint8_t Tms5220::fifo_pop() {
    if (fifo_count_ == 0) return 0;
    const uint8_t v = fifo_[fifo_head_];
    fifo_head_ = (fifo_head_ + 1) & 15;
    --fifo_count_;
    return v;
}

uint8_t Tms5220::status() const {
    uint8_t s = 0;
    if (talk_status_) s |= 0x80;                 // TS
    if (fifo_count_ < 8) s |= 0x40;              // BL
    if (fifo_count_ == 0) s |= 0x20;             // BE
    return s;
}

void Tms5220::process_command(uint8_t cmd) {
    switch (cmd & 0x70) {
        case 0x00:  // NOP
            break;
        case 0x10:  // READ BYTE / SPEAK (VSM)
            talk_status_ = true;
            break;
        case 0x30:  // READ AND BRANCH
            break;
        case 0x40:  // SPEAK EXTERNAL
            speak_external_ = true;
            talk_status_ = true;
            bit_buffer_ = 0;
            bits_left_ = 0;
            interp_step_ = 0;
            sample_in_subframe_ = 0;
            break;
        case 0x50:  // RESET
            reset();
            break;
        case 0x60:  // LOAD ADDRESS
            break;
        default:
            break;
    }
}

void Tms5220::set_wsq(bool level) {
    if (wsq_ && !level) {
        // Falling edge: commit latched data
        write_data(data_latch_);
        ready_delay_ = 80;
    }
    wsq_ = level;
}

void Tms5220::strobe_ws_rs(uint8_t ws_rs) {
    const bool ws = (ws_rs & 0x01) != 0;
    const bool rs = (ws_rs & 0x02) != 0;
    if (wsq_ && !ws) {
        write_data(data_latch_);
        ready_delay_ = 80;
    }
    if (rs_read_ && !rs) {
        ready_delay_ = 80;
    }
    wsq_ = ws;
    rs_read_ = rs;
}

void Tms5220::set_rsq(bool level) {
    if (!level) {
        // Hold in reset while low
        reset();
        speak_external_ = false;
        talk_status_ = false;
    }
    rsq_ = level;
}

bool Tms5220::readyq() const {
    // Active-low: assert (true return means pin low / not ready) when FIFO full or talking
    // MAME readyq_r() returns 1 when ready. We expose readyq() as "pin is low" = busy.
    if (!rsq_) return true;  // held in reset → treat as not ready
    if (ready_delay_ > 0) return true;
    if (fifo_count_ >= 14) return true;
    return false;
}

void Tms5220::write_data(uint8_t value) {
    if (!speak_external_ && fifo_count_ == 0 && (value & 0x80) == 0) {
        process_command(value);
        return;
    }
    fifo_push(value);
    if (speak_external_ && !talk_status_ && fifo_count_ >= 8) {
        talk_status_ = true;
    }
}

uint32_t Tms5220::extract_bits(int n) {
    while (bits_left_ < n) {
        if (fifo_count_ == 0) return 0;
        bit_buffer_ |= uint32_t(fifo_pop()) << bits_left_;
        bits_left_ += 8;
    }
    const uint32_t v = bit_buffer_ & ((1u << n) - 1);
    bit_buffer_ >>= n;
    bits_left_ -= n;
    return v;
}

bool Tms5220::parse_frame() {
    // Save previous targets
    old_energy_idx_ = new_energy_idx_;
    old_pitch_idx_ = new_pitch_idx_;
    old_k_idx_ = new_k_idx_;

    new_energy_idx_ = int(extract_bits(kEnergyBits));
    if (new_energy_idx_ == 15) {
        // Stop frame
        talk_status_ = false;
        speak_external_ = false;
        raise_irq(true);
        new_energy_idx_ = 0;
        return false;
    }
    if (new_energy_idx_ == 0) {
        // Silence: no pitch/K
        new_pitch_idx_ = 0;
        new_k_idx_.fill(0);
        return true;
    }

    new_pitch_idx_ = int(extract_bits(kPitchBits));
    if (new_pitch_idx_ == 0) {
        // Unvoiced: only K1–K4
        for (int i = 0; i < 4; ++i) new_k_idx_[i] = int(extract_bits(kKBits[i]));
        for (int i = 4; i < kNumK; ++i) new_k_idx_[i] = 0x0f;  // midpoint-ish
    } else {
        for (int i = 0; i < kNumK; ++i) new_k_idx_[i] = int(extract_bits(kKBits[i]));
    }
    return true;
}

void Tms5220::interpolate() {
    const int shift = kInterpShift[interp_step_ & 7];
    auto lerp = [&](int cur, int target) -> int {
        if (shift == 0) return target;
        return cur + ((target - cur) >> shift);
    };

    const int tgt_e = kEnergyTable[new_energy_idx_ & 15];
    const int tgt_p = kPitchTable[new_pitch_idx_ & 63];
    current_energy_ = lerp(current_energy_, tgt_e);
    current_pitch_ = lerp(current_pitch_, tgt_p);
    for (int i = 0; i < kNumK; ++i) {
        const int tgt = k_value(i, new_k_idx_[i]);
        current_k_[i] = lerp(current_k_[i], tgt);
    }
}

int16_t Tms5220::lattice(int16_t excitation) {
    // Reflection coefficient lattice (Q10-ish tables, scale by 512)
    u_[kNumK] = excitation;
    for (int i = kNumK - 1; i >= 0; --i) {
        const int32_t k = current_k_[i];
        u_[i] = u_[i + 1] - ((k * x_[i]) >> 9);
    }
    for (int i = kNumK - 1; i >= 1; --i) {
        const int32_t k = current_k_[i];
        x_[i] = x_[i - 1] + ((k * u_[i - 1]) >> 9);
    }
    x_[0] = u_[0];
    // Energy scale
    int32_t out = (u_[0] * current_energy_) >> 4;
    return int16_t(std::clamp(out, int32_t(-32768), int32_t(32767)));
}

void Tms5220::tick(int cycles) {
    if (cycles <= 0) return;
    if (ready_delay_ > 0) {
        ready_delay_ -= cycles;
        if (ready_delay_ < 0) ready_delay_ = 0;
    }
    // Generate internal 8 kHz samples based on clock ratio
    // TMS5220 ROMCLK ~ 640 kHz, sample period ~ 80 clocks → 8 kHz
    cycle_acc_ += cycles;
    const int clocks_per_sample = std::max(1, int(clock_ / internal_rate_));
    while (cycle_acc_ >= clocks_per_sample) {
        cycle_acc_ -= clocks_per_sample;
        if (!talk_status_) {
            out_sample_ = (out_sample_ * 15) / 16;
            continue;
        }

        // 25 samples per interpolation sub-frame × 8 = 200 samples/frame
        if (sample_in_subframe_ == 0) {
            if (interp_step_ == 0) {
                if (!parse_frame()) {
                    out_sample_ = 0;
                    continue;
                }
                // snap energy on new frame start for stop/silence handling
            }
            interpolate();
        }

        // Excitation
        int16_t excitation = 0;
        if (current_pitch_ == 0) {
            // Unvoiced: noise
            rng_ = rng_ * 0x10dcd + 1;
            excitation = int16_t((rng_ >> 16) & 1 ? 0x40 : -0x40);
        } else {
            // Voiced: chirp table
            if (pitch_count_ < 52)
                excitation = int16_t(int8_t(kChirp[pitch_count_]));
            else
                excitation = 0;
            if (++pitch_count_ >= current_pitch_) pitch_count_ = 0;
        }

        out_sample_ = lattice(excitation);

        if (++sample_in_subframe_ >= 25) {
            sample_in_subframe_ = 0;
            if (++interp_step_ >= 8) interp_step_ = 0;
        }
    }
}

int16_t Tms5220::update() {
    // Advance synthesis by the number of chip clocks corresponding to one host sample.
    const int clocks = std::max(1, int(clock_ / kSampleRate));
    tick(clocks);
    const float g = volume_;
    const int32_t s = int32_t(float(out_sample_ * 64) * g);
    return int16_t(std::clamp(s, int32_t(-32768), int32_t(32767)));
}

}  // namespace dsp
