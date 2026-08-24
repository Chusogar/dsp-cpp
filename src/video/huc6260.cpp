#include "video/huc6260.h"

namespace dsp {

void HuC6260::reset() {
    addr_ = 0;
    clock_mode_ = 0;
    palette_.fill(0);
}

void HuC6260::write(uint8_t port, uint8_t value) {
    switch (port & 3) {
        case 0:
            addr_ = uint16_t((addr_ & 0xFF00) | value);
            break;
        case 1:
            addr_ = uint16_t((addr_ & 0x00FF) | ((value & 1) << 8));
            clock_mode_ = (value >> 2) & 3;
            break;
        case 2:
            palette_[addr_ & 0x1FF] =
                uint16_t((palette_[addr_ & 0x1FF] & 0x100) | value);
            break;
        case 3:
            palette_[addr_ & 0x1FF] =
                uint16_t((palette_[addr_ & 0x1FF] & 0x0FF) | ((value & 1) << 8));
            addr_ = uint16_t((addr_ + 1) & 0x1FF);
            break;
    }
}

uint8_t HuC6260::read(uint8_t port) {
    switch (port & 3) {
        case 0: return uint8_t(addr_ & 0xFF);
        case 1: return uint8_t((addr_ >> 8) & 1) | uint8_t(clock_mode_ << 2);
        case 2: return uint8_t(palette_[addr_ & 0x1FF] & 0xFF);
        case 3: {
            const uint8_t v = uint8_t((palette_[addr_ & 0x1FF] >> 8) & 1);
            addr_ = uint16_t((addr_ + 1) & 0x1FF);
            return v;
        }
    }
    return 0xFF;
}

uint32_t HuC6260::color(uint16_t index) const {
    const uint16_t c = palette_[index & 0x1FF];
    const int b = (c & 7) * 36;
    const int r = ((c >> 3) & 7) * 36;
    const int g = ((c >> 6) & 7) * 36;
    return 0xFF000000u | (uint32_t(r) << 16) | (uint32_t(g) << 8) | uint32_t(b);
}

}  // namespace dsp
