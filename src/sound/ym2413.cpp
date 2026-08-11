#include "sound/ym2413.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace dsp {
namespace {

constexpr uint8_t kMulTab[16] = {
    1, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 20, 24, 24, 30, 30,
};
constexpr uint8_t kKslShift[4] = {31, 2, 1, 0};
constexpr uint16_t kSlTab[16] = {
    0, 8, 16, 24, 32, 40, 48, 56, 64, 72, 80, 88, 96, 104, 112, 120,
};

constexpr int kRateSteps = 8;

constexpr uint8_t kEgInc[15 * kRateSteps] = {
    0, 1, 0, 1, 0, 1, 0, 1,  // 0
    0, 1, 0, 1, 1, 1, 0, 1,  // 1
    0, 1, 1, 1, 0, 1, 1, 1,  // 2
    0, 1, 1, 1, 1, 1, 1, 1,  // 3
    1, 1, 1, 1, 1, 1, 1, 1,  // 4
    1, 1, 1, 2, 1, 1, 1, 2,  // 5
    1, 2, 1, 2, 1, 2, 1, 2,  // 6
    1, 2, 2, 2, 1, 2, 2, 2,  // 7
    2, 2, 2, 2, 2, 2, 2, 2,  // 8
    2, 2, 2, 4, 2, 2, 2, 4,  // 9
    2, 4, 2, 4, 2, 4, 2, 4,  // 10
    2, 4, 4, 4, 2, 4, 4, 4,  // 11
    4, 4, 4, 4, 4, 4, 4, 4,  // 12
    8, 8, 8, 8, 8, 8, 8, 8,  // 13
    0, 0, 0, 0, 0, 0, 0, 0,  // 14
};

constexpr uint8_t kEgRateSelect[16 + 64 + 16] = {
    14 * kRateSteps, 14 * kRateSteps, 14 * kRateSteps, 14 * kRateSteps, 14 * kRateSteps,
    14 * kRateSteps, 14 * kRateSteps, 14 * kRateSteps, 14 * kRateSteps, 14 * kRateSteps,
    14 * kRateSteps, 14 * kRateSteps, 14 * kRateSteps, 14 * kRateSteps, 14 * kRateSteps,
    14 * kRateSteps,
    0, 1 * kRateSteps, 2 * kRateSteps, 3 * kRateSteps, 0, 1 * kRateSteps, 2 * kRateSteps, 3 * kRateSteps,
    0, 1 * kRateSteps, 2 * kRateSteps, 3 * kRateSteps, 0, 1 * kRateSteps, 2 * kRateSteps, 3 * kRateSteps,
    0, 1 * kRateSteps, 2 * kRateSteps, 3 * kRateSteps, 0, 1 * kRateSteps, 2 * kRateSteps, 3 * kRateSteps,
    0, 1 * kRateSteps, 2 * kRateSteps, 3 * kRateSteps, 0, 1 * kRateSteps, 2 * kRateSteps, 3 * kRateSteps,
    0, 1 * kRateSteps, 2 * kRateSteps, 3 * kRateSteps, 0, 1 * kRateSteps, 2 * kRateSteps, 3 * kRateSteps,
    0, 1 * kRateSteps, 2 * kRateSteps, 3 * kRateSteps, 0, 1 * kRateSteps, 2 * kRateSteps, 3 * kRateSteps,
    0, 1 * kRateSteps, 2 * kRateSteps, 3 * kRateSteps,
    4 * kRateSteps, 5 * kRateSteps, 6 * kRateSteps, 7 * kRateSteps,
    8 * kRateSteps, 9 * kRateSteps, 10 * kRateSteps, 11 * kRateSteps,
    12 * kRateSteps, 12 * kRateSteps, 12 * kRateSteps, 12 * kRateSteps,
    12 * kRateSteps, 12 * kRateSteps, 12 * kRateSteps, 12 * kRateSteps, 12 * kRateSteps,
    12 * kRateSteps, 12 * kRateSteps, 12 * kRateSteps, 12 * kRateSteps, 12 * kRateSteps,
    12 * kRateSteps, 12 * kRateSteps, 12 * kRateSteps, 12 * kRateSteps,
};

constexpr uint8_t kEgRateShift[16 + 64 + 16] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    13, 13, 13, 13, 12, 12, 12, 12, 11, 11, 11, 11, 10, 10, 10, 10,
    9, 9, 9, 9, 8, 8, 8, 8, 7, 7, 7, 7, 6, 6, 6, 6,
    5, 5, 5, 5, 4, 4, 4, 4, 3, 3, 3, 3, 2, 2, 2, 2,
    1, 1, 1, 1,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};

