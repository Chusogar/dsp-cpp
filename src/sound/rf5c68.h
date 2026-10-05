#pragma once

#include <array>
#include <cstdint>

namespace dsp {

// Ricoh RF5C68 PCM, after MAME sound/rf5c68.cpp. Eight channels of 8-bit
// sign/magnitude PCM with per-channel envelope, pan, step and loop; 64 KB of
// sample RAM banked in 4 KB windows at the host's 0x1000-0x1fff window.
class Rf5c68 {
public:
    static constexpr int kSampleRate = 44100;
    static constexpr int kChannels = 8;
    static constexpr uint32_t kRamSize = 0x10000;

    explicit Rf5c68(uint32_t clock = 10000000, float amplitude = 1.0f);

    void reset();

    void write_reg(uint8_t offset, uint8_t data);
    uint8_t read_mem(uint16_t offset) const;
    void write_mem(uint16_t offset, uint8_t data);

    // Advances one host audio sample and returns a mono mix.
    int32_t update();

    uint8_t* ram() { return ram_.data(); }
    const uint8_t* ram() const { return ram_.data(); }

private:
    struct Channel {
        uint8_t enable = 0;
        uint8_t env = 0;
        uint8_t pan = 0;
        uint8_t start = 0;
        uint32_t addr = 0;
        uint16_t step = 0;
        uint16_t loopst = 0;
    };

    void generate_chip_sample(int32_t& left, int32_t& right);

    uint32_t clock_ = 10000000;
    float amplitude_ = 1.0f;
    std::array<Channel, kChannels> chan_{};
    std::array<uint8_t, kRamSize> ram_{};
    uint8_t cbank_ = 0;
    uint16_t wbank_ = 0;
    uint8_t enable_ = 0;
    double sample_pos_ = 0;
    double sample_step_ = 0;
    int32_t last_left_ = 0;
    int32_t last_right_ = 0;
};

}  // namespace dsp
