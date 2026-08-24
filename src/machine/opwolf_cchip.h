#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace dsp {

// Software C-Chip used by Operation Wolf, ported from opwolf_cchip.pas.
// The real Taito C-Chip MCU is not dumped in dsp-emulator; this simulation
// handles coins, difficulty, level tables and the end-of-stage flags that
// the 68000 expects.
class OpWolfCChip {
public:
    static constexpr int kCommandCycles = 80000;

    void reset();
    void set_rom(const uint16_t* rom, size_t words);

    void set_inputs(uint8_t in0, uint8_t in1);

    uint16_t data_r(uint16_t address) const;
    void data_w(uint16_t address, uint16_t value);
    uint16_t status_r() const { return 1; }
    void status_w(uint16_t value);
    void bank_w(uint16_t value);

    // 60 Hz service (coins, level completion, command dispatch).
    void update();
    // Advances the one-shot "fetch level data" timer, in 68000 cycles.
    void run_cycles(int cycles);

private:
    void update_difficulty(uint8_t mode);
    void apply_coinage(uint8_t dsw_a);
    void complete_level_command();
    void clear_level_flags();

    std::array<uint8_t, 0x400 * 8> ram_{};
    const uint16_t* rom_ = nullptr;
    size_t rom_words_ = 0;

    uint8_t current_cmd_ = 0;
    uint8_t current_bank_ = 0;
    uint8_t last_7a_ = 0;
    uint8_t last_04_ = 0xfc;
    uint8_t last_05_ = 0xff;
    uint8_t c588_ = 0;
    uint8_t c589_ = 0;
    uint8_t c58a_ = 0;
    std::array<uint8_t, 2> coins_{};
    std::array<uint8_t, 2> coins_for_credit_{};
    std::array<uint8_t, 2> credits_for_coin_{};

    int command_cycles_ = -1;
};

}  // namespace dsp
