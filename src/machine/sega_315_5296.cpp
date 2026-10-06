#include "machine/sega_315_5296.h"

namespace dsp {

void Sega3155296::set_port_read(int port, PortRead handler) {
    if (port >= 0 && port < 8) in_[size_t(port)] = std::move(handler);
}

void Sega3155296::set_port_write(int port, PortWrite handler) {
    if (port >= 0 && port < 8) out_[size_t(port)] = std::move(handler);
}

void Sega3155296::reset() {
    dir_ = 0;
    latch_.fill(0);
    cnt_ = 0;
    for (int i = 0; i < 8; i++) {
        if (out_[size_t(i)]) out_[size_t(i)](0);
    }
    if (cnt_write_) {
        for (int i = 0; i < 3; i++) cnt_write_(i, false);
    }
}

uint8_t Sega3155296::read(uint8_t offset) {
    offset &= 0x3f;
    switch (offset) {
        case 0:
        case 1:
        case 2:
        case 3:
        case 4:
        case 5:
        case 6:
        case 7:
            if (dir_ & (1u << offset)) return latch_[offset];
            return in_[offset] ? in_[offset]() : uint8_t(0xff);
        case 0x08:
            return 'S';
        case 0x09:
            return 'E';
        case 0x0a:
            return 'G';
        case 0x0b:
            return 'A';
        case 0x0c:
        case 0x0e:
            return cnt_;
        case 0x0d:
        case 0x0f:
            return dir_;
        default:
            return 0xff;
    }
}

void Sega3155296::write(uint8_t offset, uint8_t data) {
    offset &= 0x3f;
    switch (offset) {
        case 0:
        case 1:
        case 2:
        case 3:
        case 4:
        case 5:
        case 6:
        case 7:
            latch_[offset] = data;
            if ((dir_ & (1u << offset)) && out_[offset]) out_[offset](data);
            break;
        case 0x0e:
            if (cnt_write_) {
                for (int i = 0; i < 3; i++) cnt_write_(i, ((data >> i) & 1) != 0);
            }
            cnt_ = data;
            break;
        case 0x0f: {
            const uint8_t changed = uint8_t(dir_ ^ data);
            dir_ = data;
            for (int i = 0; i < 8; i++) {
                if ((changed & (1u << i)) && out_[size_t(i)]) {
                    out_[size_t(i)]((dir_ & (1u << i)) ? latch_[size_t(i)] : uint8_t(0));
                }
            }
            break;
        }
        default:
            break;
    }
}

}  // namespace dsp
