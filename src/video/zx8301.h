#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace dsp {

// Sinclair ZX8301 video of the QL: 128 KB of RAM (the screen is in it) and
// the 512x256 picture, mode 4 (4 colours) or mode 8 (256 wide, 8 colours
// with flash).
class Zx8301 {
public:
    static constexpr int kWidth = 512;
    static constexpr int kHeight = 256;
    static constexpr uint32_t kRamSize = 0x20000;

    Zx8301();
    void reset();
    void control_w(uint8_t value);
    uint8_t ram_r(uint32_t offset) const { return ram_[offset & (kRamSize - 1)]; }
    void ram_w(uint32_t offset, uint8_t value) { ram_[offset & (kRamSize - 1)] = value; }
    void render(uint32_t* framebuffer) const;
    // One row as three 1-bit planes (red, green, blue), 64 bytes each, the
    // leftmost pixel in bit 0: what the QL's digital RGB output carries, for
    // boards that make their video signal a line at a time.
    void render_row_rgb1(int y, uint8_t* red, uint8_t* green, uint8_t* blue) const;
    // The same as a plain function of a Zx8301* (Machine::Rgb1RowFn), defined
    // in zx8301.cpp so it is in the same object (boards may put it in RAM).
    static void row_rgb1(const void* self, int y, uint8_t* red, uint8_t* green, uint8_t* blue);
    void tick_flash();

    uint8_t control() const { return control_; }
    uint8_t* ram() { return ram_.data(); }

private:
    void draw_mode4(uint32_t* framebuffer, int y, uint32_t da) const;
    void draw_mode8(uint32_t* framebuffer, int y, uint32_t da) const;

    std::vector<uint8_t> ram_;  // its own allocation: the QL object stays small
    uint8_t control_ = 0x02;  // display off after reset
    bool flash_ = true;
};

}  // namespace dsp