constexpr uint8_t kLfoAmTable[210] = {
    0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4,
    5, 5, 5, 5, 6, 6, 6, 6, 7, 7, 7, 7, 8, 8, 8, 8, 9, 9, 9, 9, 10, 10, 10, 10,
    11, 11, 11, 11, 12, 12, 12, 12, 13, 13, 13, 13, 14, 14, 14, 14, 15, 15, 15, 15,
    16, 16, 16, 16, 17, 17, 17, 17, 18, 18, 18, 18, 19, 19, 19, 19, 20, 20, 20, 20,
    21, 21, 21, 21, 22, 22, 22, 22, 23, 23, 23, 23, 24, 24, 24, 24, 25, 25, 25, 25,
    26, 26, 26, 25, 25, 25, 25, 24, 24, 24, 24, 23, 23, 23, 23, 22, 22, 22, 22,
    21, 21, 21, 21, 20, 20, 20, 20, 19, 19, 19, 19, 18, 18, 18, 18, 17, 17, 17, 17,
    16, 16, 16, 16, 15, 15, 15, 15, 14, 14, 14, 14, 13, 13, 13, 13, 12, 12, 12, 12,
    11, 11, 11, 11, 10, 10, 10, 10, 9, 9, 9, 9, 8, 8, 8, 8, 7, 7, 7, 7, 6, 6, 6, 6,
    5, 5, 5, 5, 4, 4, 4, 4, 3, 3, 3, 3, 2, 2, 2, 2, 1, 1, 1, 1,
};

constexpr int32_t kLfoPmTable[64] = {
    0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, -1, 0, 0, 0, 2, 1, 0, -1, -2, -1, 0, 1,
    3, 1, 0, -1, -3, -1, 0, 1, 4, 2, 0, -2, -4, -2, 0, 2, 5, 2, 0, -2, -5, -2, 0, 2,
    6, 3, 0, -3, -6, -3, 0, 3, 7, 3, 0, -3, -7, -3, 0, 3,
};

// Instrument ROM (YM2413 patches + drums), from ym_2413.pas TABLE.
constexpr uint8_t kTable[19][8] = {
    {0x49, 0x4c, 0x4c, 0x12, 0x00, 0x00, 0x00, 0x00},
    {0x61, 0x61, 0x1e, 0x17, 0xf0, 0x78, 0x00, 0x17},
    {0x13, 0x41, 0x1e, 0x0d, 0xd7, 0xf7, 0x13, 0x13},
    {0x13, 0x01, 0x99, 0x04, 0xf2, 0xf4, 0x11, 0x23},
    {0x21, 0x61, 0x1b, 0x07, 0xaf, 0x64, 0x40, 0x27},
    {0x22, 0x21, 0x1e, 0x06, 0xf0, 0x75, 0x08, 0x18},
    {0x31, 0x22, 0x16, 0x05, 0x90, 0x71, 0x00, 0x13},
    {0x21, 0x61, 0x1d, 0x07, 0x82, 0x80, 0x10, 0x17},
    {0x23, 0x21, 0x2d, 0x16, 0xc0, 0x70, 0x07, 0x07},
    {0x61, 0x61, 0x1b, 0x06, 0x64, 0x65, 0x10, 0x17},
    {0x61, 0x61, 0x0c, 0x18, 0x85, 0xf0, 0x70, 0x07},
    {0x23, 0x01, 0x07, 0x11, 0xf0, 0xa4, 0x00, 0x22},
    {0x97, 0xc1, 0x24, 0x07, 0xff, 0xf8, 0x22, 0x12},
    {0x61, 0x10, 0x0c, 0x05, 0xf2, 0xf4, 0x40, 0x44},
    {0x01, 0x01, 0x55, 0x03, 0xf3, 0x92, 0xf3, 0xf3},
    {0x61, 0x41, 0x89, 0x03, 0xf1, 0xf4, 0xf0, 0x13},
    {0x01, 0x01, 0x18, 0x0f, 0xdf, 0xf8, 0x6a, 0x6d},  // BD
    {0x01, 0x01, 0x00, 0x00, 0xc8, 0xd8, 0xa7, 0x68},  // HH, SD
    {0x05, 0x01, 0x00, 0x00, 0xf8, 0xaa, 0x59, 0x55},  // TOM, CYM
};

