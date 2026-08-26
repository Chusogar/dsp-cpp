#include "machine/nb1414_m4.h"
#include <algorithm>
#include <cstring>

namespace dsp {

void Nb1414M4::load_rom(const std::vector<uint8_t>& data) {
    rom_.fill(0);
    std::memcpy(rom_.data(), data.data(), std::min(data.size(), rom_.size()));
}

void Nb1414M4::reset() { frame_ = 0; }

void Nb1414M4::exec(uint16_t& scroll_fg_x, uint16_t& scroll_fg_y, uint8_t frame) {
    if (mem_ == nullptr) return;
    scroll_fg_x = uint16_t(mem_[0x0d] | (mem_[0x0e] << 8));
    scroll_fg_y = uint16_t(mem_[0x0b] | (mem_[0x0c] << 8));
    const uint16_t command = uint16_t((mem_[0] << 8) | mem_[1]);
    frame_ = frame;
    switch (command & 0xff00) {
        case 0x0000: insert_coin_msg(); credit_msg(); break;
        case 0x0200: cmd_0200(uint8_t(command & 0x87)); break;
        case 0x0600: cmd_0600(uint8_t(command & 1)); break;
        case 0x0e00: cmd_0e00(uint8_t(command & 0xff)); break;
        default: break;
    }
}

void Nb1414M4::dma(uint16_t src, uint16_t dst, uint16_t size, uint8_t condition) {
    if (mem_ == nullptr) return;
    for (uint16_t f = 0; f < size; f++) {
        if (uint16_t(f + dst) < 18) continue;
        mem_[f + dst] = condition ? rom_[(f + src) & 0x3fff] : 0x20;
        mem_[f + dst + 0x400] = rom_[(f + size + src) & 0x3fff];
    }
}

void Nb1414M4::fill(uint16_t dst, uint8_t tile, uint8_t pal) {
    if (mem_ == nullptr) return;
    for (uint16_t f = 0; f < 0x400; f++) {
        if (uint16_t(f + dst) < 18) continue;
        mem_[f + dst] = tile;
        mem_[f + dst + 0x400] = pal;
    }
}

void Nb1414M4::insert_coin_msg() {
    if (mem_ == nullptr) return;
    const uint8_t fl = frame_ & 0x10;
    if (mem_[0x0f] == 0)
        dma(0x03, uint16_t(((rom_[0x01] << 8) | rom_[0x02]) & 0x3fff), 0x10, fl);
    else
        dma(0x4b, uint16_t(((rom_[0x49] << 8) | rom_[0x4a]) & 0x3fff), 0x18, 1);
}

void Nb1414M4::credit_msg() {
    if (mem_ == nullptr) return;
    const uint8_t credits = mem_[0x0f];
    const uint8_t fl = frame_ & 0x10;
    dma(0x25, uint16_t(((rom_[0x23] << 8) | rom_[0x24]) & 0x3fff), 0x10, 1);
    uint16_t dst = uint16_t((((rom_[0x45] << 8) | rom_[0x46]) & 0x3fff) + 1);
    mem_[dst] = uint8_t(credits + 0x30);
    mem_[dst + 0x400] = rom_[0x48];
    if (credits == 1)
        dma(0x7d, uint16_t(((rom_[0x7b] << 8) | rom_[0x7c]) & 0x3fff), 0x18, fl);
    else if (credits > 1)
        dma(0xaf, uint16_t(((rom_[0xad] << 8) | rom_[0xae]) & 0x3fff), 0x18, fl);
}

void Nb1414M4::cmd_0200(uint8_t command) {
    const int idx = 0x330 + ((command & 0x0f) * 2);
    const uint16_t dst = uint16_t(((rom_[idx] << 8) | rom_[idx + 1]) & 0x3fff);
    if (dst & 0x7ff)
        fill(0, rom_[dst & 0x3fff], rom_[(dst + 1) & 0x3fff]);
    else
        dma(dst, 0, 0x400, 1);
}

void Nb1414M4::cmd_0600(uint8_t is2p) {
    if (mem_ == nullptr) return;
    uint16_t dst = uint16_t(((rom_[0x1f5] << 8) | rom_[0x1f6]) & 0x3fff);
    mem_[dst] = uint8_t((mem_[7] & 0x07) + 0x30);
    dst = uint16_t(((rom_[0x1f8] << 8) | rom_[0x1f9]) & 0x3fff);
    dma(uint16_t(0x1fa + (((mem_[7] & 0x30) >> 4) * 0x18)), dst, 12, 1);
    dst = uint16_t(((rom_[0x262] << 8) | rom_[0x263]) & 0x3fff);
    dma(uint16_t(0x264 + (((mem_[7] & 0x80) >> 7) * 0x18)), dst, 12, 1);
    dst = uint16_t(((rom_[0x294] << 8) | rom_[0x295]) & 0x3fff);
    dma(uint16_t(0x296 + (((mem_[7] & 0x40) >> 6) * 0x18)), dst, 12, 1);
    dst = uint16_t(((rom_[0x2c6] << 8) | rom_[0x2c7]) & 0x3fff);
    mem_[dst] = uint8_t(((mem_[0x0f] & 0xf0) >> 4) + 0x30);
    dst = uint16_t(((rom_[0x2c9] << 8) | rom_[0x2ca]) & 0x3fff);
    mem_[dst] = uint8_t((mem_[0x0f] & 0x0f) + 0x30);
    dst = uint16_t(((rom_[0x2cc] << 8) | rom_[0x2cd]) & 0x3fff);
    mem_[dst] = uint8_t(((mem_[0x10] & 0xf0) >> 4) + 0x30);
    dst = uint16_t(((rom_[0x2cf] << 8) | rom_[0x2d0]) & 0x3fff);
    mem_[dst] = uint8_t((mem_[0x10] & 0x0f) + 0x30);
    dst = uint16_t(((rom_[0x2d2] << 8) | rom_[0x2d3]) & 0x3fff);
    mem_[dst] = uint8_t(((mem_[0x11] & 0xf0) >> 4) + 0x30);
    mem_[dst + 1] = uint8_t((mem_[0x11] & 0x0f) + 0x30);
    dst = uint16_t(((rom_[0x2d6] << 8) | rom_[0x2d7]) & 0x3fff);
    dma(uint16_t(0x2d8 + is2p * 0x18), dst, 12, 1);
    dst = uint16_t(((rom_[0x308] << 8) | rom_[0x309]) & 0x3fff);
    for (int f = 0; f < 5; f++)
        dma(uint16_t(0x310 + (((mem_[0x04] >> (4 - f)) & 1) * 6)), uint16_t(dst + f * 0x20), 3, 1);
    dst = uint16_t(((rom_[0x30a] << 8) | rom_[0x30b]) & 0x3fff);
    for (int f = 0; f < 7; f++)
        dma(uint16_t(0x310 + (((mem_[0x02 + is2p] >> (6 - f)) & 1) * 6)), uint16_t(dst + f * 0x20), 3, 1);
    dst = uint16_t(((rom_[0x30c] << 8) | rom_[0x30d]) & 0x3fff);
    for (int f = 0; f < 8; f++)
        dma(uint16_t(0x310 + (((mem_[0x05] >> (7 - f)) & 1) * 6)), uint16_t(dst + f * 0x20), 3, 1);
    dst = uint16_t(((rom_[0x30e] << 8) | rom_[0x30f]) & 0x3fff);
    for (int f = 0; f < 8; f++)
        dma(uint16_t(0x310 + (((mem_[0x06] >> (7 - f)) & 1) * 6)), uint16_t(dst + f * 0x20), 3, 1);
}

void Nb1414M4::cmd_0e00(uint8_t command) {
    if (mem_ == nullptr) return;
    uint16_t dst = uint16_t(((rom_[0xdf] << 8) | rom_[0xe0]) & 0x3fff);
    dma(0xe1, dst, 8, 1);
    if (command & 0x04) {
        dst = uint16_t(((rom_[0xfb] << 8) | rom_[0xfc]) & 0x3fff);
        dma(0xfd, dst, 8, uint8_t((command & 1) == 0));
        dst = uint16_t(((rom_[0x10d] << 8) | rom_[0x10e]) & 0x3fff);
        kozure_score_msg(dst, 0);
        if (command & 0x80) {
            dst = uint16_t(((rom_[0x117] << 8) | rom_[0x118]) & 0x3fff);
            dma(0x119, dst, 8, uint8_t((command & 2) == 0));
            dst = uint16_t(((rom_[0x129] << 8) | rom_[0x12a]) & 0x3fff);
            kozure_score_msg(dst, 1);
        }
    } else {
        dst = uint16_t(((rom_[0x133] << 8) | rom_[0x134]) & 0x3fff);
        dma(0x135, dst, 0x10, uint8_t((command & 1) == 0));
        insert_coin_msg();
        if ((command & 0x18) == 0) credit_msg();
    }
}

void Nb1414M4::kozure_score_msg(uint16_t dst, uint8_t src_base) {
    if (mem_ == nullptr) return;
    uint8_t first = 0;
    for (int f = 0; f < 6; f++) {
        const uint8_t res = uint8_t(
            (mem_[(f >> 1) + 5 + src_base * 3] >> ((~(f & 1) & 1) * 4)) & 0x0f);
        mem_[f + dst] = (first || res) ? uint8_t(res + 0x30) : 0x20;
        if (res) first = 1;
        mem_[f + dst + 0x400] = rom_[0x10f + src_base * 0x1c + f];
    }
    mem_[6 + dst] = 0x30;
    mem_[6 + dst + 0x400] = rom_[0x10f + src_base * 0x1c + 6];
    mem_[7 + dst] = 0x30;
    mem_[7 + dst + 0x400] = rom_[0x10f + src_base * 0x1c + 7];
}

}  // namespace dsp
