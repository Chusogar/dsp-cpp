#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <vector>

namespace dsp {

// Yamaha OPN FM core (the FM half of the YM2203/YM2608/YM2612 family), ported
// from fmopn.pas. Only the parts the YM2203 needs are modelled: 3 FM channels,
// the two timers and the SSG prescaler notification.
class OpnCore {
public:
    using IrqHandler = std::function<void(bool)>;
    using SsgClockHandler = std::function<void(uint32_t)>;

    static constexpr int kEnvBits = 10;
    static constexpr int kEnvLen = 1 << kEnvBits;
    static constexpr int kMaxAttIndex = kEnvLen - 1;
    static constexpr int kMinAttIndex = 0;
    static constexpr int kTlResLen = 256;
    static constexpr int kTlTabLen = 13 * 2 * kTlResLen;
    static constexpr int kEnvQuiet = kTlTabLen >> 3;
    static constexpr int kSinBits = 10;
    static constexpr int kSinLen = 1 << kSinBits;
    static constexpr int kSinMask = kSinLen - 1;
    static constexpr int kFreqSh = 16;
    static constexpr int kEgSh = 16;
    static constexpr uint32_t kFreqMask = (1u << kFreqSh) - 1;
    static constexpr int kRateSteps = 8;

    // Envelope generator states.
    static constexpr uint8_t kEgAtt = 4;
    static constexpr uint8_t kEgDec = 3;
    static constexpr uint8_t kEgSus = 2;
    static constexpr uint8_t kEgRel = 1;
    static constexpr uint8_t kEgOff = 0;

    // Operator order inside a channel.
    static constexpr int kSlot1 = 0;
    static constexpr int kSlot2 = 2;
    static constexpr int kSlot3 = 1;
    static constexpr int kSlot4 = 3;

    struct Slot {
        uint8_t det_mul_val = 0;
        uint8_t ksr_m = 0;   // key scale rate: 3 - KSR
        uint32_t ar = 0;     // attack rate
        uint32_t d1r = 0;    // decay rate
        uint32_t d2r = 0;    // sustain rate
        uint32_t rr = 0;     // release rate
        uint8_t ksr = 0;     // kcode >> (3 - KSR)
        uint32_t mul = 1;    // multiple
        uint32_t phase = 0;  // phase counter
        int32_t incr = -1;   // phase step
        uint8_t state = kEgOff;
        uint32_t tl = 0;
        int32_t volume = kMaxAttIndex;
        uint32_t sl = 0;
        int32_t vol_out = kMaxAttIndex;
        uint8_t eg_sh_ar = 0, eg_sel_ar = 0;
        uint8_t eg_sh_d1r = 0, eg_sel_d1r = 0;
        uint8_t eg_sh_d2r = 0, eg_sel_d2r = 0;
        uint8_t eg_sh_rr = 0, eg_sel_rr = 0;
        uint8_t ssg = 0;
        uint8_t ssgn = 0;
        uint32_t key = 0;
    };

    struct Channel {
        std::array<Slot, 4> slot{};
        uint8_t algo = 0;
        uint8_t fb = 0;
        std::array<int32_t, 2> op1_out{};
        // Indices into the internal signal bus; kConnectNone marks algorithm 5.
        int connect1 = 0, connect2 = 0, connect3 = 0, connect4 = 0, mem_connect = 0;
        int32_t mem_value = 0;
        int32_t pms = 0;
        uint8_t ams = 0;
        uint32_t fc = 0;
        uint8_t kcode = 0;
        uint32_t block_fnum = 0;
    };

    // Signal bus slots: modulation inputs, the one sample delay and the channel
    // outputs.
    static constexpr int kConnectNone = -1;
    static constexpr int kBusM2 = 0;
    static constexpr int kBusC1 = 1;
    static constexpr int kBusC2 = 2;
    static constexpr int kBusMem = 3;
    static constexpr int kBusOut = 4;
    static constexpr int kBusSize = kBusOut + 8;

    OpnCore(int channels, uint32_t clock, int rate);

    void set_irq_handler(IrqHandler handler) { irq_handler_ = std::move(handler); }
    void set_ssg_clock_handler(SsgClockHandler handler) { ssg_clock_handler_ = std::move(handler); }

    void write_mode(int reg, int value);
    void write_reg(int reg, int value);
    void prescaler_w(int address, int pre_divider);

    void refresh_fc_eg_chan(Channel& ch);
    void refresh_fc_eg_slot(Slot& slot, int fc, int kc);
    void advance_eg_channel(Channel& ch);
    void chan_calc(Channel& ch);
    void setup_connection(Channel& ch, int num);
    void csm_key_control(Channel& ch);

    void internal_timer_a(Channel& csm_channel);
    void internal_timer_b();
    void timer_a_over();
    void timer_b_over();

    void status_set(int flag);
    void status_reset(int flag);
    void irq_mask_set(int flag);

    void reset_channels();
    void clear_bus();
    int32_t& bus(int index) { return bus_[size_t(index)]; }
    int32_t channel_output(int channel) const { return bus_[size_t(kBusOut + channel)]; }

    Channel& channel(int index) { return channels_[size_t(index)]; }
    int channels() const { return int(channels_.size()); }

    void advance_envelopes();

    uint8_t status() const { return status_; }
    uint8_t address() const { return address_; }
    void set_address(uint8_t address) { address_ = address; }
    int mode() const { return mode_; }
    void set_mode(int mode) { mode_ = mode; }
    void set_ta(int value) { ta_ = value; }
    void set_tb(int value) { tb_ = value; }
    void reset_timers();
    void reset_eg_timer() {
        eg_timer_ = 0.0;
        eg_cnt_ = 0;
    }

    // Three slot mode state.
    struct ThreeSlot {
        std::array<uint32_t, 3> fc{};
        uint8_t fn_h = 0;
        std::array<uint8_t, 3> kcode{};
        std::array<uint32_t, 3> block_fnum{};
    };
    ThreeSlot& three_slot() { return sl3_; }

private:
    void set_det_mul(Channel& ch, Slot& slot, int value);
    void set_ar_ksr(Channel& ch, Slot& slot, int value);
    void set_dr(Slot& slot, int value);
    void set_sr(Slot& slot, int value);
    void set_sl_rr(Slot& slot, int value);
    void set_timers(int value);
    void key_on(Channel& ch, int slot);
    void key_off(Channel& ch, int slot);
    void set_prescaler(int pres, int timer_prescaler, int ssg_pres);
    void init_time_tables();

    std::vector<Channel> channels_;
    ThreeSlot sl3_;
    std::array<int32_t, kBusSize> bus_{};
    std::array<std::array<int32_t, 32>, 8> dt_tab_{};
    std::array<uint32_t, 4096> fn_table_{};
    uint32_t fn_max_ = 0;

    uint32_t clock_;
    int rate_;
    double freqbase_ = 0.0;
    int timer_prescaler_ = 0;
    uint8_t address_ = 0;
    uint8_t irq_ = 0;
    uint8_t irq_mask_ = 0;
    uint8_t status_ = 0;
    int mode_ = 0;
    uint8_t prescaler_sel_ = 0;
    uint8_t fn_h_ = 0;
    int ta_ = 0;
    double tac_ = 0.0;
    int tb_ = 0;
    double tbc_ = 0.0;
    uint32_t eg_cnt_ = 0;
    double eg_timer_ = 0.0;
    double eg_timer_add_ = 0.0;
    double eg_timer_overflow_ = 0.0;

    IrqHandler irq_handler_;
    SsgClockHandler ssg_clock_handler_;
};

}  // namespace dsp
