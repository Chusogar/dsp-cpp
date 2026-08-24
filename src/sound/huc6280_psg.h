#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace dsp {

// PSG built into the HuC6280: six wavetable channels with 32 five bit samples
// each, per channel balance on top of the global balance, direct (DDA) output
// and a noise generator on channels 5 and 6. Mixed down to mono, since the
// frontend feeds one channel to SDL.
class HuC6280Psg {
public:
    static constexpr int kSampleRate = 44100;
    static constexpr uint32_t kClock = 3579545;
    static constexpr int kChannels = 6;

    explicit HuC6280Psg(uint32_t clock = kClock);

    void reset();
    // $0800-$0809 in the hardware page.
    void write(uint8_t offset, uint8_t value);
    int16_t update();  // one output sample

private:
    struct Channel {
        uint16_t frequency = 0;
        uint8_t control = 0;
        uint8_t balance = 0;
        uint8_t noise_control = 0;
        uint8_t dda = 0;
        std::array<uint8_t, 32> wave{};
        uint8_t wave_index = 0;
        uint8_t write_index = 0;
        int counter = 0;
        int noise_counter = 0;
        uint32_t noise_shift = 0x0008000;
        uint8_t noise_level = 0;
    };

    double gain(const Channel& channel) const;
    void step(Channel& channel, int cycles, bool has_noise);

    std::array<Channel, kChannels> channels_{};
    uint32_t clock_;
    uint8_t selected_ = 0;
    uint8_t balance_ = 0xff;
    uint8_t lfo_frequency_ = 0;
    uint8_t lfo_control_ = 0;
    int cycles_per_sample_ = 0;
    int cycle_error_ = 0;
};

}  // namespace dsp
