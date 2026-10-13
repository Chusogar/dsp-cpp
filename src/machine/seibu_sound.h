#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

#include "cpu/z80.h"
#include "sound/msm5205.h"
#include "sound/okim6295.h"
#include "sound/ym2151.h"
#include "sound/ym3812.h"

namespace dsp {

// Seibu Kaihatsu sound system (seibu_sound.pas, MAME seibu_sound_device): a
// Z80 talking to the main CPU through a pair of latches and two interrupt
// sources (RST 10h from the FM chip timers, RST 18h when the main CPU posts a
// command). The coin inputs are read by the sound CPU and passed on to the
// main program. Two variants:
//   - OKI: YM3812 and an OKI M6295, the program ROM banked at 8000-ffff
//     (Blood Bros. / Sky Smasher and other Seibu-designed boards);
//   - ADPCM: YM2151 and two Seibu ADPCM channels (MSM5205 playing from ROM
//     between a start and an end address set by the Z80), no banking (Cabal).
class SeibuSound {
public:
    enum class Variant { Oki, Adpcm };
    static constexpr int kSampleRate = YM3812::kSampleRate;

    // `clock`: Z80 and FM chip clock (3.579545 MHz on most boards).
    explicit SeibuSound(uint32_t clock, uint32_t oki_clock = 1000000, Variant variant = Variant::Oki);

    SeibuSound(const SeibuSound&) = delete;
    SeibuSound& operator=(const SeibuSound&) = delete;

    // 64 KB sound program: 0000-1fff fixed, 8000-ffff two banks (the upper
    // and lower halves of the ROM). `encrypted` applies the Seibu opcode/data
    // scrambling of the first 8 KB (SEI0100 boards).
    void load_program(const std::vector<uint8_t>& rom, bool encrypted);
    void load_samples(std::vector<uint8_t> rom) { oki_.set_rom(std::move(rom)); }
    // ADPCM variant: one ROM per channel; the data lines are scrambled on the
    // board, so every byte is bit swapped here.
    void load_adpcm(const std::vector<uint8_t>& channel0, const std::vector<uint8_t>& channel1);

    void reset();

    // Main CPU side, register `offset` = (address & 0xf) >> 1.
    uint8_t main_read(int offset) const;
    void main_write(int offset, uint8_t value);

    // Coin switches seen by the sound CPU at 4013 (bit 0 coin 1, bit 1 coin 2,
    // active high).
    void set_coins(uint8_t value) { coins_ = value; }

    // Runs the Z80 for `cycles` T-states, generating sound on the way.
    int run(int cycles);
    uint32_t clock() const { return clock_; }

    void drain_audio(std::vector<int16_t>& out);

    // Test hooks.
    uint16_t pc() const { return z80_.pc(); }
    int rst18_count() const { return rst18_count_; }

private:
    enum IrqEvent {
        kVectorInit,
        kRst10Assert,
        kRst10Clear,
        kRst10Acknowledge,
        kRst10Eoi,
        kRst18Assert,
        kRst18Acknowledge,
        kRst18Eoi,
    };

    uint8_t read(uint16_t address);
    void write(uint16_t address, uint8_t value);
    void update_irq_lines(IrqEvent event);
    void on_cycles(int cycles);

    void adpcm_write(int channel, uint16_t address, uint8_t value);

    uint32_t clock_;
    Variant variant_;
    Z80 z80_;
    YM3812 ym_;
    OKIM6295 oki_;
    std::unique_ptr<YM2151> ym2151_;          // ADPCM variant
    std::unique_ptr<MSM5205> adpcm_[2];       // ADPCM variant

    std::array<uint8_t, 0x2000> data_{};    // 0000-1fff as data
    std::array<uint8_t, 0x2000> opcode_{};  // 0000-1fff as opcodes
    std::array<std::array<uint8_t, 0x8000>, 2> banks_{};
    std::array<uint8_t, 0x800> ram_{};

    std::array<uint8_t, 2> sound_latch_{}, sub2main_{};
    bool main2sub_pending_ = false, sub2main_pending_ = false;
    bool rst10_irq_ = false, rst18_irq_ = false;
    bool rst10_service_ = false, rst18_service_ = false;
    uint8_t bank_ = 0, coins_ = 0;
    int rst18_count_ = 0;

    int64_t ym_accum_ = 0, oki_accum_ = 0, adpcm_accum_ = 0;
    int32_t oki_level_ = 0;
    std::vector<int16_t> audio_;
};

}  // namespace dsp
