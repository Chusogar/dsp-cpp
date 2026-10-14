#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "core/machine.h"
#include "cpu/m6502.h"
#include "sound/pokey.h"
#include "video/gfx.h"

namespace dsp {

// Centipede / Millipede (Atari, 1980/1982), ported from dsp-emulator
// centipede_hw.pas (MAME centiped.cpp / milliped.cpp).
//
// MOS 6502 @ 1.512 MHz, one (Centipede) or two (Millipede) POKEYs, 32x32
// character playfield plus 16 sprites, 240x256 vertical monitor, 64-byte NVRAM.
class Centipede : public Machine {
public:
    MachineType machine_type() const override { return MachineType::Arcade; }

    enum class Game { Centipede, Millipede };

    static constexpr uint32_t kMasterClock = 12096000;
    static constexpr uint32_t kCpuClock = kMasterClock / 8;
    static constexpr int kScanlines = 256;
    static constexpr double kFramesPerSecond = 60.0;
    static constexpr int kWidth = 240;
    static constexpr int kHeight = 256;
    static constexpr int kNativeHeight = 240;  // the board draws 256x240
    static constexpr int kSampleRate = 44100;

    explicit Centipede(Game game = Game::Centipede);

    bool init(const std::string& rom_path, std::string* error) override;
    void reset() override;
    void run_frame() override;

    void set_inputs(const MachineInputs& inputs) override;
    void set_dip_switch(int bank, uint8_t value) override;

    const uint32_t* framebuffer() const override { return framebuffer_.data(); }
    int screen_width() const override { return kWidth; }
    int screen_height() const override { return kHeight; }
    double frames_per_second() const override { return kFramesPerSecond; }

    void drain_audio(std::vector<int16_t>& out) override;
    int sample_rate() const override { return kSampleRate; }

    const char* title() const override {
        return game_ == Game::Millipede ? "Millipede" : "Centipede";
    }

private:
    uint8_t main_read(uint16_t address);
    void main_write(uint16_t address, uint8_t value);
    void on_cycles(int cycles);
    void update_palette_centipede(uint8_t pos, uint8_t value);
    void update_palette_millipede(uint8_t pos, uint8_t value);
    void update_video();
    void draw_sprites();

    Game game_;
    M6502 cpu_{kCpuClock};
    Pokey pokey0_{kCpuClock};
    Pokey pokey1_{kCpuClock};

    std::array<uint8_t, 0x8000> memory_{};
    std::array<uint8_t, 0x40> nvram_{};
    std::array<uint32_t, 0x20> palette_{};
    std::array<uint8_t, 0x400> sprite_pen_{};  // gfx[1].colores (Millipede uses up to ~0x3ff)

    GfxSet chars_, sprites_;
    std::vector<uint32_t> screen_;  // 256x256 work buffer
    std::vector<uint32_t> framebuffer_;

    uint8_t in0_ = 0xff, in1_ = 0xff, in2_ = 0xff, in3_ = 0xff;
    uint8_t dsw_a_ = 0x54, dsw_b_ = 0x02, dsw_c_ = 0x20;
    bool flip_screen_ = false;

    int64_t audio_accum_ = 0;
    std::vector<int16_t> audio_;
};

}  // namespace dsp
