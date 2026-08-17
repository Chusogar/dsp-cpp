#include "sound/polepos_wsg.h"

#include <algorithm>

namespace dsp {

PolePosWsg::PolePosWsg(uint32_t clock) : clock_(clock) {
    namco_clock_ = int32_t(clock_);
    f_fracbits_ = 15;
    while (namco_clock_ < 192000) {
        namco_clock_ *= 2;
        f_fracbits_++;
    }
    wave_.assign(0x100, 0);
    reset();
}

void PolePosWsg::set_wavetable(const uint8_t* data, size_t size) {
    wave_.assign(0x100, 0);
    if (data == nullptr || size == 0) return;
    const size_t n = std::min(size, wave_.size());
    std::copy(data, data + n, wave_.begin());
}

void PolePosWsg::reset() {
    regs_.fill(0);
    for (auto& v : voices_) v = Voice{};
}

uint8_t PolePosWsg::read(uint8_t offset) const { return regs_[offset & 0x3f]; }

void PolePosWsg::write(uint8_t offset, uint8_t data) {
    offset &= 0x3f;
    if (regs_[offset] == data) return;
    regs_[offset] = data;
    recompute((offset & 0x1f) >> 2);
}

void PolePosWsg::recompute(int ch) {
    if (ch < 0 || ch >= kVoices) return;
    Voice& voice = voices_[size_t(ch)];
    voice.frequency = uint32_t(regs_[size_t(ch * 4 + 0x00)]) +
                      (uint32_t(regs_[size_t(ch * 4 + 0x01)]) << 8);
    const uint8_t sel = regs_[size_t(ch * 4 + 0x23)];
    voice.waveform_select = uint8_t(sel & 7);
    voice.external = (sel & 8) != 0;
    voice.ext_sel = uint8_t(sel & 0x0f);
    voice.volume[0] = regs_[size_t(ch * 4 + 0x03)] >> 4;
    voice.volume[1] = regs_[size_t(ch * 4 + 0x03)] & 0x0f;
    voice.volume[2] = sel >> 4;
    voice.volume[3] = regs_[size_t(ch * 4 + 0x02)] >> 4;
}

int PolePosWsg::waveform(uint8_t select, uint8_t pos) const {
    const uint16_t addr = uint16_t((uint16_t(select) << 5) + (pos & 0x1f));
    return int(wave_[size_t(addr & 0xff)] & 0x0f) - 8;
}

int32_t PolePosWsg::update(int chanl1, int chanl2, int chanl3, int chanl4) {
    if (!enabled_) return 0;
    const int ext[4] = {chanl1 - 8, chanl2 - 8, chanl3 - 8, chanl4 - 8};
    int32_t mix = 0;
    const uint64_t step = (uint64_t(namco_clock_) << 8) / uint64_t(kSampleRate);
    for (auto& voice : voices_) {
        voice.counter += uint32_t((uint64_t(voice.frequency) * step) >> 8);
        const int vol = voice.volume[0] + voice.volume[1] + voice.volume[2] + voice.volume[3];
        if (vol == 0) continue;
        int sample = 0;
        if (voice.external) {
            switch (voice.ext_sel) {
                case 0x08:
                    sample = ext[0];
                    break;
                case 0x09:
                    sample = ext[1];
                    break;
                case 0x0a:
                    sample = ext[2];
                    break;
                case 0x0b:
                    sample = ext[3];
                    break;
                default:
                    break;
            }
        } else {
            const uint8_t pos = uint8_t((voice.counter >> f_fracbits_) & 0x1f);
            sample = waveform(voice.waveform_select, pos);
        }
        mix += sample * vol;
    }
    return mix;
}

}  // namespace dsp
