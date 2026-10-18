#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace dsp {

// Atari POKEY, ported from pokey.pas.
class Pokey {
public:
    using IrqHandler = std::function<void(uint8_t)>;
    using PotRead = std::function<uint8_t(uint8_t)>;

    static constexpr int kChannels = 4;

    explicit Pokey(uint32_t clock, float amplitude = 1.0f);
    ~Pokey();
    Pokey(Pokey&&) noexcept = default;
    Pokey& operator=(Pokey&&) noexcept = default;

    void set_irq_handler(IrqHandler handler) { irq_handler_ = std::move(handler); }
    void set_allpot_handler(PotRead handler) { allpot_read_ = std::move(handler); }
    void set_pot_handler(int index, PotRead handler) { pot_read_[index] = std::move(handler); }
    void set_serin_handler(PotRead handler) { serin_read_ = std::move(handler); }

    void reset();
    // Testing: false runs every clock of a silent chip too (see run()).
    void set_fast_forward(bool on) { fast_forward_on_ = on; }

    // Presents a key to the keyboard scanner: `code` is the Atari KBCODE
    // (scan code in bits 0-5, shift in bit 6, control in bit 7), and
    // `pressed` drives the "key still down" bit SKSTAT reports. Raises the
    // keyboard interrupt when IRQEN enables it, as the real scanner does.
    void set_key(uint8_t code, bool pressed, bool shift_held);
    // The BREAK key has its own IRQEN/IRQST bit rather than a scan code.
    void press_break();

    uint8_t read(uint16_t offset);
    void write(uint16_t offset, uint8_t data);

    // Runs `cycles` of the chip clock.
    void run(int cycles);

    // Returns the current mixed output as a signed sample.
    int32_t update() const;

private:
    struct Channel {
        uint8_t int_mask = 0;
        uint8_t audf = 0;
        uint8_t audc = 0;
        int borrow_cnt = 0;
        int counter = 0;
        uint8_t output = 0;
        uint8_t filter_sample = 0;

        void sample() { filter_sample = output; }
        void reset_counter() { counter = audf ^ 0xff; }
        bool check_borrow();
    };

    void inc_chan(Channel& channel);
    void step_one_clock();

    void process_channel(int channel);
    void step_pot();
    void potgo();
    void write_internal(uint16_t offset, uint8_t data);

    uint32_t clock_;
    float amplitude_;

    Channel channel_[kChannels];
    // Mixer nibbles; stale once clocks ran, recomputed by update() (and kept by
    // write() before a register changes).
    mutable uint32_t output_ = 0;
    mutable bool output_stale_ = false;
    uint32_t compute_output() const;
    uint8_t borrowing_ = 0;  // channels counting a borrow down
    // Clocks of a silent chip not run yet (see run()).
    bool silent() const;
    void catch_up();
    void run_clocks(int cycles);
    bool fast_forward(uint32_t clocks);
    static constexpr uint32_t kMaxPlanClocks = 256;
    struct PairPlan;
    bool plan_pair(int lead, int slave, uint32_t clocks, PairPlan& plan) const;
    template <typename Take>
    static uint32_t last_event(const PairPlan& plan, int who, uint32_t limit, Take take);
    uint32_t deferred_ = 0;
    bool fast_forward_on_ = true;
    std::unique_ptr<PairPlan> plan_a_, plan_b_;  // made when first needed
    int clock_cnt_[3] = {};
    // Polynomial counters, brought up to ticks_ (clocks they have run) when
    // read: table positions of the 4/5/9-bit ones, the 17-bit one's state.
    void reset_polys();
    uint32_t poly4();
    uint32_t poly5();
    uint32_t poly9();
    void sync_poly17();
    uint32_t poly17_bit0();
    uint32_t poly17_byte();
    uint32_t ticks_ = 0;
    uint32_t i4_ = 0, i5_ = 0, i9_ = 0, i17_ = 0;
    const uint32_t* bits17_ = nullptr;  // shared bit-16 sequence of the 17-bit LFSR
    uint32_t at4_ = 0, at5_ = 0, at9_ = 0, at17_ = 0;

    uint8_t potx_[8] = {};
    uint8_t audctl_ = 0, allpot_ = 0, kbcode_ = 0, serin_ = 0;
    uint8_t irqst_ = 0, irqen_ = 0, skstat_ = 0, skctl_ = 0;
    uint8_t pot_counter_ = 0;

    IrqHandler irq_handler_;
    PotRead allpot_read_, serin_read_;
    PotRead pot_read_[8];
};

}  // namespace dsp
