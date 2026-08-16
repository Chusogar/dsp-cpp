#include "sound/ym3812.h"

#include <algorithm>
#include <cmath>

namespace dsp {
namespace {

constexpr int kRateSteps = 8;
constexpr int kSinLen = 1 << 10;
constexpr int kSinMask = kSinLen - 1;
constexpr int kTlResLen = 256;
constexpr int kTlTabLen = 12 * 2 * kTlResLen;
constexpr int kEnvLen = 1 << 10;
constexpr double kEnvStep = 128.0 / kEnvLen;
constexpr int kLfoAmTabElements = 210;
constexpr int kOplTypeWavesel = 0x01;

constexpr int kSlotArray[32] = {
    0,  2,  4,  1,  3,  5,  -1, -1, 6,  8,  10, 7,  9,  11, -1, -1,
    12, 14, 16, 13, 15, 17, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
};

constexpr double kDv = 0.1875 / 2.0;
constexpr double kBaseKslTab[8 * 16] = {
    // OCT 0
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    // OCT 1
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0.750, 1.125, 1.5, 1.875, 2.25, 2.625, 3,
    // OCT 2
    0, 0, 0, 0, 0, 1.125, 1.875, 2.625, 3, 3.750, 4.125, 4.500, 4.875, 5.250, 5.625, 6,
    // OCT 3
    0, 0, 0, 1.875, 3, 4.125, 4.875, 5.625, 6, 6.750, 7.125, 7.500, 7.875, 8.250, 8.625, 9,
    // OCT 4
    0, 0, 3, 4.875, 6, 7.125, 7.875, 8.625, 9, 9.750, 10.125, 10.500, 10.875, 11.250, 11.625, 12,
    // OCT 5
    0, 3, 6, 7.875, 9, 10.125, 10.875, 11.625, 12, 12.750, 13.125, 13.500, 13.875, 14.250, 14.625,
    15.000,
    // OCT 6
    0, 6, 9, 10.875, 12, 13.125, 13.875, 14.625, 15, 15.750, 16.125, 16.500, 16.875, 17.250,
    17.625, 18.000,
    // OCT 7
    0, 9, 12, 13.875, 15, 16.125, 16.875, 17.625, 18, 18.750, 19.125, 19.500, 19.875, 20.250,
    20.625, 21.000,
};

constexpr uint8_t kEgInc[15 * kRateSteps] = {
    0, 1, 0, 1, 0, 1, 0, 1,  // rates 00..12 0
    0, 1, 0, 1, 1, 1, 0, 1,  // rates 00..12 1
    0, 1, 1, 1, 0, 1, 1, 1,  // rates 00..12 2
    0, 1, 1, 1, 1, 1, 1, 1,  // rates 00..12 3
    1, 1, 1, 1, 1, 1, 1, 1,  // rate 13 0
    1, 1, 1, 2, 1, 1, 1, 2,  // rate 13 1
    1, 2, 1, 2, 1, 2, 1, 2,  // rate 13 2
    1, 2, 2, 2, 1, 2, 2, 2,  // rate 13 3
    2, 2, 2, 2, 2, 2, 2, 2,  // rate 14 0
    2, 2, 2, 4, 2, 2, 2, 4,  // rate 14 1
    2, 4, 2, 4, 2, 4, 2, 4,  // rate 14 2
    2, 4, 4, 4, 2, 4, 4, 4,  // rate 14 3
    4, 4, 4, 4, 4, 4, 4, 4,  // rate 15
    8, 8, 8, 8, 8, 8, 8, 8,  // rate 15 for attack
    0, 0, 0, 0, 0, 0, 0, 0,  // infinity rates
};

constexpr uint8_t kEgRateSelect[16 + 64 + 16] = {
    14 * kRateSteps, 14 * kRateSteps, 14 * kRateSteps, 14 * kRateSteps, 14 * kRateSteps,
    14 * kRateSteps, 14 * kRateSteps, 14 * kRateSteps, 14 * kRateSteps, 14 * kRateSteps,
    14 * kRateSteps, 14 * kRateSteps, 14 * kRateSteps, 14 * kRateSteps, 14 * kRateSteps,
    14 * kRateSteps,
    0 * kRateSteps, 1 * kRateSteps, 2 * kRateSteps, 3 * kRateSteps,
    0 * kRateSteps, 1 * kRateSteps, 2 * kRateSteps, 3 * kRateSteps,
    0 * kRateSteps, 1 * kRateSteps, 2 * kRateSteps, 3 * kRateSteps,
    0 * kRateSteps, 1 * kRateSteps, 2 * kRateSteps, 3 * kRateSteps,
    0 * kRateSteps, 1 * kRateSteps, 2 * kRateSteps, 3 * kRateSteps,
    0 * kRateSteps, 1 * kRateSteps, 2 * kRateSteps, 3 * kRateSteps,
    0 * kRateSteps, 1 * kRateSteps, 2 * kRateSteps, 3 * kRateSteps,
    0 * kRateSteps, 1 * kRateSteps, 2 * kRateSteps, 3 * kRateSteps,
    0 * kRateSteps, 1 * kRateSteps, 2 * kRateSteps, 3 * kRateSteps,
    0 * kRateSteps, 1 * kRateSteps, 2 * kRateSteps, 3 * kRateSteps,
    0 * kRateSteps, 1 * kRateSteps, 2 * kRateSteps, 3 * kRateSteps,
    0 * kRateSteps, 1 * kRateSteps, 2 * kRateSteps, 3 * kRateSteps,
    0 * kRateSteps, 1 * kRateSteps, 2 * kRateSteps, 3 * kRateSteps,
    4 * kRateSteps, 5 * kRateSteps, 6 * kRateSteps, 7 * kRateSteps,
    8 * kRateSteps, 9 * kRateSteps, 10 * kRateSteps, 11 * kRateSteps,
    12 * kRateSteps, 12 * kRateSteps, 12 * kRateSteps, 12 * kRateSteps,
    12 * kRateSteps, 12 * kRateSteps, 12 * kRateSteps, 12 * kRateSteps,
    12 * kRateSteps, 12 * kRateSteps, 12 * kRateSteps, 12 * kRateSteps,
    12 * kRateSteps, 12 * kRateSteps, 12 * kRateSteps, 12 * kRateSteps,
    12 * kRateSteps, 12 * kRateSteps, 12 * kRateSteps, 12 * kRateSteps,
};

constexpr uint8_t kEgRateShift[16 + 64 + 16] = {
    0,  0,  0,  0,  0,  0,  0,  0,  0, 0, 0, 0, 0, 0, 0, 0,
    12, 12, 12, 12, 11, 11, 11, 11, 10, 10, 10, 10, 9, 9, 9, 9,
    8,  8,  8,  8,  7,  7,  7,  7,  6, 6, 6, 6, 5, 5, 5, 5,
    4,  4,  4,  4,  3,  3,  3,  3,  2, 2, 2, 2, 1, 1, 1, 1,
    0,  0,  0,  0,  0,  0,  0,  0,  0, 0, 0, 0, 0, 0, 0, 0,
    0,  0,  0,  0,  0,  0,  0,  0,  0, 0, 0, 0, 0, 0, 0, 0,
};

constexpr int kMl = 2;
constexpr uint8_t kMulTab[16] = {
    1,          1 * kMl,  2 * kMl,  3 * kMl,  4 * kMl,  5 * kMl,  6 * kMl,  7 * kMl,
    8 * kMl,    9 * kMl,  10 * kMl, 10 * kMl, 12 * kMl, 12 * kMl, 15 * kMl, 15 * kMl,
};

constexpr uint8_t kLfoAmTable[kLfoAmTabElements] = {
    0, 0, 0, 0, 0, 0, 0,
    1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 6, 6, 6, 6,
    7, 7, 7, 7, 8, 8, 8, 8, 9, 9, 9, 9, 10, 10, 10, 10, 11, 11, 11, 11,
    12, 12, 12, 12, 13, 13, 13, 13, 14, 14, 14, 14, 15, 15, 15, 15,
    16, 16, 16, 16, 17, 17, 17, 17, 18, 18, 18, 18, 19, 19, 19, 19,
    20, 20, 20, 20, 21, 21, 21, 21, 22, 22, 22, 22, 23, 23, 23, 23,
    24, 24, 24, 24, 25, 25, 25, 25, 26, 26, 26,
    25, 25, 25, 25, 24, 24, 24, 24, 23, 23, 23, 23, 22, 22, 22, 22,
    21, 21, 21, 21, 20, 20, 20, 20, 19, 19, 19, 19, 18, 18, 18, 18,
    17, 17, 17, 17, 16, 16, 16, 16, 15, 15, 15, 15, 14, 14, 14, 14,
    13, 13, 13, 13, 12, 12, 12, 12, 11, 11, 11, 11, 10, 10, 10, 10,
    9, 9, 9, 9, 8, 8, 8, 8, 7, 7, 7, 7, 6, 6, 6, 6, 5, 5, 5, 5,
    4, 4, 4, 4, 3, 3, 3, 3, 2, 2, 2, 2, 1, 1, 1, 1,
};

constexpr int kLfoPmTable[8 * 8 * 2] = {
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    1, 0, 0, 0, -1, 0, 0, 0,
    1, 0, 0, 0, -1, 0, 0, 0,
    2, 1, 0, -1, -2, -1, 0, 1,
    1, 0, 0, 0, -1, 0, 0, 0,
    3, 1, 0, -1, -3, -1, 0, 1,
    2, 1, 0, -1, -2, -1, 0, 1,
    4, 2, 0, -2, -4, -2, 0, 2,
    2, 1, 0, -1, -2, -1, 0, 1,
    5, 2, 0, -2, -5, -2, 0, 2,
    3, 1, 0, -1, -3, -1, 0, 1,
    6, 3, 0, -3, -6, -3, 0, 3,
    3, 1, 0, -1, -3, -1, 0, 1,
    7, 3, 0, -3, -7, -3, 0, 3,
};

struct Tables {
    std::array<int32_t, kTlTabLen> tl{};
    std::array<uint32_t, kSinLen * 4> sin{};
    std::array<double, 8 * 16> ksl{};

