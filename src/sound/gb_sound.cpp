#include "sound/gb_sound.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace dsp {
namespace {

// Register offsets relative to $FF10.
constexpr uint8_t NR10 = 0x00;
constexpr uint8_t NR11 = 0x01;
constexpr uint8_t NR12 = 0x02;
constexpr uint8_t NR13 = 0x03;
constexpr uint8_t NR14 = 0x04;
constexpr uint8_t NR21 = 0x06;
constexpr uint8_t NR22 = 0x07;
constexpr uint8_t NR23 = 0x08;
constexpr uint8_t NR24 = 0x09;
constexpr uint8_t NR30 = 0x0A;
constexpr uint8_t NR31 = 0x0B;
constexpr uint8_t NR32 = 0x0C;
constexpr uint8_t NR33 = 0x0D;
constexpr uint8_t NR34 = 0x0E;
constexpr uint8_t NR41 = 0x10;
constexpr uint8_t NR42 = 0x11;
constexpr uint8_t NR43 = 0x12;
constexpr uint8_t NR44 = 0x13;
constexpr uint8_t NR50 = 0x14;
constexpr uint8_t NR51 = 0x15;
constexpr uint8_t NR52 = 0x16;
constexpr uint8_t AUD3W0 = 0x20;

constexpr int FIXED_POINT = 16;
constexpr int FREQ_BASE = GBSound::kSampleRate;

}  // namespace

GBSound::GBSound() {
    build_tables();
    reset();
}

void GBSound::build_tables() {
    for (int i = 0; i < 8; i++) {
        env_length_table_[i] = uint16_t(
            (uint64_t(i) * (uint64_t(1) << FIXED_POINT) / 64 * FREQ_BASE) >> FIXED_POINT);
        swp_time_table_[i] = uint16_t(
            (uint64_t(i) * (uint64_t(1) << FIXED_POINT) / 128 * FREQ_BASE) >> (FIXED_POINT - 1));
    }
    for (int i = 0; i < 2048; i++) {
        const double f = 131072.0 / (2048 - i);
        period_table_[i] =
            uint32_t(((uint64_t(1) << FIXED_POINT) / f) * FREQ_BASE);
        const double f3 = 65536.0 / (2048 - i);
        period_mode3_table_[i] =
            uint32_t(((uint64_t(1) << FIXED_POINT) / f3) * FREQ_BASE);
    }
    for (int i = 0; i < 8; i++) {
        for (int j = 0; j < 16; j++) {
            const double ratio = (i == 0) ? 0.5 : double(i);
            const double f = 524288.0 / ratio / double(1 << (j + 1));
            period_mode4_table_[i][j] =
                uint32_t(((uint64_t(1) << FIXED_POINT) / f) * FREQ_BASE);
        }
    }
    for (int i = 0; i < 64; i++) {
        length_table_[i] = uint16_t(
            (uint64_t(64 - i) * (uint64_t(1) << FIXED_POINT) / 256 * FREQ_BASE) >>
            FIXED_POINT);
    }
    for (int i = 0; i < 256; i++) {
        length_mode3_table_[i] = uint32_t(
            (uint64_t(256 - i) * (uint64_t(1) << FIXED_POINT) / 256 * FREQ_BASE) >>
            FIXED_POINT);
    }
}

void GBSound::reset() {
    regs_.fill(0);
    for (auto& c : ch_) c = Channel{};
    control_ = Control{};
    write_internal(NR52, 0x00);
}

// ---------------------------------------------------------------------------
// Register access
// ---------------------------------------------------------------------------

