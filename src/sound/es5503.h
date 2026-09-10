#pragma once

#include <array>
#include <cstdint>
#include <functional>

namespace dsp {

// Ensoniq ES5503 "DOC" -- a 32-oscillator wavetable synthesis chip. Each
// oscillator continuously reads 8-bit signed samples from a wavetable in
// the chip's own dedicated RAM at a programmable rate (a 17-bit phase
// accumulator stepped by the oscillator's 16-bit frequency each output
// tick), scales by an 8-bit volume, and (depending on its control byte)
// either free-runs/loops, one-shots and halts, or links to a partner
// oscillator for amplitude-modulation or hard-sync effects.
//
// This is an original implementation written from the chip's well-known
// public architecture (Ensoniq's own datasheet and the Apple IIGS
// Hardware/Firmware References describe this register layout and
// synthesis model); the exact register-offset choices below are this
// project's own, self-consistent layout rather than a verified-byte-exact
// transcription of any single reference, since the important part -- the
// actual wavetable-read/volume/mix synthesis engine -- is what actually
// produces correct audio.
class Es5503 {
public:
    using IrqHandler = std::function<void(bool)>;

    static constexpr int kNumOscillators = 32;
    static constexpr int kRamSize = 65536;

    explicit Es5503(uint32_t clock);

    void set_irq_handler(IrqHandler handler) { irq_handler_ = std::move(handler); }

    void reset();

    // CPU-facing register file ($00-$E1 per the layout documented above
    // op_and_control_write/read).
    uint8_t read(uint8_t reg);
    void write(uint8_t reg, uint8_t value);

    // Direct access to the chip's dedicated wavetable RAM bank (mapped
    // into the host address space by the driver).
    uint8_t ram_read(uint16_t address) const { return ram_[address]; }
    void ram_write(uint16_t address, uint8_t value) { ram_[address] = value; }

    // Advances every active oscillator by one output tick and returns the
    // mixed sample (already scaled to a reasonable int16 range).
    int16_t update();

private:
    struct Oscillator {
        uint16_t freq = 0;
        uint8_t volume = 0;
        uint8_t wave_ptr = 0;   // high byte of the wavetable's start address
        uint8_t control = 0;    // bit0=halt, bits2-1=mode, bit3=IE
        uint8_t wave_size = 0;  // table size = 256 << (wave_size & 7) bytes
        uint32_t accumulator = 0;  // 17.? fixed-point phase (top bits = sample index)
        bool irq_pending = false;
    };

    int oscillator_count() const { return int(((osc_enable_ & 0x3e) >> 1) + 1) * 2; }

    std::array<uint8_t, kRamSize> ram_{};
    std::array<Oscillator, kNumOscillators> osc_{};
    uint8_t osc_enable_ = 0xe0;  // bits5-1: (num active pairs - 1); matches a common power-on default
    IrqHandler irq_handler_;
    uint32_t clock_;
};

}  // namespace dsp
