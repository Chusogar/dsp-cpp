#include "sound/es5503.h"

namespace dsp {

Es5503::Es5503(uint32_t clock) : clock_(clock) {}

void Es5503::reset() {
    for (auto& o : osc_) o = Oscillator{};
    osc_enable_ = 0xe0;
}

uint8_t Es5503::read(uint8_t reg) {
    if (reg < 0x20) return uint8_t(osc_[reg].freq & 0xff);
    if (reg < 0x40) return uint8_t(osc_[reg - 0x20].freq >> 8);
    if (reg < 0x60) return osc_[reg - 0x40].volume;
    if (reg < 0x80) return osc_[reg - 0x60].wave_ptr;
    if (reg < 0xa0) {
        Oscillator& o = osc_[reg - 0x80];
        uint8_t v = o.control;
        if (o.irq_pending) v = uint8_t(v | 0x00);  // IRQ flag surfaces via $E1, not here
        return v;
    }
    if (reg < 0xc0) return osc_[reg - 0xa0].wave_size;
    if (reg == 0xe0) return osc_enable_;
    if (reg == 0xe1) {
        // Read-and-partially-clear IRQ status: returns the index of the
        // lowest-numbered oscillator with a pending interrupt (or 0xff if
        // none), matching the chip's documented single-flag-at-a-time
        // acknowledge scheme.
        for (int i = 0; i < kNumOscillators; i++) {
            if (osc_[i].irq_pending) {
                osc_[i].irq_pending = false;
                return uint8_t(i << 1);
            }
        }
        return 0xff;
    }
    return 0xff;
}

void Es5503::write(uint8_t reg, uint8_t value) {
    if (reg < 0x20) { osc_[reg].freq = uint16_t((osc_[reg].freq & 0xff00) | value); return; }
    if (reg < 0x40) { int i = reg - 0x20; osc_[i].freq = uint16_t((osc_[i].freq & 0x00ff) | (value << 8)); return; }
    if (reg < 0x60) { osc_[reg - 0x40].volume = value; return; }
    if (reg < 0x80) { osc_[reg - 0x60].wave_ptr = value; return; }
    if (reg < 0xa0) {
        Oscillator& o = osc_[reg - 0x80];
        o.control = value;
        if (value & 1) o.accumulator = 0;  // halting resets phase, matching typical usage
        return;
    }
    if (reg < 0xc0) { osc_[reg - 0xa0].wave_size = value; return; }
    if (reg == 0xe0) { osc_enable_ = value; return; }
}

int16_t Es5503::update() {
    int32_t mix = 0;
    const int active = oscillator_count();

    for (int i = 0; i < active && i < kNumOscillators; i++) {
        Oscillator& o = osc_[i];
        if (o.control & 1) continue;  // halted

        const uint32_t table_bytes = 256u << (o.wave_size & 7);
        // Phase accumulator: integer part (above kFrac bits) indexes the
        // wavetable; freq is added each tick the same way every wavetable
        // synth of this era works (a simple DDS/phase-accumulator design).
        constexpr uint32_t kFrac = 9;
        o.accumulator += uint32_t(o.freq);
        uint32_t index = (o.accumulator >> kFrac);

        if (index >= table_bytes) {
            const int mode = (o.control >> 1) & 3;
            if (mode == 1) {  // one-shot: play once, then halt
                o.control = uint8_t(o.control | 1);
                index = table_bytes - 1;
                o.accumulator = uint32_t(index) << kFrac;
                if (o.control & 8) o.irq_pending = true;
            } else {
                // free-run (mode 0) and the swap/sync modes (2,3, treated
                // as free-run here -- cross-oscillator linking isn't
                // implemented yet) all loop the table.
                o.accumulator -= uint32_t(table_bytes) << kFrac;
                index = (o.accumulator >> kFrac);
                if (o.control & 8) o.irq_pending = true;
            }
        }

        uint16_t addr = uint16_t((uint16_t(o.wave_ptr) << 8) + index);
        int sample = int(ram_[addr]) - 0x80;  // offset-binary -> signed
        mix += sample * int(o.volume);
    }

    if (irq_handler_) {
        bool any = false;
        for (auto& o : osc_) if (o.irq_pending) { any = true; break; }
        irq_handler_(any);
    }

    // Scale down: up to 32 oscillators * 127 sample * 255 volume can be
    // large, so shift into a comfortable int16 range.
    int32_t out = mix >> 8;
    if (out > 32767) out = 32767;
    if (out < -32768) out = -32768;
    return int16_t(out);
}

}  // namespace dsp
