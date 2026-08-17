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
    // Active-low /WS and /RS (TMS5220C). Both low resets the chip; a falling
    // /WS with /RS high commits data_latch_; a falling /RS with /WS high is a
    // status read. Gauntlet and Atari System 1 share this pin protocol.
    void set_wsq(bool level);
    void set_rsq(bool level);
    // EXL-100 I/O CPU: bit0=WS, bit1=RS, both active-low. RS is a status
    // read strobe (not a reset). Either edge makes /RDY busy for a few cycles.
    void strobe_ws_rs(uint8_t ws_rs);
    bool readyq() const;        // /READY pin low (true = not ready / busy)
    bool intq() const { return !irq_asserted_; }  // /INT pin high = no IRQ
    void set_data_latch(uint8_t value) { data_latch_ = value; }
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
    void interpolate();
    int16_t lattice(int16_t excitation);
    uint32_t extract_bits(int n);
    void raise_irq(bool on);
    void chip_reset();
    void apply_rs_ws(bool new_wsq, bool new_rsq);
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
    bool rs_read_ = true;
    bool irq_asserted_ = false;
    int ready_delay_ = 0;
    float volume_ = 1.0f;
};

}  // namespace dsp
