#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace dsp {

// Sanyo VLM5030 LPC speech synthesizer, ported from dsp-emulator `vlm_5030.pas`.
// The internal stream runs at clock/440 (~8 kHz with the Track & Field crystal);
// `update()` returns the last generated sample for mixing at 44100 Hz.
class Vlm5030 {
public:
    static constexpr int kSampleRate = 44100;
    static constexpr int kNumK = 10;

    explicit Vlm5030(uint32_t clock = 3579545, size_t rom_size = 0x2000, float amplitude = 4.0f);

    void reset();
    void set_rom(const std::vector<uint8_t>& rom);
    uint8_t* rom_data() { return rom_.data(); }
    size_t rom_size() const { return rom_.size(); }

    uint8_t get_bsy() const { return pin_bsy_; }
    void data_w(uint8_t data);
    void set_st(uint8_t pin);
    void set_rst(uint8_t pin);
    void update_vcu(uint8_t pin);

    // Generate one internal sample (called every clock/440 sound-CPU cycles).
    void update_stream();
    int32_t update() const { return out_; }

    uint32_t clock() const { return clock_; }
    // Sound-CPU cycles per internal sample when the VLM clock equals the Z80
    // clock, matching Pascal `cpu_clock / (clock / 440)`.
    int cycles_per_sample(uint32_t cpu_clock) const;

private:
    enum Phase : uint8_t {
        PhReset = 0,
        PhIdle = 1,
        PhSetup = 2,
        PhWait = 3,
        PhRun = 4,
        PhStop = 5,
        PhEnd = 6,
    };

    uint8_t rom_byte(uint32_t address) const;
    uint16_t get_bits(uint8_t sbit, uint8_t bits) const;
    int parse_frame();
    void setup_parameter(uint8_t param);

    uint32_t clock_ = 3579545;
    float amplitude_ = 4.0f;
    std::vector<uint8_t> rom_;
    uint32_t address_mask_ = 0;
    uint16_t address_ = 0;
    uint8_t pin_bsy_ = 0;
    uint8_t pin_st_ = 0;
    uint8_t pin_vcu_ = 0;
    uint8_t pin_rst_ = 0;
    uint8_t latch_data_ = 0;
    uint16_t vcu_addr_h_ = 0;
    uint8_t parameter_ = 0;
    uint8_t phase_ = PhIdle;
    int frame_size_ = 0;
    int pitch_offset_ = 0;
    uint8_t interp_step_ = 0;
    int interp_count_ = 0;
    uint8_t sample_count_ = 0;
    uint8_t pitch_count_ = 0;
    uint16_t old_energy_ = 0;
    uint8_t old_pitch_ = 0;
    std::array<int, kNumK> old_k_{};
    uint16_t target_energy_ = 0;
    uint8_t target_pitch_ = 0;
    std::array<int, kNumK> target_k_{};
    uint16_t new_energy_ = 0;
    uint8_t new_pitch_ = 0;
    std::array<int, kNumK> new_k_{};
    uint32_t current_energy_ = 0;
    uint32_t current_pitch_ = 0;
    std::array<int, kNumK> current_k_{};
    std::array<int, kNumK> x_{};
    int32_t out_ = 0;
    uint32_t rng_ = 1;
};

}  // namespace dsp
