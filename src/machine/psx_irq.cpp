// PlayStation interrupt controller (I_STAT / I_MASK).
// Ported/adapted from ProjectPSX (MIT) by Pedro Cortés:
//   https://github.com/BluestormDNA/ProjectPSX
#include "machine/psx_irq.h"

namespace dsp {

void PsxIrqController::reset() {
    istat_ = 0;
    imask_ = 0;
}

void PsxIrqController::raise(uint32_t irq_bit) {
    istat_ |= irq_bit & 0x7FFu;
}

uint32_t PsxIrqController::load(uint32_t addr) const {
    switch (addr & 0xF) {
        case 0: return istat_;
        case 4: return imask_;
        default: return 0xFFFFFFFFu;
    }
}

void PsxIrqController::write(uint32_t addr, uint32_t value) {
    switch (addr & 0xF) {
        case 0:
            // Acknowledge: write 0 to clear bits.
            istat_ &= value & 0x7FFu;
            break;
        case 4:
            imask_ = value & 0x7FFu;
            break;
        default:
            break;
    }
}

}  // namespace dsp
