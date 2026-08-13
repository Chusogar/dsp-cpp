#include "sound/ay8910.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace dsp {
namespace {

constexpr int kStep = 0x1000;
constexpr int kMaxOutput = 0x7fff;

enum Reg {
    AY_AFINE = 0,
    AY_ACOARSE = 1,
    AY_BFINE = 2,
    AY_BCOARSE = 3,
    AY_CFINE = 4,
    AY_CCOARSE = 5,
    AY_NOISEPER = 6,
    AY_ENABLE = 7,
    AY_AVOL = 8,
    AY_BVOL = 9,
    AY_CVOL = 10,
    AY_EFINE = 11,
    AY_ECOARSE = 12,
    AY_ESHAPE = 13,
    AY_PORTA = 14,
    AY_PORTB = 15,
};

std::array<int32_t, 32> build_volume_table() {
    std::array<int32_t, 32> table{};
    double out = kMaxOutput / 4.0;
    for (int i = 31; i >= 1; i--) {
        table[size_t(i)] = int32_t(out + 0.5);
        out /= 1.188502227;  // -1.5 dB per step
    }
    table[0] = 0;
    return table;
}

const std::array<int32_t, 32> kVolTable = build_volume_table();

}  // namespace

AY8910::AY8910(uint32_t clock, float amplitude) : clock_(clock), amplitude_(amplitude) {
    update_step_ = int32_t((int64_t(kStep) * kSampleRate * 8) / clock_);
    period_a_ = period_b_ = period_c_ = period_e_ = period_n_ = update_step_;
    reset();
}

void AY8910::set_clock(uint32_t clock) {
    if (clock == 0 || clock == clock_) return;
    clock_ = clock;
    update_step_ = int32_t((int64_t(kStep) * kSampleRate * 8) / clock_);
    period_a_ = period_b_ = period_c_ = period_e_ = period_n_ = update_step_;
}

void AY8910::set_port_handlers(PortRead port_a_read, PortRead port_b_read, PortWrite port_a_write,
                               PortWrite port_b_write) {
    port_a_read_ = std::move(port_a_read);
    port_b_read_ = std::move(port_b_read);
    port_a_write_ = std::move(port_a_write);
    port_b_write_ = std::move(port_b_write);
}

void AY8910::reset() {
    latch_ = 0;
    output_a_ = output_b_ = output_c_ = 0;
    output_n_ = 0xff;
    rng_ = 1;
    last_enable_ = -1;
    for (uint8_t reg = 0; reg <= 13; reg++) write_reg(reg, 0);
}

