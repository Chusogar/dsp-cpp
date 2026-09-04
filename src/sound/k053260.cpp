#include "sound/k053260.h"

#include <algorithm>

namespace dsp {
namespace {

// 1.16 pan multipliers for the 8 hardware angles (MAME k053260.cpp).
constexpr int kPanMul[8][2] = {
    {0, 0}, {65536, 0},     {59870, 26656}, {53684, 37950},
    {46341, 46341}, {37950, 53684}, {26656, 59870}, {0, 65536},
};

constexpr int8_t kAdpcm[16] = {0, 1, 2, 4, 8, 16, 32, 64, -128, -64, -32, -16, -8, -4, -2, -1};

uint8_t read_rom(const std::vector<uint8_t>& rom, uint32_t addr) {
    if (rom.empty()) return 0;
    if (addr >= rom.size()) return 0;
    return rom[addr];
}

}  // namespace

void K053260::Voice::update_pan() {
    const int p = int(pan & 7);
    pan_l = int32_t(volume) * kPanMul[p][0];
    pan_r = int32_t(volume) * kPanMul[p][1];
}

void K053260::Voice::play(int32_t* mix, const std::vector<uint8_t>& rom) {
    counter += K053260::kClocksPerSample;
    while (counter >= 0x1000) {
        counter = counter - 0x1000 + int32_t(pitch);

        // Pre-increment: playback starts one byte after the programmed start.
        const uint32_t bytepos = ++position >> (kadpcm ? 1 : 0);
        if (bytepos > length) {
            if (loop) {
                position = 0;
                output = 0;
            } else {
                playing = false;
                return;
            }
        }

        const uint32_t addr = start + (reverse ? uint32_t(-int32_t(bytepos)) : bytepos);
        uint8_t romdata = read_rom(rom, addr);
        if (kadpcm) {
            if (position & 1) romdata >>= 4;
            output += kAdpcm[romdata & 0xf];
        } else {
            output = int32_t(romdata);
        }
    }

    mix[0] += (output * pan_l) >> 15;
    mix[1] += (output * pan_r) >> 15;
}

K053260::K053260(std::vector<uint8_t> rom, uint32_t clock)
    : rom_(std::move(rom)), clock_(clock ? clock : 3579545) {}

void K053260::reset() {
    for (auto& v : voice_) v = Voice{};
    portdata_.fill(0);
    keyon_ = 0;
    mode_ = 0;
    timer_state_ = 0;
    timer_count_ = 0;
}

void K053260::tick(int cycles) {
    if (cycles <= 0) return;
    timer_count_ += cycles;
    while (timer_count_ >= 16) {
        timer_count_ -= 16;
        timer_step();
    }
}

void K053260::timer_step() {
    switch (timer_state_) {
        case 0:
            if (sh1_cb_) sh1_cb_(true);
            break;
        case 1:
            if (sh1_cb_) sh1_cb_(false);
            break;
        default:
            break;
    }
    timer_state_ = (timer_state_ + 1) & 3;
}

uint8_t K053260::main_read(uint8_t offset) { return portdata_[size_t(2 + (offset & 1))]; }

void K053260::main_write(uint8_t offset, uint8_t value) { portdata_[size_t(offset & 1)] = value; }

uint8_t K053260::read(uint8_t offset) {
    offset &= 0x3f;
    switch (offset) {
        case 0x00:
        case 0x01:
            return portdata_[size_t(offset)];
        case 0x29: {
            uint8_t status = 0;
            for (int i = 0; i < 4; i++)
                if (voice_[size_t(i)].playing) status |= uint8_t(1 << i);
            return status;
        }
        case 0x2e:
            if (mode_ & 1) {
                Voice& v = voice_[0];
                const uint8_t data = read_rom(rom_, v.start + v.position);
                v.position = (v.position + 1) & 0xffff;
                return data;
            }
            return 0;
        default:
            return 0;
    }
}

void K053260::write(uint8_t offset, uint8_t value) {
    offset &= 0x3f;
    if (offset <= 0x01) return;
    if (offset == 0x02 || offset == 0x03) {
        portdata_[size_t(offset)] = value;
        return;
    }
    if (offset >= 0x08 && offset <= 0x27) {
        Voice& v = voice_[size_t((offset - 8) / 8)];
        switch (offset & 7) {
            case 0:
                v.pitch = uint16_t((v.pitch & 0x0f00) | value);
                break;
            case 1:
                v.pitch = uint16_t((v.pitch & 0x00ff) | ((uint16_t(value) << 8) & 0x0f00));
                break;
            case 2:
                v.length = uint16_t((v.length & 0xff00) | value);
                break;
            case 3:
                v.length = uint16_t((v.length & 0x00ff) | (uint16_t(value) << 8));
                break;
            case 4:
                v.start = (v.start & 0x1fff00) | value;
                break;
            case 5:
                v.start = (v.start & 0x1f00ff) | (uint32_t(value) << 8);
                break;
            case 6:
                v.start = (v.start & 0x00ffff) | ((uint32_t(value) << 16) & 0x1f0000);
                break;
            case 7:
                v.volume = value & 0x7f;
                v.update_pan();
                break;
        }
        return;
    }
    if (offset == 0x28) {
        const uint8_t rising = uint8_t(value & ~keyon_);
        for (int i = 0; i < 4; i++) {
            voice_[size_t(i)].reverse = (value & (1 << (i + 4))) != 0;
            if (rising & (1 << i))
                voice_key_on(i);
            else if ((value & (1 << i)) == 0)
                voice_key_off(i);
        }
        keyon_ = value;
        return;
    }
    if (offset == 0x2a) {
        for (int i = 0; i < 4; i++) {
            voice_[size_t(i)].loop = (value & (1 << i)) != 0;
            voice_[size_t(i)].kadpcm = (value & (1 << (i + 4))) != 0;
        }
        return;
    }
    if (offset == 0x2c) {
        voice_[0].pan = value & 7;
        voice_[1].pan = (value >> 3) & 7;
        voice_[0].update_pan();
        voice_[1].update_pan();
        return;
    }
    if (offset == 0x2d) {
        voice_[2].pan = value & 7;
        voice_[3].pan = (value >> 3) & 7;
        voice_[2].update_pan();
        voice_[3].update_pan();
        return;
    }
    if (offset == 0x2f) mode_ = value;
}

void K053260::voice_key_on(int ch) {
    Voice& v = voice_[size_t(ch)];
    v.position = v.kadpcm ? 1 : 0;
    v.counter = 0x1000 - kClocksPerSample;
    v.output = 0;
    v.playing = true;
}

void K053260::voice_key_off(int ch) {
    Voice& v = voice_[size_t(ch)];
    v.playing = false;
    v.position = 0;
    v.output = 0;
}

void K053260::update(int samples, int16_t* left, int16_t* right) {
    if (!left || !right || samples <= 0) return;
    const double chip_rate = double(clock_) / double(kClocksPerSample);
    const double step = chip_rate / double(kSampleRate);
    double phase = 0;
    int32_t last[2] = {0, 0};
    for (int i = 0; i < samples; i++) {
        phase += step;
        while (phase >= 1.0) {
            phase -= 1.0;
            last[0] = last[1] = 0;
            if (mode_ & 2) {
                for (auto& v : voice_) {
                    if (v.playing) v.play(last, rom_);
                }
            }
        }
        left[i] = int16_t(std::clamp(last[0], int32_t(-32768), int32_t(32767)));
        right[i] = int16_t(std::clamp(last[1], int32_t(-32768), int32_t(32767)));
    }
}

}  // namespace dsp
