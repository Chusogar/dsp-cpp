#include "machine/konami_decrypt.h"

namespace dsp {

void konami1_decode(const uint8_t* src, uint8_t* dest, size_t length) {
    for (size_t address = 0; address < length; address++) {
        uint8_t xorval = (address & 2) ? 0x80 : 0x20;
        xorval = uint8_t(xorval | ((address & 8) ? 0x08 : 0x02));
        dest[address] = uint8_t(src[address] ^ xorval);
    }
}

}  // namespace dsp
