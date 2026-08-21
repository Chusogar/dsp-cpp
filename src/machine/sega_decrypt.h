#pragma once

#include <cstdint>
#include <vector>

namespace dsp {

// Sega's early-80s Z80 opcode scramblers. Both boards feed the CPU two
// different views of the same ROM: `opcodes` for M1 (instruction fetch)
// cycles and `data` for every other read. Neither table is copyrighted
// game content -- they are the well known, purely mathematical bitswap
// descrambling tables published in MAME's segacrpt/segacrp2 drivers.
//
// `sega_decrypt` covers the classic "System 1" scrambler (Pitfall II,
// Teddy Boy Blues, Pengo, Mr. Viking, Sega Ninja, Up'n Down, Flicky, Super
// Zaxxon, Future Spy -- selected by `SegaDecryptGame`).
enum class SegaDecryptGame {
    Pitfall2 = 0,
    TeddyboyBlues = 1,
    Pengo = 2,
    MrViking = 3,
    SegaNinja = 4,
    UpNDown = 5,
    Flicky = 6,
    SuperZaxxon = 7,
    FutureSpy = 8,
};

// `rom` is the raw (encrypted) ROM image, exactly as dumped, 0x8000 bytes.
// On return, `data` holds the decoded "read as data" view (same buffer the
// caller should keep mapped for LD A,(nn) etc.) and `opcodes` holds the
// decoded "read as instruction" view (used only for M1 fetches).
void sega_decrypt(const std::vector<uint8_t>& rom, SegaDecryptGame game,
                  std::vector<uint8_t>* data, std::vector<uint8_t>* opcodes);

// The newer "type 2" scrambler used by the 315-5177 (Wonder Boy) and
// 315-5179 chips, plus the related 317-000x scrambler (Gardia/Calorie Kun).
enum class SegaDecrypt2Chip {
    S315_5179 = 0,
    S315_5177 = 1,
    S317_000X = 2,
};

void sega_decrypt_type2(const std::vector<uint8_t>& rom, SegaDecrypt2Chip chip, int shift,
                        std::vector<uint8_t>* data, std::vector<uint8_t>* opcodes);

}  // namespace dsp
