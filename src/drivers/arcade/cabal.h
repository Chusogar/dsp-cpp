#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "core/machine.h"
#include "cpu/m68000.h"
#include "machine/seibu_sound.h"
#include "video/gfx.h"

namespace dsp {

// Cabal (TAD, 1988), ported from dsp-emulator cabal_hw.pas (MAME cabal.cpp).
//
// A 68000 at 10 MHz with a fixed 256x256 background of 16x16 tiles, a 32x32
// layer of 2 bpp text and 512 16x16 sprites, 1024 colours of 4-bit RGB. The
// sound board is the Seibu sound system in its ADPCM form (encrypted Z80,
// YM2151 and two ADPCM channels, see machine/seibu_sound.h), which also reads
// the coins. The original cabinet has a trackball; this driver uses the
// joystick version's inputs.
class Cabal : public Machine {
public:
    MachineType machine_type() const override { return MachineType::Arcade; }

    static constexpr uint32_t kMainClock = 20000000 / 2;
    static constexpr uint32_t kSoundClock = 3579545;
    static constexpr int kScanlines = 256;
    static constexpr double kFramesPerSecond = 59.60;
    static constexpr int kWidth = 256;
    static constexpr int kHeight = 224;

    Cabal();

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

    void drain_audio(std::vector<int16_t>& out) override { sound_.drain_audio(out); }
    int sample_rate() const override { return SeibuSound::kSampleRate; }

    const char* title() const override { return "Cabal"; }

    // Test hooks.
    uint32_t main_pc() const { return main_cpu_.pc(); }
    const SeibuSound& sound() const { return sound_; }

private:
    uint16_t main_read(uint32_t address);
    void main_write(uint32_t address, uint16_t value);
    void update_video();

    M68000 main_cpu_{kMainClock};
    SeibuSound sound_{kSoundClock, 1000000, SeibuSound::Variant::Adpcm};

    std::array<uint16_t, 0x20000> rom_{};
    std::array<uint16_t, 0x8000> ram_{};  // 40000-4ffff, sprites at 43800
    std::array<uint16_t, 0x400> fg_ram_{};
    std::array<uint16_t, 0x200> bg_ram_{};
    std::array<uint16_t, 0x400> pal_ram_{};
    std::array<uint32_t, 0x400> palette_{};

    GfxSet chars_, tiles_, sprites_;
    std::vector<uint32_t> screen_;  // 256x256
    std::vector<uint32_t> framebuffer_;

    uint16_t in0_ = 0xffff, in1_ = 0xffff, dsw_ = 0xefff;
    double main_debt_ = 0, sound_debt_ = 0;
};

}  // namespace dsp
