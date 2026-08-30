#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace dsp {

// K053260 PCM sound chip (4 voices) — simplified port of Pascal/MAME
class K053260 {
public:
    explicit K053260(std::vector<uint8_t> rom, uint32_t clock = 3579545);

    void reset();
    void update(int samples, int16_t* left, int16_t* right);

    // Main CPU side (2 registers)
    uint8_t main_read(uint8_t offset);
    void main_write(uint8_t offset, uint8_t value);

    // Sound CPU side
    uint8_t read(uint8_t offset);
    void write(uint8_t offset, uint8_t value);

    static constexpr int kSampleRate = 44100;

private:
    struct Voice {
        uint32_t start = 0;
        uint16_t length = 0;
        uint16_t pitch = 0;
        uint8_t volume = 0;
        uint8_t pan = 0;
        bool loop = false;
        bool kadpcm = false;
        bool playing = false;
        uint32_t position = 0;
        uint16_t counter = 0;
        int8_t output = 0;
    };

    void voice_key_on(int ch);
    void voice_key_off(int ch);
    int16_t decode_sample(Voice& v);

    std::vector<uint8_t> rom_;
    std::array<Voice, 4> voice_{};
    std::array<uint8_t, 4> portdata_{};
    uint8_t keyon_ = 0;
    uint8_t mode_ = 0;
    uint32_t clock_ = 3579545;
    // Accumulators for rate conversion
    double phase_ = 0;
};

}  // namespace dsp
