#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <utility>

namespace dsp {

// Texas Instruments TMS5220 / TMS5220C LPC speech synthesizer.
//
// The synthesis section produces one sample every 80 input clocks and walks 8
// interpolation periods (IP) of 13 parameter cycles (PC), each of two
// subcycles, exactly as described by US patent 4,331,836. Speech is gated by
// the SPEN / TALK / TALKD latch chain, so a stop frame or an empty FIFO stops
// speech only at the next frame boundary and the energy ramps down instead of
// being cut off. The host side is a 16 byte FIFO behind the /WS, /RS, /READY
// and /INT pins.
//
// Only speak-external (FIFO) speech is modelled; no VSM speech ROM is
// attached, so bits read for a SPEAK command float high and decode as a stop
// frame, like real hardware without a speech ROM.
class Tms5220 {
public:
    using IrqCallback = std::function<void(bool)>;

    enum class Variant { Tms5220, Tms5220C };

    static constexpr int kSampleRate = 44100;
    static constexpr int kInternalRate = 8000;
    static constexpr int kNumK = 10;
    static constexpr int kFifoSize = 16;
    // One synthesized sample per 80 input clocks.
    static constexpr int kClocksPerSample = 80;
    // /READY stays inactive for about 16 clocks after a /WS or /RS falling edge.
    static constexpr int kIoReadyClocks = 16;

    explicit Tms5220(uint32_t clock = 640000, Variant variant = Variant::Tms5220);

    void set_irq_callback(IrqCallback cb) { irq_cb_ = std::move(cb); }

    void reset();
    void write_data(uint8_t value);
    uint8_t status() const;

    // Active-low /WS and /RS. Both low reset a TMS5220C; a falling /WS with
    // /RS high commits the latched data byte after the /READY delay.
    void set_wsq(bool level);
    void set_rsq(bool level);
    void strobe_ws_rs(uint8_t ws_rs);

    bool readyq() const;  // true = /READY inactive (the chip is busy)
    bool intq() const { return !irq_pin_; }

    void set_data_latch(uint8_t value) {
        write_latch_ = value;
        data_latched_ = true;
    }
    void set_volume(float v) { volume_ = v; }
    void set_clock(uint32_t clock) { clock_ = clock ? clock : 1; }
    uint32_t clock() const { return clock_; }

    void tick(int cycles);
    int16_t last_sample() const;
    int16_t update();

    bool talking() const { return talk_status(); }

    // Entry of the voiced excitation ROM (the chirp) used within pitch periods.
    static int8_t voiced_excitation(int index);

private:
    bool has_rate_control() const { return variant_ == Variant::Tms5220C; }
    bool talk_status() const { return spen_ || talkd_; }
    bool new_frame_stop_flag() const { return new_energy_idx_ == 0x0f; }
    bool new_frame_silence_flag() const { return new_energy_idx_ == 0; }
    bool new_frame_unvoiced_flag() const { return new_pitch_idx_ == 0; }

    void chip_reset();
    void data_write(uint8_t value);
    void process_command(uint8_t cmd);
    void update_fifo_status_and_ints();
    void set_interrupt_state(bool state);
    void set_idle_frame();
    int read_bits(int count);
    void parse_frame();
    int32_t lattice_filter();
    void process_sample();
    void advance_counters(bool speaking);
    void apply_rs_ws(bool new_wsq, bool new_rsq);
    void service_io_ready();

    uint32_t clock_;
    Variant variant_;
    IrqCallback irq_cb_;

    // Host interface.
    std::array<uint8_t, kFifoSize> fifo_{};
    int fifo_head_ = 0;
    int fifo_tail_ = 0;
    int fifo_count_ = 0;
    int fifo_bits_taken_ = 0;
    uint8_t write_latch_ = 0;
    bool data_latched_ = false;
    uint8_t rs_ws_ = 0x03;
    bool io_ready_ = true;
    int io_ready_delay_ = 0;
    bool irq_pin_ = false;

    // Control latches.
    bool spen_ = false;   // speech enabled
    bool ddis_ = false;   // speak external (FIFO) mode
    bool talk_ = false;
    bool talkd_ = false;  // TALK delayed by one frame
    bool previous_talk_status_ = false;
    bool buffer_low_ = true;
    bool buffer_empty_ = true;
    uint8_t c_variant_rate_ = 0;

    // Frame parameters.
    int new_energy_idx_ = 0;
    int new_pitch_idx_ = 0;
    std::array<int, kNumK> new_k_idx_{};
    int current_energy_ = 0;
    int previous_energy_ = 0;
    int current_pitch_ = 0;
    std::array<int, kNumK> current_k_{};
    bool zpar_ = false;    // zero all parameters
    bool uv_zpar_ = false; // zero K4-K10 (unvoiced frame)
    bool olde_ = true;     // previous frame was silence
    bool oldp_ = true;     // previous frame was unvoiced
    bool inhibit_ = true;
    bool pitch_zero_ = false;

    // Synthesis section.
    int subcycle_ = 0;
    int pc_ = 0;
    int ip_ = 0;
    int pitch_count_ = 0;
    int excitation_data_ = 0;
    uint16_t rng_ = 0x1fff;
    std::array<int32_t, kNumK + 1> u_{};
    std::array<int32_t, kNumK> x_{};

    int32_t out_sample_ = 0;
    float volume_ = 1.0f;
    int64_t cycle_acc_ = 0;
    uint32_t update_cycle_acc_ = 0;
};

} // namespace dsp
