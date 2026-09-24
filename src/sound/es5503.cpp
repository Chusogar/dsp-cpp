#include "sound/es5503.h"

namespace dsp {
namespace {

constexpr uint16_t kWaveSizes[8] = {256, 512, 1024, 2048, 4096, 8192, 16384, 32768};
constexpr uint32_t kWaveMasks[8] = {0x1ff00, 0x1fe00, 0x1fc00, 0x1f800, 0x1f000, 0x1e000, 0x1c000, 0x18000};
constexpr uint32_t kAccMasks[8] = {0xff, 0x1ff, 0x3ff, 0x7ff, 0xfff, 0x1fff, 0x3fff, 0x7fff};
constexpr int kResShifts[8] = {9, 10, 11, 12, 13, 14, 15, 16};

enum { kModeFree = 0, kModeOneShot = 1, kModeSync = 2, kModeSwap = 3 };

}  // namespace

Es5503::Es5503(uint32_t clock) : clock_(clock) {}

void Es5503::reset() {
    for (auto& o : osc_) o = Oscillator{};
    rege0_ = 0xff;
    oscs_enabled_ = 1;
    set_irq(false);
}

void Es5503::set_irq(bool state) {
    irq_line_ = state;
    if (irq_handler_) irq_handler_(state);
}

void Es5503::halt_osc(int onum, int type, uint32_t* accumulator, int resshift) {
    Oscillator& o = osc_[size_t(onum)];
    Oscillator& partner = osc_[size_t(onum ^ 1)];
    const int mode = (o.control >> 1) & 3;
    const int omode = (partner.control >> 1) & 3;

    if (mode != kModeFree || type != 0) {
        o.control |= 1;
    } else {
        // Preserve the relative phase when looping.
        uint32_t wtsize = uint32_t(o.wtsize - 1);
        uint32_t altram = (*accumulator) >> resshift;
        altram = altram > wtsize ? altram - wtsize : 0;
        *accumulator = altram << resshift;
    }

    if (mode == kModeSwap) {
        partner.control &= uint8_t(~1);
        partner.accumulator = 0;
    } else if (omode == kModeSwap && (onum & 1) == 0) {
        // Even oscillator of a pair whose partner is in swap mode retriggers.
        o.control &= uint8_t(~1);
        uint32_t wtsize = uint32_t(o.wtsize - 1);
        uint32_t altram = (*accumulator) >> resshift;
        altram = altram > wtsize ? altram - wtsize : 0;
        *accumulator = altram << resshift;
    }

    if (o.control & 0x08) {
        o.irqpend = true;
        set_irq(true);
    }
}

int32_t Es5503::generate() {
    int32_t mix = 0;
    for (int osc = 0; osc <= oscs_enabled_ && osc < kNumOscillators; osc++) {
        Oscillator& o = osc_[size_t(osc)];
        if (o.control & 1) continue;
        const uint32_t wtptr = o.wavetblpointer & kWaveMasks[o.wavetblsize];
        uint32_t acc = o.accumulator;
        const uint32_t wtsize = uint32_t(o.wtsize - 1);
        const int resshift = kResShifts[o.resolution] - o.wavetblsize;
        const uint32_t sizemask = kAccMasks[o.wavetblsize];

        uint32_t altram = acc >> resshift;
        uint32_t ramptr = altram & sizemask;
        acc += o.freq;
        uint8_t raw = ram_[(ramptr + wtptr) & 0xffff];
        o.data = raw;
        if (raw == 0x00) {
            halt_osc(osc, 1, &acc, resshift);
        } else {
            mix += (int32_t(raw) - 0x80) * int32_t(o.vol);
            if (altram >= wtsize) halt_osc(osc, 0, &acc, resshift);
        }
        o.accumulator = acc;
    }
    return mix;
}

uint8_t Es5503::read(uint8_t reg) {
    if (reg < 0xe0) {
        const Oscillator& o = osc_[reg & 0x1f];
        switch (reg & 0xe0) {
            case 0x00: return uint8_t(o.freq & 0xff);
            case 0x20: return uint8_t(o.freq >> 8);
            case 0x40: return o.vol;
            case 0x60: return o.data;
            case 0x80: return uint8_t(o.wavetblpointer >> 8);
            case 0xa0: return o.control;
            case 0xc0: {
                uint8_t v = 0;
                if (o.wavetblpointer & 0x10000) v |= 0x40;
                v = uint8_t(v | (o.wavetblsize << 3) | o.resolution);
                return v;
            }
        }
        return 0;
    }
    switch (reg) {
        case 0xe0: {
            uint8_t retval = rege0_;
            set_irq(false);
            for (int i = 0; i <= oscs_enabled_ && i < kNumOscillators; i++) {
                if (osc_[size_t(i)].irqpend) {
                    retval = uint8_t(i << 1);
                    rege0_ = uint8_t(retval | 0x80);
                    osc_[size_t(i)].irqpend = false;
                    break;
                }
            }
            for (int i = 0; i <= oscs_enabled_ && i < kNumOscillators; i++) {
                if (osc_[size_t(i)].irqpend) {
                    set_irq(true);
                    break;
                }
            }
            return uint8_t(retval | 0x41);
        }
        case 0xe1: return uint8_t((oscs_enabled_ << 1) & 0xff);
        case 0xe2: return 0x80;  // A/D converter: mid-scale
    }
    return 0;
}

void Es5503::write(uint8_t reg, uint8_t value) {
    if (reg < 0xe0) {
        Oscillator& o = osc_[reg & 0x1f];
        switch (reg & 0xe0) {
            case 0x00: o.freq = uint16_t((o.freq & 0xff00) | value); break;
            case 0x20: o.freq = uint16_t((o.freq & 0x00ff) | (value << 8)); break;
            case 0x40: o.vol = value; break;
            case 0x60: break;
            case 0x80: o.wavetblpointer = (o.wavetblpointer & 0x10000) | (uint32_t(value) << 8); break;
            case 0xa0:
                if ((o.control & 1) && !(value & 1)) o.accumulator = 0;
                o.control = value;
                break;
            case 0xc0:
                if (value & 0x40) o.wavetblpointer |= 0x10000; else o.wavetblpointer &= 0xffff;
                o.wavetblsize = uint8_t((value >> 3) & 7);
                o.wtsize = kWaveSizes[o.wavetblsize];
                o.resolution = uint8_t(value & 7);
                break;
        }
        return;
    }
    if (reg == 0xe1) oscs_enabled_ = (value >> 1) & 0x1f;
}

}  // namespace dsp
