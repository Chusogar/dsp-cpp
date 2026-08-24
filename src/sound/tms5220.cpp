#include "sound/tms5220.h"

#include <algorithm>
#include <cstdint>

namespace dsp {
namespace {

// Coefficient ROM of the decapped TMS5220/TMS5220C (identical on both parts).
constexpr int kEnergyBits = 4;
constexpr int kPitchBits = 6;
constexpr int kKBits[Tms5220::kNumK] = {5, 5, 4, 4, 4, 4, 4, 3, 3, 3};

constexpr uint16_t kEnergy[16] = {0, 1, 2, 3, 4, 6, 8, 11, 16, 23, 33, 47, 63, 85, 114, 0};

constexpr uint16_t kPitch[64] = {
    0,  15,  16,  17,  18,  19,  20,  21,  22,  23,  24,  25,  26,  27,  28,  29,
    30, 31,  32,  33,  34,  35,  36,  37,  38,  39,  40,  41,  42,  44,  46,  48,
    50, 52,  53,  56,  58,  60,  62,  65,  68,  70,  72,  76,  78,  80,  84,  86,
    91, 94,  98, 101, 105, 109, 114, 118, 122, 127, 132, 137, 142, 148, 153, 159};

constexpr int16_t kK[Tms5220::kNumK][32] = {
    {-501, -498, -497, -495, -493, -491, -488, -482, -478, -474, -469, -464, -459, -452,
     -445, -437, -412, -380, -339, -288, -227, -158, -81, -1, 80, 157, 226, 287, 337, 379,
     411, 436},
    {-328, -303, -274, -244, -211, -175, -138, -99, -59, -18, 24, 64, 105, 143, 180, 215,
     248, 278, 306, 331, 354, 374, 392, 408, 422, 435, 445, 455, 463, 470, 476, 506},
    {-441, -387, -333, -279, -225, -171, -117, -63, -9, 45, 98, 152, 206, 260, 314, 368},
    {-328, -273, -217, -161, -106, -50, 5, 61, 116, 172, 228, 283, 339, 394, 450, 506},
    {-328, -282, -235, -189, -142, -96, -50, -3, 43, 90, 136, 182, 229, 275, 322, 368},
    {-256, -212, -168, -123, -79, -35, 10, 54, 98, 143, 187, 232, 276, 320, 365, 409},
    {-308, -260, -212, -164, -117, -69, -21, 27, 75, 122, 170, 218, 266, 314, 361, 409},
    {-256, -161, -66, 29, 124, 219, 314, 409},
    {-256, -176, -96, -15, 65, 146, 226, 307},
    {-205, -132, -59, 14, 87, 160, 234, 307}};

// Voiced excitation ROM of the TMS5220/TMS5220C (the "later" TI chirp, verified
// against the decap; sum = 0x3da). Address 51 is held for the rest of a pitch
// period because the address incrementer stops there.
constexpr int8_t kChirp[52] = {
    0x00, 0x03, 0x0f, 0x28, 0x4c, 0x6c, 0x71, 0x50, 0x25, 0x26, 0x4c, 0x44, 0x1a,
    0x32, 0x3b, 0x13, 0x37, 0x1a, 0x25, 0x1f, 0x1d, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

constexpr int kInterpCoeff[8] = {0, 3, 3, 3, 2, 2, 1, 1};

// Interpolation period reload, only non-zero on the rate-controlled parts.
constexpr int kReloadTable[4] = {0, 2, 4, 6};

int coefficient(int index, int value) {
    return kK[index][std::clamp(value, 0, (1 << kKBits[index]) - 1)];
}

// The k coefficient is 10 bits, the running result 14 bits; both wrap.
int32_t matrix_multiply(int32_t a, int32_t b) {
    while (a > 511) a -= 1024;
    while (a < -512) a += 1024;
    while (b > 16383) b -= 32768;
    while (b < -16384) b += 32768;
    return (a * b) >> 9;
}

// Clips the 14 bit lattice result to the 10 bits visible on the analog pin and
// range-extends it to 16 bits, as the patent shows.
int16_t clip_analog(int32_t sample) {
    int32_t clipped = std::clamp(sample, int32_t(-2048), int32_t(2047));
    clipped &= ~0xf;
    return int16_t((clipped << 4) | ((clipped & 0x7f0) >> 3) | ((clipped & 0x400) >> 10));
}

} // namespace

int8_t Tms5220::voiced_excitation(int index) {
    return kChirp[size_t(std::clamp(index, 0, 51))];
}

Tms5220::Tms5220(uint32_t clock, Variant variant)
    : clock_(clock ? clock : 640000), variant_(variant) {
    reset();
}

void Tms5220::reset() {
    rs_ws_ = 0x03;
    io_ready_ = true;
    write_latch_ = 0;
    chip_reset();
}

void Tms5220::chip_reset() {
    fifo_.fill(0);
    fifo_head_ = fifo_tail_ = fifo_count_ = fifo_bits_taken_ = 0;

    spen_ = ddis_ = talk_ = talkd_ = previous_talk_status_ = false;
    set_interrupt_state(false);
    buffer_empty_ = buffer_low_ = true;

    data_latched_ = false;
    io_ready_delay_ = 0;

    new_energy_idx_ = current_energy_ = previous_energy_ = 0;
    new_pitch_idx_ = current_pitch_ = 0;
    zpar_ = uv_zpar_ = false;
    new_k_idx_.fill(0);
    current_k_.fill(0);

    inhibit_ = true;
    pitch_zero_ = false;
    subcycle_ = pc_ = pitch_count_ = 0;
    c_variant_rate_ = 0;
    olde_ = oldp_ = true;
    ip_ = kReloadTable[c_variant_rate_ & 3];
    excitation_data_ = 0;
    rng_ = 0x1fff;
    u_.fill(0);
    x_.fill(0);

    out_sample_ = 0;
    cycle_acc_ = 0;
    update_cycle_acc_ = 0;
}

void Tms5220::set_interrupt_state(bool state) {
    if (state == irq_pin_) return;
    irq_pin_ = state;
    if (irq_cb_) irq_cb_(state);
}

// Idle state observed on the chip when speech is armed but no frame was parsed
// yet: zero energy and pitch, K1-K4 zero, K5-K7 at 0xf and K8-K10 at 0x7.
void Tms5220::set_idle_frame() {
    new_energy_idx_ = 0;
    new_pitch_idx_ = 0;
    for (int i = 0; i < 4; ++i) new_k_idx_[i] = 0;
    for (int i = 4; i < 7; ++i) new_k_idx_[i] = 0x0f;
    for (int i = 7; i < kNumK; ++i) new_k_idx_[i] = 0x07;
}

void Tms5220::write_data(uint8_t value) {
    write_latch_ = value;
    data_latched_ = true;
    data_write(value);
}

void Tms5220::data_write(uint8_t value) {
    const bool old_buffer_low = buffer_low_;

    if (ddis_) {
        if (fifo_count_ < kFifoSize) {
            fifo_[size_t(fifo_tail_)] = value;
            fifo_tail_ = (fifo_tail_ + 1) % kFifoSize;
            ++fifo_count_;
            update_fifo_status_and_ints();

            // Speech starts on the falling edge of BL, i.e. once the ninth byte
            // pushes the FIFO past half full, and only if SPEN was clear.
            if (!spen_ && old_buffer_low && !buffer_low_) {
                zpar_ = true;
                uv_zpar_ = true;
                olde_ = true;
                oldp_ = true;
                spen_ = true;
                set_idle_frame();
            }
        }
    } else {
        process_command(value);
    }

    data_latched_ = false;
    io_ready_ = true;
}

void Tms5220::process_command(uint8_t cmd) {
    switch (cmd & 0x70) {
        case 0x00:
        case 0x20: // set rate on the TMS5220C, NOP otherwise
            if (has_rate_control()) c_variant_rate_ = cmd & 0x0f;
            break;

        case 0x10: // read byte
        case 0x30: // read and branch
        case 0x40: // load address
            // All three only make sense with a VSM speech ROM attached.
            break;

        case 0x50: // speak from VSM
            spen_ = true;
            ddis_ = false;
            zpar_ = true;
            uv_zpar_ = true;
            olde_ = true;
            oldp_ = true;
            set_idle_frame();
            break;

        case 0x60: // speak external
            // /SPKEE clears the FIFO and its counters for two clocks; SPEN is
            // only set later, when the FIFO passes half full.
            fifo_.fill(0);
            fifo_head_ = fifo_tail_ = fifo_count_ = fifo_bits_taken_ = 0;
            ddis_ = true;
            zpar_ = true;
            uv_zpar_ = true;
            olde_ = true;
            oldp_ = true;
            set_idle_frame();
            break;

        case 0x70: // reset
            chip_reset();
            break;

        default:
            break;
    }

    update_fifo_status_and_ints();
}

void Tms5220::update_fifo_status_and_ints() {
    // BL is active while byte 9 of the FIFO is unused, i.e. up to 8 bytes held.
    if (fifo_count_ <= 8) {
        if (!buffer_low_) {
            buffer_low_ = true;
            set_interrupt_state(true);
        }
    } else {
        buffer_low_ = false;
    }

    if (fifo_count_ == 0) {
        if (!buffer_empty_) {
            buffer_empty_ = true;
            set_interrupt_state(true);
        }
        // /BE clears TALK through TCON, which clears SPEN, but only in speak
        // external mode. TALKD stays set until the next frame boundary.
        if (ddis_) talk_ = spen_ = false;
    } else {
        buffer_empty_ = false;
    }

    if (previous_talk_status_ && !talk_status()) {
        set_interrupt_state(true);
        ddis_ = false;
    }
    previous_talk_status_ = talk_status();
}

uint8_t Tms5220::status() const {
    return uint8_t((talk_status() ? 0x80 : 0x00) | (buffer_low_ ? 0x40 : 0x00) |
                   (buffer_empty_ ? 0x20 : 0x00));
}

bool Tms5220::readyq() const {
    return !(((fifo_count_ < kFifoSize) || !ddis_) && io_ready_);
}

void Tms5220::apply_rs_ws(bool new_wsq, bool new_rsq) {
    const uint8_t new_val = uint8_t((new_rsq ? 0x02 : 0x00) | (new_wsq ? 0x01 : 0x00));
    if (new_val == rs_ws_) return;

    const uint8_t falling = uint8_t((rs_ws_ ^ new_val) & ~new_val);
    rs_ws_ = new_val;

    switch (new_val) {
        case 0x00: // both active: reset on the rate-controlled parts, else illegal
            if (has_rate_control()) reset();
            return;

        case 0x03: // high impedance
            return;

        case 0x02: // /WS active
            if (!(falling & 0x01)) return;
            io_ready_ = false;
            io_ready_delay_ = kIoReadyClocks;
            return;

        case 0x01: // /RS active
            if (!(falling & 0x02)) return;
            // A status read clears /INT.
            set_interrupt_state(false);
            io_ready_ = false;
            io_ready_delay_ = kIoReadyClocks;
            return;

        default:
            return;
    }
}

void Tms5220::set_wsq(bool level) { apply_rs_ws(level, (rs_ws_ & 0x02) != 0); }

void Tms5220::set_rsq(bool level) { apply_rs_ws((rs_ws_ & 0x01) != 0, level); }

void Tms5220::strobe_ws_rs(uint8_t ws_rs) {
    apply_rs_ws((ws_rs & 0x01) != 0, (ws_rs & 0x02) != 0);
}

void Tms5220::service_io_ready() {
    if (rs_ws_ == 0x02) {
        // A write cannot be serviced while the FIFO is full in speak external.
        if (ddis_ && fifo_count_ >= kFifoSize) {
            io_ready_delay_ = kIoReadyClocks;
            return;
        }
        if (data_latched_) {
            data_latched_ = false;
            data_write(write_latch_);
            return;
        }
    }
    io_ready_ = true;
}

int Tms5220::read_bits(int count) {
    int value = 0;

    if (!ddis_) {
        // No VSM attached: the bits float high, which decodes as a stop frame.
        return (1 << count) - 1;
    }

    while (count--) {
        value = (value << 1) | ((fifo_[size_t(fifo_head_)] >> fifo_bits_taken_) & 1);
        ++fifo_bits_taken_;
        if (fifo_bits_taken_ >= 8) {
            fifo_bits_taken_ = 0;
            fifo_[size_t(fifo_head_)] = 0;
            if (fifo_count_ > 0) {
                --fifo_count_;
                fifo_head_ = (fifo_head_ + 1) % kFifoSize;
                update_fifo_status_and_ints();
            }
        }
    }
    return value;
}

void Tms5220::parse_frame() {
    // Parsing a frame means we are talking, and RESETL4 cleared the E=0 and P=0
    // latches just before, so both zero-parameter flags start clear.
    zpar_ = uv_zpar_ = false;

    if (has_rate_control() && (c_variant_rate_ & 0x04))
        ip_ = kReloadTable[read_bits(2) & 3];
    else
        ip_ = kReloadTable[c_variant_rate_ & 3];

    update_fifo_status_and_ints();
    if (ddis_ && buffer_empty_) return;

    new_energy_idx_ = read_bits(kEnergyBits);
    update_fifo_status_and_ints();
    if (ddis_ && buffer_empty_) return;
    // Silence and stop frames carry no further data.
    if (new_energy_idx_ == 0 || new_energy_idx_ == 0x0f) return;

    const int repeat = read_bits(1);
    new_pitch_idx_ = read_bits(kPitchBits);
    // An unvoiced frame zeroes K5-K10.
    uv_zpar_ = new_frame_unvoiced_flag();
    update_fifo_status_and_ints();
    if (ddis_ && buffer_empty_) return;
    if (repeat) return; // reuse the previous coefficients

    for (int i = 0; i < 4; ++i) {
        new_k_idx_[size_t(i)] = read_bits(kKBits[i]);
        update_fifo_status_and_ints();
        if (ddis_ && buffer_empty_) return;
    }

    if (new_pitch_idx_ == 0) return; // unvoiced frames only carry four Ks

    for (int i = 4; i < kNumK; ++i) {
        new_k_idx_[size_t(i)] = read_bits(kKBits[i]);
        update_fifo_status_and_ints();
        if (ddis_ && buffer_empty_) return;
    }
}

int32_t Tms5220::lattice_filter() {
    u_[10] = matrix_multiply(previous_energy_, int32_t(excitation_data_) << 6);
    for (int i = 9; i >= 0; --i)
        u_[size_t(i)] = u_[size_t(i) + 1] - matrix_multiply(current_k_[size_t(i)], x_[size_t(i)]);

    for (int i = 9; i >= 1; --i)
        x_[size_t(i)] = x_[size_t(i) - 1] +
                        matrix_multiply(current_k_[size_t(i) - 1], u_[size_t(i) - 1]);
    x_[0] = u_[0];

    previous_energy_ = current_energy_;
    return u_[0];
}

void Tms5220::process_sample() {
    if (!talkd_) {
        advance_counters(false);
        // The chip drives -1 on the analog pin while idle.
        out_sample_ = -1;
        return;
    }

    if (ip_ == 0 && pc_ == 12 && subcycle_ == 1) {
        // The interpolation count is reloaded before the frame is parsed.
        ip_ = kReloadTable[c_variant_rate_ & 3];
        parse_frame();

        // A stop frame clears TALK and SPEN; TALKD stays set for one more frame
        // so the energy ramps down instead of cutting off.
        if (new_frame_stop_flag()) {
            talk_ = spen_ = false;
            update_fifo_status_and_ints();
        }

        // Interpolation is inhibited across voicing changes and when leaving
        // silence or entering silence from an unvoiced frame.
        inhibit_ = (oldp_ != new_frame_unvoiced_flag()) ||
                   (olde_ && !new_frame_silence_flag()) ||
                   (oldp_ && new_frame_silence_flag());
    } else if (subcycle_ == 2) {
        // Parameters are only updated on the B cycle of each parameter count.
        const int gate = (inhibit_ && ip_ != 0) ? 0 : 1;
        const int shift = kInterpCoeff[size_t(ip_)];
        switch (pc_) {
            case 0:
                if (ip_ == 0) pitch_zero_ = false;
                current_energy_ =
                    zpar_ ? 0
                          : current_energy_ +
                                (((int(kEnergy[size_t(new_energy_idx_)]) - current_energy_) *
                                  gate) >>
                                 shift);
                break;
            case 1:
                current_pitch_ =
                    zpar_ ? 0
                          : current_pitch_ +
                                (((int(kPitch[size_t(new_pitch_idx_)]) - current_pitch_) * gate) >>
                                 shift);
                break;
            default:
                if (pc_ <= 11) {
                    const int k = pc_ - 2;
                    const bool zero = (k < 4) ? zpar_ : uv_zpar_;
                    const int target = coefficient(k, new_k_idx_[size_t(k)]);
                    current_k_[size_t(k)] =
                        zero ? 0
                             : current_k_[size_t(k)] +
                                   (((target - current_k_[size_t(k)]) * gate) >> shift);
                }
                break;
        }
    }

    if (oldp_) {
        // Unvoiced: plus or minus half of the chirp table's maximum value.
        excitation_data_ = (rng_ & 1) ? ~0x3f : 0x40;
    } else {
        excitation_data_ = kChirp[size_t(std::min(pitch_count_, 51))];
    }

    // The LFSR is clocked 20 times per sample, once per T cycle.
    for (int i = 0; i < 20; ++i) {
        const uint16_t bit = uint16_t(((rng_ >> 12) ^ (rng_ >> 3) ^ (rng_ >> 2) ^ rng_) & 1);
        rng_ = uint16_t((rng_ << 1) | bit);
    }

    int32_t sample = lattice_filter();
    // The final addition in the K1 stage can overflow the 14 bit result.
    while (sample > 16383) sample -= 32768;
    while (sample < -16384) sample += 32768;
    out_sample_ = clip_analog(sample);

    advance_counters(true);

    ++pitch_count_;
    if (pitch_count_ >= current_pitch_ || pitch_zero_) pitch_count_ = 0;
    pitch_count_ &= 0x1ff;
}

void Tms5220::advance_counters(bool speaking) {
    ++subcycle_;
    if (subcycle_ == 2 && pc_ == 12) { // RESETF3
        if (ip_ == 7) {                // RESETL4
            if (speaking) {
                // Circuit 412 zeroes the pitch counter if interpolation was
                // inhibited during the frame transition.
                if (inhibit_) pitch_zero_ = true;
                olde_ = new_frame_silence_flag();
                oldp_ = new_frame_unvoiced_flag();
            }
            talkd_ = talk_;
            update_fifo_status_and_ints();
            if (!talk_ && spen_) talk_ = true;
        }
        subcycle_ = 1;
        pc_ = 0;
        ip_ = (ip_ + 1) & 7;
    } else if (subcycle_ == 3) {
        subcycle_ = 1;
        ++pc_;
    }
}

void Tms5220::tick(int cycles) {
    if (cycles <= 0) return;

    if (io_ready_delay_ > 0) {
        io_ready_delay_ -= cycles;
        if (io_ready_delay_ <= 0) {
            io_ready_delay_ = 0;
            service_io_ready();
        }
    }

    cycle_acc_ += cycles;
    while (cycle_acc_ >= kClocksPerSample) {
        cycle_acc_ -= kClocksPerSample;
        process_sample();
    }
}

int16_t Tms5220::last_sample() const {
    return int16_t(std::clamp(int32_t(float(out_sample_) * volume_), int32_t(-32768),
                              int32_t(32767)));
}

int16_t Tms5220::update() {
    update_cycle_acc_ += clock_;
    const uint32_t cycles = update_cycle_acc_ / kSampleRate;
    update_cycle_acc_ %= kSampleRate;
    tick(int(cycles));
    return last_sample();
}

} // namespace dsp
