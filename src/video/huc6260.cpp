#include "video/huc6260.h"

namespace dsp {
namespace {

// The colour table holds 3 bits per gun, so every level is repeated to fill a
// byte the same way the composite output does.
constexpr uint32_t expand(int level) {
    const uint32_t value = uint32_t(level & 7);
    return (value << 5) | (value << 2) | (value >> 1);
}

}  // namespace

void HuC6260::reset() {
    table_.fill(0);
    for (int i = 0; i < kEntries; i++) refresh(i);
    address_ = 0;
    control_ = 0;
    latch_ = 0;
}

void HuC6260::refresh(int index) {
    const uint16_t value = table_[size_t(index)];
    const uint32_t g = expand((value >> 6) & 7);
    const uint32_t r = expand((value >> 3) & 7);
    const uint32_t b = expand(value & 7);
    argb_[size_t(index)] = 0xff000000u | (r << 16) | (g << 8) | b;
}

int HuC6260::active_width() const {
    switch (control_ & 3) {
        case 0: return 256;
        case 1: return 341;
        default: return 512;
    }
}

uint8_t HuC6260::read(uint8_t offset) {
    switch (offset & 7) {
        case 4: return uint8_t(table_[size_t(address_)] & 0xff);
        case 5: {
            const uint8_t value = uint8_t((table_[size_t(address_)] >> 8) & 0x01);
            address_ = uint16_t((address_ + 1) & (kEntries - 1));
            // Only bit 0 is driven; the rest of the bus floats high.
            return uint8_t(value | 0xfe);
        }
        default: return 0xff;
    }
}

void HuC6260::write(uint8_t offset, uint8_t value) {
    switch (offset & 7) {
        case 0:
            control_ = value;
            break;
        case 2:
            address_ = uint16_t((address_ & 0x100) | value);
            break;
        case 3:
            address_ = uint16_t((address_ & 0xff) | (uint16_t(value & 1) << 8));
            break;
        case 4:
            latch_ = value;
            table_[size_t(address_)] = uint16_t((table_[size_t(address_)] & 0x100) | value);
            refresh(address_);
            break;
        case 5:
            table_[size_t(address_)] = uint16_t((uint16_t(value & 1) << 8) | latch_);
            refresh(address_);
            address_ = uint16_t((address_ + 1) & (kEntries - 1));
            break;
        default:
            break;
    }
}

}  // namespace dsp
