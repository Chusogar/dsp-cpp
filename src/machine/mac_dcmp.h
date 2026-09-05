#pragma once

#include <cstdint>
#include <vector>

namespace dsp {

// System 7 compressed-resource header is 0xA89F6572.
// Type 8 / dcmp 0 (DonnBits) is what the Finder uses for CODE.
// Type 9 / dcmp 2 (GreggyBits) is what the System file itself uses.
// Returns empty on failure or if the data is not compressed.
std::vector<uint8_t> mac_decompress_resource(const uint8_t* data, size_t size);

}  // namespace dsp
