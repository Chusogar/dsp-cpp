#pragma once

#include <cstdint>
#include <functional>

namespace dsp {

// Yamaha YM2151 (OPM), ported from fm_2151.pas.
class YM2151 {
public:
    using IrqHandler = std::function<void(bool)>;
    using PortHandler = std::function<void(uint8_t)>;

    static constexpr int kSampleRate = 44100;

    explicit YM2151(uint32_t clock, float amplitude = 1.0f);

    YM2151(const YM2151&) = delete;
    YM2151& operator=(const YM2151&) = delete;

    void set_irq_handler(IrqHandler handler) { irq_handler_ = std::move(handler); }
    void set_port_handler(PortHandler handler) { port_handler_ = std::move(handler); }

    void reset();

    void select_register(uint8_t reg) { last_register_ = reg; }
    void write(uint8_t data) { write_reg(last_register_, data); }
    void write_reg(uint8_t reg, uint8_t data);
    uint8_t status() const { return uint8_t(status_); }

    // Advances the two internal timers by `cycles` of the chip clock.
    void run_timers(int cycles);

    // Generates the next mixed sample (sample rate is kSampleRate).
    int32_t update();
    int32_t last_left() const { return out_left_; }
    int32_t last_right() const { return out_right_; }

private:
    struct Operator {
        uint32_t phase;
        uint32_t freq;
        int32_t dt1;
        uint32_t mul;
        uint32_t dt1_i;
        uint32_t dt2;
        int32_t* connect;
        int32_t* mem_connect;
        int32_t mem_value;
        uint32_t fb_shift;
        int32_t fb_out_curr;
        int32_t fb_out_prev;
        uint32_t kc;
        uint32_t kc_i;
        uint32_t pms;
        uint32_t ams;
        uint32_t am_mask;
        uint32_t state;
        uint8_t eg_sh_ar, eg_sel_ar;
        uint32_t tl;
        int32_t volume;
        uint8_t eg_sh_d1r, eg_sel_d1r;
        uint32_t d1l;
        uint8_t eg_sh_d2r, eg_sel_d2r;
        uint8_t eg_sh_rr, eg_sel_rr;
        uint32_t key;
        uint32_t ks;
        uint32_t ar, d1r, d2r, rr;
    };

    static int32_t op_calc(const Operator& op, uint32_t env, int32_t pm);
    static int32_t op_calc1(const Operator& op, uint32_t env, int32_t pm);
    static uint32_t volume_calc(const Operator& op, uint32_t am);

    void init_chip_tables();
    void key_on(int op_index, uint32_t key_set);
    static void key_off(Operator& op, uint32_t key_clr);
    void envelope_konkoff(int op_index, uint8_t v);
    void set_connect(int op_index, int channel, uint8_t v);
    void refresh_eg(int op_index);
    void chan_calc(int channel);
    void chan7_calc();
    void advance_eg();
    void advance();
    void timer_a_expired();
    void timer_b_expired();
    void set_irq_bit(uint8_t bit);
    void clear_irq_bit(uint8_t bit);

    uint32_t clock_;
    uint32_t sample_rate_ = kSampleRate;
    float amplitude_;

    Operator oper_[32] = {};
    int32_t chanout_[8] = {};
    int32_t m2_ = 0, c1_ = 0, c2_ = 0, mem_ = 0;
    uint32_t pan_[16] = {};
    uint32_t eg_cnt_ = 0, eg_timer_ = 0, eg_timer_add_ = 0, eg_timer_overflow_ = 0;
    uint32_t lfo_phase_ = 0, lfo_timer_ = 0, lfo_timer_add_ = 0, lfo_overflow_ = 0;
    uint32_t lfo_counter_ = 0, lfo_counter_add_ = 0;
    uint8_t lfo_wsel_ = 0, amd_ = 0;
    int16_t pmd_ = 0;
    uint32_t lfa_ = 0;
    int32_t lfp_ = 0;
    uint8_t test_ = 0, ct_ = 0;
    uint32_t noise_ = 0, noise_rng_ = 0, noise_p_ = 0, noise_f_ = 0;
    uint32_t csm_req_ = 0, irq_enable_ = 0, status_ = 0;
    uint8_t connect_[8] = {};
    uint8_t irq_line_state_ = 0;
    uint8_t last_register_ = 0;

    bool timer_a_enabled_ = false, timer_b_enabled_ = false;
    int32_t timer_a_counter_ = 0, timer_b_counter_ = 0;
    uint32_t timer_a_index_ = 0, timer_b_index_ = 0;

    uint32_t freq_[11 * 768] = {};
    int32_t dt1_freq_[8 * 32] = {};
    uint32_t noise_tab_[32] = {};

    int32_t out_left_ = 0, out_right_ = 0;

    IrqHandler irq_handler_;
    PortHandler port_handler_;
};

}  // namespace dsp
