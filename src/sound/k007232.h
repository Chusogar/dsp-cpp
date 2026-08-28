#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <vector>

namespace dsp {

// Konami K007232 PCM chip, ported from k007232.pas.
class K007232 {
public:
    using VolumeCallback = std::function<void(uint8_t value)>;

    K007232(uint32_t clock, std::vector<uint8_t> rom, float amplify = 0.5f,
            VolumeCallback cb = {}, bool stereo = false);

    void reset();
    void write(uint8_t address, uint8_t value);
    uint8_t read(uint8_t address) const;
    void set_volume(int channel, uint8_t vol_a, uint8_t vol_b);
    void set_bank(int bank_a, int bank_b);

    // Generate one sample at the host mix rate (called at YM2151 sample rate).
    int32_t update();

private:
    static constexpr int kChannels = 2;
    static constexpr int kBaseShift = 12;

    uint32_t clock_;
    std::vector<uint8_t> rom_;
    float amplify_;
    VolumeCallback callback_;
    bool stereo_;

    std::array<uint8_t, 0x10> wreg_{};
    std::array<uint32_t, kChannels> address_{};
    std::array<uint32_t, kChannels> start_{};
    std::array<uint32_t, kChannels> step_{};
    std::array<uint32_t, kChannels> bank_{};
    std::array<bool, kChannels> play_{};
    std::array<std::array<uint8_t, 2>, kChannels> vol_{};
    std::array<uint32_t, 0x200> fncode_{};

    int64_t sample_accum_ = 0;
};

}  // namespace dsp
