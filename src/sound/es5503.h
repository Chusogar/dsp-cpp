#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <vector>

namespace dsp {

// Ensoniq ES5503 "DOC": 32 wavetable oscillators reading 8-bit samples from
// 128 KB of dedicated RAM (the Apple IIGS fits 64 KB). Register map:
//   $00-$1F frequency low       $20-$3F frequency high
//   $40-$5F volume              $60-$7F last sample read (data)
//   $80-$9F wavetable pointer   $A0-$BF control (halt, mode, IE, channel)
//   $C0-$DF bank/table size/resolution
//   $E0 interrupt status        $E1 oscillator enable   $E2 A/D converter
// A sample value of $00 stops the oscillator. Modes: 0 free-run, 1 one-shot,
// 2 sync/AM, 3 swap (start the partner when this one halts).
// Behaviour follows the documented chip semantics (as also implemented by
// MAME's es5503 device).
class Es5503 {
public:
    using IrqHandler = std::function<void(bool)>;

    static constexpr int kNumOscillators = 32;
    static constexpr int kRamSize = 0x10000;

    explicit Es5503(uint32_t clock);

    void set_irq_handler(IrqHandler handler) { irq_handler_ = std::move(handler); }

    void reset();

    uint8_t read(uint8_t reg);
    void write(uint8_t reg, uint8_t value);

    uint8_t ram_read(uint16_t address) const { return ram_[address]; }
    void ram_write(uint16_t address, uint8_t value) { ram_[address] = value; }

    // Output sample rate for the current number of enabled oscillators.
    double output_rate() const { return double(clock_) / 8.0 / double(oscs_enabled_ + 2); }
    // Generates one DOC output sample (all enabled oscillators, mono mix).
    int32_t generate();
    bool irq_asserted() const { return irq_line_; }

private:
    struct Oscillator {
        uint16_t freq = 0;
        uint16_t wtsize = 256;
        uint8_t control = 1;
        uint8_t vol = 0;
        uint8_t data = 0x80;
        uint32_t wavetblpointer = 0;
        uint8_t wavetblsize = 0;
        uint8_t resolution = 0;
        uint32_t accumulator = 0;
        bool irqpend = false;
    };

    void halt_osc(int onum, int type, uint32_t* accumulator, int resshift);
    void set_irq(bool state);

    std::array<uint8_t, kRamSize> ram_{};
    std::array<Oscillator, kNumOscillators> osc_{};
    int oscs_enabled_ = 1;
    uint8_t rege0_ = 0xff;
    bool irq_line_ = false;
    IrqHandler irq_handler_;
    uint32_t clock_;
};

}  // namespace dsp
