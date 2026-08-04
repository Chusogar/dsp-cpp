#include "sound/okim6295.h"

#include <algorithm>
#include <cmath>

namespace dsp {
namespace {

const std::array<int, 8> kIndexShift = {-1, -1, -1, -1, 2, 4, 6, 8};

// Only nine of the sixteen volume steps are used, the rest mute the voice.
const std::array<uint8_t, 16> kVolumeTable = {0x20, 0x16, 0x10, 0x0b, 0x08, 0x06, 0x04, 0x03,
                                              0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

const std::array<float, 49 * 16>& diff_table() {
    static const std::array<float, 49 * 16> table = [] {
        static const int nibble_to_bit[16][4] = {
            {1, 0, 0, 0},  {1, 0, 0, 1},  {1, 0, 1, 0},  {1, 0, 1, 1},
            {1, 1, 0, 0},  {1, 1, 0, 1},  {1, 1, 1, 0},  {1, 1, 1, 1},
            {-1, 0, 0, 0}, {-1, 0, 0, 1}, {-1, 0, 1, 0}, {-1, 0, 1, 1},
            {-1, 1, 0, 0}, {-1, 1, 0, 1}, {-1, 1, 1, 0}, {-1, 1, 1, 1}};
        std::array<float, 49 * 16> values{};
        for (int step = 0; step <= 48; step++) {
            int step_value = int(std::floor(16.0 * std::pow(11.0 / 10.0, step)));
            for (int nibble = 0; nibble < 16; nibble++) {
                const int* bits = nibble_to_bit[nibble];
                values[size_t(step * 16 + nibble)] =
                    float(bits[0] * (step_value * bits[1] + step_value / 2.0 * bits[2] +
                                     step_value / 4.0 * bits[3] + step_value / 8.0));
            }
        }
        return values;
    }();
    return table;
}

}  // namespace

OKIM6295::OKIM6295(uint32_t clock, bool pin7_high)
    : clock_(clock), divisor_(pin7_high ? 132 : 165) {
    reset();
}

void OKIM6295::reset() {
    command_ = -1;
    for (Voice& voice : voices_) {
        voice = Voice{};
        reset_adpcm(voice);
    }
}

void OKIM6295::reset_adpcm(Voice& voice) {
    voice.signal = -2;
    voice.step = 0;
}

int OKIM6295::clock_adpcm(Voice& voice, uint8_t nibble) {
    voice.signal += int(diff_table()[size_t(voice.step * 16 + (nibble & 0x0f))]);
    voice.signal = std::clamp(voice.signal, -2048, 2047);
    voice.step += kIndexShift[size_t(nibble & 0x07)];
    voice.step = std::clamp(voice.step, 0, 48);
    return voice.signal;
}

int OKIM6295::generate_adpcm(Voice& voice) {
    size_t offset = size_t(voice.base_offset + (voice.sample >> 1));
    uint8_t byte = offset < rom_.size() ? rom_[offset] : 0;
    uint8_t nibble = (voice.sample & 1) == 0 ? uint8_t(byte >> 4) : uint8_t(byte & 0x0f);
    voice.sample++;
    if (voice.sample >= voice.count) voice.playing = false;
    return clock_adpcm(voice, nibble) * (voice.volume >> 1);
}

uint8_t OKIM6295::read() const {
    uint8_t result = 0xf0;
    for (int index = 0; index < kVoices; index++) {
        if (voices_[size_t(index)].playing) result = uint8_t(result | (1 << index));
    }
    return result;
}

void OKIM6295::write(uint8_t value) {
    if (command_ != -1) {
        uint8_t mask = uint8_t(value >> 4);
        for (int index = 0; index < kVoices; index++, mask = uint8_t(mask >> 1)) {
            if ((mask & 1) == 0) continue;
            Voice& voice = voices_[size_t(index)];
            if (voice.playing) continue;
            size_t base = size_t(command_) * 8;
            auto rom_byte = [this](size_t offset) {
                return offset < rom_.size() ? uint32_t(rom_[offset]) : 0u;
            };
            uint32_t start = ((rom_byte(base) << 16) | (rom_byte(base + 1) << 8) |
                              rom_byte(base + 2)) & 0x3ffff;
            uint32_t stop = ((rom_byte(base + 3) << 16) | (rom_byte(base + 4) << 8) |
                             rom_byte(base + 5)) & 0x3ffff;
            if (start >= stop) continue;
            voice.playing = true;
            voice.base_offset = start;
            voice.sample = 0;
            voice.count = 2 * (stop - start + 1);
            reset_adpcm(voice);
            voice.volume = kVolumeTable[size_t(value & 0x0f)];
        }
        command_ = -1;
        return;
    }
    if ((value & 0x80) != 0) {
        command_ = value & 0x7f;
        return;
    }
    // Silence command: bits 3 to 6 select the voices to stop.
    uint8_t mask = uint8_t(value >> 3);
    for (int index = 0; index < kVoices; index++, mask = uint8_t(mask >> 1)) {
        if ((mask & 1) != 0) voices_[size_t(index)].playing = false;
    }
}

int32_t OKIM6295::update() {
    int32_t out = 0;
    for (Voice& voice : voices_) {
        if (voice.playing) out += generate_adpcm(voice);
    }
    return std::clamp(out, -32767, 32767);
}

}  // namespace dsp
