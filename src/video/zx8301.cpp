#include "video/zx8301.h"

namespace dsp {
namespace {

const uint32_t kPalette[8] = {
    0xff000000, 0xff0000ff, 0xffff0000, 0xffff00ff,
    0xff00ff00, 0xff00ffff, 0xffffff00, 0xffffffff,
};
const int kMode4[4] = {0, 2, 4, 7};

}  // namespace

void Zx8301::reset() {
    ram_.fill(0);
    control_ = 0x02;
    flash_ = true;
}

void Zx8301::control_w(uint8_t value) { control_ = value; }

uint8_t Zx8301::ram_r(uint32_t offset) const { return ram_[offset & (kRamSize - 1)]; }

void Zx8301::ram_w(uint32_t offset, uint8_t value) { ram_[offset & (kRamSize - 1)] = value; }

void Zx8301::tick_flash() { flash_ = !flash_; }

void Zx8301::draw_mode4(uint32_t* framebuffer, int y, uint32_t da) {
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

void Zx8301::draw_mode8(uint32_t* framebuffer, int y, uint32_t da) {
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

void Zx8301::render(uint32_t* framebuffer) {
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
