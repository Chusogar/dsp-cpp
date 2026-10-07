// PlayStation CD-ROM XA-ADPCM decoder.
// Ported/adapted from ProjectPSX (MIT) by Pedro Cortés:
//   https://github.com/BluestormDNA/ProjectPSX
#pragma once

#include <cstdint>
#include <vector>

namespace dsp {

// Decode one MODE2 Form2 XA sector (2352 bytes) to interleaved stereo S16
// samples at 44100 Hz. Returns empty on unsupported coding (8-bit).
std::vector<int16_t> decode_xa_adpcm_sector(const uint8_t* sector, uint8_t coding_info);

}  // namespace dsp