// KSL table in 0.1875 dB units (from original KSL_TAB / DV).
constexpr double kDv = 0.1875;
constexpr double kKslRaw[8 * 16] = {
    // OCT 0
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    // OCT 1
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0.750, 1.125, 1.500, 1.875, 2.250, 2.625, 3.000,
    // OCT 2
    0, 0, 0, 0, 0, 1.125, 1.875, 2.625, 3.000, 3.750, 4.125, 4.500, 4.875, 5.250, 5.625, 6.000,
    // OCT 3
    0, 0, 0, 1.875, 3.000, 4.125, 4.875, 5.625, 6.000, 6.750, 7.125, 7.500, 7.875, 8.250, 8.625, 9.000,
    // OCT 4
    0, 0, 3.000, 4.875, 6.000, 7.125, 7.875, 8.625, 9.000, 9.750, 10.125, 10.500, 10.875, 11.250, 11.625, 12.000,
    // OCT 5
    0, 3.000, 6.000, 7.875, 9.000, 10.125, 10.875, 11.625, 12.000, 12.750, 13.125, 13.500, 13.875, 14.250, 14.625, 15.000,
    // OCT 6
    0, 6.000, 9.000, 10.875, 12.000, 13.125, 13.875, 14.625, 15.000, 15.750, 16.125, 16.500, 16.875, 17.250, 17.625, 18.000,
    // OCT 7
    0, 9.000, 12.000, 13.875, 15.000, 16.125, 16.875, 17.625, 18.000, 18.750, 19.125, 19.500, 19.875, 20.250, 20.625, 21.000,
};

}  // namespace

bool YM2413::tables_ready_ = false;
std::array<int32_t, YM2413::kTlTabLen> YM2413::tl_tab_{};
std::array<uint32_t, YM2413::kSinLen * 2> YM2413::sin_tab_{};
std::array<uint32_t, 1024> YM2413::fn_tab_{};
std::array<uint32_t, 8 * 16> YM2413::ksl_tab_{};

void YM2413::init_tables() {
    if (tables_ready_) return;
    tables_ready_ = true;

    for (int x = 0; x < kTlResLen; ++x) {
        double m = (1 << 16) / std::pow(2.0, (x + 1) * (kEnvStep / 4.0) / 8.0);
        m = std::floor(m);
        int n = int(m);
        n >>= 4;
        if (n & 1) {
            n = (n >> 1) + 1;
        } else {
            n >>= 1;
        }
        tl_tab_[x * 2 + 0] = n;
        tl_tab_[x * 2 + 1] = -tl_tab_[x * 2 + 0];
        for (int i = 1; i <= 10; ++i) {
            tl_tab_[x * 2 + 0 + i * 2 * kTlResLen] = tl_tab_[x * 2 + 0] >> i;
            tl_tab_[x * 2 + 1 + i * 2 * kTlResLen] = -tl_tab_[x * 2 + 0 + i * 2 * kTlResLen];
        }
    }

    for (int i = 0; i < kSinLen; ++i) {
        const double m = std::sin(((i * 2) + 1) * M_PI / kSinLen);
        double o = (m > 0.0) ? 8.0 * std::log(1.0 / m) / std::log(2.0)
                             : 8.0 * std::log(-1.0 / m) / std::log(2.0);
        o /= (kEnvStep / 4.0);
        int n = int(2.0 * o);
        if (n & 1) {
            n = (n >> 1) + 1;
        } else {
            n >>= 1;
        }
        sin_tab_[i] = (m >= 0) ? uint32_t(n * 2 + 0) : uint32_t(n * 2 + 1);
        if (i & (1 << (kSinBits - 1))) {
            sin_tab_[kSinLen + i] = kTlTabLen;
        } else {
            sin_tab_[kSinLen + i] = sin_tab_[i];
        }
    }

    for (int i = 0; i < 1024; ++i) {
        fn_tab_[i] = uint32_t(i * (64 << (kFreqSh - 10)));
    }

    for (int i = 0; i < 8 * 16; ++i) {
        ksl_tab_[i] = uint32_t(kKslRaw[i] / kDv);
    }
}

YM2413::YM2413(uint32_t clock, float amplitude)
    : clock_(clock), amplitude_(amplitude) {
    init_tables();
    for (int i = 0; i < 19; ++i) {
        for (int j = 0; j < 8; ++j) inst_tab_[i][j] = kTable[i][j];
    }
    lfo_am_inc_ = (1u << kLfoSh) / 64;
    lfo_pm_inc_ = (1u << kLfoSh) / 1024;
    noise_f_ = 1u << kFreqSh;
    eg_timer_add_ = 1u << kEgSh;
    eg_timer_overflow_ = 1u << kEgSh;
    reset();
}

void YM2413::reset() {
    eg_timer_ = 0;
    eg_cnt_ = 0;
    noise_rng_ = 1;
    noise_p_ = 0;
    lfo_am_cnt_ = 0;
    lfo_pm_cnt_ = 0;
    rhythm_ = 0;
    addr_ = 0;
    instvol_r_.fill(0);
    write_int(0x0f, 0);
    for (int i = 0x3f; i >= 0x10; --i) write_int(uint8_t(i), 0);
    for (int c = 0; c < 9; ++c) {
        for (int s = 0; s < 2; ++s) {
            ch_[c].slot[s].wavetable = 0;
            ch_[c].slot[s].state = kEgOff;
            ch_[c].slot[s].volume = kMaxAttIndex;
            ch_[c].slot[s].key = 0;
            ch_[c].slot[s].op1_out[0] = 0;
            ch_[c].slot[s].op1_out[1] = 0;
        }
    }
}

