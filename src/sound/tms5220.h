#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <utility>

namespace dsp {

class Tms5220 {
public:
    using IrqCallback = std::function<void(bool)>;
    static constexpr int kSampleRate = 44100;
    static constexpr int kInternalRate = 8000;
    static constexpr int kNumK = 10;
    explicit Tms5220(uint32_t clock = 640000);
    void set_irq_callback(IrqCallback cb) { irq_cb_ = std::move(cb); }
    void reset();
    void write_data(uint8_t value);
    uint8_t status() const;
    void set_wsq(bool level);
    void set_rsq(bool level);
    void strobe_ws_rs(uint8_t ws_rs);
    bool readyq() const;
    bool intq() const { return !irq_asserted_; }
    void set_data_latch(uint8_t value) { data_latch_ = value; data_pending_ = true; }
    void set_volume(float v) { volume_ = v; }
    void set_clock(uint32_t clock) { clock_ = clock ? clock : 1; }
    uint32_t clock() const { return clock_; }
    void tick(int cycles);
    int16_t last_sample() const;
    int16_t update();
    bool talking() const { return talk_status_; }
private:
    void process_command(uint8_t cmd);
    bool parse_frame();
    int16_t lattice(int16_t excitation);
    uint32_t extract_bits(int count);
    void raise_irq(bool on);
    void chip_reset();
    void apply_rs_ws(bool new_wsq, bool new_rsq);
    void update_fifo_flags(bool edge_irq = true);
    void generate_sample();
    uint32_t clock_; IrqCallback irq_cb_;
    std::array<uint8_t,16> fifo_{}; int fifo_head_=0,fifo_tail_=0,fifo_count_=0;
    uint8_t bit_buffer_=0; int bits_left_=0;
    bool speak_external_=false,talk_status_=false,stop_pending_=false;
    uint8_t c_variant_rate_=0;
    int old_energy_idx_=0,new_energy_idx_=0,old_pitch_idx_=0,new_pitch_idx_=0;
    std::array<int,kNumK> old_k_idx_{},new_k_idx_{};
    int current_energy_=0,previous_energy_=0,current_pitch_=0; std::array<int,kNumK> current_k_{};
    int ip_=0,pc_=0,subcycle_=0,pitch_count_=0; bool inhibit_=true;
    bool old_unvoiced_=true,old_silence_=true,zpar_=true,uv_zpar_=true;
    uint16_t rng_=0x1fff; std::array<int32_t,kNumK+1> u_{},x_{};
    int32_t out_sample_=0; int64_t cycle_acc_=0; uint32_t update_cycle_acc_=0;
    uint8_t data_latch_=0; bool data_pending_=false,wsq_=true,rsq_=true,rs_read_=true,irq_asserted_=false;
    int ready_delay_=0; float volume_=1.0f; bool buffer_low_=true,buffer_empty_=true;
};
} // namespace dsp
