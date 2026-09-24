#include "sound/asc.h"

#include <cstring>

namespace dsp {
namespace {
constexpr int kVersion = 0x00, kMode = 0x01, kControl = 0x02, kFifoMode = 0x03, kFifoStat = 0x04;
constexpr uint8_t kHalfA = 0x01, kFullA = 0x02, kHalfB = 0x04, kFullB = 0x08;
}  // namespace

void Asc::reset() {
    std::memset(regs_, 0, sizeof regs_);
    std::memset(fifo_, 0, sizeof fifo_);
    for (int i = 0; i < 2; i++) rdptr_[i] = wrptr_[i] = cap_[i] = 0;
    for (int i = 0; i < 4; i++) phase_[i] = incr_[i] = 0;
    set_irq(false);
}

void Asc::set_irq(bool on) {
    irq_line_ = on;
    if (irq_) irq_(on);
}

uint8_t Asc::read(uint32_t offset) {
    offset &= 0xfff;
    if (offset < 0x400) return fifo_[0][offset];
    if (offset < 0x800) return fifo_[1][offset - 0x400];
    const uint32_t r = offset - 0x800;
    switch (r) {
        case kVersion: return 0x00;
        case kFifoStat: {
            uint8_t v = regs_[kFifoStat];
            regs_[kFifoStat] = 0;
            set_irq(false);
            return v;
        }
        default: break;
    }
    if (r >= 0x10 && r < 0x30) {
        const int ch = int((r - 0x10) >> 3);
        const uint32_t v = (r & 4) ? incr_[ch] : phase_[ch];
        switch (r & 3) {
            case 1: return uint8_t(v >> 16);
            case 2: return uint8_t(v >> 8);
            case 3: return uint8_t(v);
            default: return 0;
        }
    }
    return regs_[r & 0x7ff];
}

void Asc::write(uint32_t offset, uint8_t data) {
    offset &= 0xfff;
    if (offset < 0x800) {
        const int f = offset < 0x400 ? 0 : 1;
        if ((regs_[kMode] & 3) == 2) {  // wavetable mode: plain RAM
            fifo_[f][offset & 0x3ff] = data;
            return;
        }
        fifo_[f][wrptr_[f]] = data;
        wrptr_[f] = (wrptr_[f] + 1) & 0x3ff;
        if (cap_[f] < 0x400) cap_[f]++;
        const uint8_t half = f ? kHalfB : kHalfA, full = f ? kFullB : kFullA;
        if (cap_[f] >= 0x200) {
            regs_[kFifoStat] &= uint8_t(~half);
            if (cap_[f] >= 0x3ff) regs_[kFifoStat] |= full;
        } else if (cap_[f] > 0) {
            regs_[kFifoStat] &= uint8_t(~full);
        }
        return;
    }
    const uint32_t r = offset - 0x800;
    switch (r) {
        case kMode:
            data &= 3;
            if (data != regs_[kMode]) {
                for (int i = 0; i < 2; i++) rdptr_[i] = wrptr_[i] = cap_[i] = 0;
            }
            regs_[kMode] = data;
            return;
        case kFifoMode:
            if (data & 0x80) {
                for (int i = 0; i < 2; i++) rdptr_[i] = wrptr_[i] = cap_[i] = 0;
                regs_[kFifoStat] |= 0x0a;
            }
            regs_[kFifoMode] = data;
            return;
        case kFifoStat:
            return;
        default: break;
    }
    if (r >= 0x10 && r < 0x30) {
        const int ch = int((r - 0x10) >> 3);
        uint32_t& v = (r & 4) ? incr_[ch] : phase_[ch];
        switch (r & 3) {
            case 1: v = (v & 0x00ffff) | (uint32_t(data) << 16); break;
            case 2: v = (v & 0xff00ff) | (uint32_t(data) << 8); break;
            case 3: v = (v & 0xffff00) | data; break;
            default: break;
        }
        return;
    }
    if (offset == 0xe00) {
        regs_[kFifoStat] |= 0x0f;
        set_irq(true);
    }
    regs_[r & 0x7ff] = data;
}

int32_t Asc::generate() {
    switch (regs_[kMode] & 3) {
        case 1: {  // FIFO
            const bool stereo = regs_[kControl] & 2;
            int32_t a = int8_t(fifo_[0][rdptr_[0]] ^ 0x80);
            int32_t b = stereo ? int8_t(fifo_[1][rdptr_[1]] ^ 0x80) : a;
            const int cap_a = cap_[0], cap_b = cap_[1];
            if (cap_[0]) {
                rdptr_[0] = (rdptr_[0] + 1) & 0x3ff;
                cap_[0]--;
            }
            if (stereo && cap_[1]) {
                rdptr_[1] = (rdptr_[1] + 1) & 0x3ff;
                cap_[1]--;
            }
            if (cap_a == 0x1ff) {
                regs_[kFifoStat] = uint8_t((regs_[kFifoStat] | kHalfA) & ~kFullA);
                set_irq(true);
            }
            if (stereo && cap_b == 0x1ff) {
                regs_[kFifoStat] = uint8_t((regs_[kFifoStat] | kHalfB) & ~kFullB);
                set_irq(true);
            }
            return (a + b) * 2;
        }
        case 2: {  // wavetable
            int32_t mix = 0;
            static const uint32_t kOffs[2] = {0, 0x200};
            for (int ch = 0; ch < 4; ch++) {
                phase_[ch] = (phase_[ch] + incr_[ch]) & 0xffffff;
                const uint8_t* table = fifo_[ch < 2 ? 0 : 1];
                mix += int8_t(table[((phase_[ch] >> 15) & 0x1ff) + kOffs[ch & 1]] ^ 0x80);
            }
            return mix;
        }
        default:
            return 0;
    }
}

}  // namespace dsp