int32_t YM2413::volume_calc(int ch, int slot) const {
    return ch_[ch].slot[slot].tll + ch_[ch].slot[slot].volume +
           int32_t(lfo_am_ & ch_[ch].slot[slot].am_mask);
}

int32_t YM2413::op_calc1(uint32_t phase, uint32_t env, int32_t pm, uint32_t wave_tab) const {
    const int32_t i = int32_t(phase & ~uint32_t(kFreqMask)) + pm;
    const int tmp = (i >> kFreqSh) & kSinMask;
    const uint32_t p = (env << 5) + sin_tab_[wave_tab + uint32_t(tmp)];
    if (p >= uint32_t(kTlTabLen)) return 0;
    return tl_tab_[p];
}

int32_t YM2413::op_calc(uint32_t phase, uint32_t env, int32_t pm, uint32_t wave_tab) const {
    const int tmp =
        int(((int32_t(phase & ~uint32_t(kFreqMask)) + (pm << 17)) >> kFreqSh) & kSinMask);
    const uint32_t p = (env << 5) + sin_tab_[wave_tab + uint32_t(tmp)];
    if (p >= uint32_t(kTlTabLen)) return 0;
    return tl_tab_[p];
}

void YM2413::advance_lfo() {
    lfo_am_cnt_ += lfo_am_inc_;
    constexpr uint32_t kAmLimit = 210u << kLfoSh;
    if (lfo_am_cnt_ >= kAmLimit) lfo_am_cnt_ -= kAmLimit;
    lfo_am_ = kLfoAmTable[lfo_am_cnt_ >> kLfoSh] >> 1;
    lfo_pm_cnt_ += lfo_pm_inc_;
    lfo_pm_ = int32_t((lfo_pm_cnt_ >> kLfoSh) & 7);
}

void YM2413::chan_calc(int ch) {
    auto& s1 = ch_[ch].slot[kSlot1];
    auto& s2 = ch_[ch].slot[kSlot2];

    uint32_t env = uint32_t(volume_calc(ch, kSlot1));
    int32_t out = s1.op1_out[0] + s1.op1_out[1];
    s1.op1_out[0] = s1.op1_out[1];
    const int32_t phase_mod = s1.op1_out[0];
    s1.op1_out[1] = 0;
    if (env < uint32_t(kEnvQuiet)) {
        if (s1.fb_shift == 0) out = 0;
        s1.op1_out[1] = op_calc1(s1.phase, env, out << s1.fb_shift, s1.wavetable);
    }

    env = uint32_t(volume_calc(ch, kSlot2));
    if (env < uint32_t(kEnvQuiet)) {
        output_[0] += op_calc(s2.phase, env, phase_mod, s2.wavetable);
    }
}

