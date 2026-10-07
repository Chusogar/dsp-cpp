// Minimal PlayStation SPU stub.
// Register layout adapted from ProjectPSX (MIT) by Pedro Cortés:
//   https://github.com/BluestormDNA/ProjectPSX
#include "machine/psx_spu.h"

#include <cstring>

namespace dsp {

void PsxSpu::reset() {
    ram_.fill(0);
    regs_.fill(0);
    transfer_addr_ = 0;
    // SPUSTAT bit 10 = transfer ready-ish; bit 0-5 status.
    regs_[(0x1DA >> 1)] = 0x0000;  // SPUCNT
    // Leave ENDX and voice regs at 0 so BIOS does not hang waiting forever.
}

uint16_t PsxSpu::read16(uint32_t addr) const {
    const uint32_t off = addr & 0x3FF;
    if (off < 0x400) {
        // Voice / control registers 1F801C00-1F801DFF
        if ((off >> 1) < regs_.size()) return regs_[off >> 1];
    }
    return 0;
}

void PsxSpu::write16(uint32_t addr, uint16_t value) {
    const uint32_t off = addr & 0x3FF;
    if ((off >> 1) >= regs_.size()) return;
    regs_[off >> 1] = value;

    // Data transfer address (1F801DA6)
    if (off == 0x1A6) {
        transfer_addr_ = uint32_t(value) << 3;
    }
    // Data FIFO write (1F801DA8)
    if (off == 0x1A8) {
        if (transfer_addr_ + 1 < kRamSize) {
            ram_[transfer_addr_] = uint8_t(value);
            ram_[transfer_addr_ + 1] = uint8_t(value >> 8);
            transfer_addr_ = (transfer_addr_ + 2) & (kRamSize - 1);
        }
    }
    // Key on / key off: acknowledge by clearing AFTER writing ENDX-ish behaviour.
    if (off == 0x188 || off == 0x18A) {
        // KEY ON low/high — stub: mark voices ended immediately so software proceeds.
        regs_[0x19C >> 1] |= value;  // ENDX
    }
    if (off == 0x18C || off == 0x18E) {
        regs_[0x19C >> 1] &= ~value;
    }
}

uint32_t PsxSpu::load32(uint32_t addr) const {
    const uint16_t lo = read16(addr);
    const uint16_t hi = read16(addr + 2);
    return uint32_t(lo) | (uint32_t(hi) << 16);
}

void PsxSpu::write32(uint32_t addr, uint32_t value) {
    write16(addr, uint16_t(value));
    write16(addr + 2, uint16_t(value >> 16));
}

void PsxSpu::dma_write(const uint32_t* data, int words) {
    for (int i = 0; i < words; i++) {
        const uint32_t w = data[i];
        if (transfer_addr_ + 3 < kRamSize) {
            ram_[transfer_addr_] = uint8_t(w);
            ram_[transfer_addr_ + 1] = uint8_t(w >> 8);
            ram_[transfer_addr_ + 2] = uint8_t(w >> 16);
            ram_[transfer_addr_ + 3] = uint8_t(w >> 24);
        }
        transfer_addr_ = (transfer_addr_ + 4) & (kRamSize - 1);
    }
}

void PsxSpu::dma_read(uint32_t* out, int words) {
    for (int i = 0; i < words; i++) {
        uint32_t w = 0;
        if (transfer_addr_ + 3 < kRamSize) {
            w = uint32_t(ram_[transfer_addr_]) |
                (uint32_t(ram_[transfer_addr_ + 1]) << 8) |
                (uint32_t(ram_[transfer_addr_ + 2]) << 16) |
                (uint32_t(ram_[transfer_addr_ + 3]) << 24);
        }
        out[i] = w;
        transfer_addr_ = (transfer_addr_ + 4) & (kRamSize - 1);
    }
}

void PsxSpu::drain_silence(std::vector<int16_t>& out, int sample_count) {
    const size_t old = out.size();
    out.resize(old + size_t(sample_count));
    std::memset(out.data() + old, 0, size_t(sample_count) * sizeof(int16_t));
}

}  // namespace dsp
