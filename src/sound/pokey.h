#pragma once

#include <cstdint>
#include <functional>
#include <vector>

namespace dsp {

// Atari POKEY, ported from pokey.pas.
class Pokey {
public:
    using IrqHandler = std::function<void(uint8_t)>;
    using PotRead = std::function<uint8_t(uint8_t)>;

    static constexpr int kChannels = 4;

    explicit Pokey(uint32_t clock, float amplitude = 1.0f);

    void set_irq_handler(IrqHandler handler) { irq_handler_ = std::move(handler); }
    void set_allpot_handler(PotRead handler) { allpot_read_ = std::move(handler); }
    void set_pot_handler(int index, PotRead handler) { pot_read_[index] = std::move(handler); }
    void set_serin_handler(PotRead handler) { serin_read_ = std::move(handler); }

    void reset();

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
    uint32_t output_ = 0;
    int clock_cnt_[3] = {};
    uint32_t p4_ = 0, p5_ = 0, p9_ = 0, p17_ = 0;

    uint8_t potx_[8] = {};
    uint8_t audctl_ = 0, allpot_ = 0, kbcode_ = 0, serin_ = 0;
    uint8_t irqst_ = 0, irqen_ = 0, skstat_ = 0, skctl_ = 0;
    uint8_t pot_counter_ = 0;

    std::vector<uint32_t> poly4_, poly5_, poly9_, poly17_;

    IrqHandler irq_handler_;
    PotRead allpot_read_, serin_read_;
    PotRead pot_read_[8];
};

}  // namespace dsp
