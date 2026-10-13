#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "core/machine.h"
#include "cpu/hu6280.h"
#include "cpu/m68000.h"
#include "machine/deco146.h"
#include "sound/okim6295.h"
#include "sound/ym2151.h"
#include "video/deco16ic.h"
#include "video/gfx.h"

namespace dsp {

// Boogie Wings (Data East, 1992), ported from dsp-emulator boogiewings_hw.pas
// (MAME boogwing.cpp).
//
// A 68000 at 14 MHz whose program is encrypted with the DECO 102 (separate
// opcode and data views), a DECO 104 protection chip that also reads the
// inputs and passes the sound commands, two DECO 55/16IC tilemap chips (the
// first one with 5 bpp 16x16 tiles, all graphics encrypted with the DECO 56),
// two sets of DECO 52 sprites and a 2048 colour palette faded by the DECO ACE.
// The sound board is a HuC6280 with a YM2151 and two OKI M6295 whose sample
// banks are switched by the YM2151 port.
class BoogieWings : public Machine {
public:
    MachineType machine_type() const override { return MachineType::Arcade; }

    static constexpr uint32_t kMainClock = 28000000 / 2;
    static constexpr uint32_t kSoundXtal = 32220000;
    static constexpr uint32_t kSoundClock = kSoundXtal / 12;
    static constexpr uint32_t kYmClock = kSoundXtal / 9;
    static constexpr int kScanlines = 274;
    static constexpr double kFramesPerSecond = 57.79965;
    static constexpr int kWidth = 320;
    static constexpr int kHeight = 240;
    static constexpr int kFirstLine = 8;

    BoogieWings();

    bool init(const std::string& rom_path, std::string* error) override;
    void reset() override;
    void run_frame() override;

    void set_inputs(const MachineInputs& inputs) override;
    // Bank 0: low byte of the 16-bit DIP word, bank 1: high byte.
    void set_dip_switch(int bank, uint8_t value) override;

    const uint32_t* framebuffer() const override { return framebuffer_.data(); }
    int screen_width() const override { return kWidth; }
    int screen_height() const override { return kHeight; }
    double frames_per_second() const override { return kFramesPerSecond; }

    void drain_audio(std::vector<int16_t>& out) override;
    int sample_rate() const override { return YM2151::kSampleRate; }

    const char* title() const override { return "Boogie Wings"; }

    // Test hooks.
    uint32_t main_pc() const { return main_cpu_.pc(); }
    int sound_commands() const { return sound_commands_; }
    uint16_t sound_pc() const { return sound_cpu_.pc(); }
    uint16_t ram_word(size_t index) const { return ram_[index & 0x7fff]; }

private:
    uint16_t main_read(uint32_t address);
    void main_write(uint32_t address, uint16_t value);
    uint8_t sound_read(uint32_t address);
    void sound_write(uint32_t address, uint8_t value);
    void on_sound_cycles(int cycles);
    void update_palette();
    void draw_sprites(int set, int priority);
    void update_video();

    M68000 main_cpu_{kMainClock};
    HuC6280 sound_cpu_{kSoundClock};
    YM2151 ym_{kYmClock};
    OKIM6295 oki1_{kSoundXtal / 32, true};
    OKIM6295 oki2_{kSoundXtal / 16, true};
    Deco146 deco104_{Deco146::Type::Deco104,
                     Deco146::kInterfaceScrambleReverse | Deco146::kUseMagicAddressXor};
    Deco16ic tilegen_[2];

    std::vector<uint16_t> rom_opcodes_, rom_data_;  // 0x80000 words each
    std::vector<uint8_t> sound_rom_;
    std::array<uint16_t, 0x8000> ram_{};
    std::array<uint16_t, 0x400> sprite_ram_[2];
    std::array<uint32_t, 0x800> palette_ram_{}, palette_buffer_{};
    std::array<uint16_t, 0x28> ace_ram_{};
    std::array<uint8_t, 0x2000> sound_ram_{};
    std::array<uint32_t, 0x1000> palette_{};  // 0-7ff faded, 800-fff as written

    GfxSet chars_, tiles1_, tiles2_, sprites_[2];
    std::vector<uint32_t> screen_;  // 320x240
    std::vector<uint32_t> framebuffer_;

    uint16_t in0_ = 0xffff, in1_ = 0x0007, dsw_ = 0xffff, priority_ = 0;
    uint8_t sound_latch_ = 0;
    uint32_t frame_counter_ = 0;
    double main_debt_ = 0, sound_debt_ = 0;
    int64_t ym_accum_ = 0, oki1_accum_ = 0, oki2_accum_ = 0, audio_accum_ = 0;
    int32_t oki1_level_ = 0, oki2_level_ = 0;
    int sound_commands_ = 0;
    std::vector<int16_t> audio_;
};

}  // namespace dsp
