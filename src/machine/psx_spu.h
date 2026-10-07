// Minimal PlayStation SPU stub (512 KiB RAM + register R/W).
// Register layout adapted from ProjectPSX (MIT) by Pedro Cortés:
//   https://github.com/BluestormDNA/ProjectPSX
#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace dsp {

class PsxSpu {
public:
    static constexpr int kRamSize = 512 * 1024;
    static constexpr int kSampleRate = 44100;

    void reset();

    uint16_t read16(uint32_t addr) const;
    void write16(uint32_t addr, uint16_t value);

    uint32_t load32(uint32_t addr) const;
    void write32(uint32_t addr, uint32_t value);

    // DMA helpers.
    void dma_write(const uint32_t* data, int words);
    void dma_read(uint32_t* out, int words);

    // Optional CD audio push (ignored by stub; kept for API parity).
    void push_cd_samples(const int16_t* /*samples*/, int /*count*/) {}

    // Returns true if SPU IRQ should fire (stub: never).
    bool tick(int /*cycles*/) { return false; }

    // Append silence samples for the frame (mono or stereo interleaved L/R mono).
    void drain_silence(std::vector<int16_t>& out, int sample_count);

    uint8_t* ram() { return ram_.data(); }
    const uint8_t* ram() const { return ram_.data(); }

private:
    std::array<uint8_t, kRamSize> ram_{};
    std::array<uint16_t, 0x200> regs_{};  // voice + control window mirror
    uint32_t transfer_addr_ = 0;
};

}  // namespace dsp
