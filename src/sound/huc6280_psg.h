#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace dsp {

class HuC6280Psg {
public:
    static constexpr int kSampleRate = 44100;
    static constexpr int kChannels = 6;

    explicit HuC6280Psg(uint32_t clock);

    void reset();
    void write(uint8_t port, uint8_t value);
    void update(int cpu_cycles, std::vector<int16_t>& out);

private:
    struct Channel {
        uint16_t frequency = 0;
        uint8_t control = 0;
        uint8_t balance = 0xFF;
        std::array<uint8_t, 32> wave{};
        int wave_index = 0;
        uint8_t noise_ctrl = 0;
        uint32_t phase = 0;
        uint32_t noise_lfsr = 1;
        uint8_t dda_out = 0;
    };

    int16_t volume_scale(int sample, int ch_vol, int bal_nibble) const;

    uint32_t clock_;
    int select_ = 0;
    uint8_t main_balance_ = 0xFF;
    uint8_t lfo_freq_ = 0;
    uint8_t lfo_control_ = 0;
    uint32_t lfo_phase_ = 0;
    std::array<Channel, kChannels> ch_{};
    int64_t accum_ = 0;
};

}  // namespace dsp
