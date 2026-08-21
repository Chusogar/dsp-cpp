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
    const int16_t* table = kKTables[which];
    const int max = (1 << kKBits[which]) - 1;
    return table[std::clamp(idx, 0, max)];
}

}  // namespace

Tms5220::Tms5220(uint32_t clock) : clock_(clock ? clock : 640000) { reset(); }

void Tms5220::raise_irq(bool on) {
    irq_asserted_ = on;
    if (irq_cb_) irq_cb_(on);
}

void Tms5220::chip_reset() {
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
    current_energy_ = current_pitch_ = previous_energy_ = 0;
    current_k_.fill(0);
    ip_ = 0;
    pc_ = 0;
    subcycle_ = 0;
    pitch_count_ = 0;
    inhibit_ = false;
    old_unvoiced_ = true;
    old_silence_ = true;
    zpar_ = false;
    uv_zpar_ = false;
    rng_ = 1;
    u_.fill(0);
    x_.fill(0);
    out_sample_ = 0;
    cycle_acc_ = 0;
    data_latch_ = 0;
    data_pending_ = false;
    ready_delay_ = 0;
    raise_irq(false);
}

void Tms5220::reset() {
    wsq_ = rsq_ = rs_read_ = true;
    volume_ = 1.0f;
    chip_reset();
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
    if (fifo_count_ < 8) s |= 0x40;              // BL (buffer low)
    if (fifo_count_ == 0) s |= 0x20;             // BE (buffer empty)
    return s;
}

void Tms5220::process_command(uint8_t cmd) {
    // Command field is bits 6-4 (MAME tms5220.cpp / TI TMS5220).
    switch (cmd & 0x70) {
        case 0x00:
        case 0x20:  // NOP / set rate (5220C)
            break;
        case 0x10:  // READ BYTE (VSM)
            break;
        case 0x30:  // READ AND BRANCH (VSM)
            break;
        case 0x40:  // LOAD ADDRESS (VSM)
            break;
        case 0x50:  // SPEAK (VSM)
            break;
        case 0x60:  // SPEAK EXTERNAL
            speak_external_ = true;
            talk_status_ = false;
            bit_buffer_ = 0;
            bits_left_ = 0;
            ip_ = 0;
            pc_ = 0;
            subcycle_ = 0;
            pitch_count_ = 0;
            // Keep any bytes already in the FIFO; start if ≥ 9 (datasheet).
            if (fifo_count_ >= 9) {
                talk_status_ = true;
            }
            raise_irq(false);
            break;
        case 0x70:  // RESET
            chip_reset();
            break;
        default:
            break;
    }
}

void Tms5220::apply_rs_ws(bool new_wsq, bool new_rsq) {
    const bool old_wsq = wsq_;
    const bool old_rsq = rsq_;
    if (old_wsq == new_wsq && old_rsq == new_rsq) return;
    wsq_ = new_wsq;
    rsq_ = new_rsq;

    // TMS5220C: both /WS and /RS low → reset
    if (!new_wsq && !new_rsq) {
        chip_reset();
        return;
    }
    // Falling /WS with /RS high → commit latched byte if not already written
    if (old_wsq && !new_wsq && new_rsq) {
        if (data_pending_) {
            data_pending_ = false;
            write_data(data_latch_);
        }
        ready_delay_ = 16;  // ~16 ROM clocks (MAME)
    }
    // Falling /RS with /WS high → status read strobe
    if (old_rsq && !new_rsq && new_wsq) {
        ready_delay_ = 16;
    }
}

void Tms5220::set_wsq(bool level) { apply_rs_ws(level, rsq_); }

void Tms5220::set_rsq(bool level) { apply_rs_ws(wsq_, level); }

void Tms5220::strobe_ws_rs(uint8_t ws_rs) {
    // Star Wars / EXL: bit0=/WS, bit1=/RS (active low as 0)
    const bool ws = (ws_rs & 0x01) != 0;
    const bool rs = (ws_rs & 0x02) != 0;
    apply_rs_ws(ws, rs);
}

bool Tms5220::readyq() const {
    // Active-low /READY semantics as MAME readyq_r():
    //   true  → pin high → NOT ready (busy)
    //   false → pin low  → ready
    if (!wsq_ && !rsq_) return true;
    if (ready_delay_ > 0) return true;
    if (fifo_count_ >= 14) return true;
    return false;
}

