#pragma once

#include <array>
#include <cstdint>

namespace dsp {

// Sinclair ZX8301 "Iris" — 128K DRAM + Mode 4/8 video for the QL.
class Zx8301 {
public:
    static constexpr int kWidth = 512;
    static constexpr int kHeight = 256;
    static constexpr uint32_t kRamSize = 0x20000;

    void reset();
    void control_w(uint8_t value);
    uint8_t ram_r(uint32_t offset) const;
    void ram_w(uint32_t offset, uint8_t value);
    void render(uint32_t* framebuffer);
    void tick_flash();

    uint8_t control() const { return control_; }
    uint8_t* ram() { return ram_.data(); }

private:
    void draw_mode4(uint32_t* framebuffer, int y, uint32_t da);
    void draw_mode8(uint32_t* framebuffer, int y, uint32_t da);

    std::array<uint8_t, kRamSize> ram_{};
    uint8_t control_ = 0x02;  // display off after reset
    bool flash_ = true;
};

}  // namespace dsp
