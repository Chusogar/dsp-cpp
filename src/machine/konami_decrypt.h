#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace dsp {

// Konami-1 opcode descrambler, from dsp-emulator `konami_decrypt.pas` / MAME
// `konami1.cpp`. Only instruction fetches are encrypted: XOR D1/D3/D5/D7
// according to address bits A1 and A3. `src` is the raw ROM dump; `dest`
// receives the view used for opcode fetches. Length is in bytes.
void konami1_decode(const uint8_t* src, uint8_t* dest, size_t length);

inline std::vector<uint8_t> konami1_decode(const uint8_t* src, size_t length) {
    std::vector<uint8_t> dest(length);
    konami1_decode(src, dest.data(), length);
    return dest;
}

}  // namespace dsp
