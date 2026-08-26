#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

namespace dsp {

// Namco 3-voice Waveform Sound Generator, as used on the Galaga/Dig Dug/
// Xevious/Bosconian era hardware (registers at $6800-$681F on the main CPU).
//
// 32 four-bit registers: voice N's running position counter, waveform
// select and step (frequency) live at fixed offsets; see set().  Each tick
// (paced by the caller at clock/32) every voice's counter is advanced by its
// step, the top 5 bits of the 20-bit counter select a sample from one of
// eight 32-step 4-bit waveforms stored in an external PROM, and the signed
// sample is scaled by the voice's 4-bit volume.
class NamcoWsg {
public:
    static constexpr int kSampleRate = 96000;  // typical CPU clock (3.072MHz) / 32

    explicit NamcoWsg(uint32_t clock) : clock_(clock) {}

    void set_waveform_rom(std::vector<uint8_t> rom) { rom_ = std::move(rom); }

    void reset() {
        regs_.fill(0);
    }

    // CPU-side register write ($6800 + n, n = 0..0x1f).
    void write(int reg, uint8_t value) {
        if (reg >= 0 && reg < int(regs_.size())) regs_[size_t(reg)] = uint8_t(value & 0x0f);
    }
    uint8_t read(int reg) const { return reg >= 0 && reg < int(regs_.size()) ? regs_[size_t(reg)] : 0; }

    uint32_t clock() const { return clock_; }

    // Advance every voice by one tick (clock/32) and return the mixed
    // sample.
    int16_t update() {
        int32_t mix = 0;
        for (int v = 0; v < 3; v++) mix += update_voice(v);
        return int16_t(std::max(-32768, std::min(32767, mix)));
    }

private:
    // Register layout (nibble offsets within regs_):
    //   voice0: counter 0x00-0x04, waveform 0x05, freq 0x10-0x14, volume 0x15
    //   voice1: counter 0x06-0x09, waveform 0x0a, freq 0x16-0x19, volume 0x1a (LSB nibble fixed 0)
    //   voice2: counter 0x0b-0x0e, waveform 0x0f, freq 0x1b-0x1e, volume 0x1f (LSB nibble fixed 0)
    int32_t update_voice(int voice) {
        const int counter_base = voice == 0 ? 0x00 : (voice == 1 ? 0x06 : 0x0b);
        const int counter_nibbles = voice == 0 ? 5 : 4;
        const int freq_base = voice == 0 ? 0x10 : (voice == 1 ? 0x16 : 0x1b);
        const int waveform_reg = voice == 0 ? 0x05 : (voice == 1 ? 0x0a : 0x0f);
        const int volume_reg = voice == 0 ? 0x15 : (voice == 1 ? 0x1a : 0x1f);

        uint32_t counter = 0;
        uint32_t freq = 0;
        const int shift_bias = voice == 0 ? 0 : 4;  // voices 1/2 have an implicit zero LSB nibble
        for (int i = 0; i < counter_nibbles; i++) counter |= uint32_t(regs_[size_t(counter_base + i)]) << (4 * i + shift_bias);
        for (int i = 0; i < counter_nibbles; i++) freq |= uint32_t(regs_[size_t(freq_base + i)]) << (4 * i + shift_bias);

        counter = (counter + freq) & 0xfffffu;

        // write the running counter back so CPU reads observe live state
        for (int i = 0; i < counter_nibbles; i++)
            regs_[size_t(counter_base + i)] = uint8_t((counter >> (4 * i + shift_bias)) & 0xf);

        const int waveform = regs_[size_t(waveform_reg)] & 0x7;
        const int volume = regs_[size_t(volume_reg)] & 0xf;
        const int sample_index = int((counter >> 15) & 0x1f);

        if (rom_.empty()) return 0;
        const size_t rom_index = size_t(waveform) * 32 + size_t(sample_index);
        const uint8_t nibble = rom_index < rom_.size() ? uint8_t(rom_[rom_index] & 0xf) : 0;
        return (int32_t(nibble) - 8) * volume * 64;
    }

    uint32_t clock_;
    std::array<uint8_t, 0x20> regs_{};
    std::vector<uint8_t> rom_;
};

}  // namespace dsp