void Tms5220::write_data(uint8_t value) {
    data_pending_ = false;
    // SPEAK EXTERNAL: every write feeds the FIFO.
    // Otherwise the byte is a command (bits 6-4).
    if (speak_external_) {
        fifo_push(value);
        // Datasheet: Talk Status after nine bytes following SPEAK EXTERNAL
        if (!talk_status_ && fifo_count_ >= 9) {
            talk_status_ = true;
            ip_ = 0;
            pc_ = 0;
            subcycle_ = 0;
            bit_buffer_ = 0;
            bits_left_ = 0;
            raise_irq(false);
        }
        // Buffer low while talking → /INT
        if (talk_status_ && fifo_count_ < 8) {
            raise_irq(true);
        }
        return;
    }
    process_command(value);
}

uint32_t Tms5220::extract_bits(int n) {
    // MAME order: serial bits are taken LSB-first from each FIFO byte, but
    // shifted into the result MSB-first (val = (val<<1)|bit).
    uint32_t val = 0;
    while (n-- > 0) {
        if (bits_left_ <= 0) {
            if (fifo_count_ == 0) {
                // Starve: inject zeros (silence) rather than freezing
                bits_left_ = 0;
                bit_buffer_ = 0;
                return val << (n + 1);  // remaining bits stay 0
            }
            bit_buffer_ = fifo_pop();
            bits_left_ = 8;
        }
        val = (val << 1) | (bit_buffer_ & 1u);
        bit_buffer_ >>= 1;
        --bits_left_;
    }
    return val;
}

bool Tms5220::parse_frame() {
    old_energy_idx_ = new_energy_idx_;
    old_pitch_idx_ = new_pitch_idx_;
    old_k_idx_ = new_k_idx_;

    // Speak-external: empty FIFO ends talk (BE clears TS).
    if (speak_external_ && fifo_count_ == 0 && bits_left_ == 0) {
        talk_status_ = false;
        speak_external_ = false;
        raise_irq(true);
        return false;
    }

    // TMS5220 frame (MAME parse_frame):
    //   energy 4b; 0=silence, 15=stop
    //   else: repeat 1b, pitch 6b; if !repeat: K1-4, and K5-10 if pitched
    new_energy_idx_ = int(extract_bits(kEnergyBits));
    if (new_energy_idx_ == 15) {
        talk_status_ = false;
        speak_external_ = false;
        raise_irq(true);
        new_energy_idx_ = 0;
        return false;
    }
    if (new_energy_idx_ == 0) {
        new_pitch_idx_ = 0;
        new_k_idx_.fill(0);
        return true;
    }

    const int rep_flag = int(extract_bits(1));
    new_pitch_idx_ = int(extract_bits(kPitchBits));
    if (rep_flag) {
        new_k_idx_ = old_k_idx_;  // reuse previous coefficients
        return true;
    }

    for (int i = 0; i < 4; ++i) new_k_idx_[i] = int(extract_bits(kKBits[i]));
    if (new_pitch_idx_ == 0) {
        for (int i = 4; i < kNumK; ++i) new_k_idx_[i] = 0x0f;
    } else {
        for (int i = 4; i < kNumK; ++i) new_k_idx_[i] = int(extract_bits(kKBits[i]));
    }
    return true;
}



int16_t Tms5220::lattice(int16_t excitation) {
    // MAME lattice_filter:
    //   u[10] = matrix_multiply(previous_energy, excitation<<6)
    //   10 reflection stages; return u[0]; then previous_energy = current_energy
    auto mul = [](int32_t a, int32_t b) -> int32_t {
        while (a > 511) a -= 1024;
        while (a < -512) a += 1024;
        while (b > 16383) b -= 32768;
        while (b < -16384) b += 32768;
        return (a * b) >> 9;
    };
    u_[10] = mul(previous_energy_, int32_t(excitation) << 6);
    for (int i = 9; i >= 0; --i)
        u_[i] = u_[i + 1] - mul(current_k_[i], x_[i]);
    for (int i = 9; i >= 1; --i)
        x_[i] = x_[i - 1] + mul(current_k_[i], u_[i]);
    x_[0] = u_[0];
    previous_energy_ = current_energy_;

    int32_t out = u_[0];
    while (out > 16383) out -= 32768;
    while (out < -16384) out += 32768;
    out = (out << 1) | ((out >> 9) & 1);
    return int16_t(std::clamp(out, int32_t(-32768), int32_t(32767)));
}

