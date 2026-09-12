#pragma once

#include <array>
#include <cstdint>

namespace dsp {

// Philips SAA1099 6-channel stereo square-wave synthesizer, as used by the
// SAM Coupe (register-addressed: write the register number to the
// "address" port, then the value to the "data" port).
//
// 6 tone channels (3 per stereo side, sharing 2 noise generators and 2
// envelope generators), each with independent left/right amplitude.
class Saa1099 {
public:
    explicit Saa1099(uint32_t clock);

    void reset();
    // Selects which of the 32 internal registers a following write() targets.
    void select(uint8_t reg) { addr_ = reg & 0x1f; }
    void write(uint8_t value);

    // Advances one sample period (sample rate is kSampleRate) and returns
    // the mixed (left, right) pair, each roughly +/-8000.
    void update(int32_t& left, int32_t& right);

    static constexpr int kSampleRate = 44100;

private:
    struct Channel {
        uint8_t amplitude = 0;   // bits0-3 right, bits4-7 left
        uint16_t frequency = 0;  // 8-bit code, 0-255
        uint8_t octave = 0;      // 0-7
        bool tone_enable = false;
        bool noise_enable = false;
        double phase = 0.0;
        bool level = false;
    };

    struct Noise {
        uint8_t freq_select = 0;  // 0-3 (3 = tied to channel frequency, simplified to fixed rate)
        uint32_t lfsr = 0x1ffff;
        double phase = 0.0;
        bool level = false;
    };

    struct Envelope {
        bool enabled = false;
        bool right_channel = false;  // which stereo side this envelope drives
        uint8_t resolution4bit = 0;
        uint8_t waveform = 0;
        bool invert_right = false;
        int step = 0;
        bool active = true;
    };

    double channel_freq_hz(const Channel& ch) const;

    uint32_t clock_;
    uint8_t addr_ = 0;
    bool enabled_ = false;
    std::array<Channel, 6> channels_{};
    std::array<Noise, 2> noise_{};
    std::array<Envelope, 2> envelope_{};
};

}  // namespace dsp
