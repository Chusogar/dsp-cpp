#pragma once

#include <array>
#include <cstdint>
#include <functional>

namespace dsp {

// Sega PCM, after MAME sound/segapcm.cpp.
//
// Two variants share the voice logic:
//  * Discrete: the Hang-On / Space Harrier sound board built from TTL.
//    8 voices, registers at 0x40+8*v (volume, loop, end, delta) and
//    0xc0+8*v (current address, control); no ROM banking; one output sample
//    every 64 clocks.
//  * 315-5218: the custom chip (Enduro Racer, OutRun, ...).  16 voices at
//    8*v and 0x80+8*v, bank bits in the control register; one output sample
//    every 128 clocks.
// The 256-byte register window is mirrored over the whole mapped range.
class SegaPcm {
public:
    using ReadRom = std::function<uint8_t(uint32_t)>;

    enum class Variant { Discrete, Sega315_5218 };

    static constexpr int kBank256 = 11;
    static constexpr int kBank512 = 12;
    static constexpr int kBank12M = 13;
    static constexpr int kSampleRate = 44100;

    explicit SegaPcm(uint32_t clock, float amplitude = 1.0f,
                     Variant variant = Variant::Sega315_5218);

    void set_read_rom(ReadRom handler) { read_rom_ = std::move(handler); }
    void set_bank(uint32_t bank);
    void reset();

    uint8_t read(uint16_t address) const { return ram_[address & 0xff]; }
    void write(uint16_t address, uint8_t value) { ram_[address & 0xff] = value; }

    // Advances one output sample (clock / (voices * 8)).
    void clock();

    // Sum of the voices (32768 = MAME full scale) times `amplitude`; not
    // clamped.
    int32_t left() const { return int32_t(float(out_left_) * amplitude_); }
    int32_t right() const { return int32_t(float(out_right_) * amplitude_); }
    // Mono mix for the SDL frontend.
    int32_t last_sample() const { return (left() + right()) / 2; }

    uint32_t clock_hz() const { return clock_; }
    int voices() const { return variant_ == Variant::Discrete ? 8 : 16; }
    uint32_t tick_rate() const { return clock_ / uint32_t(voices() * 8); }

private:
    ReadRom read_rom_;
    std::array<uint8_t, 0x100> ram_{};
    std::array<uint8_t, 16> low_{};
    uint32_t clock_ = 0;
    Variant variant_;
    uint8_t bankshift_ = 0;
    uint8_t bankmask_ = 0;
    float amplitude_ = 1.0f;
    int32_t out_left_ = 0;
    int32_t out_right_ = 0;
};

}  // namespace dsp
