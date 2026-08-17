#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace dsp {

// OKI MSM6295 ADPCM sample player, ported from oki6295.pas.
class OKIM6295 {
public:
    static constexpr int kVoices = 4;

    // `pin7_high` selects the /132 divider instead of /165.
    OKIM6295(uint32_t clock, bool pin7_high);

    void set_rom(std::vector<uint8_t> rom) { rom_ = std::move(rom); }
    // CPS1 sound maps $F006 to pin 7 (divisor 132 when high, 165 when low).
    void set_pin7(bool high) { divisor_ = high ? 132 : 165; }

    void reset();

    uint8_t read() const;
    void write(uint8_t value);

    // Frequency of the internal sample generator.
    uint32_t sample_frequency() const { return clock_ / uint32_t(divisor_); }

    // Generates the next sample, in the 16 bit range used by the other chips.
    int32_t update();

private:
    struct Voice {
        bool playing = false;
        uint32_t base_offset = 0;
        uint32_t sample = 0;
        uint32_t count = 0;
        int signal = -2;
        int step = 0;
        uint8_t volume = 0;
    };

    void reset_adpcm(Voice& voice);
    int clock_adpcm(Voice& voice, uint8_t nibble);
    int generate_adpcm(Voice& voice);

    uint32_t clock_;
    int divisor_;
    std::vector<uint8_t> rom_;
    std::array<Voice, kVoices> voices_{};
    int command_ = -1;
};

}  // namespace dsp
