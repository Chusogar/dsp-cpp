#include "sound/fmopn.h"

#include <cmath>

namespace dsp {
namespace {

constexpr int kRateSteps = OpnCore::kRateSteps;

constexpr uint8_t kEgRateSelectInit[32 + 64 + 32] = {
    18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18,
    18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18,
    0,  1,  2,  3,  0,  1,  2,  3,  0,  1,  2,  3,  0,  1,  2,  3,
    0,  1,  2,  3,  0,  1,  2,  3,  0,  1,  2,  3,  0,  1,  2,  3,
    0,  1,  2,  3,  0,  1,  2,  3,  0,  1,  2,  3,  0,  1,  2,  3,
    4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15, 16, 16, 16, 16,
    16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16,
    16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16,
};

constexpr uint8_t kEgRateShift[32 + 64 + 32] = {
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, 0, 0, 0, 0,
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, 0, 0, 0, 0,
    11, 11, 11, 11, 10, 10, 10, 10, 9,  9,  9,  9, 8, 8, 8, 8,
    7,  7,  7,  7,  6,  6,  6,  6,  5,  5,  5,  5, 4, 4, 4, 4,
    3,  3,  3,  3,  2,  2,  2,  2,  1,  1,  1,  1, 0, 0, 0, 0,
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, 0, 0, 0, 0,
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, 0, 0, 0, 0,
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, 0, 0, 0, 0,
};

constexpr uint8_t kEgInc[19 * kRateSteps] = {
    0, 1, 0, 1, 0, 1, 0, 1,          // rates 00..11 0
    0, 1, 0, 1, 1, 1, 0, 1,          // rates 00..11 1
    0, 1, 1, 1, 0, 1, 1, 1,          // rates 00..11 2
    0, 1, 1, 1, 1, 1, 1, 1,          // rates 00..11 3
    1, 1, 1, 1, 1, 1, 1, 1,          // rate 12 0
    1, 1, 1, 2, 1, 1, 1, 2,          // rate 12 1
    1, 2, 1, 2, 1, 2, 1, 2,          // rate 12 2
    1, 2, 2, 2, 1, 2, 2, 2,          // rate 12 3
    2, 2, 2, 2, 2, 2, 2, 2,          // rate 13 0
    2, 2, 2, 4, 2, 2, 2, 4,          // rate 13 1
    2, 4, 2, 4, 2, 4, 2, 4,          // rate 13 2
    2, 4, 4, 4, 2, 4, 4, 4,          // rate 13 3
    4, 4, 4, 4, 4, 4, 4, 4,          // rate 14 0
    4, 4, 4, 8, 4, 4, 4, 8,          // rate 14 1
    4, 8, 4, 8, 4, 8, 4, 8,          // rate 14 2
    4, 8, 8, 8, 4, 8, 8, 8,          // rate 14 3
    8, 8, 8, 8, 8, 8, 8, 8,          // rate 15
    16, 16, 16, 16, 16, 16, 16, 16,  // rate 15 for attack
    0, 0, 0, 0, 0, 0, 0, 0,          // infinity rates
};

constexpr uint8_t kOpnFkTable[16] = {0, 0, 0, 0, 0, 0, 0, 1, 2, 3, 3, 3, 3, 3, 3, 3};

constexpr uint8_t kDtTab[128] = {
    // FD=0
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    // FD=1
    0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2,
    2, 3, 3, 3, 4, 4, 4, 5, 5, 6, 6, 7, 8, 8, 8, 8,
    // FD=2
    1, 1, 1, 1, 2, 2, 2, 2, 2, 3, 3, 3, 4, 4, 4, 5,
    5, 6, 6, 7, 8, 8, 9, 10, 11, 12, 13, 14, 16, 16, 16, 16,
    // FD=3
    2, 2, 2, 2, 2, 3, 3, 3, 4, 4, 4, 5, 5, 6, 6, 7,
    8, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 20, 22, 22, 22, 22,
};

struct Tables {
    std::array<int32_t, OpnCore::kTlTabLen> tl{};
    std::array<int32_t, OpnCore::kSinLen> sin{};
    std::array<uint8_t, 32 + 64 + 32> eg_rate_select{};
    std::array<uint32_t, 16> sl{};