void YM2413::rhythm_calc(uint32_t noise) {
    // Bass drum (ch 6)
    {
        auto& s1 = ch_[6].slot[kSlot1];
        auto& s2 = ch_[6].slot[kSlot2];
        uint32_t env = uint32_t(volume_calc(6, kSlot1));
        int32_t out = s1.op1_out[0] + s1.op1_out[1];
        s1.op1_out[0] = s1.op1_out[1];
        const int32_t phase_mod = s1.op1_out[0];
        s1.op1_out[1] = 0;
        if (env < uint32_t(kEnvQuiet)) {
            if (s1.fb_shift == 0) out = 0;
            s1.op1_out[1] = op_calc1(s1.phase, env, out << s1.fb_shift, s1.wavetable);
        }
        env = uint32_t(volume_calc(6, kSlot2));
        if (env < uint32_t(kEnvQuiet)) {
            output_[1] += op_calc(s2.phase, env, phase_mod, s2.wavetable) * 2;
        }
    }

    // High hat (ch7 slot1)
    {
        uint32_t env = uint32_t(volume_calc(7, kSlot1));
        if (env < uint32_t(kEnvQuiet)) {
            const uint32_t phase7 = ch_[7].slot[kSlot1].phase >> kFreqSh;
            const uint32_t phase8 = ch_[8].slot[kSlot2].phase >> kFreqSh;
            const int bit7 = (phase7 >> 7) & 1;
            const int bit3 = (phase7 >> 3) & 1;
            const int bit2 = (phase7 >> 2) & 1;
            const int res1 = (bit2 ^ bit7) | bit3;
            uint32_t phase = res1 ? (0x200 | (0xd0 >> 2)) : 0xd0;
            const int bit5e = (phase8 >> 5) & 1;
            const int bit3e = (phase8 >> 3) & 1;
            if (bit3e | bit5e) phase = 0x200 | (0xd0 >> 2);
            if (phase & 0x200) {
                if (noise) phase = 0x200 | 0xd0;
            } else if (noise) {
                phase = 0xd0 >> 2;
            }
            output_[1] +=
                op_calc(phase << kFreqSh, env, 0, ch_[7].slot[kSlot1].wavetable) * 2;
        }
    }

    // Snare (ch7 slot2)
    {
        uint32_t env = uint32_t(volume_calc(7, kSlot2));
        if (env < uint32_t(kEnvQuiet)) {
            const int bit8 = ((ch_[7].slot[kSlot1].phase >> kFreqSh) >> 8) & 1;
            uint32_t phase = bit8 ? 0x200 : 0x100;
            if (noise) phase ^= 0x100;
            output_[1] +=
                op_calc(phase << kFreqSh, env, 0, ch_[7].slot[kSlot2].wavetable) * 2;
        }
    }

    // Tom (ch8 slot1)
    {
        uint32_t env = uint32_t(volume_calc(8, kSlot1));
        if (env < uint32_t(kEnvQuiet)) {
            output_[1] +=
                op_calc(ch_[8].slot[kSlot1].phase, env, 0, ch_[8].slot[kSlot1].wavetable) * 2;
        }
    }

    // Top cymbal (ch8 slot2)
    {
        uint32_t env = uint32_t(volume_calc(8, kSlot2));
        if (env < uint32_t(kEnvQuiet)) {
            const uint32_t phase7 = ch_[7].slot[kSlot1].phase >> kFreqSh;
            const uint32_t phase8 = ch_[8].slot[kSlot2].phase >> kFreqSh;
            const int bit7 = (phase7 >> 7) & 1;
            const int bit3 = (phase7 >> 3) & 1;
            const int bit2 = (phase7 >> 2) & 1;
            const int res1 = (bit2 ^ bit7) | bit3;
            uint32_t phase = res1 ? 0x300 : 0x100;
            const int bit5e = (phase8 >> 5) & 1;
            const int bit3e = (phase8 >> 3) & 1;
            if (bit3e | bit5e) phase = 0x300;
            output_[1] +=
                op_calc(phase << kFreqSh, env, 0, ch_[8].slot[kSlot2].wavetable) * 2;
        }
    }
}

void YM2413::advance() {
    eg_timer_ += eg_timer_add_;
    while (eg_timer_ >= eg_timer_overflow_) {
        eg_timer_ -= eg_timer_overflow_;
        ++eg_cnt_;
        for (int i = 0; i < 18; ++i) {
            const int ch = i / 2;
            const int op = i & 1;
            auto& s = ch_[ch].slot[op];
            switch (s.state) {
                case kEgDmp:
                    if ((eg_cnt_ & ((1u << s.eg_sh_dp) - 1)) == 0) {
                        s.volume += kEgInc[s.eg_sel_dp + ((eg_cnt_ >> s.eg_sh_dp) & 7)];
                        if (s.volume >= kMaxAttIndex) {
                            s.volume = kMaxAttIndex;
                            s.state = kEgAtt;
                            s.phase = 0;
                        }
                    }
                    break;
                case kEgAtt:
                    if ((eg_cnt_ & ((1u << s.eg_sh_ar) - 1)) == 0) {
                        s.volume +=
                            (~s.volume *
                             kEgInc[s.eg_sel_ar + ((eg_cnt_ >> s.eg_sh_ar) & 7)]) >>
                            3;
                        if (s.volume <= kMinAttIndex) {
                            s.volume = kMinAttIndex;
                            s.state = kEgDec;
                        }
                    }
                    break;
                case kEgDec:
                    if ((eg_cnt_ & ((1u << s.eg_sh_dr) - 1)) == 0) {
                        s.volume += kEgInc[s.eg_sel_dr + ((eg_cnt_ >> s.eg_sh_dr) & 7)];
                        if (s.volume >= int32_t(s.sl)) s.state = kEgSus;
                    }
                    break;
                case kEgSus:
                    if (s.eg_type) break;  // sustained
                    if ((eg_cnt_ & ((1u << s.eg_sh_rr) - 1)) == 0) {
                        s.volume += kEgInc[s.eg_sel_rr + ((eg_cnt_ >> s.eg_sh_rr) & 7)];
                        if (s.volume >= kMaxAttIndex) {
                            s.volume = kMaxAttIndex;
                            s.state = kEgOff;
                        }
                    }
                    break;
                case kEgRel:
                    if ((eg_cnt_ & ((1u << s.eg_sh_rs) - 1)) == 0) {
                        s.volume += kEgInc[s.eg_sel_rs + ((eg_cnt_ >> s.eg_sh_rs) & 7)];
                        if (s.volume >= kMaxAttIndex) {
                            s.volume = kMaxAttIndex;
                            s.state = kEgOff;
                        }
                    }
                    break;
                default:
                    break;
            }
        }
    }

    for (int ch = 0; ch < 9; ++ch) {
        auto& c = ch_[ch];
        for (int op = 0; op < 2; ++op) {
            auto& s = c.slot[op];
            if (s.vib) {
                const uint32_t block_fnum = c.block_fnum;
                const uint32_t fnum_lfo = (block_fnum & 0x1c0) >> 4;
                const int32_t lfo_fn =
                    kLfoPmTable[fnum_lfo + uint32_t(lfo_pm_)];
                const uint32_t block = (block_fnum & 0x1c00) >> 10;
                int32_t tmp = int32_t(fn_tab_[block_fnum & 0x3ff] >> (7 - int(block))) +
                              lfo_fn;
                if (tmp < 0) tmp = 0;
                s.phase += uint32_t(tmp) * s.mul;
            } else {
                s.phase += s.freq;
            }
        }
    }

    noise_p_ += noise_f_;
    if (noise_p_ & 0x10000) {
        noise_p_ &= 0xffff;
        uint32_t noise = noise_rng_;
        noise_rng_ = (noise_rng_ >> 1) ^ ((noise & 1) ? 0x800302 : 0);
    }
}

