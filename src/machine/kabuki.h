#pragma once

#include <cstdint>
#include <vector>

namespace dsp {

// Capcom Kabuki Z80 decrypt, from MAME / FBNeo kabuki.cpp.
// `swap_key1`/`swap_key2` are permutations of 0-7 packed as hex digits.
void kabuki_decode(const uint8_t* src, uint8_t* dest_op, uint8_t* dest_data, int length,
                   uint32_t swap_key1, uint32_t swap_key2, int addr_key, int xor_key);

// CPS1 QSound helper: decrypts 32K of Z80 ROM into opcode and data maps.
void kabuki_cps1_decode(const std::vector<uint8_t>& src, std::vector<uint8_t>& dest_op,
                        std::vector<uint8_t>& dest_data, uint32_t swap_key1, uint32_t swap_key2,
                        int addr_key, int xor_key);

}  // namespace dsp
