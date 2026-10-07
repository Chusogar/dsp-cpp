// PlayStation interrupt controller (I_STAT / I_MASK).
// Ported/adapted from ProjectPSX (MIT) by Pedro Cortés:
//   https://github.com/BluestormDNA/ProjectPSX
#pragma once

#include <cstdint>

namespace dsp {

// IRQ bit masks matching ProjectPSX Interrupt enum / hardware I_STAT.
enum PsxIrq : uint32_t {
    kPsxIrqVblank = 1u << 0,  // IRQ0
    kPsxIrqGpu = 1u << 1,     // IRQ1
    kPsxIrqCdrom = 1u << 2,   // IRQ2
    kPsxIrqDma = 1u << 3,     // IRQ3
    kPsxIrqTmr0 = 1u << 4,    // IRQ4
    kPsxIrqTmr1 = 1u << 5,    // IRQ5
    kPsxIrqTmr2 = 1u << 6,    // IRQ6
    kPsxIrqController = 1u << 7,  // IRQ7
    kPsxIrqSio = 1u << 8,     // IRQ8
    kPsxIrqSpu = 1u << 9,     // IRQ9
    kPsxIrqPio = 1u << 10,    // IRQ10
};

class PsxIrqController {
public:
    void reset();

    void raise(uint32_t irq_bit);
    bool pending() const { return (istat_ & imask_) != 0; }

    uint32_t load(uint32_t addr) const;
    void write(uint32_t addr, uint32_t value);

    uint32_t istat() const { return istat_; }
    uint32_t imask() const { return imask_; }

private:
    uint32_t istat_ = 0;
    uint32_t imask_ = 0;
};

}  // namespace dsp