void YM2413::calc_fcslot(int ch, int slot) {
    auto& c = ch_[ch];
    auto& s = c.slot[slot];
    s.freq = c.fc * s.mul;
    const uint8_t ksr = uint8_t(c.kcode >> s.ksr_m);
    if (s.ksr != ksr) {
        s.ksr = ksr;
        if ((s.ar + s.ksr) < (16 + 62)) {
            s.eg_sh_ar = kEgRateShift[s.ar + s.ksr];
            s.eg_sel_ar = kEgRateSelect[s.ar + s.ksr];
        } else {
            s.eg_sh_ar = 0;
            s.eg_sel_ar = uint8_t(13 * kRateSteps);
        }
        s.eg_sh_dr = kEgRateShift[s.dr + s.ksr];
        s.eg_sel_dr = kEgRateSelect[s.dr + s.ksr];
        s.eg_sh_rr = kEgRateShift[s.rr + s.ksr];
        s.eg_sel_rr = kEgRateSelect[s.rr + s.ksr];
    }
    const uint32_t slot_rs = c.sus ? uint32_t(16 + (5 << 2)) : uint32_t(16 + (7 << 2));
    s.eg_sh_rs = kEgRateShift[slot_rs + s.ksr];
    s.eg_sel_rs = kEgRateSelect[slot_rs + s.ksr];
    const uint32_t slot_dp = 16 + (13 << 2);
    s.eg_sh_dp = kEgRateShift[slot_dp + s.ksr];
    s.eg_sel_dp = kEgRateSelect[slot_dp + s.ksr];
}

void YM2413::set_mul(int slot, uint8_t v) {
    const int ch = slot / 2;
    const int s = slot & 1;
    auto& sl = ch_[ch].slot[s];
    sl.mul = kMulTab[v & 0x0f];
    sl.ksr_m = (v & 0x10) ? 0 : 2;
    sl.eg_type = uint8_t(v & 0x20);
    sl.vib = uint8_t(v & 0x40);
    sl.am_mask = (v & 0x80) ? 0xffffffffu : 0;
    calc_fcslot(ch, s);
}

void YM2413::set_ksl_tl(int chan, uint8_t v) {
    auto& s = ch_[chan].slot[kSlot1];
    s.ksl = kKslShift[v >> 6];
    s.tl = uint32_t(v & 0x3f) << (kEnvBits - 2 - 7);
    s.tll = int32_t(s.tl) + int32_t(ch_[chan].ksl_base >> s.ksl);
}

void YM2413::set_ksl_wave_fb(int chan, uint8_t v) {
    auto& s1 = ch_[chan].slot[kSlot1];
    auto& s2 = ch_[chan].slot[kSlot2];
    s1.wavetable = uint32_t((v & 0x08) >> 3) * kSinLen;
    s1.fb_shift = (v & 7) ? uint8_t((v & 7) + 8) : 0;
    s2.ksl = kKslShift[v >> 6];
    s2.tll = int32_t(s2.tl) + int32_t(ch_[chan].ksl_base >> s2.ksl);
    s2.wavetable = uint32_t((v & 0x10) >> 4) * kSinLen;
}

void YM2413::set_ar_dr(int slot, uint8_t v) {
    const int ch = slot / 2;
    const int s = slot & 1;
    auto& sl = ch_[ch].slot[s];
    sl.ar = (v >> 4) ? uint32_t(16 + ((v >> 4) << 2)) : 0;
    if ((sl.ar + sl.ksr) < (16 + 62)) {
        sl.eg_sh_ar = kEgRateShift[sl.ar + sl.ksr];
        sl.eg_sel_ar = kEgRateSelect[sl.ar + sl.ksr];
    } else {
        sl.eg_sh_ar = 0;
        sl.eg_sel_ar = uint8_t(13 * kRateSteps);
    }
    sl.dr = (v & 0x0f) ? uint32_t(16 + ((v & 0x0f) << 2)) : 0;
    sl.eg_sh_dr = kEgRateShift[sl.dr + sl.ksr];
    sl.eg_sel_dr = kEgRateSelect[sl.dr + sl.ksr];
}

