#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "cpu/z80.h"
#include "sound/msm5205.h"
#include "sound/ym2151.h"

namespace dsp {

// Seibu sound system used by Cabal / Raiden / etc. (dsp-emulator seibu_sound.pas).
// Cabal uses the ADPCM variant: Z80 @ ~3.58 MHz, YM2151, two MSM5205s, and an
// encrypted 8 KiB opcode window at $0000–$1FFF.
class SeibuSound {
public:
    static constexpr uint32_t kDefaultClock = 3579545;
    static constexpr uint32_t kMsmClock = 12000000 / 32;  // 375000

    explicit SeibuSound(uint32_t clock = kDefaultClock);

    // `boot` is the first 8 KiB (encrypted on Cabal). `bank` is the 32 KiB ROM at $8000.
    bool init_roms(const uint8_t* boot, size_t boot_size, const uint8_t* bank, size_t bank_size,
                   bool encrypted, std::string* error);

    // Two ADPCM banks of `bank_size` bytes each (Cabal: 64 KiB + 64 KiB), bit-swapped.
    void load_adpcm_roms(const uint8_t* data, size_t bank_size);

    void reset();
    // Run the Z80 for `cycles` of its clock; advances YM timers, MSM VCLK and audio.
    // Returns the T-states actually executed.
    int run(int cycles);

    // Main-CPU mailbox at $E8000 + offset*2 (offset 0..6), matching seibu_snd_0.get/put.
    uint8_t main_read(int offset) const;
    void main_write(int offset, uint8_t value);

    uint8_t& coin_input() { return input_; }
    uint8_t coin_input() const { return input_; }

    void drain_audio(std::vector<int16_t>& out);
    int sample_rate() const { return YM2151::kSampleRate; }

    Z80& z80() { return z80_; }
    YM2151& ym() { return ym_; }

private:
    enum IrqOp {
        VectorInit = 0,
        Rst10Assert,
        Rst10Clear,
        Rst10Ack,
        Rst10Eoi,
        Rst18Assert,
        Rst18Ack,
        Rst18Eoi,
    };

    uint8_t read(uint16_t address);
    void write(uint16_t address, uint8_t value);
    uint8_t read_opcode(uint16_t address);
    void update_irq_lines(IrqOp op);
    void refresh_irq_line();
    void on_ym_irq(bool state);
    void on_cycles(int cycles);
    static uint8_t decrypt_data(uint16_t a, uint8_t src);
    static uint8_t decrypt_opcode(uint16_t a, uint8_t src);
    void decrypt_sound();

    uint32_t clock_;
    Z80 z80_;
    YM2151 ym_;
    MSM5205 msm0_;
    MSM5205 msm1_;

    std::array<uint8_t, 0x10000> mem_{};
    std::array<uint8_t, 0x2000> decrypt_{};
    std::array<uint8_t, 2> sound_latch_{};
    std::array<uint8_t, 2> sub2main_{};

    bool main2sub_pending_ = false;
    bool sub2main_pending_ = false;
    bool rst10_irq_ = false;
    bool rst18_irq_ = false;
    bool rst10_service_ = false;
    bool rst18_service_ = false;
    uint8_t input_ = 0xfc;
    uint8_t current_irq_vector_ = 0;

    int64_t msm_accum_ = 0;
    int64_t audio_accum_ = 0;
    std::vector<int16_t> audio_;
};

}  // namespace dsp
