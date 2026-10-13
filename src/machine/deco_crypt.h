#pragma once

#include <cstddef>
#include <cstdint>

namespace dsp {

// Data East ROM encryption, ported from dsp-emulator deco_decr.pas (MAME
// decocrpt.cpp and deco102.cpp).

// DECO 56 / DECO 74 graphics: each 16 bit word (big endian in `data`) is moved
// to a scrambled address inside its 4 KB block, XORed and bit swapped.
void deco56_decrypt_gfx(uint8_t* data, size_t length);
void deco74_decrypt_gfx(uint8_t* data, size_t length);
// Only the address shuffle of the DECO 56 (a ROM on the same bus as encrypted
// ones but stored plain).
void deco56_remap_gfx(uint8_t* data, size_t length);

// DECO 102 68000 program: `rom` holds `words` 16 bit words (high byte = even
// address). Writes the separate data and opcode views.
void deco102_decrypt_cpu(const uint16_t* rom, uint16_t* opcodes, uint16_t* data,
                         size_t words, uint16_t address_xor, uint16_t data_select_xor,
                         uint16_t opcode_select_xor);

}  // namespace dsp
