#pragma once

#include <cstdint>
#include <vector>

namespace dsp {

enum class Fd1089Type { A, B };

// Hitachi FD1089A/B 68000 encryption, ported from fd1089.pas (MAME algorithm).
void fd1089_decrypt(const uint16_t* src, uint16_t* opcodes, uint16_t* data, uint32_t bytes,
                    const uint8_t* key, Fd1089Type type);

}  // namespace dsp
