#pragma once

#include <cstdint>
#include <vector>

#include "core/rom_loader.h"

namespace dsp {
namespace system18_roms {

// Michael Jackson's Moonwalker (World) — MAME mwalk
inline const std::vector<RomEntry> kMwalkMain = {
    {"epr-13235.a6", 0x40000, 0x00000, 0x6983e129},
    {"epr-13234.a5", 0x40000, 0x00001, 0xc9fd20f2},
};

inline const std::vector<RomEntry> kMwalkKey = {
    {"317-0159.key", 0x2000, 0x0000, 0x507838f0},
};

inline const std::vector<RomEntry> kMwalkTiles = {
    {"mpr-13216.b1", 0x40000, 0x00000, 0x862d2c03},
    {"mpr-13217.b2", 0x40000, 0x40000, 0x7d1ac3ec},
    {"mpr-13218.b3", 0x40000, 0x80000, 0x56d3393c},
};

inline const std::vector<RomEntry> kMwalkSprites = {
    {"mpr-13224.b11", 0x40000, 0x000001, 0xc59f107b},
    {"mpr-13231.a11", 0x40000, 0x000000, 0xa5e96346},
    {"mpr-13223.b10", 0x40000, 0x080001, 0x364f60ff},
    {"mpr-13230.a10", 0x40000, 0x080000, 0x9550091f},
    {"mpr-13222.b9", 0x40000, 0x100001, 0x523df3ed},
    {"mpr-13229.a9", 0x40000, 0x100000, 0xf40dc45d},
    {"epr-13221.b8", 0x40000, 0x180001, 0x9ae7546a},
    {"epr-13228.a8", 0x40000, 0x180000, 0xde3786be},
};

inline const std::vector<RomEntry> kMwalkSound = {
    {"epr-13225.a4", 0x20000, 0x000000, 0x56c2e82b},
    {"mpr-13219.b4", 0x40000, 0x080000, 0x19e2061f},
    {"mpr-13220.b5", 0x40000, 0x100000, 0x58d4d9ce},
    {"mpr-13249.b6", 0x40000, 0x180000, 0x623edc5d},
};

inline const std::vector<RomEntry> kMwalkMcu = {
    {"315-5437.ic4", 0x1000, 0x0000, 0x4bf63bc1},
};

}  // namespace system18_roms
}  // namespace dsp
