#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <vector>

namespace dsp {

// Konami K053260: 4-voice PCM/KADPCM plus main<->sound CPU ports.
// Register map, pitch stepping, SH1 timer and KADPCM match MAME's k053260.cpp.
class K053260 {
public:
    using LineCallback = std::function<void(bool)>;

    static constexpr int kSampleRate = 44100;
    static constexpr int kClocksPerSample = 64;

    explicit K053260(std::vector<uint8_t> rom, uint32_t clock = 3579545);

    void reset();
    void set_sh1_callback(LineCallback cb) { sh1_cb_ = std::move(cb); }

    // Advance the SH1/SH2 divider (16 input clocks per state).
    void tick(int cycles);

    void update(int samples, int16_t* left, int16_t* right);

    uint8_t main_read(uint8_t offset);
    void main_write(uint8_t offset, uint8_t value);

    uint8_t read(uint8_t offset);
    void write(uint8_t offset, uint8_t value);

    bool voice_playing(int ch) const {
        return ch >= 0 && ch < 4 && voice_[size_t(ch)].playing;
    }

private:
    struct Voice {
        uint32_t start = 0;
        uint16_t length = 0;
        uint16_t pitch = 0;
        uint8_t volume = 0;
        uint8_t pan = 0;
        bool loop = false;
        bool kadpcm = false;
        bool reverse = false;
        bool playing = false;
        uint32_t position = 0;
        int32_t counter = 0;
        int32_t output = 0;
        int32_t pan_l = 0;
        int32_t pan_r = 0;

        void update_pan();
        void play(int32_t* mix, const std::vector<uint8_t>& rom);
    };

    void voice_key_on(int ch);
    void voice_key_off(int ch);
    void timer_step();

    std::vector<uint8_t> rom_;
    std::array<Voice, 4> voice_{};
    std::array<uint8_t, 4> portdata_{};
    uint8_t keyon_ = 0;
    uint8_t mode_ = 0;
    uint32_t clock_ = 3579545;
    int timer_state_ = 0;
    int timer_count_ = 0;
    LineCallback sh1_cb_;
};

}  // namespace dsp
