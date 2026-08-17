#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace dsp {

// Capcom QSound, ported from qsound.pas (16 channels, 4 MHz / 166 sample clock).
class QSound {
public:
    static constexpr int kChannels = 16;
    static constexpr int kClockDiv = 166;
    static constexpr int kSampleRate = 44100;

    explicit QSound(uint32_t sample_rom_size = 0x200000);

    void set_rom(std::vector<uint8_t> rom);
    void reset();

    void write(uint8_t offset, uint8_t data);
    uint8_t read() const { return 0x80; }

    // Advances one internal sample (4000000 / 166 Hz).
    void clock();

    int32_t left() const { return out_left_; }
    int32_t right() const { return out_right_; }
    int32_t mixed() const { return (out_left_ + out_right_) / 2; }

private:
    struct Channel {
        int bank = 0;
        int address = 0;
        int pitch = 0;
        int loop = 0;
        int end_addr = 0;
        int vol = 0;
        int pan = 0;
        int key = 0;
        int lvol = 0;
        int rvol = 0;
        int lastdt = 0;
        int offset = 0;
    };

    void set_command(uint8_t data, uint16_t value);

    std::array<Channel, kChannels> channels_{};
    uint16_t data_ = 0;
    std::vector<uint8_t> rom_;
    uint32_t rom_mask_ = 0;
    std::array<int, 33> pan_table_{};
    int32_t out_left_ = 0;
    int32_t out_right_ = 0;
};

}  // namespace dsp
