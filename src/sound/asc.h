#pragma once

#include <cstdint>
#include <functional>

namespace dsp {

// Apple Sound Chip (344S0053), original version as fitted to the Macintosh II.
// Two 1 KB FIFOs (FIFO mode, 22257 Hz) or four 512-byte wavetables with
// 9.15 fixed-point phase accumulators (wavetable mode).
//   $000-$3FF FIFO A / wavetables 0-1   $400-$7FF FIFO B / wavetables 2-3
//   $800 version   $801 mode   $802 control   $803 FIFO mode
//   $804 FIFO status (read clears; bit0/2 half empty A/B, bit1/3 empty/full)
//   $805 wavetable control   $806 volume   $807 clock rate
//   $810-$82F wavetable phase/increment pairs
// The IRQ line (half-empty FIFO) is active high here; the Mac II wires it,
// inverted, to VIA2 CB1. Behaviour follows MAME's asc_device.
class Asc {
public:
    static constexpr int kSampleRate = 22257;

    void set_irq_handler(std::function<void(bool)> h) { irq_ = std::move(h); }
    void reset();
    uint8_t read(uint32_t offset);
    void write(uint32_t offset, uint8_t data);
    // One output sample (mono, signed, roughly +-128*4).
    int32_t generate();

private:
    void set_irq(bool on);

    uint8_t regs_[0x800] = {};
    uint8_t fifo_[2][0x400] = {};
    int rdptr_[2] = {0, 0}, wrptr_[2] = {0, 0}, cap_[2] = {0, 0};
    uint32_t phase_[4] = {}, incr_[4] = {};
    bool irq_line_ = false;
    std::function<void(bool)> irq_;
};

}  // namespace dsp
