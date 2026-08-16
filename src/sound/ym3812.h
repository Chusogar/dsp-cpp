#pragma once

#include <array>
#include <cstdint>
#include <functional>

namespace dsp {

// Yamaha YM3812 (OPL2) and YM3526 (OPL), ported from fmopl.pas + ym_3812.pas.
class YM3812 {
public:
    using IrqHandler = std::function<void(bool)>;

    static constexpr int kSampleRate = 44100;

    enum Type { kYM3812, kYM3526 };

    explicit YM3812(uint32_t clock, Type type = kYM3812, float amplitude = 1.0f);

    YM3812(const YM3812&) = delete;
    YM3812& operator=(const YM3812&) = delete;

    void set_irq_handler(IrqHandler handler) { irq_handler_ = std::move(handler); }

    void reset();

    void control(uint8_t value) { address_ = value; }
    void write(uint8_t value) { write_reg(address_, value); }
    void write_reg(int reg, int value);
    uint8_t status() const { return uint8_t((status_ & (status_mask_ | 0x80)) | 0x06); }
    uint8_t read() const { return 0xff; }

    // Generates the next sample (sample rate is kSampleRate).
    int32_t update();

private:
    static constexpr int kEnvBits = 10;
    static constexpr int kEnvLen = 1 << kEnvBits;
    static constexpr int kMaxAttIndex = (1 << (kEnvBits - 1)) - 1;  // 511
    static constexpr int kMinAttIndex = 0;
    static constexpr int kSinBits = 10;
    static constexpr int kSinLen = 1 << kSinBits;
    static constexpr int kSinMask = kSinLen - 1;
    static constexpr int kTlResLen = 256;
    static constexpr int kTlTabLen = 12 * 2 * kTlResLen;
    static constexpr int kEnvQuiet = kTlTabLen >> 4;
    static constexpr int kFreqSh = 16;
    static constexpr int kEgSh = 16;
    static constexpr int kLfoSh = 24;
    static constexpr uint32_t kFreqMask = (1u << kFreqSh) - 1;
    static constexpr int kRateSteps = 8;

    static constexpr uint8_t kEgAtt = 4;
    static constexpr uint8_t kEgDec = 3;
    static constexpr uint8_t kEgSus = 2;
    static constexpr uint8_t kEgRel = 1;
    static constexpr uint8_t kEgOff = 0;

    static constexpr int kSlot1 = 0;
    static constexpr int kSlot2 = 1;

    struct Slot {
        uint32_t ar = 0, dr = 0, rr = 0;
        uint8_t ksr_m = 0, ksl = 0, ksr = 0, mul = 0;
        uint32_t cnt = 0, incr = 0;
        uint8_t fb = 0;
        bool connect_output = false;  // false: phase modulation, true: channel output
        std::array<int32_t, 2> op1_out{};
        uint8_t con = 0;
        uint8_t eg_type = 0;
        uint8_t state = kEgOff;
        uint32_t tl = 0;
        int32_t tll = 0;
        int32_t volume = kMaxAttIndex;
        uint32_t sl = 0;
        uint8_t eg_sh_ar = 0, eg_sel_ar = 0;
        uint8_t eg_sh_dr = 0, eg_sel_dr = 0;
        uint8_t eg_sh_rr = 0, eg_sel_rr = 0;
        uint32_t key = 0;
        uint32_t am_mask = 0;
        uint8_t vib = 0;
        uint32_t wavetable = 0;
    };

    struct Channel {
        std::array<Slot, 2> slot{};
        uint32_t block_fnum = 0;
        uint32_t fc = 0;
        uint32_t ksl_base = 0;
        uint8_t kcode = 0;
    };

    void initialize();
    void calc_fcslot(Channel& ch, Slot& slot);
    void set_mul(int slot_v, int v);
    void set_ksl_tl(int slot_v, int v);
    void set_ar_dr(int slot_v, int v);
    void set_sl_rr(int slot_v, int v);
    static void key_on(Slot& slot, uint32_t key_set);
    static void key_off(Slot& slot, uint32_t key_clr);
    void csm_key_control(Channel& ch);
    void status_set(int flag);
    void status_reset(int flag);
    void status_mask_set(int flag);
    void timer_over(int timer);
    void run_timers();
    void advance_lfo();
    void advance();
    void calc_channel(Channel& ch);
    void calc_rhythm(uint32_t noise);
    uint32_t volume_calc(const Slot& slot) const;

    std::array<Channel, 9> channels_{};
    uint32_t eg_cnt_ = 0;
    double eg_timer_ = 0.0, eg_timer_add_ = 0.0, eg_timer_overflow_ = 0.0;
    uint8_t rhythm_ = 0;
    std::array<uint32_t, 1024> fn_tab_{};
    uint8_t lfo_am_depth_ = 0, lfo_pm_depth_range_ = 0;
    uint32_t lfo_am_cnt_ = 0, lfo_am_inc_ = 0, lfo_pm_cnt_ = 0, lfo_pm_inc_ = 0;
    uint32_t lfo_am_ = 0;
    int32_t lfo_pm_ = 0;
    uint32_t noise_rng_ = 1, noise_p_ = 0, noise_f_ = 0;
    uint8_t wavesel_ = 0;
    std::array<uint32_t, 2> timer_period_{};
    std::array<double, 2> timer_count_{};
    std::array<uint8_t, 2> timer_enabled_{};
    uint8_t type_flags_ = 0;
    uint8_t address_ = 0;
    uint8_t status_ = 0;
    uint8_t status_mask_ = 0;
    uint8_t mode_ = 0;
    uint32_t clock_;
    double freqbase_ = 0.0;
    int32_t phase_modulation_ = 0;
    int32_t output_ = 0;
    float amplitude_;
    IrqHandler irq_handler_;
};

}  // namespace dsp