uint8_t GBSound::sound_r(uint8_t offset) const {
    switch (offset) {
        case NR10: return uint8_t(0x80 | regs_[offset]);
        case NR11: return uint8_t(0x3F | regs_[offset]);
        case NR12: return regs_[offset];
        case NR13: return 0xFF;
        case NR14: return uint8_t(0xBF | regs_[offset]);
        case NR21: return uint8_t(0x3F | regs_[offset]);
        case NR22: return regs_[offset];
        case NR23: return 0xFF;
        case NR24: return uint8_t(0xBF | regs_[offset]);
        case NR30: return uint8_t(0x7F | regs_[offset]);
        case NR31: return 0xFF;
        case NR32: return uint8_t(0x9F | regs_[offset]);
        case NR33: return 0xFF;
        case NR34: return uint8_t(0xBF | regs_[offset]);
        case NR41: return 0xFF;
        case NR42: return regs_[offset];
        case NR43: return regs_[offset];
        case NR44: return uint8_t(0xBF | regs_[offset]);
        case NR50: return regs_[offset];
        case NR51: return regs_[offset];
        case 0x05:
        case 0x0A:
            return 0xFF;
        case NR52:
            return uint8_t(0x70 | regs_[offset]);
        default:
            return regs_[offset];
    }
}

void GBSound::sound_w(uint8_t offset, uint8_t value) {
    // When master is off only NR52 is writable.
    if (!control_.on && offset != NR52) return;
    write_internal(offset, value);
}

uint8_t GBSound::wave_r(uint8_t offset) const {
    return uint8_t(regs_[AUD3W0 + (offset & 0x0F)] | (ch_[2].on ? 0xFF : 0));
}

void GBSound::wave_w(uint8_t offset, uint8_t value) {
    regs_[AUD3W0 + (offset & 0x0F)] = value;
}