void AY8910::write_reg(uint8_t reg, uint8_t value) {
    regs_[reg] = value;
    int32_t old = 0;
    switch (reg) {
        case AY_AFINE:
        case AY_ACOARSE:
            regs_[AY_ACOARSE] &= 0x0f;
            old = period_a_;
            period_a_ = (regs_[AY_AFINE] + 256 * regs_[AY_ACOARSE]) * update_step_;
            if (period_a_ == 0) period_a_ = update_step_;
            count_a_ += period_a_ - old;
            if (count_a_ <= 0) count_a_ = 1;
            break;
        case AY_BFINE:
        case AY_BCOARSE:
            regs_[AY_BCOARSE] &= 0x0f;
            old = period_b_;
            period_b_ = (regs_[AY_BFINE] + 256 * regs_[AY_BCOARSE]) * update_step_;
            if (period_b_ == 0) period_b_ = update_step_;
            count_b_ += period_b_ - old;
            if (count_b_ <= 0) count_b_ = 1;
            break;
        case AY_CFINE:
        case AY_CCOARSE:
            regs_[AY_CCOARSE] &= 0x0f;
            old = period_c_;
            period_c_ = (regs_[AY_CFINE] + 256 * regs_[AY_CCOARSE]) * update_step_;
            if (period_c_ == 0) period_c_ = update_step_;
            count_c_ += period_c_ - old;
            if (count_c_ <= 0) count_c_ = 1;
            break;
        case AY_NOISEPER:
            regs_[AY_NOISEPER] &= 0x1f;
            old = period_n_;
            period_n_ = regs_[AY_NOISEPER] * update_step_;
            if (period_n_ == 0) period_n_ = update_step_;
            count_n_ += period_n_ - old;
            if (count_n_ <= 0) count_n_ = 1;
            break;
        case AY_ENABLE:
            if (last_enable_ == -1 || ((last_enable_ & 0x40) != (regs_[AY_ENABLE] & 0x40))) {
                if (port_a_write_) {
                    port_a_write_((regs_[AY_ENABLE] & 0x40) ? regs_[AY_PORTA] : 0xff);
                }
            }
            if (last_enable_ == -1 || ((last_enable_ & 0x80) != (regs_[AY_ENABLE] & 0x80))) {
                if (port_b_write_) {
                    port_b_write_((regs_[AY_ENABLE] & 0x80) ? regs_[AY_PORTB] : 0xff);
                }
            }
            last_enable_ = regs_[AY_ENABLE];
            break;
        case AY_AVOL:
            regs_[AY_AVOL] &= 0x1f;
            envelope_a_ = regs_[AY_AVOL] & 0x10;
            old = regs_[AY_AVOL] ? regs_[AY_AVOL] * 2 + 1 : 0;
            vol_a_ = envelope_a_ ? vol_e_ : kVolTable[size_t(old)];
            break;
        case AY_BVOL:
            regs_[AY_BVOL] &= 0x1f;
            envelope_b_ = regs_[AY_BVOL] & 0x10;
            old = regs_[AY_BVOL] ? regs_[AY_BVOL] * 2 + 1 : 0;
            vol_b_ = envelope_b_ ? vol_e_ : kVolTable[size_t(old)];
            break;
        case AY_CVOL:
            regs_[AY_CVOL] &= 0x1f;
            envelope_c_ = regs_[AY_CVOL] & 0x10;
            old = regs_[AY_CVOL] ? regs_[AY_CVOL] * 2 + 1 : 0;
            vol_c_ = envelope_c_ ? vol_e_ : kVolTable[size_t(old)];
            break;
        case AY_EFINE:
        case AY_ECOARSE:
            old = period_e_;
            period_e_ = (regs_[AY_EFINE] + 256 * regs_[AY_ECOARSE]) * update_step_;
            if (period_e_ == 0) period_e_ = update_step_ / 2;
            count_e_ += period_e_ - old;
            if (count_e_ <= 0) count_e_ = 1;
            break;
        case AY_ESHAPE:
            regs_[AY_ESHAPE] &= 0x0f;
            attack_ = (regs_[AY_ESHAPE] & 0x04) ? 0x1f : 0;
            if ((regs_[AY_ESHAPE] & 0x08) == 0) {
                hold_ = 1;
                alternate_ = attack_;
            } else {
                hold_ = regs_[AY_ESHAPE] & 1;
                alternate_ = regs_[AY_ESHAPE] & 2;
            }
            count_e_ = period_e_;
            count_env_ = 0x1f;
            holding_ = 0;
            vol_e_ = kVolTable[size_t(count_env_ ^ attack_)];
            if (envelope_a_) vol_a_ = vol_e_;
            if (envelope_b_) vol_b_ = vol_e_;
            if (envelope_c_) vol_c_ = vol_e_;
            break;
        case AY_PORTA:
            if (port_a_write_) port_a_write_(value);
            break;
        case AY_PORTB:
            if (port_b_write_) port_b_write_(value);
            break;
        default:
            break;
    }
}

uint8_t AY8910::read_reg(uint8_t reg) {
    if (reg == AY_PORTA && port_a_read_) regs_[AY_PORTA] = port_a_read_();
    if (reg == AY_PORTB && port_b_read_) regs_[AY_PORTB] = port_b_read_();
    return regs_[reg];
}

