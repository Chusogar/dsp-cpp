#pragma once

#include <array>
#include <cstdint>
#include <functional>

namespace dsp {

// Game Boy Advance sound: the four Game Boy PSG channels (two pulses with
// envelope and sweep, the 2x32-sample wave channel, noise) plus the two
// Direct Sound 8-bit FIFOs fed by DMA1/DMA2 and clocked by timer 0 or 1.
// Mixed to mono at 32768 Hz (512 CPU cycles per sample).
class GbaApu {
public:
    static constexpr int kSampleRate = 32768;
    static constexpr int kCyclesPerSample = 512;

    void reset();
    // Registers $04000060-$040000AF, `offset` relative to $04000060.
    uint8_t read8(uint32_t offset) const;
    void write8(uint32_t offset, uint8_t value);
    // Timer 0/1 overflowed: the FIFOs clocked by it play their next sample.
    void timer_overflow(int timer);
    // One output sample.
    int16_t sample();

    // Called when FIFO A (0) or B (1) drops to 16 bytes: the driver runs the
    // sound DMA that targets it.
    std::function<void(int)> request_fifo;

    int fifo_count(int ch) const { return fifo_[size_t(ch)].count; }
    bool master_enabled() const { return (regs_[0x24] & 0x80) != 0; }

private:
    struct Square {
        bool enabled = false;
        int duty = 0, duty_pos = 0;
        int length = 0;
        bool length_enable = false;
        int volume = 0, env_period = 0, env_timer = 0;
        bool env_up = false;
        int freq = 0;
        int32_t timer = 0;
        // channel 1 sweep
        int sweep_period = 0, sweep_shift = 0, sweep_timer = 0, shadow = 0;
        bool sweep_down = false, sweep_enabled = false;
    };
    struct Wave {
        bool enabled = false;
        int length = 0;
        bool length_enable = false;
        int freq = 0;
        int32_t timer = 0;
        int pos = 0;
    };
    struct Noise {
        bool enabled = false;
        int length = 0;
        bool length_enable = false;
        int volume = 0, env_period = 0, env_timer = 0;
        bool env_up = false;
        uint16_t lfsr = 0x7FFF;
        int32_t timer = 0;
    };
    struct Fifo {
        std::array<int8_t, 32> data{};
        int read = 0, write = 0, count = 0;
        int8_t current = 0;
        void clear() { read = write = count = 0; current = 0; }
    };

    void trigger_square(int ch);
    void trigger_wave();
    void trigger_noise();
    int sweep_calc(bool update);
    void frame_step();
    void clock_channels();
    int wave_bank_playing() const { return (regs_[0x10] >> 6) & 1; }

    std::array<uint8_t, 0x50> regs_{};
    std::array<std::array<uint8_t, 16>, 2> wave_ram_{};
    std::array<Square, 2> sq_{};
    Wave wave_{};
    Noise noise_{};
    std::array<Fifo, 2> fifo_{};
    int frame_counter_ = 0;
    int frame_step_ = 0;
};

}  // namespace dsp
