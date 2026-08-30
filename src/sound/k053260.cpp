#include "sound/k053260.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace dsp {

K053260::K053260(std::vector<uint8_t> rom, uint32_t clock)
    : rom_(std::move(rom)), clock_(clock ? clock : 3579545) {}

void K053260::reset() {
    for (auto& v : voice_) v = Voice{};
    portdata_.fill(0);
    keyon_ = 0;
    mode_ = 0;
    phase_ = 0;
}

// Main CPU: reads sub→main ports (portdata[2]/portdata[3])
uint8_t K053260::main_read(uint8_t offset) {
    return portdata_[size_t(2 + (offset & 1))];
}

// Main CPU: writes main→sub ports (portdata[0]/portdata[1])
void K053260::main_write(uint8_t offset, uint8_t value) {
    portdata_[size_t(offset & 1)] = value;
    static int mw; if (mw < 5) {
        std::fprintf(stderr, "k260 main_w %d=%02x\n", int(offset & 1), value);
        mw++;
    }
}

uint8_t K053260::read(uint8_t offset) {
    offset &= 0x3f;
    switch (offset) {
        case 0x00:
        case 0x01: {
            uint8_t v = portdata_[size_t(offset)];
            static int sr; if (sr < 10) {
                std::fprintf(stderr, "k260 snd_r port %d=%02x\n", int(offset), v);
                sr++;
            }
            return v;
        }
        case 0x29: {
            uint8_t status = 0;
            for (int i = 0; i < 4; i++)
                if (voice_[size_t(i)].playing) status |= uint8_t(1 << i);
            return status;
        }
        case 0x2e:
            // ROM read mode (self-test of sample ROMs)
            if (mode_ & 1) {
                const uint32_t addr = voice_[0].start & 0xffffff;
                if (addr < rom_.size()) {
                    const uint8_t v = rom_[addr];
                    // auto-increment for sequential reads
                    voice_[0].start = (voice_[0].start + 1) & 0xffffff;
                    return v;
                }
            }
            return 0;
        default:
            return 0;
    }
}

void K053260::write(uint8_t offset, uint8_t value) {
    offset &= 0x3f;
    switch (offset) {
        case 0x00:
        case 0x01:
            // read-only from sound side
            return;
        case 0x02:
        case 0x03:
            // sub→main communication ports
            portdata_[size_t(offset)] = value;
            static int sw; if (sw < 5) {
                std::fprintf(stderr, "k260 snd_w port %d=%02x\n", int(offset), value);
                sw++;
            }
            return;
        default:
            break;
    }
    if (offset >= 0x08 && offset < 0x28) {
        // Voice regs: 8 bytes per channel starting at 0x08
        // MAME: channels at 0x08, 0x10, 0x18, 0x20 — actually 0x00-0x07 unused for voice
        // Standard map: 0x08-0x0f ch0, 0x10-0x17 ch1, 0x18-0x1f ch2, 0x20-0x27 ch3
        const int base = offset - 0x08;
        const int ch = base / 8;
        const int reg = base & 7;
        if (ch < 0 || ch > 3) return;
        Voice& v = voice_[size_t(ch)];
        switch (reg) {
            case 0: v.start = (v.start & 0xffff00) | value; break;
            case 1: v.start = (v.start & 0xff00ff) | (uint32_t(value) << 8); break;
            case 2: v.start = (v.start & 0x00ffff) | (uint32_t(value) << 16); break;
            case 3: v.length = (v.length & 0xff00) | value; break;
            case 4: v.length = (v.length & 0x00ff) | (uint16_t(value) << 8); break;
            case 5: v.pitch = (v.pitch & 0xff00) | value; break;
            case 6: v.pitch = (v.pitch & 0x00ff) | (uint16_t(value) << 8); break;
            case 7: v.volume = value; break;
        }
        return;
    }
    if (offset >= 0x28 && offset < 0x2c) {
        // Some maps put pan/loop here differently; use 0x2a style if needed
        return;
    }
    if (offset == 0x28) {
        for (int i = 0; i < 4; i++) {
            const bool on = (value & (1 << i)) != 0;
            const bool was = (keyon_ & (1 << i)) != 0;
            if (on && !was) voice_key_on(i);
            if (!on && was) voice_key_off(i);
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
    if (offset == 0x2f) {
        mode_ = value;
        return;
    }
}

void K053260::voice_key_on(int ch) {
    Voice& v = voice_[size_t(ch)];
    v.position = 0;
    v.counter = 0;
    v.playing = true;
    v.output = 0;
}

void K053260::voice_key_off(int ch) {
    voice_[size_t(ch)].playing = false;
}

int16_t K053260::decode_sample(Voice& v) {
    if (!v.playing || rom_.empty()) return 0;
    const uint32_t addr = v.start + v.position;
    if (addr >= rom_.size() || (v.length && v.position >= v.length)) {
        if (v.loop) {
            v.position = 0;
        } else {
            v.playing = false;
            return 0;
        }
    }
    const uint8_t raw = rom_[v.start + v.position];
    v.position++;
    // Signed 8-bit PCM
    return int16_t(int8_t(raw)) * int16_t(v.volume);
}

void K053260::update(int samples, int16_t* left, int16_t* right) {
    if (!left || !right || samples <= 0) return;
    const double chip_rate = double(clock_) / 32.0;  // approximate
    const double step = chip_rate / double(kSampleRate);
    for (int i = 0; i < samples; i++) {
        int32_t mix_l = 0, mix_r = 0;
        phase_ += step;
        while (phase_ >= 1.0) {
            phase_ -= 1.0;
            for (int ch = 0; ch < 4; ch++) {
                Voice& v = voice_[size_t(ch)];
                if (!v.playing) continue;
                v.output = int8_t(decode_sample(v) / std::max(1, int(v.volume)));
            }
        }
        for (int ch = 0; ch < 4; ch++) {
            const Voice& v = voice_[size_t(ch)];
            if (!v.playing) continue;
            const int32_t s = int32_t(int8_t(v.output)) * int32_t(v.volume);
            // simple center pan
            mix_l += s;
            mix_r += s;
        }
        left[i] = int16_t(std::clamp(mix_l, -32768, 32767));
        right[i] = int16_t(std::clamp(mix_r, -32768, 32767));
    }
}

}  // namespace dsp
