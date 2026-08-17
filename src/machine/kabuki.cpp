#include "machine/kabuki.h"

#include <algorithm>

namespace dsp {
namespace {

int bitswap1(int src, int key, int select) {
    if (select & (1 << ((key >> 0) & 7))) {
        src = (src & 0xfc) | ((src & 0x01) << 1) | ((src & 0x02) >> 1);
    }
    if (select & (1 << ((key >> 4) & 7))) {
        src = (src & 0xf3) | ((src & 0x04) << 1) | ((src & 0x08) >> 1);
    }
    if (select & (1 << ((key >> 8) & 7))) {
        src = (src & 0xcf) | ((src & 0x10) << 1) | ((src & 0x20) >> 1);
    }
    if (select & (1 << ((key >> 12) & 7))) {
        src = (src & 0x3f) | ((src & 0x40) << 1) | ((src & 0x80) >> 1);
    }
    return src;
}

int bitswap2(int src, int key, int select) {
    if (select & (1 << ((key >> 12) & 7))) {
        src = (src & 0xfc) | ((src & 0x01) << 1) | ((src & 0x02) >> 1);
    }
    if (select & (1 << ((key >> 8) & 7))) {
        src = (src & 0xf3) | ((src & 0x04) << 1) | ((src & 0x08) >> 1);
    }
    if (select & (1 << ((key >> 4) & 7))) {
        src = (src & 0xcf) | ((src & 0x10) << 1) | ((src & 0x20) >> 1);
    }
    if (select & (1 << ((key >> 0) & 7))) {
        src = (src & 0x3f) | ((src & 0x40) << 1) | ((src & 0x80) >> 1);
    }
    return src;
}

int bytedecode(int src, uint32_t swap_key1, uint32_t swap_key2, int xor_key, int select) {
    src = bitswap1(src, int(swap_key1 & 0xffff), select & 0xff);
    src = ((src & 0x7f) << 1) | ((src & 0x80) >> 7);
    src = bitswap2(src, int(swap_key1 >> 16), select & 0xff);
    src ^= xor_key;
    src = ((src & 0x7f) << 1) | ((src & 0x80) >> 7);
    src = bitswap2(src, int(swap_key2 & 0xffff), select >> 8);
    src = ((src & 0x7f) << 1) | ((src & 0x80) >> 7);
    src = bitswap1(src, int(swap_key2 >> 16), select >> 8);
    return src;
}

}  // namespace

void kabuki_decode(const uint8_t* src, uint8_t* dest_op, uint8_t* dest_data, int length,
                   uint32_t swap_key1, uint32_t swap_key2, int addr_key, int xor_key) {
    for (int address = 0; address < length; address++) {
        int select = address + addr_key;
        dest_op[address] =
            uint8_t(bytedecode(src[address], swap_key1, swap_key2, xor_key, select));
        select = (address ^ 0x1fc0) + addr_key + 1;
        dest_data[address] =
            uint8_t(bytedecode(src[address], swap_key1, swap_key2, xor_key, select));
    }
}

void kabuki_cps1_decode(const std::vector<uint8_t>& src, std::vector<uint8_t>& dest_op,
                        std::vector<uint8_t>& dest_data, uint32_t swap_key1, uint32_t swap_key2,
                        int addr_key, int xor_key) {
    dest_op.assign(0x8000, 0);
    dest_data.assign(0x8000, 0);
    int length = int(std::min<size_t>(src.size(), 0x8000));
    if (length <= 0) return;
    kabuki_decode(src.data(), dest_op.data(), dest_data.data(), length, swap_key1, swap_key2,
                  addr_key, xor_key);
}

}  // namespace dsp