int32_t AY8910::update() {
    if (regs_[AY_ENABLE] & 0x01) {
        if (count_a_ <= kStep) count_a_ += kStep;
        output_a_ = 1;
    } else if (regs_[AY_AVOL] == 0) {
        if (count_a_ <= kStep) count_a_ += kStep;
    }
    if (regs_[AY_ENABLE] & 0x02) {
        if (count_b_ <= kStep) count_b_ += kStep;
        output_b_ = 1;
    } else if (regs_[AY_BVOL] == 0) {
        if (count_b_ <= kStep) count_b_ += kStep;
    }
    if (regs_[AY_ENABLE] & 0x04) {
        if (count_c_ <= kStep) count_c_ += kStep;
        output_c_ = 1;
    } else if (regs_[AY_CVOL] == 0) {
        if (count_c_ <= kStep) count_c_ += kStep;
    }
    if ((regs_[AY_ENABLE] & 0x38) == 0x38) {
        if (count_n_ <= kStep) count_n_ += kStep;
    }

    int32_t out_noise = output_n_ | regs_[AY_ENABLE];
    int32_t vol_a = 0, vol_b = 0, vol_c = 0;
    int32_t left = kStep;

    do {
        int32_t next_event = std::min(count_n_, left);

        if (out_noise & 0x08) {
            if (output_a_) vol_a += count_a_;
            count_a_ -= next_event;
            while (count_a_ <= 0) {
                count_a_ += period_a_;
                if (count_a_ > 0) {
                    output_a_ ^= 1;
                    if (output_a_) vol_a += period_a_;
                    break;
                }
                count_a_ += period_a_;
                vol_a += period_a_;
            }
            if (output_a_) vol_a -= count_a_;
        } else {
            count_a_ -= next_event;
            while (count_a_ <= 0) {
                count_a_ += period_a_;
                if (count_a_ > 0) {
                    output_a_ ^= 1;
                    break;
                }
                count_a_ += period_a_;
            }
        }

        if (out_noise & 0x10) {
            if (output_b_) vol_b += count_b_;
            count_b_ -= next_event;
            while (count_b_ <= 0) {
                count_b_ += period_b_;
                if (count_b_ > 0) {
                    output_b_ ^= 1;
                    if (output_b_) vol_b += period_b_;
                    break;
                }
                count_b_ += period_b_;
                vol_b += period_b_;
            }
            if (output_b_) vol_b -= count_b_;
        } else {
            count_b_ -= next_event;
            while (count_b_ <= 0) {
                count_b_ += period_b_;
                if (count_b_ > 0) {
                    output_b_ ^= 1;
                    break;
                }
                count_b_ += period_b_;
            }
        }

        if (out_noise & 0x20) {
            if (output_c_) vol_c += count_c_;
            count_c_ -= next_event;
            while (count_c_ <= 0) {
                count_c_ += period_c_;
                if (count_c_ > 0) {
                    output_c_ ^= 1;
                    if (output_c_) vol_c += period_c_;
                    break;
                }
                count_c_ += period_c_;
                vol_c += period_c_;
            }
            if (output_c_) vol_c -= count_c_;
        } else {
            count_c_ -= next_event;
            while (count_c_ <= 0) {
                count_c_ += period_c_;
                if (count_c_ > 0) {
                    output_c_ ^= 1;
                    break;
                }
                count_c_ += period_c_;
            }
        }

        count_n_ -= next_event;
        if (count_n_ <= 0) {
            if ((rng_ + 1) & 2) {  // (bit0 ^ bit1)
                output_n_ = ~output_n_;
                out_noise = output_n_ | regs_[AY_ENABLE];
            }
            if (rng_ & 1) rng_ ^= 0x28000;  // Galois configuration
            rng_ >>= 1;
            count_n_ += period_n_;
        }
        left -= next_event;
    } while (left > 0);

    if (holding_ == 0) {
        count_e_ -= kStep;
        if (count_e_ <= 0) {
            do {
                count_env_--;
                count_e_ += period_e_;
            } while (count_e_ <= 0);
            if (count_env_ < 0) {
                if (hold_) {
                    if (alternate_) attack_ ^= 0x1f;
                    holding_ = 1;
                    count_env_ = 0;
                } else {
                    if (alternate_ && (count_env_ & 0x20)) attack_ ^= 0x1f;
                    count_env_ &= 0x1f;
                }
            }
            vol_e_ = kVolTable[size_t(count_env_ ^ attack_)];
            if (envelope_a_) vol_a_ = vol_e_;
            if (envelope_b_) vol_b_ = vol_e_;
            if (envelope_c_) vol_c_ = vol_e_;
        }
    }

    double mixed = (double(vol_a) * vol_a_ / kStep + double(vol_b) * vol_b_ / kStep +
                    double(vol_c) * vol_c_ / kStep) *
                   amplitude_;
    return int32_t(mixed);
}

}  // namespace dsp