void GBSound::write_internal(uint8_t offset, uint8_t value) {
    regs_[offset] = value;

    switch (offset) {
        // ---- Channel 1 ------------------------------------------------
        case NR10: {
            ch_[0].swp_shift = value & 0x07;
            ch_[0].swp_direction = (value & 0x08) >> 3;
            ch_[0].swp_direction |= (ch_[0].swp_direction - 1);
            ch_[0].swp_time = swp_time_table_[(value & 0x70) >> 4];
            break;
        }
        case NR11:
            ch_[0].duty = int8_t((value & 0xC0) >> 6);
            ch_[0].length = length_table_[value & 0x3F];
            break;
        case NR12:
            ch_[0].env_value = value >> 4;
            ch_[0].env_direction = int8_t((value & 0x08) >> 3);
            ch_[0].env_direction = int8_t(ch_[0].env_direction | (ch_[0].env_direction - 1));
            ch_[0].env_length = env_length_table_[value & 0x07];
            break;
        case NR13:
            ch_[0].frequency = uint32_t(((regs_[NR14] & 0x07) << 8) | regs_[NR13]);
            ch_[0].period = period_table_[ch_[0].frequency & 0x7FF];
            break;
        case NR14:
            ch_[0].mode = (value & 0x40) != 0;
            ch_[0].frequency = uint32_t(((regs_[NR14] & 0x07) << 8) | regs_[NR13]);
            ch_[0].period = period_table_[ch_[0].frequency & 0x7FF];
            if (value & 0x80) {
                if (!ch_[0].on) ch_[0].pos = 0;
                ch_[0].on = true;
                ch_[0].count = 0;
                ch_[0].env_value = regs_[NR12] >> 4;
                ch_[0].env_count = 0;
                ch_[0].swp_count = 0;
                ch_[0].signal = 1;
                regs_[NR52] = uint8_t(regs_[NR52] | 0x01);
            }
            break;

        // ---- Channel 2 ------------------------------------------------
        case NR21:
            ch_[1].duty = int8_t((value & 0xC0) >> 6);
            ch_[1].length = length_table_[value & 0x3F];
            break;
        case NR22:
            ch_[1].env_value = value >> 4;
            ch_[1].env_direction = int8_t((value & 0x08) >> 3);
            ch_[1].env_direction = int8_t(ch_[1].env_direction | (ch_[1].env_direction - 1));
            ch_[1].env_length = env_length_table_[value & 0x07];
            break;
        case NR23:
            ch_[1].period =
                period_table_[(((regs_[NR24] & 0x07) << 8) | regs_[NR23]) & 0x7FF];
            break;
        case NR24:
            ch_[1].mode = (value & 0x40) != 0;
            ch_[1].period =
                period_table_[(((regs_[NR24] & 0x07) << 8) | regs_[NR23]) & 0x7FF];
            if (value & 0x80) {
                if (!ch_[1].on) ch_[1].pos = 0;
                ch_[1].on = true;
                ch_[1].count = 0;
                ch_[1].env_value = regs_[NR22] >> 4;
                ch_[1].env_count = 0;
                ch_[1].signal = 1;
                regs_[NR52] = uint8_t(regs_[NR52] | 0x02);
            }
            break;

        // ---- Channel 3 ------------------------------------------------
        case NR30:
            if ((value & 0x80) == 0) {
                ch_[2].on = false;
                regs_[NR52] = uint8_t(regs_[NR52] & 0xFB);
            }
            break;
        case NR31:
            ch_[2].length = uint16_t(length_mode3_table_[value]);
            break;
        case NR32:
            ch_[2].level = uint8_t((value & 0x60) >> 5);
            break;
        case NR33:
            ch_[2].period = period_mode3_table_[
                (((regs_[NR34] & 0x07) << 8) | regs_[NR33]) & 0x7FF];
            break;
        case NR34:
            ch_[2].mode = (value & 0x40) != 0;
            ch_[2].period = period_mode3_table_[
                (((regs_[NR34] & 0x07) << 8) | regs_[NR33]) & 0x7FF];
            if (value & 0x80) {
                if (!ch_[2].on) {
                    ch_[2].pos = 0;
                    ch_[2].offset = 0;
                    ch_[2].duty = 0;
                }
                ch_[2].on = true;
                ch_[2].count = 0;
                ch_[2].duty = 1;
                ch_[2].dutycount = 0;
                regs_[NR52] = uint8_t(regs_[NR52] | 0x04);
            }
            break;

        // ---- Channel 4 ------------------------------------------------
        case NR41:
            ch_[3].length = length_table_[value & 0x3F];
            break;
        case NR42:
            ch_[3].env_value = value >> 4;
            ch_[3].env_direction = int8_t((value & 0x08) >> 3);
            ch_[3].env_direction = int8_t(ch_[3].env_direction | (ch_[3].env_direction - 1));
            ch_[3].env_length = env_length_table_[value & 0x07];
            break;
        case NR43: {
            ch_[3].period = period_mode4_table_[value & 0x07][(value & 0xF0) >> 4];
            ch_[3].ply_step = (value & 0x08) != 0;
            break;
        }
        case NR44:
            ch_[3].mode = (value & 0x40) != 0;
            if (value & 0x80) {
                if (!ch_[3].on) ch_[3].pos = 0;
                ch_[3].on = true;
                ch_[3].count = 0;
                ch_[3].env_value = regs_[NR42] >> 4;
                ch_[3].env_count = 0;
                ch_[3].signal = int8_t(regs_[NR43]);
                ch_[3].ply_value = 0x7FFF;
                regs_[NR52] = uint8_t(regs_[NR52] | 0x08);
            }
            break;

        // ---- Mixer ----------------------------------------------------
        case NR50:
            control_.vol_left = uint8_t((value & 0x70) >> 4);
            control_.vol_right = value & 0x07;
            break;
        case NR51:
            control_.mode_right[0] = (value & 0x01) != 0;
            control_.mode_left[0] = (value & 0x10) != 0;
            control_.mode_right[1] = (value & 0x02) != 0;
            control_.mode_left[1] = (value & 0x20) != 0;
            control_.mode_right[2] = (value & 0x04) != 0;
            control_.mode_left[2] = (value & 0x40) != 0;
            control_.mode_right[3] = (value & 0x08) != 0;
            control_.mode_left[3] = (value & 0x80) != 0;
            break;
        case NR52:
            control_.on = (value & 0x80) != 0;
            if (!control_.on) {
                // Power off: reset all registers (recursively safe because
                // control_.on is already false for nested writes except NR52).
                write_internal(NR10, 0x80);
                write_internal(NR11, 0x3F);
                write_internal(NR12, 0x00);
                write_internal(NR13, 0xFE);
                write_internal(NR14, 0xBF);
                write_internal(NR21, 0x3F);
                write_internal(NR22, 0x00);
                write_internal(NR23, 0xFF);
                write_internal(NR24, 0xBF);
                write_internal(NR30, 0x7F);
                write_internal(NR31, 0xFF);
                write_internal(NR32, 0x9F);
                write_internal(NR33, 0xFF);
                write_internal(NR34, 0xBF);
                write_internal(NR41, 0xFF);
                write_internal(NR42, 0x00);
                write_internal(NR43, 0x00);
                write_internal(NR44, 0xBF);
                write_internal(NR50, 0x00);
                write_internal(NR51, 0x00);
                ch_[0].on = ch_[1].on = ch_[2].on = ch_[3].on = false;
                regs_[NR52] = 0;
            } else {
                regs_[NR52] = uint8_t(0x80 | (regs_[NR52] & 0x0F));
            }
            break;
        default:
            break;
    }
}

