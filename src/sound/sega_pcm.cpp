#include "sound/sega_pcm.h"

#include <algorithm>

namespace dsp {

SegaPcm::SegaPcm(uint32_t clock, float amplitude) : clock_(clock), amplitude_(amplitude) {
    reset();
}

void SegaPcm::set_bank(uint32_t bank) {
    bankshift_ = uint8_t(bank & 0xf);
    bankmask_ = uint8_t(0x70 | ((bank >> 16) & 0xfc));
}

void SegaPcm::reset() {
    ram_.fill(0xff);
    low_.fill(0);
    out_left_ = 0;
    out_right_ = 0;
}

void SegaPcm::clock() {
    out_left_ = 0;
    out_right_ = 0;
    for (int channel = 0; channel < 16; channel++) {
        const int base = channel * 8;
        uint8_t control = ram_[size_t(0x86 + base)];
        if (control & 1) continue;

        const uint32_t offset = uint32_t(control & bankmask_) << bankshift_;
        uint32_t addr = (uint32_t(ram_[size_t(0x85 + base)]) << 16) |
                        (uint32_t(ram_[size_t(0x84 + base)]) << 8) | low_[size_t(channel)];
        const uint32_t loop = (uint32_t(ram_[size_t(0x05 + base)]) << 16) |
                              (uint32_t(ram_[size_t(0x04 + base)]) << 8);
        const uint8_t end = uint8_t(ram_[size_t(0x06 + base)] + 1);

        if (uint8_t(addr >> 16) == end) {
            if (control & 2) {
                ram_[size_t(0x86 + base)] = uint8_t(control | 1);
                ram_[size_t(0x84 + base)] = uint8_t(addr >> 8);
                ram_[size_t(0x85 + base)] = uint8_t(addr >> 16);
                low_[size_t(channel)] = 0;
                continue;
            }
            addr = loop;
        }

        uint8_t sample = read_rom_ ? read_rom_(offset + (addr >> 8)) : 0x80;
        int v = int(sample) - 0x80;
        out_left_ += v * int(ram_[size_t(0x02 + base)] & 0x7f);
        out_right_ += v * int(ram_[size_t(0x03 + base)] & 0x7f);
        addr = (addr + ram_[size_t(0x07 + base)]) & 0xffffffu;
        ram_[size_t(0x84 + base)] = uint8_t(addr >> 8);
        ram_[size_t(0x85 + base)] = uint8_t(addr >> 16);
        control = ram_[size_t(0x86 + base)];
        low_[size_t(channel)] = (control & 1) ? 0 : uint8_t(addr);
    }
    out_left_ = std::clamp(out_left_, -32767, 32767);
    out_right_ = std::clamp(out_right_, -32767, 32767);
}

}  // namespace dsp
