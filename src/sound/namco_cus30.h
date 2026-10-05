#pragma once

#include <algorithm>
#include <array>
#include <cstdint>

namespace dsp {

// Namco CUS30 custom sound (8 voices with on-chip wave RAM), as used on
// Baraduke / Metro-Cross and Namco System 1. Ported from namco_snd.pas
// (namcos1_cus30_* + the wave_ram 8-voice update path).
//
// Shared 1KB RAM window:
//   $000-$0ff  wave data (each byte expands to two 4-bit samples)
//   $100-$13f  voice registers (8 voices x 8 bytes)
//   $140-$3ff  general RAM (also used as MCU work RAM on Baraduke)
class NamcoCus30 {
public:
    static constexpr int kSampleRate = 44100;
    static constexpr int kVoices = 8;
    // Pascal CONST_RE24 = round(24_000_000 / FREQ_BASE_AUDIO).
    static constexpr int kFreqScale = 24000000 / kSampleRate;  // 544

    void reset() {
        enabled_ = true;
        regs_.fill(0);
        ram_.fill(0);
        wave_.fill(0);
        for (auto& v : voice_) {
            v = Voice{};
        }
    }

    uint8_t read(uint16_t address) const {
        return ram_[address & 0x3ff];
    }

    void write(uint16_t address, uint8_t value) {
        const uint16_t offset = address & 0x3ff;
        if (offset <= 0x00ff) {
            if (ram_[offset] == value) return;
            ram_[offset] = value;
            update_waveform(offset, value);
            return;
        }
        if (offset <= 0x013f) {
            ram_[offset] = value;
            sound_write(uint8_t(offset & 0x3f), value);
            return;
        }
        ram_[offset] = value;
    }

    int16_t update() {
        if (!enabled_) return 0;
        int32_t sample = 0;
        for (int i = 0; i < kVoices; i++) {
            Voice& v = voice_[size_t(i)];
            if (!v.active) continue;
            const uint32_t offset = v.pos;
            const int wave_base = 32 * int(v.waveform);
            const uint8_t nibble = wave_[size_t(wave_base + int((offset >> 25) & 0x1f))];
            sample += int32_t(nibble) * int32_t(v.volume) * 64;
            v.pos = offset + uint32_t(v.frequency);
        }
        sample = (sample / kVoices) * 4;
        return int16_t(std::max(-32768, std::min(32767, sample)));
    }

    void set_enabled(bool enabled) { enabled_ = enabled; }

private:
    struct Voice {
        uint8_t volume = 0;
        uint8_t waveform = 0;
        int frequency = 0;
        bool active = false;
        uint32_t pos = 0;
    };

    void update_waveform(uint16_t offset, uint8_t data) {
        wave_[size_t(offset) * 2] = uint8_t((data >> 4) & 0x0f);
        wave_[size_t(offset) * 2 + 1] = uint8_t(data & 0x0f);
    }

    void sound_write(uint8_t address, uint8_t value) {
        if (regs_[address] == value) return;
        regs_[address] = value;
        const int ch = address / 8;
        Voice& v = voice_[size_t(ch)];
        switch (address - ch * 8) {
            case 0x00:
                v.volume = uint8_t(value & 0x0f);
                break;
            case 0x01:
                v.waveform = uint8_t((value >> 4) & 0x0f);
                break;
            case 0x02:
            case 0x03: {
                int freq = (regs_[size_t(ch * 8 + 0x01)] & 0x0f) << 16;
                freq += regs_[size_t(ch * 8 + 0x02)] << 8;
                freq = (freq + regs_[size_t(ch * 8 + 0x03)]) * kFreqScale;
                v.frequency = freq;
                break;
            }
            default:
                break;
        }
        if (v.frequency == 0 && v.volume == 0) {
            v.active = false;
            v.pos = 0;
        } else {
            v.active = true;
        }
    }

    bool enabled_ = true;
    std::array<uint8_t, 0x40> regs_{};
    std::array<uint8_t, 0x400> ram_{};
    std::array<uint8_t, 0x200> wave_{};
    std::array<Voice, kVoices> voice_{};
};

}  // namespace dsp
