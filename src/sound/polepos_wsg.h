#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace dsp {

// Namco Pole Position 8-voice WSG. Clock is MASTER_CLOCK/512. Voices whose
// waveform-select bit 3 is set mux a 52xx/54xx DAC through that voice's gain
// instead of a wavetable.
class PolePosWsg {
public:
    static constexpr int kSampleRate = 44100;
    static constexpr int kVoices = 8;

    explicit PolePosWsg(uint32_t clock);

    void set_wavetable(const uint8_t* data, size_t size);
    void reset();
    void sound_enable(bool enabled) { enabled_ = enabled; }

    uint8_t read(uint8_t offset) const;
    void write(uint8_t offset, uint8_t data);

    // Mixes one 44.1 kHz sample. DAC nibble arguments are 0-15 (52/54xx).
    int32_t update(int chanl1, int chanl2, int chanl3, int chanl4);

private:
    struct Voice {
        uint32_t frequency = 0;
        uint32_t counter = 0;
        int volume[4] = {};
        uint8_t waveform_select = 0;
        bool external = false;
        uint8_t ext_sel = 0;
    };

    int waveform(uint8_t select, uint8_t pos) const;
    void recompute(int ch);

    uint32_t clock_;
    int32_t namco_clock_ = 0;
    int f_fracbits_ = 17;
    bool enabled_ = false;
    std::array<uint8_t, 0x40> regs_{};
    std::array<Voice, kVoices> voices_{};
    std::vector<uint8_t> wave_;
};

}  // namespace dsp
