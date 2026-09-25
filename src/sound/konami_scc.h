#pragma once

#include <array>
#include <cstdint>

namespace dsp {

// Konami SCC (051649) wavetable sound chip found in Konami SCC cartridges
// (Nemesis 2/3, Salamander, Metal Gear 2, SD Snatcher...).  Five channels
// play 32-sample signed waveforms; channels 4 and 5 share one waveform.
// Registers (SCC mode, mirrored every 256 bytes in $9800-$9FFF):
//   $00-$7F waveforms 1-4, $80-$89 periods (12 bit), $8A-$8E volumes,
//   $8F channel enable, $90-$9F mirror of $80-$8F, $E0-$FF deformation.
class KonamiScc {
public:
    void reset() {
        wave_ = {};
        period_.fill(0);
        volume_.fill(0);
        phase_.fill(0);
        enable_ = 0;
    }

    uint8_t read(uint8_t reg) const {
        if (reg < 0x80) return uint8_t(wave_[reg >> 5][reg & 31]);
        if (reg >= 0xa0 && reg < 0xc0) return uint8_t(wave_[3][reg & 31]);  // channel 5 view
        return 0xff;
    }

    void write(uint8_t reg, uint8_t value) {
        if (reg < 0x80) {
            wave_[reg >> 5][reg & 31] = int8_t(value);
            return;
        }
        if (reg >= 0xa0) return;  // $A0-$DF read only, $E0-$FF deformation (ignored)
        reg &= 0x8f;
        if (reg < 0x8a) {
            const int ch = (reg - 0x80) >> 1;
            if (reg & 1) period_[size_t(ch)] = uint16_t((period_[size_t(ch)] & 0x0ff) | ((value & 0x0f) << 8));
            else period_[size_t(ch)] = uint16_t((period_[size_t(ch)] & 0xf00) | value);
        } else if (reg < 0x8f) {
            volume_[size_t(reg - 0x8a)] = value & 0x0f;
        } else {
            enable_ = value & 0x1f;
        }
    }

    // Next output sample at `rate` Hz, for a chip clocked at `clock` Hz.
    int32_t update(uint32_t clock, int rate) {
        int32_t out = 0;
        for (int ch = 0; ch < 5; ch++) {
            const uint32_t period = period_[size_t(ch)];
            // Periods below 9 stop the channel on the real chip.
            if (period < 9) continue;
            // One waveform step every period+1 clocks: 32 steps per cycle.
            // Phase is 16.16 fixed point over the 32-sample table.
            const uint64_t step = (uint64_t(clock) << 16) / (uint64_t(period + 1) * uint64_t(rate));
            phase_[size_t(ch)] = uint32_t((phase_[size_t(ch)] + step) & ((32u << 16) - 1));
            if (!((enable_ >> ch) & 1)) continue;
            const int w = ch < 4 ? ch : 3;
            out += int32_t(wave_[size_t(w)][phase_[size_t(ch)] >> 16]) * volume_[size_t(ch)];
        }
        return out;
    }

    bool active() const { return enable_ != 0; }

private:
    std::array<std::array<int8_t, 32>, 4> wave_{};
    std::array<uint16_t, 5> period_{};
    std::array<uint8_t, 5> volume_{};
    std::array<uint32_t, 5> phase_{};
    uint8_t enable_ = 0;
};

}  // namespace dsp