    Tables() {
        for (size_t i = 0; i < ksl.size(); i++) ksl[i] = kBaseKslTab[i] / kDv;
        for (int x = 0; x < kTlResLen; x++) {
            double m = double(1 << 16) / std::pow(2.0, (x + 1) * (kEnvStep / 4.0) / 8.0);
            m = std::floor(m);
            int n = int(m);
            n >>= 4;
            n = (n & 1) != 0 ? (n >> 1) + 1 : n >> 1;
            n <<= 1;
            tl[size_t(x * 2)] = n;
            tl[size_t(x * 2 + 1)] = -n;
            for (int i = 1; i < 12; i++) {
                tl[size_t(x * 2 + i * 2 * kTlResLen)] = n >> i;
                tl[size_t(x * 2 + 1 + i * 2 * kTlResLen)] = -(n >> i);
            }
        }
        for (int i = 0; i < kSinLen; i++) {
            const double m = std::sin(((i * 2) + 1) * M_PI / kSinLen);
            double o = 8.0 * std::log(1.0 / (m > 0.0 ? m : -m)) / std::log(2.0);
            o /= kEnvStep / 4.0;
            int n = int(2.0 * o);
            n = (n & 1) != 0 ? (n >> 1) + 1 : n >> 1;
            sin[size_t(i)] = uint32_t(m >= 0.0 ? n * 2 : n * 2 + 1);
        }
        for (int i = 0; i < kSinLen; i++) {
            // waveform 1: positive half of the sinus only
            sin[size_t(kSinLen + i)] =
                (i & (1 << (10 - 1))) != 0 ? uint32_t(kTlTabLen) : sin[size_t(i)];
            // waveform 2: abs(sin)
            sin[size_t(2 * kSinLen + i)] = sin[size_t(i & (kSinMask >> 1))];
            // waveform 3: first quarter of abs(sin)
            sin[size_t(3 * kSinLen + i)] = (i & (1 << (10 - 2))) != 0
                                               ? uint32_t(kTlTabLen)
                                               : sin[size_t(i & (kSinMask >> 2))];
        }
    }
};

const Tables& tables() {
    static const Tables instance;
    return instance;
}

// Pascal's sshr(): shifts the magnitude, keeping the sign.
int32_t sshr(int32_t value, int shift) {
    return value < 0 ? -(int32_t(uint32_t(-value) >> shift)) : value >> shift;
}

int32_t op_calc(uint32_t phase, uint32_t env, int32_t pm, uint32_t wave_tab) {
    const auto& tab = tables();
    const int32_t tmp = int32_t((phase & ~uint32_t((1u << 16) - 1)) + uint32_t(pm << 16));
    const uint32_t p = (env << 4) + tab.sin[wave_tab + uint32_t(sshr(tmp, 16) & kSinMask)];
    if (p >= uint32_t(kTlTabLen)) return 0;
    return tab.tl[size_t(p)];
}

int32_t op_calc1(uint32_t phase, uint32_t env, int32_t pm, uint32_t wave_tab) {
    const auto& tab = tables();
    const int32_t tmp = int32_t((phase & ~uint32_t((1u << 16) - 1)) + uint32_t(pm));
    const uint32_t p = (env << 4) + tab.sin[wave_tab + uint32_t(sshr(tmp, 16) & kSinMask)];
    if (p >= uint32_t(kTlTabLen)) return 0;
    return tab.tl[size_t(p)];
}

uint32_t sl_tab(int index) {
    static const std::array<uint32_t, 16> table = [] {
        std::array<uint32_t, 16> t{};
        for (int i = 0; i < 15; i++) t[size_t(i)] = uint32_t(i * (2.0 / kEnvStep));
        t[15] = uint32_t(31 * (2.0 / kEnvStep));
        return t;
    }();
    return table[size_t(index)];
}

}  // namespace

YM3812::YM3812(uint32_t clock, Type type, float amplitude)
    : clock_(clock), amplitude_(amplitude) {
    tables();
    type_flags_ = type == kYM3812 ? uint8_t(kOplTypeWavesel) : 0;
    initialize();
    reset();
}

void YM3812::initialize() {
    freqbase_ = (double(clock_) / 72.0) / kSampleRate;
    for (int i = 0; i < 1024; i++) {
        fn_tab_[size_t(i)] = uint32_t(i * 64 * freqbase_ * (1 << (kFreqSh - 10)));
    }
    lfo_am_inc_ = uint32_t((1.0 / 64.0) * (1 << kLfoSh) * freqbase_);
    lfo_pm_inc_ = uint32_t((1.0 / 1024.0) * (1 << kLfoSh) * freqbase_);
    noise_f_ = uint32_t((1 << kFreqSh) * freqbase_);
    eg_timer_add_ = double(1 << kEgSh) * freqbase_;
    eg_timer_overflow_ = double(1 << kEgSh);
}

void YM3812::reset() {
    eg_timer_ = 0.0;
    eg_cnt_ = 0;
    noise_rng_ = 1;
    mode_ = 0;
    status_reset(0x7f);

    write_reg(0x01, 0);  // wavesel disable
    write_reg(0x02, 0);  // timer 1
    write_reg(0x03, 0);  // timer 2
    write_reg(0x04, 0);  // IRQ mask clear
    for (int reg = 0xff; reg >= 0x20; reg--) write_reg(reg, 0);

    for (auto& ch : channels_) {
        for (auto& slot : ch.slot) {
            slot.wavetable = 0;
            slot.state = kEgOff;
            slot.volume = kMaxAttIndex;
        }
    }
}

void YM3812::status_set(int flag) {
    status_ = uint8_t(status_ | flag);
    if ((status_ & 0x80) != 0) return;
    if ((status_ & status_mask_) == 0) return;
    status_ = uint8_t(status_ | 0x80);
    if (irq_handler_) irq_handler_(true);
}

void YM3812::status_reset(int flag) {
    status_ = uint8_t(status_ & ~flag);
    if ((status_ & 0x80) == 0) return;
    if ((status_ & status_mask_) != 0) return;
    status_ = uint8_t(status_ & 0x7f);
    if (irq_handler_) irq_handler_(false);
}

void YM3812::status_mask_set(int flag) {
    status_mask_ = uint8_t(flag);
    status_set(0);
    status_reset(0);
}

void YM3812::calc_fcslot(Channel& ch, Slot& slot) {
    slot.incr = ch.fc * slot.mul;
    const uint8_t ksr = uint8_t(ch.kcode >> slot.ksr_m);
    if (slot.ksr == ksr) return;
    slot.ksr = ksr;
    if (slot.ar + slot.ksr < 16 + 62) {
        slot.eg_sh_ar = kEgRateShift[slot.ar + slot.ksr];
        slot.eg_sel_ar = kEgRateSelect[slot.ar + slot.ksr];
    } else {
        slot.eg_sh_ar = 0;
        slot.eg_sel_ar = 13 * kRateSteps;
    }
    slot.eg_sh_dr = kEgRateShift[slot.dr + slot.ksr];
    slot.eg_sel_dr = kEgRateSelect[slot.dr + slot.ksr];
    slot.eg_sh_rr = kEgRateShift[slot.rr + slot.ksr];
    slot.eg_sel_rr = kEgRateSelect[slot.rr + slot.ksr];
}

void YM3812::set_mul(int slot_v, int v) {
    Channel& ch = channels_[size_t(slot_v >> 1)];
    Slot& slot = ch.slot[size_t(slot_v & 1)];
    slot.mul = kMulTab[v & 0x0f];
    slot.ksr_m = (v & 0x10) != 0 ? 0 : 2;
    slot.eg_type = uint8_t(v & 0x20);
    slot.vib = uint8_t(v & 0x40);
    slot.am_mask = (v & 0x80) != 0 ? 0xffffffffu : 0;
    calc_fcslot(ch, slot);
}

void YM3812::set_ksl_tl(int slot_v, int v) {
    Channel& ch = channels_[size_t(slot_v >> 1)];
    Slot& slot = ch.slot[size_t(slot_v & 1)];
    const int ksl = v >> 6;
    slot.ksl = ksl != 0 ? uint8_t(3 - ksl) : 31;
    slot.tl = uint32_t((v & 0x3f) << (kEnvBits - 1 - 7));
    slot.tll = int32_t(slot.tl + (ch.ksl_base >> slot.ksl));
}

void YM3812::set_ar_dr(int slot_v, int v) {
    Slot& slot = channels_[size_t(slot_v >> 1)].slot[size_t(slot_v & 1)];
    slot.ar = (v >> 4) != 0 ? uint32_t(16 + ((v >> 4) << 2)) : 0;
    if (slot.ar + slot.ksr < 16 + 62) {
        slot.eg_sh_ar = kEgRateShift[slot.ar + slot.ksr];
        slot.eg_sel_ar = kEgRateSelect[slot.ar + slot.ksr];
    } else {
        slot.eg_sh_ar = 0;
        slot.eg_sel_ar = 13 * kRateSteps;
    }
    slot.dr = (v & 0x0f) != 0 ? uint32_t(16 + ((v & 0x0f) << 2)) : 0;
    slot.eg_sh_dr = kEgRateShift[slot.dr + slot.ksr];
    slot.eg_sel_dr = kEgRateSelect[slot.dr + slot.ksr];
}

void YM3812::set_sl_rr(int slot_v, int v) {
    Slot& slot = channels_[size_t(slot_v >> 1)].slot[size_t(slot_v & 1)];
    slot.sl = sl_tab(v >> 4);
    slot.rr = (v & 0x0f) != 0 ? uint32_t(16 + ((v & 0x0f) << 2)) : 0;
    slot.eg_sh_rr = kEgRateShift[slot.rr + slot.ksr];
    slot.eg_sel_rr = kEgRateSelect[slot.rr + slot.ksr];
}

void YM3812::key_on(Slot& slot, uint32_t key_set) {
    if (slot.key == 0) {
        slot.cnt = 0;
        slot.state = kEgAtt;
    }
    slot.key |= key_set;
}

void YM3812::key_off(Slot& slot, uint32_t key_clr) {
    if (slot.key == 0) return;
    slot.key &= ~key_clr;
    if (slot.key == 0 && slot.state > kEgRel) slot.state = kEgRel;
}

void YM3812::csm_key_control(Channel& ch) {
    key_on(ch.slot[kSlot1], 4);
    key_on(ch.slot[kSlot2], 4);
    key_off(ch.slot[kSlot1], 4);
    key_off(ch.slot[kSlot2], 4);
}

void YM3812::timer_over(int timer) {
    if (timer != 0) {
        status_set(0x20);
    } else {
        status_set(0x40);
        if ((mode_ & 0x80) != 0) {
            for (auto& ch : channels_) csm_key_control(ch);
        }
    }
    timer_count_[size_t(timer)] = double(timer_period_[size_t(timer)]);
}

void YM3812::run_timers() {
    for (int timer = 0; timer < 2; timer++) {
        if (timer_enabled_[size_t(timer)] == 0) continue;
        timer_count_[size_t(timer)] -= freqbase_;
        if (timer_count_[size_t(timer)] <= 0.0) timer_over(timer);
    }
}

void YM3812::write_reg(int reg, int value) {
    reg &= 0xff;
    value &= 0xff;
    switch (reg & 0xe0) {
        case 0x00:
            switch (reg & 0x1f) {
                case 0x01:
                    if ((type_flags_ & kOplTypeWavesel) != 0) wavesel_ = uint8_t(value & 0x20);
                    break;
                case 0x02:
                    timer_period_[0] = uint32_t((256 - value) * 4);
                    break;
                case 0x03:
                    timer_period_[1] = uint32_t((256 - value) * 16);
                    break;
                case 0x04:
                    if ((value & 0x80) != 0) {
                        status_reset(0x7f - 0x08);
                    } else {
                        const uint8_t st1 = uint8_t(value & 1);
                        const uint8_t st2 = uint8_t((value >> 1) & 1);
                        status_reset(value & (0x78 - 0x08));
                        status_mask_set(~value & 0x78);
                        if (timer_enabled_[1] != st2) {
                            timer_enabled_[1] = st2;
                            timer_count_[1] = st2 != 0 ? double(timer_period_[1]) : 0.0;
                        }
                        if (timer_enabled_[0] != st1) {
                            timer_enabled_[0] = st1;
                            timer_count_[0] = st1 != 0 ? double(timer_period_[0]) : 0.0;
                        }
                    }
                    break;
                case 0x08:
                    mode_ = uint8_t(value);
                    break;
                default:
                    break;
            }
            break;
        case 0x20: {
            const int slot = kSlotArray[reg & 0x1f];
            if (slot < 0) return;
            set_mul(slot, value);
            break;
        }
        case 0x40: {
            const int slot = kSlotArray[reg & 0x1f];
            if (slot < 0) return;
            set_ksl_tl(slot, value);
            break;
        }
        case 0x60: {
            const int slot = kSlotArray[reg & 0x1f];
            if (slot < 0) return;
            set_ar_dr(slot, value);
            break;
        }
        case 0x80: {
            const int slot = kSlotArray[reg & 0x1f];
            if (slot < 0) return;
            set_sl_rr(slot, value);
            break;
        }
        case 0xa0: {
            if (reg == 0xbd) {
                lfo_am_depth_ = uint8_t(value & 0x80);
                lfo_pm_depth_range_ = (value & 0x40) != 0 ? 8 : 0;
                rhythm_ = uint8_t(value & 0x3f);
                if ((rhythm_ & 0x20) != 0) {
                    if ((value & 0x10) != 0) {
                        key_on(channels_[6].slot[kSlot1], 2);
                        key_on(channels_[6].slot[kSlot2], 2);
                    } else {
                        key_off(channels_[6].slot[kSlot1], 2);
                        key_off(channels_[6].slot[kSlot2], 2);
                    }
                    if ((value & 0x01) != 0) key_on(channels_[7].slot[kSlot1], 2);
                    else key_off(channels_[7].slot[kSlot1], 2);
                    if ((value & 0x08) != 0) key_on(channels_[7].slot[kSlot2], 2);
                    else key_off(channels_[7].slot[kSlot2], 2);
                    if ((value & 0x04) != 0) key_on(channels_[8].slot[kSlot1], 2);
                    else key_off(channels_[8].slot[kSlot1], 2);
                    if ((value & 0x02) != 0) key_on(channels_[8].slot[kSlot2], 2);
                    else key_off(channels_[8].slot[kSlot2], 2);
                } else {
                    key_off(channels_[6].slot[kSlot1], 2);
                    key_off(channels_[6].slot[kSlot2], 2);
                    key_off(channels_[7].slot[kSlot1], 2);
                    key_off(channels_[7].slot[kSlot2], 2);
                    key_off(channels_[8].slot[kSlot1], 2);
                    key_off(channels_[8].slot[kSlot2], 2);
                }
                return;
            }
            if ((reg & 0x0f) > 8) return;
            Channel& ch = channels_[size_t(reg & 0x0f)];
            uint32_t block_fnum = 0;
            if ((reg & 0x10) == 0) {
                block_fnum = (ch.block_fnum & 0x1f00) | uint32_t(value);
            } else {
                block_fnum = (uint32_t(value & 0x1f) << 8) | (ch.block_fnum & 0xff);
                if ((value & 0x20) != 0) {
                    key_on(ch.slot[kSlot1], 1);
                    key_on(ch.slot[kSlot2], 1);
                } else {
                    key_off(ch.slot[kSlot1], 1);
                    key_off(ch.slot[kSlot2], 1);
                }
            }
            if (ch.block_fnum == block_fnum) return;
            const uint8_t block = uint8_t(block_fnum >> 10);
            ch.block_fnum = block_fnum;
            ch.ksl_base = uint32_t(tables().ksl[size_t(block_fnum >> 6)]);
            ch.fc = fn_tab_[size_t(block_fnum & 0x03ff)] >> (7 - block);
            ch.kcode = uint8_t((ch.block_fnum & 0x1c00) >> 9);
            if ((mode_ & 0x40) != 0) {
                ch.kcode = uint8_t(ch.kcode | ((ch.block_fnum & 0x100) >> 8));
            } else {
                ch.kcode = uint8_t(ch.kcode | ((ch.block_fnum & 0x200) >> 9));
            }
            ch.slot[kSlot1].tll = int32_t(ch.slot[kSlot1].tl + (ch.ksl_base >> ch.slot[kSlot1].ksl));
            ch.slot[kSlot2].tll = int32_t(ch.slot[kSlot2].tl + (ch.ksl_base >> ch.slot[kSlot2].ksl));
            calc_fcslot(ch, ch.slot[kSlot1]);
            calc_fcslot(ch, ch.slot[kSlot2]);
            break;
        }
        case 0xc0: {
            if ((reg & 0x0f) > 8) return;
            Channel& ch = channels_[size_t(reg & 0x0f)];
            const int feedback = (value >> 1) & 7;
            ch.slot[kSlot1].fb = feedback != 0 ? uint8_t(feedback + 7) : 0;
            ch.slot[kSlot1].con = uint8_t(value & 1);
            ch.slot[kSlot1].connect_output = ch.slot[kSlot1].con != 0;
            break;
        }
        case 0xe0: {
            if (wavesel_ == 0) return;
            const int slot = kSlotArray[reg & 0x1f];
            if (slot < 0) return;
            channels_[size_t(slot >> 1)].slot[size_t(slot & 1)].wavetable =
                uint32_t((value & 0x03) * kSinLen);
            break;
        }
        default:
            break;
    }
}

void YM3812::advance_lfo() {
    lfo_am_cnt_ += lfo_am_inc_;
    if (lfo_am_cnt_ >= uint32_t(kLfoAmTabElements) << kLfoSh) {
        lfo_am_cnt_ -= uint32_t(kLfoAmTabElements) << kLfoSh;
    }
    const uint8_t value = kLfoAmTable[lfo_am_cnt_ >> kLfoSh];
    lfo_am_ = lfo_am_depth_ != 0 ? value : uint32_t(value >> 2);
    lfo_pm_cnt_ += lfo_pm_inc_;
    lfo_pm_ = int32_t(((lfo_pm_cnt_ >> kLfoSh) & 7) | lfo_pm_depth_range_);
}

void YM3812::advance() {
    eg_timer_ += eg_timer_add_;
    while (eg_timer_ >= eg_timer_overflow_) {
        eg_timer_ -= eg_timer_overflow_;
        eg_cnt_++;
        for (int i = 0; i < 9 * 2; i++) {
            Slot& op = channels_[size_t(i >> 1)].slot[size_t(i & 1)];
            switch (op.state) {
                case kEgAtt:
                    if ((eg_cnt_ & ((1u << op.eg_sh_ar) - 1)) == 0) {
                        op.volume +=
                            (~op.volume * kEgInc[op.eg_sel_ar + ((eg_cnt_ >> op.eg_sh_ar) & 7)]) /
                            8;
                        if (op.volume <= kMinAttIndex) {
                            op.volume = kMinAttIndex;
                            op.state = kEgDec;
                        }
                    }
                    break;
                case kEgDec:
                    if ((eg_cnt_ & ((1u << op.eg_sh_dr) - 1)) == 0) {
                        op.volume += kEgInc[op.eg_sel_dr + ((eg_cnt_ >> op.eg_sh_dr) & 7)];
                        if (uint32_t(op.volume) >= op.sl) op.state = kEgSus;
                    }
                    break;
                case kEgSus:
                    if (op.eg_type == 0) {
                        // percussive mode: the release rate is applied during sustain
                        if ((eg_cnt_ & ((1u << op.eg_sh_rr) - 1)) == 0) {
                            op.volume += kEgInc[op.eg_sel_rr + ((eg_cnt_ >> op.eg_sh_rr) & 7)];
                            if (op.volume >= kMaxAttIndex) op.volume = kMaxAttIndex;
                        }
                    }
                    break;
                case kEgRel:
                    if ((eg_cnt_ & ((1u << op.eg_sh_rr) - 1)) == 0) {
                        op.volume += kEgInc[op.eg_sel_rr + ((eg_cnt_ >> op.eg_sh_rr) & 7)];
                        if (op.volume >= kMaxAttIndex) {
                            op.volume = kMaxAttIndex;
                            op.state = kEgOff;
                        }
                    }
                    break;
                default:
                    break;
            }
        }
    }

    for (int i = 0; i < 9 * 2; i++) {
        Channel& ch = channels_[size_t(i >> 1)];
        Slot& op = ch.slot[size_t(i & 1)];
        if (op.vib != 0) {
            uint32_t block_fnum = ch.block_fnum;
            const uint32_t fnum_lfo = (block_fnum & 0x0380) >> 7;
            const int offset = kLfoPmTable[size_t(lfo_pm_ + 16 * int(fnum_lfo))];
            if (offset != 0) {
                block_fnum = uint32_t(int32_t(block_fnum) + offset);
                const uint8_t block = uint8_t((block_fnum & 0x1c00) >> 10);
                op.cnt += uint32_t(sshr(int32_t(fn_tab_[size_t(block_fnum & 0x03ff)]),
                                        7 - block) *
                                   int32_t(op.mul));
            } else {
                op.cnt += op.incr;
            }
        } else {
            op.cnt += op.incr;
        }
    }

    noise_p_ += noise_f_;
    int shifts = int(noise_p_ >> kFreqSh);
    noise_p_ &= kFreqMask;
    while (shifts-- > 0) {
        if ((noise_rng_ & 1) != 0) noise_rng_ ^= 0x800302;
        noise_rng_ >>= 1;
    }
}

uint32_t YM3812::volume_calc(const Slot& slot) const {
    return uint32_t(slot.tll) + uint32_t(slot.volume) + (lfo_am_ & slot.am_mask);
}

void YM3812::calc_channel(Channel& ch) {
    phase_modulation_ = 0;
    Slot& slot1 = ch.slot[kSlot1];
    uint32_t env = volume_calc(slot1);
    int32_t out = slot1.op1_out[0] + slot1.op1_out[1];
    slot1.op1_out[0] = slot1.op1_out[1];
    if (slot1.connect_output) {
        output_ += slot1.op1_out[0];
    } else {
        phase_modulation_ += slot1.op1_out[0];
    }
    slot1.op1_out[1] = 0;
    if (env < uint32_t(kEnvQuiet)) {
        if (slot1.fb == 0) out = 0;
        slot1.op1_out[1] = op_calc1(slot1.cnt, env, out << slot1.fb, slot1.wavetable);
    }

    Slot& slot2 = ch.slot[kSlot2];
    env = volume_calc(slot2);
    if (env < uint32_t(kEnvQuiet)) {
        output_ += op_calc(slot2.cnt, env, phase_modulation_, slot2.wavetable);
    }
}

void YM3812::calc_rhythm(uint32_t noise) {
    Slot& slot7_1 = channels_[7].slot[kSlot1];
    Slot& slot7_2 = channels_[7].slot[kSlot2];
    Slot& slot8_1 = channels_[8].slot[kSlot1];
    Slot& slot8_2 = channels_[8].slot[kSlot2];

    phase_modulation_ = 0;
    Slot& bd1 = channels_[6].slot[kSlot1];
    uint32_t env = volume_calc(bd1);
    int32_t out = bd1.op1_out[0] + bd1.op1_out[1];
    bd1.op1_out[0] = bd1.op1_out[1];
    if (bd1.con == 0) phase_modulation_ = bd1.op1_out[0];
    bd1.op1_out[1] = 0;
    if (env < uint32_t(kEnvQuiet)) {
        if (bd1.fb == 0) out = 0;
        bd1.op1_out[1] = op_calc1(bd1.cnt, env, out << bd1.fb, bd1.wavetable);
    }

    Slot& bd2 = channels_[6].slot[kSlot2];
    env = volume_calc(bd2);
    if (env < uint32_t(kEnvQuiet)) {
        output_ += op_calc(bd2.cnt, env, phase_modulation_, bd2.wavetable) * 2;
    }

    // High hat
    env = volume_calc(slot7_1);
    if (env < uint32_t(kEnvQuiet)) {
        const uint32_t bit7 = ((slot7_1.cnt >> kFreqSh) >> 7) & 1;
        const uint32_t bit3 = ((slot7_1.cnt >> kFreqSh) >> 3) & 1;
        const uint32_t bit2 = ((slot7_1.cnt >> kFreqSh) >> 2) & 1;
        const uint32_t res1 = (bit2 ^ bit7) | bit3;
        uint32_t phase = res1 != 0 ? uint32_t(0x200 | (0xd0 >> 2)) : 0xd0;
        const uint32_t bit5e = ((slot8_2.cnt >> kFreqSh) >> 5) & 1;
        const uint32_t bit3e = ((slot8_2.cnt >> kFreqSh) >> 3) & 1;
        if ((bit3e ^ bit5e) != 0) phase = uint32_t(0x200 | (0xd0 >> 2));
        if ((phase & 0x200) != 0) {
            if (noise != 0) phase = 0x200 | 0xd0;
        } else {
            if (noise != 0) phase = 0xd0 >> 2;
        }
        output_ += op_calc(phase << kFreqSh, env, 0, slot7_1.wavetable) * 2;
    }

    // Snare drum
    env = volume_calc(slot7_2);
    if (env < uint32_t(kEnvQuiet)) {
        const uint32_t bit8 = ((slot7_1.cnt >> kFreqSh) >> 8) & 1;
        uint32_t phase = bit8 != 0 ? 0x200u : 0x100u;
        if (noise != 0) phase ^= 0x100;
        output_ += op_calc(phase << kFreqSh, env, 0, slot7_2.wavetable) * 2;
    }

    // Tom tom
    env = volume_calc(slot8_1);
    if (env < uint32_t(kEnvQuiet)) {
        output_ += op_calc(slot8_1.cnt, env, 0, slot8_1.wavetable) * 2;
    }

    // Top cymbal
    env = volume_calc(slot8_2);
    if (env < uint32_t(kEnvQuiet)) {
        const uint32_t bit7 = ((slot7_1.cnt >> kFreqSh) >> 7) & 1;
        const uint32_t bit3 = ((slot7_1.cnt >> kFreqSh) >> 3) & 1;
        const uint32_t bit2 = ((slot7_1.cnt >> kFreqSh) >> 2) & 1;
        const uint32_t res1 = (bit2 ^ bit7) | bit3;
        uint32_t phase = res1 != 0 ? 0x300u : 0x100u;
        const uint32_t bit5e = ((slot8_2.cnt >> kFreqSh) >> 5) & 1;
        const uint32_t bit3e = ((slot8_2.cnt >> kFreqSh) >> 3) & 1;
        if ((bit3e ^ bit5e) != 0) phase = 0x300;
        output_ += op_calc(phase << kFreqSh, env, 0, slot8_2.wavetable) * 2;
    }
}

int32_t YM3812::update() {
    output_ = 0;
    advance_lfo();
    for (int i = 0; i < 6; i++) calc_channel(channels_[size_t(i)]);
    if ((rhythm_ & 0x20) == 0) {
        for (int i = 6; i < 9; i++) calc_channel(channels_[size_t(i)]);
    } else {
        calc_rhythm(noise_rng_ & 1);
    }
    int32_t out = int32_t(float(output_ * 2) * amplitude_);
    out = std::max(-0x7fff, std::min(0x7fff, out));
    advance();
    run_timers();
    return out;
}

}  // namespace dsp