void YM2413::set_sl_rr(int slot, uint8_t v) {
    const int ch = slot / 2;
    const int s = slot & 1;
    auto& sl = ch_[ch].slot[s];
    sl.sl = kSlTab[v >> 4];
    sl.rr = (v & 0x0f) ? uint32_t(16 + ((v & 0x0f) << 2)) : 0;
    sl.eg_sh_rr = kEgRateShift[sl.rr + sl.ksr];
    sl.eg_sel_rr = kEgRateSelect[sl.rr + sl.ksr];
}

void YM2413::load_instrument(int chan, int /*slot*/, const uint8_t* inst) {
    set_mul(chan * 2, inst[0]);
    set_mul(chan * 2 + 1, inst[1]);
    set_ksl_tl(chan, inst[2]);
    set_ksl_wave_fb(chan, inst[3]);
    set_ar_dr(chan * 2, inst[4]);
    set_ar_dr(chan * 2 + 1, inst[5]);
    set_sl_rr(chan * 2, inst[6]);
    set_sl_rr(chan * 2 + 1, inst[7]);
}

void YM2413::key_on(int slot, int chan, uint32_t key_set) {
    auto& s = ch_[chan].slot[slot];
    if (s.key == 0) s.state = kEgDmp;
    s.key |= key_set;
}

void YM2413::key_off(int slot, int chan, uint32_t key_clr) {
    auto& s = ch_[chan].slot[slot];
    if (s.key != 0) {
        s.key &= key_clr;
        if (s.key == 0 && s.state > kEgRel) s.state = kEgRel;
    }
}

void YM2413::update_instrument_zero(uint8_t r) {
    const uint8_t* inst = inst_tab_[0].data();
    int chan_max = (rhythm_ & 0x20) ? 6 : 9;
    for (int chan = 0; chan < chan_max; ++chan) {
        if ((instvol_r_[chan] & 0xf0) != 0) continue;
        switch (r) {
            case 0:
                set_mul(chan * 2, inst[0]);
                break;
            case 1:
                set_mul(chan * 2 + 1, inst[1]);
                break;
            case 2:
                set_ksl_tl(chan, inst[2]);
                break;
            case 3:
                set_ksl_wave_fb(chan, inst[3]);
                break;
            case 4:
                set_ar_dr(chan * 2, inst[4]);
                break;
            case 5:
                set_ar_dr(chan * 2 + 1, inst[5]);
                break;
            case 6:
                set_sl_rr(chan * 2, inst[6]);
                break;
            case 7:
                set_sl_rr(chan * 2 + 1, inst[7]);
                break;
            default:
                break;
        }
    }
}

void YM2413::write_reg(uint8_t reg, uint8_t data) { write_int(reg, data); }

