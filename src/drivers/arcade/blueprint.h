#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "core/machine.h"
#include "cpu/z80.h"
#include "sound/ay8910.h"
#include "video/gfx.h"

namespace dsp {

// Blue Print (Zilec / Bally Midway, 1982), Saturn (Jaleco, 1983) and Grasspin
// (Jaleco, 1983), ported from dsp-emulator blueprint_hw.pas (MAME
// blueprnt.cpp).
//
// A 3.5 MHz Z80 with a 32x32 tilemap of 8x8 characters scrolled per column
// (two banks of 256 characters picked by the attribute of the tile above),
// tiles with a priority bit drawn over the 64 8x16 sprites, and a palette
// built from resistor logic (no PROM). A 1.25 MHz sound Z80 with two AY-3-8910
// takes commands through a latch and NMI; the first AY's port A latches the
// DIP switch value the main CPU reads, the second AY's ports read the two DIP
// banks. The monitor is turned 270 degrees.
class Blueprint : public Machine {
public:
    MachineType machine_type() const override { return MachineType::Arcade; }
    enum class Game { Blueprint, Saturn, Grasspin };

    static constexpr uint32_t kMainClock = 7000000 / 2;
    static constexpr uint32_t kSoundClock = 10000000 / 8;
    static constexpr int kScanlines = 256;
    static constexpr double kFramesPerSecond = 60.0;
    static constexpr int kWidth = 224;   // after the 270 degree turn
    static constexpr int kHeight = 256;

    explicit Blueprint(Game game = Game::Blueprint);

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
    int sample_rate() const override { return AY8910::kSampleRate; }

    const char* title() const override;

    // Test hooks.
    uint16_t main_pc() const { return main_cpu_.pc(); }
    int sound_nmis() const { return sound_nmis_; }

private:
    uint8_t main_read(uint16_t address);
    void main_write(uint16_t address, uint8_t value);
    uint8_t sound_read(uint16_t address);
    void sound_write(uint16_t address, uint8_t value);
    void on_sound_cycles(int cycles);
    void update_video();

    Game game_;
    Z80 main_cpu_{kMainClock};
    Z80 sound_cpu_{kSoundClock};
    AY8910 ay0_{1250000};
    AY8910 ay1_{625000};

    std::array<uint8_t, 0x8000> rom_{};
    std::array<uint8_t, 0x800> ram_{};
    std::array<uint8_t, 0x400> video_ram_{}, color_ram_{};
    std::array<uint8_t, 0x100> scroll_ram_{}, sprite_ram_{};
    std::array<uint8_t, 0x4000> sound_rom_{};
    std::array<uint8_t, 0x400> sound_ram_{};
    std::array<uint32_t, 0x208> palette_{};

    GfxSet chars_, sprites_;
    std::vector<uint32_t> back_, front_;  // 256x256 tilemaps (front: priority tiles, 0 = clear)
    std::vector<uint32_t> screen_;        // 256x256 composition
    std::vector<uint32_t> framebuffer_;

    uint8_t in0_ = 0, in1_ = 0, dsw_a_ = 0, dsw_b_ = 0;
    uint8_t sound_latch_ = 0, dip_latch_ = 0, gfx_bank_ = 0;
    bool flip_ = false;
    double main_debt_ = 0, sound_debt_ = 0;
    int64_t audio_accum_ = 0;
    int sound_irq_counter_ = 0;
    int sound_nmis_ = 0;
    std::vector<int16_t> audio_;
};

}  // namespace dsp
