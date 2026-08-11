#pragma once

#include <array>
#include <cstdint>

namespace dsp {

// Yamaha YM2413 (OPLL) FM synthesizer, ported from ym_2413.pas (dsp-emulator /
// MAME Jarek OPLL core). 9 channels × 2 operators, fixed instrument ROM +
// user patch, rhythm mode (BD/HH/SD/TOM/CYM).
class YM2413 {
public:
    static constexpr int kSampleRate = 44100;

    explicit YM2413(uint32_t clock, float amplitude = 1.0f);

    void reset();
    void address(uint8_t reg) { addr_ = reg; }
    void write(uint8_t data) { write_reg(addr_, data); }
    void write_reg(uint8_t reg, uint8_t data);

    // Next mixed mono sample at kSampleRate.
    int32_t update();

private:
    static constexpr int kFreqSh = 16;
    static constexpr int kEgSh = 16;
    static constexpr int kLfoSh = 24;
    static constexpr int kFreqMask = (1 << kFreqSh) - 1;

    static constexpr int kEnvBits = 10;
    static constexpr int kEnvLen = 1 << kEnvBits;
    static constexpr double kEnvStep = 128.0 / kEnvLen;
    static constexpr int kMaxAttIndex = (1 << (kEnvBits - 2)) - 1;  // 255
    static constexpr int kMinAttIndex = 0;

    static constexpr int kSlot1 = 0;
    static constexpr int kSlot2 = 1;

    static constexpr int kEgDmp = 5;
    static constexpr int kEgAtt = 4;
    static constexpr int kEgDec = 3;
    static constexpr int kEgSus = 2;
    static constexpr int kEgRel = 1;
    static constexpr int kEgOff = 0;

    static constexpr int kRateSteps = 8;
    static constexpr int kTlResLen = 256;
    static constexpr int kTlTabLen = 11 * 2 * kTlResLen;
    static constexpr int kEnvQuiet = kTlTabLen >> 5;
    static constexpr int kSinBits = 10;
    static constexpr int kSinLen = 1 << kSinBits;
    static constexpr int kSinMask = kSinLen - 1;

    struct Slot {
        uint32_t ar = 0, dr = 0, rr = 0;
        uint8_t ksr_m = 0;
        uint8_t ksl = 0;
        uint8_t ksr = 0;
        uint8_t mul = 0;
        uint32_t phase = 0;
        uint32_t freq = 0;
        uint8_t fb_shift = 0;
        int32_t op1_out[2] = {};
        uint8_t eg_type = 0;
        uint8_t state = kEgOff;
        uint32_t tl = 0;
        int32_t tll = 0;
        int32_t volume = kMaxAttIndex;
        uint32_t sl = 0;
        uint8_t eg_sh_dp = 0, eg_sel_dp = 0;
        uint8_t eg_sh_ar = 0, eg_sel_ar = 0;
        uint8_t eg_sh_dr = 0, eg_sel_dr = 0;
        uint8_t eg_sh_rr = 0, eg_sel_rr = 0;
        uint8_t eg_sh_rs = 0, eg_sel_rs = 0;
        uint32_t key = 0;
        uint32_t am_mask = 0;
        uint8_t vib = 0;
        uint32_t wavetable = 0;
    };

    struct Channel {
        Slot slot[2];
        uint32_t block_fnum = 0;
        uint32_t fc = 0;
        uint32_t ksl_base = 0;
        uint8_t kcode = 0;
        uint8_t sus = 0;
    };

    void init_tables();
    void write_int(uint8_t reg, uint8_t value);
    void update_instrument_zero(uint8_t r);
    void set_mul(int slot, uint8_t v);
    void calc_fcslot(int ch, int slot);
    void set_ksl_tl(int chan, uint8_t v);
    void set_ksl_wave_fb(int chan, uint8_t v);
    void set_ar_dr(int slot, uint8_t v);
    void set_sl_rr(int slot, uint8_t v);
    void load_instrument(int chan, int slot, const uint8_t* inst);
    void key_on(int slot, int chan, uint32_t key_set);
    void key_off(int slot, int chan, uint32_t key_clr);
    void advance_lfo();
    void chan_calc(int ch);
    void rhythm_calc(uint32_t noise);
    int32_t volume_calc(int ch, int slot) const;
    int32_t op_calc1(uint32_t phase, uint32_t env, int32_t pm, uint32_t wave_tab) const;
    int32_t op_calc(uint32_t phase, uint32_t env, int32_t pm, uint32_t wave_tab) const;
    void advance();

    uint32_t clock_;
    float amplitude_;
    uint8_t addr_ = 0;

    std::array<Channel, 9> ch_{};
    std::array<uint8_t, 9> instvol_r_{};
    uint32_t eg_cnt_ = 0;
    uint32_t eg_timer_ = 0;
    uint32_t eg_timer_add_ = 0;
    uint32_t eg_timer_overflow_ = 0;
    uint8_t rhythm_ = 0;

    uint32_t lfo_am_ = 0;
    int32_t lfo_pm_ = 0;
    uint32_t lfo_am_cnt_ = 0;
    uint32_t lfo_am_inc_ = 0;
    uint32_t lfo_pm_cnt_ = 0;
    uint32_t lfo_pm_inc_ = 0;

    uint32_t noise_rng_ = 1;
    uint32_t noise_p_ = 0;
    uint32_t noise_f_ = 0;

    std::array<std::array<uint8_t, 8>, 19> inst_tab_{};
    int32_t output_[2] = {};

    // Shared lookup tables (built once).
    static bool tables_ready_;
    static std::array<int32_t, kTlTabLen> tl_tab_;
    static std::array<uint32_t, kSinLen * 2> sin_tab_;
    static std::array<uint32_t, 1024> fn_tab_;
    static std::array<uint32_t, 8 * 16> ksl_tab_;
};

}  // namespace dsp
