#include "sound/sega_pcm.h"

#include <algorithm>

namespace dsp {

SegaPcm::SegaPcm(uint32_t clock, float amplitude, Variant variant)
    : clock_(clock), variant_(variant), amplitude_(amplitude) {
    set_bank(kBank512);
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
    const bool discrete = variant_ == Variant::Discrete;
    const int count = voices();
    int32_t lsum = 0, rsum = 0;
    for (int voice = 0; voice < count; voice++) {
        // Register blocks: "regs" (volume, loop, end, delta) and "state"
        // (current address, control).
        const size_t regs = size_t((discrete ? 0x40 : 0x00) + voice * 8);
        const size_t state = size_t((discrete ? 0xc0 : 0x80) + voice * 8);
        uint8_t& ctrl = ram_[state + 6];
        if (ctrl & 1) {
            low_[size_t(voice)] = 0;
            continue;
        }
        uint32_t addr = (uint32_t(ram_[state + 5]) << 16) | (uint32_t(ram_[state + 4]) << 8) |
                        low_[size_t(voice)];
        const uint8_t end = uint8_t(ram_[regs + 6] + 1);
        if (uint8_t(addr >> 16) == end) {
            if (ctrl & 2) {
                ctrl |= 1;
                continue;
            }
            addr = (uint32_t(ram_[regs + 5]) << 16) | (uint32_t(ram_[regs + 4]) << 8);
        }
        const uint32_t bank = discrete ? 0 : uint32_t(ctrl & bankmask_) << bankshift_;
        const uint8_t sample = read_rom_ ? read_rom_(bank + (addr >> 8)) : 0x80;
        const int v = int(int8_t(uint8_t(sample - 0x80)));
        lsum += v * int(ram_[regs + 2] & 0x7f);
        rsum += v * int(ram_[regs + 3] & 0x7f);
        addr = (addr + ram_[regs + 7]) & 0xffffffu;
        ram_[state + 4] = uint8_t(addr >> 8);
        ram_[state + 5] = uint8_t(addr >> 16);
        low_[size_t(voice)] = uint8_t(addr);
    }
    // MAME: put_int(sum, 32768), i.e. 32768 is full scale.  Several loud
    // voices can exceed it; the mixer applies the board gain and clamps.
    out_left_ = lsum;
    out_right_ = rsum;
}

}  // namespace dsp
