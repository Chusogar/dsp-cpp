#pragma once

#include <array>
#include <cstdint>

#include "cpu/m68000.h"

namespace dsp {

// Atari SLAPSTIC bank switching protection, ported from arcade/misc/slapstic.pas.
class Slapstic {
public:
    struct MaskValue {
        uint32_t mask;
        uint32_t value;
    };

    struct Config {
        int bankstart;
        std::array<uint32_t, 4> bank;
        MaskValue alt1, alt2, alt3, alt4;
        int altshift;
        MaskValue bit1, bit2c0, bit2s0, bit2c1, bit2s1, bit3;
        MaskValue add1, add2, addplus1, addplus2, add3;
    };

    // `cpu` is only needed by the alternate-bank kludge of the 68000 based games.
    Slapstic(int number, M68000* cpu);

    // Reconfigures the chip for another SLAPSTIC number once the ROM set is known.
    void set_type(int number);

    void reset();
    // Returns the currently selected bank after processing the access.
    uint8_t tweak(uint16_t offset);
    uint8_t current_bank() const { return current_bank_; }

private:
    enum State {
        kDisabled = 0,
        kEnabled,
        kAlternate1,
        kAlternate2,
        kAlternate3,
        kBitwise1,
        kBitwise2,
        kBitwise3,
        kAdditive1,
        kAdditive2,
        kAdditive3,
    };

    uint8_t alt2_kludge();

    Config config_;
    M68000* cpu_;
    uint8_t state_ = kDisabled;
    uint8_t current_bank_ = 0;
    uint8_t alt_bank_ = 0;
    uint8_t bit_bank_ = 0;
    uint8_t add_bank_ = 0;
    uint8_t bit_xor_ = 0;
};

}  // namespace dsp