    Tables() {
        constexpr double env_step = 128.0 / OpnCore::kEnvLen;
        for (int x = 0; x < OpnCore::kTlResLen; x++) {
            double m = double(1 << 16) / std::pow(2.0, (x + 1) * (env_step / 4.0) / 8.0);
            m = std::floor(m);
            int n = int(m);
            n >>= 4;
            n = (n & 1) != 0 ? (n >> 1) + 1 : n >> 1;
            n <<= 2;
            tl[size_t(x * 2)] = n;
            tl[size_t(x * 2 + 1)] = -n;
            for (int i = 1; i < 13; i++) {
                tl[size_t(x * 2 + i * 2 * OpnCore::kTlResLen)] = n >> i;
                tl[size_t(x * 2 + 1 + i * 2 * OpnCore::kTlResLen)] = -(n >> i);
            }
        }
        for (int i = 0; i < OpnCore::kSinLen; i++) {
            const double m = std::sin(((i * 2) + 1) * M_PI / OpnCore::kSinLen);
            double o = 8.0 * std::log(1.0 / (m > 0.0 ? m : -m)) / std::log(2.0);
            o /= env_step / 4.0;
            int n = int(2.0 * o);
            n = (n & 1) != 0 ? (n >> 1) + 1 : n >> 1;
            sin[size_t(i)] = m >= 0.0 ? n * 2 : n * 2 + 1;
        }
        for (size_t i = 0; i < eg_rate_select.size(); i++) {
            eg_rate_select[i] = uint8_t(kEgRateSelectInit[i] * kRateSteps);
        }
        for (int i = 0; i < 15; i++) sl[size_t(i)] = uint32_t(std::lround(i * (4.0 / env_step)));
        sl[15] = uint32_t(std::lround(31 * (4.0 / env_step)));
    }
};

const Tables& tables() {
    static const Tables instance;
    return instance;
}

int32_t op_calc(uint32_t phase, uint32_t env, int32_t pm) {
    const auto& tab = tables();
    const int32_t tmp =
        int32_t((((phase & ~OpnCore::kFreqMask) + uint32_t(pm << 15)) >> OpnCore::kFreqSh) &
                OpnCore::kSinMask);
    const uint32_t p = (env << 3) + uint32_t(tab.sin[size_t(tmp)]);
    if (p >= uint32_t(OpnCore::kTlTabLen)) return 0;
    return tab.tl[size_t(p)];
}

int32_t op_calc1(uint32_t phase, uint32_t env, int32_t pm) {
    const auto& tab = tables();
    const int32_t tmp = int32_t((((phase & ~OpnCore::kFreqMask) + uint32_t(pm)) >>
                                OpnCore::kFreqSh) &
                               OpnCore::kSinMask);
    const uint32_t p = (env << 3) + uint32_t(tab.sin[size_t(tmp)]);
    if (p >= uint32_t(OpnCore::kTlTabLen)) return 0;
    return tab.tl[size_t(p)];
}

}  // namespace

OpnCore::OpnCore(int channels, uint32_t clock, int rate)
    : channels_(size_t(channels)), clock_(clock), rate_(rate) {
    tables();
}

void OpnCore::init_time_tables() {
    for (int d = 0; d < 4; d++) {
        for (int i = 0; i < 32; i++) {
            const double rate = kDtTab[d * 32 + i] * kSinLen * freqbase_ * (1 << kFreqSh);
            const int32_t value = int32_t(rate / double(1 << 20));
            dt_tab_[size_t(d)][size_t(i)] = value;
            dt_tab_[size_t(d + 4)][size_t(i)] = -value;
        }
    }
}

void OpnCore::set_prescaler(int pres, int timer_prescaler, int ssg_pres) {
    freqbase_ = rate_ != 0 ? (double(clock_) / rate_) / pres : 0.0;
    eg_timer_add_ = double(1 << kEgSh) * freqbase_;
    eg_timer_overflow_ = 3.0 * (1 << kEgSh);
    timer_prescaler_ = timer_prescaler;
    if (ssg_pres != 0 && ssg_clock_handler_) {
        ssg_clock_handler_(uint32_t(double(clock_) * 2 / ssg_pres));
    }
    init_time_tables();
    for (int i = 0; i < 4096; i++) {
        fn_table_[size_t(i)] = uint32_t(i * 32 * freqbase_ * (1 << (kFreqSh - 10)));
    }
    fn_max_ = uint32_t(0x20000 * freqbase_ * (1 << (kFreqSh - 10)));
}

void OpnCore::prescaler_w(int address, int pre_divider) {
    static constexpr int kOpnPres[4] = {2 * 12, 2 * 12, 6 * 12, 3 * 12};
    static constexpr int kSsgPres[4] = {1, 1, 4, 2};
    switch (address) {
        case 0:
            prescaler_sel_ = 2;
            break;
        case 1:
            break;
        case 0x2d:
            prescaler_sel_ = uint8_t(prescaler_sel_ | 0x02);
            break;
        case 0x2e:
            prescaler_sel_ = uint8_t(prescaler_sel_ | 0x01);
            break;
        case 0x2f:
            prescaler_sel_ = 0;
            break;
        default:
            break;
    }
    const int sel = prescaler_sel_ & 3;
    set_prescaler(kOpnPres[sel] * pre_divider, kOpnPres[sel] * pre_divider,
                  kSsgPres[sel] * pre_divider);
}

void OpnCore::refresh_fc_eg_slot(Slot& slot, int fc, int kc) {
    const auto& tab = tables();
    const uint8_t ksr = uint8_t(kc >> slot.ksr_m);
    fc += dt_tab_[slot.det_mul_val][size_t(kc)];
    if (fc < 0) fc += int(fn_max_);
    slot.incr = int32_t((uint32_t(fc) * slot.mul) >> 1);
    if (slot.ksr == ksr) return;
    slot.ksr = ksr;
    if (slot.ar + slot.ksr < 32 + 62) {
        slot.eg_sh_ar = kEgRateShift[slot.ar + slot.ksr];
        slot.eg_sel_ar = tab.eg_rate_select[slot.ar + slot.ksr];
    } else {
        slot.eg_sh_ar = 0;
        slot.eg_sel_ar = 17 * kRateSteps;
    }
    slot.eg_sh_d1r = kEgRateShift[slot.d1r + slot.ksr];
    slot.eg_sh_d2r = kEgRateShift[slot.d2r + slot.ksr];
    slot.eg_sh_rr = kEgRateShift[slot.rr + slot.ksr];
    slot.eg_sel_d1r = tab.eg_rate_select[slot.d1r + slot.ksr];
    slot.eg_sel_d2r = tab.eg_rate_select[slot.d2r + slot.ksr];
    slot.eg_sel_rr = tab.eg_rate_select[slot.rr + slot.ksr];
}

void OpnCore::refresh_fc_eg_chan(Channel& ch) {
    if (ch.slot[kSlot1].incr != -1) return;
    const int fc = int(ch.fc);
    const int kc = ch.kcode;
    refresh_fc_eg_slot(ch.slot[kSlot1], fc, kc);
    refresh_fc_eg_slot(ch.slot[kSlot2], fc, kc);
    refresh_fc_eg_slot(ch.slot[kSlot3], fc, kc);
    refresh_fc_eg_slot(ch.slot[kSlot4], fc, kc);
}

void OpnCore::clear_bus() { bus_.fill(0); }

void OpnCore::chan_calc(Channel& ch) {
    bus_[kBusM2] = 0;
    bus_[kBusC1] = 0;
    bus_[kBusC2] = 0;
    bus_[kBusMem] = 0;
    bus_[size_t(ch.mem_connect)] = ch.mem_value;

    int32_t eg_out = ch.slot[kSlot1].vol_out;
    int32_t out = ch.op1_out[0] + ch.op1_out[1];
    ch.op1_out[0] = ch.op1_out[1];
    if (ch.connect1 == kConnectNone) {
        // algorithm 5
        bus_[kBusMem] = ch.op1_out[0];
        bus_[kBusC1] = ch.op1_out[0];
        bus_[kBusC2] = ch.op1_out[0];
    } else {
        bus_[size_t(ch.connect1)] += ch.op1_out[0];
    }
    ch.op1_out[1] = 0;
    if (eg_out < kEnvQuiet) {
        if (ch.fb == 0) out = 0;
        ch.op1_out[1] = op_calc1(ch.slot[kSlot1].phase, uint32_t(eg_out), out << ch.fb);
    }

    eg_out = ch.slot[kSlot3].vol_out;
    if (eg_out < kEnvQuiet) {
        bus_[size_t(ch.connect3)] +=
            op_calc(ch.slot[kSlot3].phase, uint32_t(eg_out), bus_[kBusM2]);
    }
    eg_out = ch.slot[kSlot2].vol_out;
    if (eg_out < kEnvQuiet) {
        bus_[size_t(ch.connect2)] +=
            op_calc(ch.slot[kSlot2].phase, uint32_t(eg_out), bus_[kBusC1]);
    }
    eg_out = ch.slot[kSlot4].vol_out;
    if (eg_out < kEnvQuiet) {
        bus_[size_t(ch.connect4)] +=
            op_calc(ch.slot[kSlot4].phase, uint32_t(eg_out), bus_[kBusC2]);
    }

    ch.mem_value = bus_[kBusMem];
    for (auto& slot : ch.slot) slot.phase += uint32_t(slot.incr);
}

void OpnCore::key_on(Channel& ch, int index) {
    Slot& slot = ch.slot[size_t(index)];
    if (slot.key != 0) return;
    slot.key = 1;
    slot.phase = 0;
    slot.ssgn = uint8_t((slot.ssg & 0x04) >> 1);
    slot.state = kEgAtt;
}

void OpnCore::key_off(Channel& ch, int index) {
    Slot& slot = ch.slot[size_t(index)];
    if (slot.key == 0) return;
    slot.key = 0;
    if (slot.state > kEgRel) slot.state = kEgRel;
}

void OpnCore::csm_key_control(Channel& ch) {
    for (int index : {kSlot1, kSlot2, kSlot3, kSlot4}) {
        if (ch.slot[size_t(index)].key != 0) continue;
        key_on(ch, index);
        key_off(ch, index);
    }
}

void OpnCore::setup_connection(Channel& ch, int num) {
    const int carrier = kBusOut + num;
    switch (ch.algo) {
        case 0:
            ch.connect1 = kBusC1;
            ch.connect2 = kBusMem;
            ch.connect3 = kBusC2;
            ch.mem_connect = kBusM2;
            break;
        case 1:
            ch.connect1 = kBusMem;
            ch.connect2 = kBusMem;
            ch.connect3 = kBusC2;
            ch.mem_connect = kBusM2;
            break;
        case 2:
            ch.connect1 = kBusC2;
            ch.connect2 = kBusMem;
            ch.connect3 = kBusC2;
            ch.mem_connect = kBusM2;
            break;
        case 3:
            ch.connect1 = kBusC1;
            ch.connect2 = kBusMem;
            ch.connect3 = kBusC2;
            ch.mem_connect = kBusC2;
            break;
        case 4:
            ch.connect1 = kBusC1;
            ch.connect2 = carrier;
            ch.connect3 = kBusC2;
            ch.mem_connect = kBusMem;
            break;
        case 5:
            ch.connect1 = kConnectNone;
            ch.connect2 = carrier;
            ch.connect3 = carrier;
            ch.mem_connect = kBusM2;
            break;
        case 6:
            ch.connect1 = kBusC1;
            ch.connect2 = carrier;
            ch.connect3 = carrier;
            ch.mem_connect = kBusMem;
            break;
        default:
            ch.connect1 = carrier;
            ch.connect2 = carrier;
            ch.connect3 = carrier;
            ch.mem_connect = kBusMem;
            break;
    }
    ch.connect4 = carrier;
}

void OpnCore::status_set(int flag) {
    status_ = uint8_t(status_ | flag);
    if (irq_ == 0) {
        irq_ = 1;
        if (irq_handler_) irq_handler_(true);
    }
}

void OpnCore::status_reset(int flag) {
    status_ = uint8_t(status_ & ~flag);
    if (irq_ != 0) {
        irq_ = 0;
        if (irq_handler_) irq_handler_(false);
    }
}

void OpnCore::irq_mask_set(int flag) {
    irq_mask_ = uint8_t(flag);
    status_set(0);
    status_reset(0);
}

void OpnCore::reset_timers() {
    ta_ = 0;
    tac_ = 0.0;
    tb_ = 0;
    tbc_ = 0.0;
}

void OpnCore::set_timers(int value) {
    mode_ = value;
    if ((value & 0x20) != 0) status_reset(0x02);
    if ((value & 0x10) != 0) status_reset(0x01);
    if ((value & 0x02) != 0) {
        if (tbc_ == 0.0) tbc_ = double((256 - tb_) << 4);
    } else {
        tbc_ = 0.0;
    }
    if ((value & 0x01) != 0) {
        if (tac_ == 0.0) tac_ = double(1024 - ta_);
    } else {
        tac_ = 0.0;
    }
}

void OpnCore::timer_a_over() {
    if ((mode_ & 0x04) != 0) status_set(0x01);
    tac_ = double(1024 - ta_);
}

void OpnCore::timer_b_over() {
    if ((mode_ & 0x08) != 0) status_set(0x02);
    tbc_ = double((256 - tb_) << 4);
}

void OpnCore::internal_timer_a(Channel& csm_channel) {
    if (tac_ == 0.0) return;
    tac_ -= freqbase_;
    if (tac_ > 0.0) return;
    timer_a_over();
    if ((mode_ & 0x80) != 0) csm_key_control(csm_channel);
}

void OpnCore::internal_timer_b() {
    if (tbc_ == 0.0) return;
    tbc_ -= freqbase_;
    if (tbc_ <= 0.0) timer_b_over();
}

void OpnCore::write_mode(int reg, int value) {
    switch (reg) {
        case 0x24:
            ta_ = (ta_ & 0x03) | (value << 2);
            break;
        case 0x25:
            ta_ = (ta_ & 0x3fc) | (value & 3);
            break;
        case 0x26:
            tb_ = value;
            break;
        case 0x27:
            set_timers(value);
            break;
        case 0x28: {
            int c = value & 0x03;
            if (c == 3 || c >= int(channels_.size())) return;
            Channel& ch = channels_[size_t(c)];
            if ((value & 0x10) != 0) key_on(ch, kSlot1); else key_off(ch, kSlot1);
            if ((value & 0x20) != 0) key_on(ch, kSlot2); else key_off(ch, kSlot2);
            if ((value & 0x40) != 0) key_on(ch, kSlot3); else key_off(ch, kSlot3);
            if ((value & 0x80) != 0) key_on(ch, kSlot4); else key_off(ch, kSlot4);
            break;
        }
        default:
            break;
    }
}

void OpnCore::set_det_mul(Channel& ch, Slot& slot, int value) {
    slot.mul = (value & 0x0f) != 0 ? uint32_t((value & 0x0f) * 2) : 1;
    slot.det_mul_val = uint8_t((value >> 4) & 7);
    ch.slot[kSlot1].incr = -1;
}

void OpnCore::set_ar_ksr(Channel& ch, Slot& slot, int value) {
    const auto& tab = tables();
    const uint8_t old_ksr = slot.ksr_m;
    slot.ar = (value & 0x1f) != 0 ? uint32_t(32 + ((value & 0x1f) << 1)) : 0;
    slot.ksr_m = uint8_t(3 - (value >> 6));
    if (slot.ksr_m != old_ksr) ch.slot[kSlot1].incr = -1;
    if (slot.ar + slot.ksr < 32 + 62) {
        slot.eg_sh_ar = kEgRateShift[slot.ar + slot.ksr];
        slot.eg_sel_ar = tab.eg_rate_select[slot.ar + slot.ksr];
    } else {
        slot.eg_sh_ar = 0;
        slot.eg_sel_ar = 17 * kRateSteps;
    }
}

void OpnCore::set_dr(Slot& slot, int value) {
    const auto& tab = tables();
    slot.d1r = (value & 0x1f) != 0 ? uint32_t(32 + ((value & 0x1f) << 1)) : 0;
    slot.eg_sh_d1r = kEgRateShift[slot.d1r + slot.ksr];
    slot.eg_sel_d1r = tab.eg_rate_select[slot.d1r + slot.ksr];
}

void OpnCore::set_sr(Slot& slot, int value) {
    const auto& tab = tables();
    slot.d2r = (value & 0x1f) != 0 ? uint32_t(32 + ((value & 0x1f) << 1)) : 0;
    slot.eg_sh_d2r = kEgRateShift[slot.d2r + slot.ksr];
    slot.eg_sel_d2r = tab.eg_rate_select[slot.d2r + slot.ksr];
}

void OpnCore::set_sl_rr(Slot& slot, int value) {
    const auto& tab = tables();
    slot.sl = tab.sl[size_t(value >> 4)];
    slot.rr = uint32_t(34 + ((value & 0x0f) << 2));
    slot.eg_sh_rr = kEgRateShift[slot.rr + slot.ksr];
    slot.eg_sel_rr = tab.eg_rate_select[slot.rr + slot.ksr];
}

void OpnCore::write_reg(int reg, int value) {
    int c = reg & 3;
    if (c == 3) return;
    if (c >= int(channels_.size())) return;
    Channel& ch = channels_[size_t(c)];
    const int slot_index = (reg >> 2) & 3;
    Slot& slot = ch.slot[size_t(slot_index)];
    switch (reg & 0xf0) {
        case 0x30:
            set_det_mul(ch, slot, value);
            break;
        case 0x40:
            slot.tl = uint32_t((value & 0x7f) << (kEnvBits - 7));
            break;
        case 0x50:
            set_ar_ksr(ch, slot, value);
            break;
        case 0x60:
            set_dr(slot, value);
            break;
        case 0x70:
            set_sr(slot, value);
            break;
        case 0x80:
            set_sl_rr(slot, value);
            break;
        case 0x90:
            slot.ssg = uint8_t(value & 0x0f);
            slot.ssgn = uint8_t((value & 0x04) >> 1);
            break;
        case 0xa0:
            switch (slot_index) {
                case 0: {  // fnum1
                    const uint32_t fn = (uint32_t(fn_h_ & 7) << 8) + uint32_t(value);
                    const uint8_t blk = uint8_t(fn_h_ >> 3);
                    ch.kcode = uint8_t((blk << 2) | kOpnFkTable[fn >> 7]);
                    ch.fc = fn_table_[size_t(fn * 2)] >> (7 - blk);
                    ch.block_fnum = (uint32_t(blk) << 11) | fn;
                    ch.slot[kSlot1].incr = -1;
                    break;
                }
                case 1:
                    fn_h_ = uint8_t(value & 0x3f);
                    break;
                case 2: {  // 3 channel mode fnum1
                    const uint32_t fn = (uint32_t(sl3_.fn_h & 7) << 8) + uint32_t(value);
                    const uint8_t blk = uint8_t(sl3_.fn_h >> 3);
                    sl3_.kcode[size_t(c)] = uint8_t((blk << 2) | kOpnFkTable[fn >> 7]);
                    sl3_.fc[size_t(c)] = fn_table_[size_t(fn * 2)] >> (7 - blk);
                    sl3_.block_fnum[size_t(c)] = fn;
                    if (channels_.size() > 2) channels_[2].slot[kSlot1].incr = -1;
                    break;
                }
                default:
                    sl3_.fn_h = uint8_t(value & 0x3f);
                    break;
            }
            break;
        case 0xb0:
            if (slot_index == 0) {
                const int feedback = (value >> 3) & 7;
                ch.algo = uint8_t(value & 7);
                ch.fb = feedback != 0 ? uint8_t(feedback + 6) : 0;
                setup_connection(ch, c);
            }
            break;
        default:
            break;
    }
}

void OpnCore::advance_eg_channel(Channel& ch) {
    for (int index = 0; index < 4; index++) {
        Slot& slot = ch.slot[size_t(index)];
        uint32_t swap_flag = 0;
        switch (slot.state) {
            case kEgAtt:
                if ((eg_cnt_ & ((1u << slot.eg_sh_ar) - 1)) == 0) {
                    slot.volume +=
                        (~slot.volume *
                         kEgInc[slot.eg_sel_ar + ((eg_cnt_ >> slot.eg_sh_ar) & 7)]) /
                        16;
                    if (slot.volume <= kMinAttIndex) {
                        slot.volume = kMinAttIndex;
                        slot.state = kEgDec;
                    }
                }
                break;
            case kEgDec:
                if ((eg_cnt_ & ((1u << slot.eg_sh_d1r) - 1)) == 0) {
                    const int32_t step =
                        kEgInc[slot.eg_sel_d1r + ((eg_cnt_ >> slot.eg_sh_d1r) & 7)];
                    slot.volume += (slot.ssg & 0x08) != 0 ? 4 * step : step;
                    if (uint32_t(slot.volume) >= slot.sl) slot.state = kEgSus;
                }
                break;
            case kEgSus:
                if ((slot.ssg & 0x08) != 0) {
                    if ((eg_cnt_ & ((1u << slot.eg_sh_d2r) - 1)) == 0) {
                        slot.volume +=
                            4 * kEgInc[slot.eg_sel_d2r + ((eg_cnt_ >> slot.eg_sh_d2r) & 7)];
                        if (slot.volume >= kEnvQuiet) {
                            if ((slot.ssg & 0x01) != 0) {
                                if ((slot.ssgn & 1) == 0) swap_flag = (slot.ssg & 0x02) | 1;
                            } else {
                                slot.volume = 511;
                                slot.state = kEgAtt;
                                swap_flag = slot.ssg & 0x02;
                            }
                        }
                    }
                } else if ((eg_cnt_ & ((1u << slot.eg_sh_d2r) - 1)) == 0) {
                    slot.volume += kEgInc[slot.eg_sel_d2r + ((eg_cnt_ >> slot.eg_sh_d2r) & 7)];
                    if (slot.volume >= kMaxAttIndex) slot.volume = kMaxAttIndex;
                }
                break;
            case kEgRel:
                if ((eg_cnt_ & ((1u << slot.eg_sh_rr) - 1)) == 0) {
                    slot.volume += kEgInc[slot.eg_sel_rr + ((eg_cnt_ >> slot.eg_sh_rr) & 7)];
                    if (slot.volume >= kMaxAttIndex) {
                        slot.volume = kMaxAttIndex;
                        slot.state = kEgOff;
                    }
                }
                break;
            default:
                break;
        }
        int32_t out = slot.volume;
        if ((slot.ssg & 0x08) != 0 && (slot.ssgn & 2) != 0 && slot.state > kEgRel) {
            out ^= kMaxAttIndex;
        }
        slot.vol_out = out + int32_t(slot.tl);
        slot.ssgn = uint8_t(slot.ssgn ^ swap_flag);
    }
}

void OpnCore::advance_envelopes() {
    eg_timer_ += eg_timer_add_;
    while (eg_timer_ >= eg_timer_overflow_) {
        eg_timer_ -= eg_timer_overflow_;
        eg_cnt_++;
        for (auto& ch : channels_) advance_eg_channel(ch);
    }
}

void OpnCore::reset_channels() {
    for (auto& ch : channels_) {
        ch.fc = 0;
        for (auto& slot : ch.slot) {
            slot.ssg = 0;
            slot.ssgn = 0;
            slot.state = kEgOff;
            slot.volume = kMaxAttIndex;
            slot.vol_out = kMaxAttIndex;
        }
    }
}

}  // namespace dsp
