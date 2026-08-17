#pragma once

#include <array>
#include <cstdint>
#include <functional>

namespace dsp {

// Sega PCM (315-5218), 16 channels. Ported from sega_pcm.pas.
class SegaPcm {
public:
    using ReadRom = std::function<uint8_t(uint32_t)>;

    static constexpr int kBank256 = 11;
    static constexpr int kBank512 = 12;
    static constexpr int kBank12M = 13;
    static constexpr int kSampleRate = 44100;

    explicit SegaPcm(uint32_t clock, float amplitude = 1.0f);

    void set_read_rom(ReadRom handler) { read_rom_ = std::move(handler); }
    void set_bank(uint32_t bank);
    void reset();

    uint8_t read(uint16_t address) const { return ram_[address & 0x7ff]; }
    void write(uint16_t address, uint8_t value) { ram_[address & 0x7ff] = value; }

    // Advances one chip tick (clock / 128), matching internal_update_segapcm.
    void clock();

    int32_t left() const { return int32_t(float(out_left_) * amplitude_); }
    int32_t right() const { return int32_t(float(out_right_) * amplitude_); }
    // Mono mix for the SDL frontend.
    int32_t last_sample() const { return (left() + right()) / 2; }

    uint32_t clock_hz() const { return clock_; }
    uint32_t tick_rate() const { return clock_ / 128; }

private:
    ReadRom read_rom_;
    std::array<uint8_t, 0x800> ram_{};
    std::array<uint8_t, 16> low_{};
    uint32_t clock_ = 0;
    uint8_t bankshift_ = 0;
    uint8_t bankmask_ = 0;
    float amplitude_ = 1.0f;
    int32_t out_left_ = 0;
    int32_t out_right_ = 0;
};

}  // namespace dsp
