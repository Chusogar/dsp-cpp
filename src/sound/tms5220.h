#pragma once

#include <array>
#include <cstdint>
#include <functional>

namespace dsp {

// TMS5220 with full LPC-10 lattice (tables + interpolation + chirp).
class Tms5220 {
public:
    using IrqCallback = std::function<void(bool)>;

    static constexpr int kSampleRate = 44100;
    static constexpr int kNumK = 10;
    static constexpr int kInterpSteps = 8;

    explicit Tms5220(uint32_t clock = 640000);

    void set_irq_callback(IrqCallback cb) { irq_cb_ = std::move(cb); }

    void reset();
    void write_data(uint8_t value);
    uint8_t status() const;
    // Active-low strobes (Gauntlet / System 1 sound board)
    void set_wsq(bool level);   // write strobe: falling edge commits data_latch_
    void set_rsq(bool level);   // reset strobe: low holds chip in reset
    bool readyq() const;        // active-low ready (true = not ready / busy)
    void set_data_latch(uint8_t value) { data_latch_ = value; }
    void set_volume(float v) { volume_ = v; }

    void tick(int cycles);
    int16_t update();

    bool talking() const { return talk_status_; }

private:
    void process_command(uint8_t cmd);
    bool parse_frame();
    void interpolate();
    int16_t lattice(int16_t excitation);
    uint32_t extract_bits(int n);
    void raise_irq(bool on);
    void fifo_push(uint8_t v);
    uint8_t fifo_pop();

    uint32_t clock_;
    IrqCallback irq_cb_;

    std::array<uint8_t, 16> fifo_{};
    int fifo_head_ = 0, fifo_tail_ = 0, fifo_count_ = 0;
    uint32_t bit_buffer_ = 0;
    int bits_left_ = 0;

    bool speak_external_ = false;
    bool talk_status_ = false;

    int old_energy_idx_ = 0, new_energy_idx_ = 0;
    int old_pitch_idx_ = 0, new_pitch_idx_ = 0;
    std::array<int, kNumK> old_k_idx_{};
    std::array<int, kNumK> new_k_idx_{};

    int current_energy_ = 0;
    int current_pitch_ = 0;
    std::array<int, kNumK> current_k_{};

    int interp_step_ = 0;
    int sample_in_subframe_ = 0;
    int pitch_count_ = 0;
    int32_t rng_ = 1;
    std::array<int32_t, kNumK + 1> u_{};
    std::array<int32_t, kNumK + 1> x_{};

    int32_t out_sample_ = 0;
    int64_t cycle_acc_ = 0;
    int internal_rate_ = 8000;
    uint8_t data_latch_ = 0;
    bool wsq_ = true;
    bool rsq_ = true;
    float volume_ = 1.0f;
};

}  // namespace dsp
