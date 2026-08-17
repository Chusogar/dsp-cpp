#pragma once

#include <array>
#include <cstdint>
#include <functional>

namespace dsp {

// Ricoh 2A03 APU, ported from n2a03.pas (MAME-derived NES sound).
// `advance()` is the 4-cycle sound_advance timer; `update()` resamples the
// internal ~447 kHz buffer down to 44100 Hz the same way sound_update does.
class NesApu {
public:
    static constexpr int kSampleRate = 44100;
    static constexpr uint32_t kClock = 1789773;

    explicit NesApu(uint32_t clock = kClock);

    void reset();
    uint8_t read(uint16_t address);
    void write(uint16_t address, uint8_t value);

    void set_dpcm_reader(std::function<uint8_t(uint16_t)> reader) {
        dpcm_read_ = std::move(reader);
    }
    void set_irq_handler(std::function<void()> handler) { irq_ = std::move(handler); }

    void advance();   // every 4 CPU cycles
    int16_t update(); // one output sample

    bool frame_irq() const { return frame_irq_; }
    bool frame_irq_timer_enabled() const { return frame_irq_timer_enabled_; }
    bool dmc_irq() const { return dpcm_.irq; }

private:
    struct Square {
        std::array<uint8_t, 4> regs{};
        int vbl_length = 0;
        int freq = 0;
        int phaseacc = 0;
        int env_phase = 0;
        int sweep_phase = 0;
        uint8_t adder = 0;
        uint8_t env_vol = 0;
        bool enabled = false;
        uint8_t output = 0;
    };
    struct Triangle {
        std::array<uint8_t, 4> regs{};
        int linear_length = 0;
        int vbl_length = 0;
        int write_latency = 0;
        int phaseacc = 0;
        uint8_t adder = 0;
        bool counter_started = false;
        bool enabled = false;
        int output = 0;
        bool linear_reload = false;
    };
    struct Noise {
        std::array<uint8_t, 4> regs{};
        uint32_t lfsr = 1;
        int vbl_length = 0;
        int phaseacc = 0;
        int env_phase = 0;
        uint8_t env_vol = 0;
        bool enabled = false;
        uint8_t output = 0;
    };
    struct Dpcm {
        std::array<uint8_t, 4> regs{};
        uint32_t address = 0;
        uint16_t length = 0;
        uint8_t bits_left = 0;
        int phaseacc = 0;
        uint8_t cur_byte = 0;
        bool enabled = false;
        bool irq = false;
        uint8_t output = 0;
        uint8_t vol = 0;
    };

    void apu_regwrite(uint8_t address, uint8_t value);
    void apu_square(int chan);
    void apu_triangle();
    void apu_noise();
    void apu_dpcm();
    void apu_dpcm_reset();
    void build_tables();

    uint32_t clock_ = kClock;
    Square squ_[2];
    Triangle tri_;
    Noise noi_;
    Dpcm dpcm_;
    std::array<uint8_t, 0x18> regs_{};
    int step_mode_ = 4;
    bool frame_irq_ = true;
    bool frame_irq_timer_enabled_ = false;

    std::array<uint32_t, 32> vbl_times_{};
    std::array<uint32_t, 32> sync_times1_{};
    std::array<uint32_t, 128> sync_times2_{};
    std::array<float, 32> square_lut_{};
    float tnd_lut_[16][16][128]{};

    static constexpr int kBufferSize = 200;
    std::array<float, kBufferSize + 1> buffer_{};
    int buffer_pos_ = 1;
    float old_res_ = 0;
    int samps_per_sync_ = 0;

    std::function<uint8_t(uint16_t)> dpcm_read_;
    std::function<void()> irq_;
};

}  // namespace dsp