void Tms5220::tick(int cycles) {
    if (cycles <= 0) return;
    if (ready_delay_ > 0) {
        ready_delay_ -= cycles;
        if (ready_delay_ < 0) ready_delay_ = 0;
    }
    // One LPC sample every (clock_/8000) ROM clocks (MAME stream = clock/80 ≈ 8 kHz).
    cycle_acc_ += cycles;
    const int clocks_per_sample = std::max(1, int(clock_ / internal_rate_));
    while (cycle_acc_ >= clocks_per_sample) {
        cycle_acc_ -= clocks_per_sample;
        if (!talk_status_) {
            out_sample_ = (out_sample_ * 15) / 16;
            continue;
        }

        // --- Frame boundary: IP=0, PC=12, subcycle=1 (MAME) ---
        if (ip_ == 0 && pc_ == 12 && subcycle_ == 1) {
            const bool prev_unvoiced = (new_pitch_idx_ == 0);
            const bool prev_silence = (new_energy_idx_ == 0);
            if (!parse_frame()) {
                out_sample_ = 0;
                // still advance counters below
            } else {
                // Inhibit interpolation on voiced↔unvoiced / silence→active transitions
                const bool now_unvoiced = (new_pitch_idx_ == 0);
                const bool now_silence = (new_energy_idx_ == 0);
                inhibit_ = (old_unvoiced_ != now_unvoiced) ||
                           (old_silence_ && !now_silence);
                zpar_ = now_silence;
                uv_zpar_ = now_unvoiced;
            }
        }

        // --- Parameter interpolation on subcycle==2, PC 0..11 ---
        // INTERP_SHIFT = >> interp_coeff[IP]
        if (subcycle_ == 2 && pc_ <= 11) {
            const int shift = kInterpShift[ip_ & 7];
            const int inhib = (inhibit_ && ip_ != 0) ? 0 : 1;
            auto step = [&](int cur, int target) -> int {
                if (!inhib) return cur;
                if (shift == 0) return target;
                return cur + ((target - cur) >> shift);
            };
            switch (pc_) {
                case 0:
                    current_energy_ = zpar_ ? 0
                        : step(current_energy_, kEnergyTable[new_energy_idx_ & 15]);
                    break;
                case 1:
                    current_pitch_ = zpar_ ? 0
                        : step(current_pitch_, kPitchTable[new_pitch_idx_ & 63]);
                    break;
                default: {
                    // PC 2..11 → K0..K9
                    const int ki = pc_ - 2;
                    if (ki >= 0 && ki < kNumK) {
                        const int tgt = k_value(ki, new_k_idx_[ki]);
                        const bool kill = (ki < 4) ? zpar_ : uv_zpar_;
                        current_k_[ki] = kill ? 0 : step(current_k_[ki], tgt);
                    }
                    break;
                }
            }
        }

        // --- Excitation (OLDP = old_unvoiced_) ---
        int16_t excitation = 0;
        if (old_unvoiced_) {
            for (int n = 0; n < 20; ++n) {
                const int bitout =
                    ((rng_ >> 12) ^ (rng_ >> 3) ^ (rng_ >> 2) ^ (rng_ >> 0)) & 1;
                rng_ = int32_t((uint32_t(rng_) << 1) | uint32_t(bitout));
            }
            excitation = int16_t((rng_ & 1) ? (~0x3F) : 0x40);
        } else {
            const int pc = pitch_count_ >= 51 ? 51 : pitch_count_;
            excitation = int16_t(int8_t(kChirp[pc]));
        }

        out_sample_ = lattice(excitation);

        // --- Advance pitch counter ---
        if (current_pitch_ > 0) {
            if (++pitch_count_ >= current_pitch_) pitch_count_ = 0;
        } else {
            pitch_count_ = 0;
        }

        // --- Advance IP / PC / subcycle (MAME) ---
        // subc_reload = 0 for standard 5220
        ++subcycle_;
        if (subcycle_ == 2 && pc_ == 12) {
            // RESETF3 / end of IP period
            if (ip_ == 7) {
                // Latch OLDE / OLDP at end of IP=7
                old_silence_ = (new_energy_idx_ == 0);
                old_unvoiced_ = (new_pitch_idx_ == 0);
                old_pitch_idx_ = new_pitch_idx_;
                old_energy_idx_ = new_energy_idx_;
            }
            subcycle_ = 0;
            pc_ = 0;
            ip_ = (ip_ + 1) & 7;
        } else if (subcycle_ == 3) {
            subcycle_ = 0;
            ++pc_;
        }
    }
}

int16_t Tms5220::last_sample() const {
    const int32_t s = int32_t(float(out_sample_) * volume_);
    return int16_t(std::clamp(s, int32_t(-32768), int32_t(32767)));
}

int16_t Tms5220::update() {
    const int clocks = std::max(1, int(clock_ / kSampleRate));
    tick(clocks);
    return last_sample();
}

}  // namespace dsp
