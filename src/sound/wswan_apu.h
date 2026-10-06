#pragma once

#include <array>
#include <cstdint>
#include <functional>

namespace dsp {

// Bandai WonderSwan / WonderSwan Color PSG (ports $80-$9E).
// Four wave channels with optional voice (ch2), sweep (ch3) and noise (ch4),
// plus a simple Hyper Voice path for SPHINX. Wave tables live in shared IRAM
// at `wave_base << 6`. Output is mixed to mono for the SDL front end.
class WswanApu {
public:
    static constexpr int kSampleRate = 24000;
    static constexpr uint32_t kClock = 3072000;

    using MemRead = std::function<uint8_t(uint32_t)>;

    void reset(bool color);
    void set_memory_reader(MemRead reader) { read_mem_ = std::move(reader); }

    uint8_t read(uint16_t port);
    void write(uint16_t port, uint8_t value);

    // Advance one output sample (~kClock/kSampleRate CPU clocks) and return
    // a mono PCM sample.
    int16_t update();

    // Optional sound DMA (WonderSwan Color): call once per CPU cycle budget.
    void step_dma(int cycles);

    uint8_t wave_base() const { return wave_base_; }
    uint8_t control() const { return control_; }

private:
    void advance(int cycles);
    void mix_channel(int ch, int32_t& left, int32_t& right);
    uint8_t wave_sample(int ch, uint8_t pos) const;
    void run_dma_byte();

    bool color_ = true;
    MemRead read_mem_;

    std::array<uint16_t, 4> period_{};
    std::array<uint8_t, 4> volume_{};
    std::array<int32_t, 4> period_counter_{};
    std::array<uint8_t, 4> sample_pos_{};
    std::array<int32_t, 4> last_left_{};
    std::array<int32_t, 4> last_right_{};

    uint8_t sweep_value_ = 0;
    uint8_t sweep_step_ = 0;
    uint8_t sweep_counter_ = 1;
    int32_t sweep_divider_ = 8192;

    uint8_t noise_control_ = 0;
    uint16_t noise_lfsr_ = 0;
    uint8_t voice_volume_ = 0;

    uint8_t control_ = 0;
    uint8_t output_control_ = 0x80;  // headphones connected
    uint8_t wave_base_ = 0;
    uint8_t master_volume_ = 3;

    uint8_t hyper_ctrl_ = 0;
    uint8_t hyper_chan_ctrl_ = 0;
    uint8_t hyper_voice_ = 0;
    int32_t hyper_left_ = 0;
    int32_t hyper_right_ = 0;

    // Sound DMA (SPHINX)
    uint32_t dma_source_ = 0;
    uint32_t dma_length_ = 0;
    uint32_t dma_source_saved_ = 0;
    uint32_t dma_length_saved_ = 0;
    uint8_t dma_control_ = 0;
    int dma_timer_ = 0;

    int32_t cycle_error_ = 0;  // fractional cycles toward next sample
};

}  // namespace dsp
