#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "core/machine.h"
#include "cpu/m6502.h"
#include "machine/slapstic.h"
#include "sound/pokey.h"
#include "video/gfx.h"

namespace dsp {

// Atari Tetris (1988), ported from tetris_atari_hw.pas.
// M6502 main CPU (1.79 MHz) with Atari SLAPSTIC bank switching, two POKEY
// sound chips and a 64x32 tilemap of 8x8 characters on a 336x240 screen.
class AtariTetris : public Machine {
public:
    static constexpr int kScreenWidth = 336;
    static constexpr int kScreenHeight = 240;
    static constexpr double kFramesPerSecond = 59.922743;
    static constexpr int kScanlines = 262;
    static constexpr uint32_t kCpuClock = 1789772;
    static constexpr int kSampleRate = 44100;

    AtariTetris();

    bool init(const std::string& rom_path, std::string* error) override;
    void reset() override;
    void run_frame() override;

    void set_inputs(const MachineInputs& inputs) override;
    void set_dip_switch(int bank, uint8_t value) override;

    const uint32_t* framebuffer() const override { return framebuffer_.data(); }
    int screen_width() const override { return kScreenWidth; }
    int screen_height() const override { return kScreenHeight; }
    double frames_per_second() const override { return kFramesPerSecond; }

    void drain_audio(std::vector<int16_t>& out) override;
    int sample_rate() const override { return kSampleRate; }

    const char* title() const override { return "Tetris (Atari)"; }
    bool uses_pointer() const override { return false; }

private:
    uint8_t main_read(uint16_t address);
    void main_write(uint16_t address, uint8_t value);
    void on_sound_cycles(int cycles);
    void set_palette(int index, uint8_t value);
    void update_video();

    M6502 cpu_;
    Slapstic slapstic_;
    Pokey pokey0_;
    Pokey pokey1_;

    GfxSet chars_;
    std::array<uint8_t, 0x10000> memoria_{};
    std::array<std::array<uint8_t, 0x4000>, 2> rom_mem_{};
    std::array<uint8_t, 0x200> nv_ram_{};
    std::array<uint8_t, 0x100> buffer_paleta_{};
    std::array<uint32_t, 0x100> palette_{};
    std::vector<uint32_t> framebuffer_;

    uint8_t rom_bank_ = 1;
    bool nvram_write_enable_ = false;
    uint8_t in0_ = 0x40;
    uint8_t in1_ = 0;
    uint8_t dsw_a_ = 0xff;

    int64_t audio_accumulator_ = 0;
    std::vector<int16_t> audio_;
};

}  // namespace dsp