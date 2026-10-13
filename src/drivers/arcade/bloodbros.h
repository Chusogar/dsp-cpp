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

// Blood Bros. (TAD, 1990) and Sky Smasher (Nihon System, 1990), ported from
// dsp-emulator bloodbros_hw.pas (MAME bloodbros.cpp).
//
// A 68000 at 10 MHz and the Seibu sound system (Z80, YM3812, OKI M6295; see
// machine/seibu_sound.h), which also reads the coins. Video: an 8x8 text
// layer, two scrolling 16x16 tile layers (512x256) and 512 16x16 sprites of
// up to 8x8 tiles, with a priority bit that puts them behind the front tile
// layer. 2048 colours, xxxxBBBBGGGGRRRR. Sky Smasher is a vertical game and
// scrambles the CRT register addresses.
class BloodBros : public Machine {
public:
    MachineType machine_type() const override { return MachineType::Arcade; }
    enum class Game { BloodBros, SkySmasher };

    static constexpr uint32_t kMainClock = 20000000 / 2;
    static constexpr uint32_t kSoundClock = 7159090 / 2;
    static constexpr int kScanlines = 256;
    static constexpr double kFramesPerSecond = 59.39;
    static constexpr int kWidth = 256;
    static constexpr int kHeight = 224;

    explicit BloodBros(Game game = Game::BloodBros);

    bool init(const std::string& rom_path, std::string* error) override;
    void reset() override;
    void run_frame() override;

    void set_inputs(const MachineInputs& inputs) override;
    // Bank 0: low byte of the 16-bit DIP word, bank 1: high byte.
    void set_dip_switch(int bank, uint8_t value) override;

    const uint32_t* framebuffer() const override { return framebuffer_.data(); }
    int screen_width() const override { return rotated() ? kHeight : kWidth; }
    int screen_height() const override { return rotated() ? kWidth : kHeight; }
    double frames_per_second() const override { return kFramesPerSecond; }

    void drain_audio(std::vector<int16_t>& out) override { sound_.drain_audio(out); }
    int sample_rate() const override { return SeibuSound::kSampleRate; }

    const char* title() const override {
        return game_ == Game::SkySmasher ? "Sky Smasher" : "Blood Bros.";
    }

    // Test hooks.
    uint32_t main_pc() const { return main_cpu_.pc(); }
    uint16_t main_word(uint32_t address) { return main_read(address); }
    const SeibuSound& sound() const { return sound_; }

private:
    bool rotated() const { return game_ == Game::SkySmasher; }
    uint16_t main_read(uint32_t address);
    void main_write(uint32_t address, uint16_t value);
    int crt_index(uint32_t address) const;
    void set_color(int index, uint16_t value);
    void update_video();
    void draw_sprites(int priority);

    Game game_;
    M68000 main_cpu_{kMainClock};
    SeibuSound sound_{kSoundClock};

    std::array<uint16_t, 0x40000> rom_{};
    std::array<uint16_t, 0x8000> ram_{};       // 080000-08ffff (palette area unused)
    std::array<uint16_t, 0x800> pal_ram_{};    // 08e800-08f7ff
    std::array<uint16_t, 0x28> crt_{};
    std::array<uint32_t, 0x801> palette_{};    // 0x800: the "nothing" colour (black)

    GfxSet chars_, tiles_, sprites_;
    std::vector<uint32_t> screen_;       // 256x256 composition, visible rows 16..239
    std::vector<uint32_t> framebuffer_;

    uint16_t in0_ = 0xffff, in1_ = 0xffff, dsw_ = 0xffff;
    int irq_level_ = 4;
    double main_debt_ = 0, sound_debt_ = 0;
};

}  // namespace dsp