void YM2413::write_int(uint8_t reg, uint8_t value) {
    switch (reg & 0xf0) {
        case 0x00: {
            const uint8_t r = reg & 0x0f;
            if (r <= 7) {
                inst_tab_[0][r] = value;
                update_instrument_zero(r);
            } else if (r == 0x0e) {
                // Rhythm control
                const uint8_t old = rhythm_;
                rhythm_ = value;
                if ((value & 0x20) != 0) {
                    // Load drum instruments
                    load_instrument(6, 12, inst_tab_[16].data());
                    load_instrument(7, 14, inst_tab_[17].data());
                    load_instrument(8, 16, inst_tab_[18].data());
                    if (value & 0x10) {
                        key_on(kSlot1, 6, 2);
                        key_on(kSlot2, 6, 2);
                    } else {
                        key_off(kSlot1, 6, ~2u);
                        key_off(kSlot2, 6, ~2u);
                    }
                    if (value & 0x01) {
                        key_on(kSlot1, 7, 2);
                    } else {
                        key_off(kSlot1, 7, ~2u);
                    }
                    if (value & 0x08) {
                        key_on(kSlot2, 7, 2);
                    } else {
                        key_off(kSlot2, 7, ~2u);
                    }
                    if (value & 0x04) {
                        key_on(kSlot1, 8, 2);
                    } else {
                        key_off(kSlot1, 8, ~2u);
                    }
                    if (value & 0x02) {
                        key_on(kSlot2, 8, 2);
                    } else {
                        key_off(kSlot2, 8, ~2u);
                    }
                } else if ((old & 0x20) != 0) {
                    key_off(kSlot1, 6, ~2u);
                    key_off(kSlot2, 6, ~2u);
                    key_off(kSlot1, 7, ~2u);
                    key_off(kSlot2, 7, ~2u);
                    key_off(kSlot1, 8, ~2u);
                    key_off(kSlot2, 8, ~2u);
                }
            }
            break;
        }
        case 0x10: {
            int chan = reg & 0x0f;
            if (chan >= 9) chan -= 9;
            const uint32_t block_fnum =
                (ch_[chan].block_fnum & 0x1f00) | value;
            ch_[chan].block_fnum = block_fnum;
            ch_[chan].ksl_base = ksl_tab_[block_fnum >> 6];
            ch_[chan].kcode = uint8_t((block_fnum & 0x1c00) >> 9) |
                              uint8_t((block_fnum >> 8) & 1);
            const uint32_t block = (block_fnum & 0x1c00) >> 10;
            ch_[chan].fc = fn_tab_[block_fnum & 0x03ff] >> (7 - int(block));
            ch_[chan].slot[kSlot1].tll =
                int32_t(ch_[chan].slot[kSlot1].tl) +
                int32_t(ch_[chan].ksl_base >> ch_[chan].slot[kSlot1].ksl);
            ch_[chan].slot[kSlot2].tll =
                int32_t(ch_[chan].slot[kSlot2].tl) +
                int32_t(ch_[chan].ksl_base >> ch_[chan].slot[kSlot2].ksl);
            calc_fcslot(chan, kSlot1);
            calc_fcslot(chan, kSlot2);
            break;
        }
        case 0x20: {
            int chan = reg & 0x0f;
            if (chan >= 9) chan -= 9;
            const uint32_t block_fnum =
                ((value & 0x1f) << 8) | (ch_[chan].block_fnum & 0xff);
            const uint32_t block = (value & 0x1c) >> 2;
            ch_[chan].block_fnum = block_fnum;
            ch_[chan].ksl_base = ksl_tab_[block_fnum >> 6];
            ch_[chan].kcode =
                uint8_t((value & 0x1c) >> 2) | uint8_t((value >> 1) & 1);
            ch_[chan].fc = fn_tab_[block_fnum & 0x03ff] >> (7 - int(block));
            ch_[chan].slot[kSlot1].tll =
                int32_t(ch_[chan].slot[kSlot1].tl) +
                int32_t(ch_[chan].ksl_base >> ch_[chan].slot[kSlot1].ksl);
            ch_[chan].slot[kSlot2].tll =
                int32_t(ch_[chan].slot[kSlot2].tl) +
                int32_t(ch_[chan].ksl_base >> ch_[chan].slot[kSlot2].ksl);
            calc_fcslot(chan, kSlot1);
            calc_fcslot(chan, kSlot2);
            if (value & 0x20) {
                key_on(kSlot1, chan, 1);
                key_on(kSlot2, chan, 1);
            } else {
                key_off(kSlot1, chan, ~1u);
                key_off(kSlot2, chan, ~1u);
            }
            ch_[chan].sus = uint8_t(value & 0x10);
            break;
        }
        case 0x30: {
            int chan = reg & 0x0f;
            if (chan >= 9) chan -= 9;
            const uint8_t old = instvol_r_[chan];
            instvol_r_[chan] = value;
            ch_[chan].slot[kSlot2].tl =
                uint32_t((value & 0x0f) << 2) << (kEnvBits - 2 - 7);
            ch_[chan].slot[kSlot2].tll =
                int32_t(ch_[chan].slot[kSlot2].tl) +
                int32_t(ch_[chan].ksl_base >> ch_[chan].slot[kSlot2].ksl);
            if (chan >= 6 && (rhythm_ & 0x20)) {
                if (chan >= 7) {
                    ch_[chan].slot[kSlot1].tl =
                        uint32_t((value >> 4) << 2) << (kEnvBits - 2 - 7);
                    ch_[chan].slot[kSlot1].tll =
                        int32_t(ch_[chan].slot[kSlot1].tl) +
                        int32_t(ch_[chan].ksl_base >> ch_[chan].slot[kSlot1].ksl);
                }
            } else {
                if ((old & 0xf0) != (value & 0xf0)) {
                    const uint8_t* inst = inst_tab_[value >> 4].data();
                    load_instrument(chan, chan * 2, inst);
                }
            }
            break;
        }
        default:
            break;
    }
}

int32_t YM2413::update() {
    output_[0] = 0;
    output_[1] = 0;
    advance_lfo();
    for (int j = 0; j < 6; ++j) chan_calc(j);
    if ((rhythm_ & 0x20) == 0) {
        for (int j = 6; j < 9; ++j) chan_calc(j);
    } else {
        rhythm_calc(noise_rng_ & 1);
    }
    int32_t mixed = output_[0] + output_[1];
    mixed = std::clamp(mixed, int32_t(-32768), int32_t(32767));
    advance();
    return int32_t(mixed * amplitude_);
}

}  // namespace dsp
