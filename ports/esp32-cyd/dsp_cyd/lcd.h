#pragma once

#include <cstdint>

// 320x240 SPI panel of the CYD (ILI9341, or ST7789 on the "CYD2USB"), on
// HSPI with DMA. Pixels are RGB565 sent big-endian.
namespace cyd {

struct LcdConfig {
    bool st7789 = false;   // false: ILI9341
    bool invert = false;   // display inversion (some panels need it)
    bool bgr = true;       // colour order
    bool flip = false;     // rotate 180 degrees
    int mhz = 40;          // SPI clock
};

bool lcd_begin(const LcdConfig& cfg);
// Sets the window for the following pixels (inclusive coordinates).
void lcd_window(int x0, int y0, int x1, int y1);
// Sends `count` pixels (already big-endian RGB565) to the window; the buffer
// must stay untouched until lcd_wait() (two buffers can be in flight).
void lcd_push(const uint16_t* pixels, int count);
void lcd_wait();
// Fills a rectangle with one colour.
void lcd_fill(int x, int y, int w, int h, uint16_t rgb565);
// DMA-capable line buffers owned by the driver: kBandRows rows of 320 pixels.
constexpr int kBandRows = 16;
uint16_t* lcd_band(int index);  // index 0 or 1

inline uint16_t to565be(uint32_t argb) {
    const uint16_t c = uint16_t(((argb >> 8) & 0xf800) | ((argb >> 5) & 0x07e0) | ((argb >> 3) & 0x001f));
    return uint16_t((c >> 8) | (c << 8));
}

}  // namespace cyd