// ---------------------------------------------------------------------------
// Sample generation
// ---------------------------------------------------------------------------

int16_t GBSound::update() {
    if (!control_.on) return 0;

    int left = 0;
    int right = 0;

    // ---- Channel 1: square + sweep + envelope ----
    if (ch_[0].on) {
        int sample = int(ch_[0].signal) * ch_[0].env_value;
        ch_[0].pos++;
        const uint32_t half =
            uint32_t(double(ch_[0].period) / kDutyTable[ch_[0].duty & 3]) >> 16;
        if (ch_[0].pos == half) {
            ch_[0].signal = int8_t(-ch_[0].signal);
        } else if (ch_[0].pos > (ch_[0].period >> 16)) {
            ch_[0].pos = 0;
            ch_[0].signal = int8_t(-ch_[0].signal);
        }
        if (ch_[0].length != 0 && ch_[0].mode) {
            ch_[0].count++;
            if (ch_[0].count >= int(ch_[0].length)) {
                ch_[0].on = false;
                regs_[NR52] = uint8_t(regs_[NR52] & 0xFE);
            }
        }
        if (ch_[0].env_length != 0) {
            ch_[0].env_count++;
            if (ch_[0].env_count >= int(ch_[0].env_length)) {
                ch_[0].env_count = 0;
                ch_[0].env_value += ch_[0].env_direction;
                if (ch_[0].env_value < 0) ch_[0].env_value = 0;
                if (ch_[0].env_value > 15) ch_[0].env_value = 15;
            }
        }
        // Frequency sweep
        if (ch_[0].swp_time != 0) {
            ch_[0].swp_count++;
            if (ch_[0].swp_count >= int(ch_[0].swp_time)) {
                ch_[0].swp_count = 0;
                if (ch_[0].swp_shift != 0) {
                    int32_t freq = int32_t(ch_[0].frequency);
                    const int32_t delta = freq >> ch_[0].swp_shift;
                    freq += delta * ch_[0].swp_direction;
                    if (freq <= 0) {
                        ch_[0].on = false;
                        regs_[NR52] = uint8_t(regs_[NR52] & 0xFE);
                    } else if (freq < 2048) {
                        ch_[0].frequency = uint32_t(freq);
                        ch_[0].period = period_table_[freq];
                    }
                }
            }
        }
        if (control_.mode_left[0]) left += sample;
        if (control_.mode_right[0]) right += sample;
    }

    // ---- Channel 2: square + envelope ----
    if (ch_[1].on) {
        int sample = int(ch_[1].signal) * ch_[1].env_value;
        ch_[1].pos++;
        const uint32_t half =
            uint32_t(double(ch_[1].period) / kDutyTable[ch_[1].duty & 3]) >> 16;
        if (ch_[1].pos == half) {
            ch_[1].signal = int8_t(-ch_[1].signal);
        } else if (ch_[1].pos > (ch_[1].period >> 16)) {
            ch_[1].pos = 0;
            ch_[1].signal = int8_t(-ch_[1].signal);
        }
        if (ch_[1].length != 0 && ch_[1].mode) {
            ch_[1].count++;
            if (ch_[1].count >= int(ch_[1].length)) {
                ch_[1].on = false;
                regs_[NR52] = uint8_t(regs_[NR52] & 0xFD);
            }
        }
        if (ch_[1].env_length != 0) {
            ch_[1].env_count++;
            if (ch_[1].env_count >= int(ch_[1].env_length)) {
                ch_[1].env_count = 0;
                ch_[1].env_value += ch_[1].env_direction;
                if (ch_[1].env_value < 0) ch_[1].env_value = 0;
                if (ch_[1].env_value > 15) ch_[1].env_value = 15;
            }
        }
        if (control_.mode_left[1]) left += sample;
        if (control_.mode_right[1]) right += sample;
    }

    // ---- Channel 3: wave ----
    if (ch_[2].on) {
        int sample = 0;
        // 4-bit samples packed in wave RAM; level shifts 0/1/2/3.
        const uint8_t byte = regs_[AUD3W0 + (ch_[2].offset >> 1)];
        uint8_t nibble = (ch_[2].offset & 1) ? (byte & 0x0F) : (byte >> 4);
        if (ch_[2].level > 0) {
            nibble = uint8_t(nibble >> (ch_[2].level - 1));
            sample = int(nibble) - 8;  // centre around 0
        }
        ch_[2].pos++;
        if (ch_[2].pos >= (ch_[2].period >> 16)) {
            ch_[2].pos = 0;
            ch_[2].offset = uint8_t((ch_[2].offset + 1) & 0x1F);
        }
        if (ch_[2].length != 0 && ch_[2].mode) {
            ch_[2].count++;
            if (ch_[2].count >= int(ch_[2].length)) {
                ch_[2].on = false;
                regs_[NR52] = uint8_t(regs_[NR52] & 0xFB);
            }
        }
        if (control_.mode_left[2]) left += sample;
        if (control_.mode_right[2]) right += sample;
    }

    // ---- Channel 4: noise ----
    if (ch_[3].on) {
        int sample = int(ch_[3].signal & 1) * ch_[3].env_value;
        // Convert 0/1 to +/- for mixing consistency.
        if ((ch_[3].signal & 1) == 0) sample = -ch_[3].env_value;
        ch_[3].pos++;
        if (ch_[3].pos > (ch_[3].period >> 16)) {
            ch_[3].pos = 0;
            uint32_t mask;
            if (ch_[3].ply_step) {
                mask = (((ch_[3].ply_value & 2) / 2) ^ (ch_[3].ply_value & 1)) << 6;
            } else {
                mask = (((ch_[3].ply_value & 2) / 2) ^ (ch_[3].ply_value & 1)) << 14;
            }
            ch_[3].ply_value = (ch_[3].ply_value >> 1) | mask;
            if (ch_[3].ply_step)
                ch_[3].ply_value &= 0x7F;
            else
                ch_[3].ply_value &= 0x7FFF;
            ch_[3].signal = int8_t(ch_[3].ply_value);
        }
        if (ch_[3].length != 0 && ch_[3].mode) {
            ch_[3].count++;
            if (ch_[3].count >= int(ch_[3].length)) {
                ch_[3].on = false;
                regs_[NR52] = uint8_t(regs_[NR52] & 0xF7);
            }
        }
        if (ch_[3].env_length != 0) {
            ch_[3].env_count++;
            if (ch_[3].env_count >= int(ch_[3].env_length)) {
                ch_[3].env_count = 0;
                ch_[3].env_value += ch_[3].env_direction;
                if (ch_[3].env_value < 0) ch_[3].env_value = 0;
                if (ch_[3].env_value > 15) ch_[3].env_value = 15;
            }
        }
        if (control_.mode_left[3]) left += sample;
        if (control_.mode_right[3]) right += sample;
    }

    left *= control_.vol_left;
    right *= control_.vol_right;
    // Boost (matches Pascal `shl 6`).
    left <<= 6;
    right <<= 6;

    // Refresh NR52 activity bits.
    regs_[NR52] = uint8_t((regs_[NR52] & 0xF0) | (ch_[0].on ? 1 : 0) |
                          (ch_[1].on ? 2 : 0) | (ch_[2].on ? 4 : 0) |
                          (ch_[3].on ? 8 : 0));

    // Mono mix, clamp to int16.
    int mixed = (left + right) / 2;
    if (mixed > 32767) mixed = 32767;
    if (mixed < -32768) mixed = -32768;
    return int16_t(mixed);
}

}  // namespace dsp
