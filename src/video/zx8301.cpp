#include "video/zx8301.h"

#include <algorithm>
#include <cstring>

namespace dsp {
namespace {

const uint32_t kPalette[8] = {
    0xff000000, 0xff0000ff, 0xffff0000, 0xffff00ff,
    0xff00ff00, 0xff00ffff, 0xffffff00, 0xffffffff,
};
const int kMode4[4] = {0, 2, 4, 7};

// Bit order reversed: the QL keeps the leftmost pixel in bit 7.
struct Tables {
    uint8_t reverse[256];
    // Mode 8, four pixels a byte pair: bits 7, 5, 3, 1 (red of `lo`, green of
    // `hi`) or 6, 4, 2, 0 (blue of `lo`, flash of `hi`), each pixel two wide.
    uint8_t odd_doubled[256], even_doubled[256];
    Tables() {
        for (int v = 0; v < 256; ++v) {
            uint8_t r = 0, odd = 0, even = 0;
            for (int b = 0; b < 8; ++b)
                if (v & (1 << b)) r = uint8_t(r | (0x80 >> b));
            for (int p = 0; p < 4; ++p) {  // pixel p (leftmost 0) uses bits 7-2p / 6-2p
                if (v & (0x80 >> (2 * p))) odd = uint8_t(odd | (3 << (2 * p)));
                if (v & (0x40 >> (2 * p))) even = uint8_t(even | (3 << (2 * p)));
            }
            reverse[v] = r;
            odd_doubled[v] = odd;
            even_doubled[v] = even;
        }
    }
};
const Tables kTables;

}  // namespace

Zx8301::Zx8301() : ram_(kRamSize, 0) {}

void Zx8301::reset() {
    std::fill(ram_.begin(), ram_.end(), uint8_t(0));
    control_ = 0x02;
    flash_ = true;
}

void Zx8301::control_w(uint8_t value) { control_ = value; }

void Zx8301::tick_flash() { flash_ = !flash_; }

void Zx8301::draw_mode4(uint32_t* framebuffer, int y, uint32_t da) const {
    uint32_t* row = framebuffer + y * kWidth;
    int x = 0;
    for (int word = 0; word < 64; word++) {
        uint8_t hi = ram_r(da++);
        uint8_t lo = ram_r(da++);
        for (int pixel = 0; pixel < 8; pixel++) {
            const int color = ((hi >> 7) << 1) | (lo >> 7);
            row[x++] = kPalette[kMode4[color & 3]];
            hi = uint8_t(hi << 1);
            lo = uint8_t(lo << 1);
        }
    }
}

void Zx8301::draw_mode8(uint32_t* framebuffer, int y, uint32_t da) const {
    uint32_t* row = framebuffer + y * kWidth;
    int x = 0;
    bool flash_active = false;
    int flash_color = 0;
    for (int word = 0; word < 64; word++) {
        uint8_t hi = ram_r(da++);
        uint8_t lo = ram_r(da++);
        for (int pixel = 0; pixel < 4; pixel++) {
            int red = (lo >> 7) & 1;
            int green = (hi >> 7) & 1;
            int blue = (lo >> 6) & 1;
            int flash = (hi >> 6) & 1;
            int color = (green << 2) | (red << 1) | blue;
            if (flash_active) color = flash_color;
            if (flash && flash_) {
                flash_active = !flash_active;
                flash_color = color;
            }
            row[x++] = kPalette[color & 7];
            row[x++] = kPalette[color & 7];
            hi = uint8_t(hi << 2);
            lo = uint8_t(lo << 2);
        }
    }
}

void Zx8301::render_row_rgb1(int y, uint8_t* red, uint8_t* green, uint8_t* blue) const {
    if ((control_ & 0x02) != 0 || y < 0 || y >= kHeight) {
        std::memset(red, 0, kWidth / 8);
        std::memset(green, 0, kWidth / 8);
        std::memset(blue, 0, kWidth / 8);
        return;
    }
    uint32_t da = (uint32_t((control_ >> 7) & 1) << 15) + uint32_t(y) * 128;
    if ((control_ & 0x08) == 0) {  // mode 4: green bits in the first byte, red in the second
        for (int word = 0; word < 64; word++, da += 2) {
            const uint8_t hi = ram_r(da), lo = ram_r(da + 1);
            red[word] = kTables.reverse[lo];
            green[word] = kTables.reverse[hi];
            blue[word] = kTables.reverse[uint8_t(hi & lo)];  // white
        }
        return;
    }
    // Mode 8: G F G F.. / R B R B.., four pixels a word, each two wide; a
    // set flash bit (while flash_ is on) toggles holding the colour of that
    // pixel for the rest of the row, as draw_mode8 does.
    bool flash_active = false;
    int flash_color = 0;
    for (int word = 0; word < 64; word++, da += 2) {
        const uint8_t hi = ram_r(da), lo = ram_r(da + 1);
        if (!flash_active && (!flash_ || (hi & 0x55) == 0)) {
            red[word] = kTables.odd_doubled[lo];
            green[word] = kTables.odd_doubled[hi];
            blue[word] = kTables.even_doubled[lo];
            continue;
        }
        uint8_t r = 0, g = 0, b = 0;
        for (int pixel = 0; pixel < 4; pixel++) {
            const int shift = 7 - 2 * pixel;
            int color = (((hi >> shift) & 1) << 2) | (((lo >> shift) & 1) << 1) | ((lo >> (shift - 1)) & 1);
            const int flash = (hi >> (shift - 1)) & 1;
            if (flash_active) color = flash_color;
            if (flash && flash_) {
                flash_active = !flash_active;
                flash_color = color;
            }
            const uint8_t bits = uint8_t(3 << (2 * pixel));
            if (color & 2) r = uint8_t(r | bits);
            if (color & 4) g = uint8_t(g | bits);
            if (color & 1) b = uint8_t(b | bits);
        }
        red[word] = r;
        green[word] = g;
        blue[word] = b;
    }
}

void Zx8301::row_rgb1(const void* self, int y, uint8_t* red, uint8_t* green, uint8_t* blue) {
    static_cast<const Zx8301*>(self)->render_row_rgb1(y, red, green, blue);
}

void Zx8301::render(uint32_t* framebuffer) const {
    if ((control_ & 0x02) != 0) {
        for (int i = 0; i < kWidth * kHeight; i++) framebuffer[i] = 0xff000000;
        return;
    }
    uint32_t da = uint32_t((control_ >> 7) & 1) << 15;
    const bool mode8 = (control_ & 0x08) != 0;
    for (int y = 0; y < kHeight; y++) {
        if (mode8) draw_mode8(framebuffer, y, da);
        else draw_mode4(framebuffer, y, da);
        da += 128;
    }
}

}  // namespace dsp
