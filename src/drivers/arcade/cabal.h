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

// Cabal (Tad / Seibu Kaihatsu, 1988) — port of dsp-emulator cabal_hw.pas.
//
// 68000 @ 10 MHz, Seibu ADPCM sound (Z80 + YM2151 + 2×MSM5205), 8×8 text,
// 16×16 background tiles and 16×16 sprites. Trackball ports are stubbed
// (joystick mode), matching the Pascal driver.
class Cabal : public Machine {
public:
    MachineType machine_type() const override { return MachineType::Arcade; }
    static constexpr int kScreenWidth = 256;
    static constexpr int kScreenHeight = 224;
    static constexpr int kScanlines = 256;
    static constexpr double kFramesPerSecond = 59.60;
    static constexpr uint32_t kMainClock = 10000000;

    Cabal();
    ~Cabal() override;

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
    int sample_rate() const override { return sound_.sample_rate(); }

    const char* title() const override { return "Cabal"; }

private:
    uint8_t read8(uint32_t address);
    void write8(uint32_t address, uint8_t value);
    uint16_t read16(uint32_t address);
    void write16(uint32_t address, uint16_t value);
    void set_palette_entry(int index, uint16_t value);
    void update_video();

    M68000 main_cpu_;
    SeibuSound sound_;

    std::vector<uint8_t> rom_;  // 256 KiB interleaved program
    std::array<uint16_t, 0x8000> main_ram_{};
    std::array<uint16_t, 0x400> fg_ram_{};
    std::array<uint16_t, 0x200> bg_ram_{};
    std::array<uint16_t, 0x400> pal_ram_{};
    std::array<uint32_t, 0x400> palette_{};

    GfxSet chars_;
    GfxSet sprites_;
    GfxSet tiles_;
    std::vector<uint32_t> framebuffer_;
    std::vector<uint32_t> screen_;  // 256×256 composition; visible rows 16..239

    bool flip_screen_ = false;
    uint16_t in0_ = 0xffff;
    uint16_t in1_ = 0xffff;
    uint16_t dsw_ = 0xefff;

    double main_debt_ = 0;
    double sound_debt_ = 0;
};

}  // namespace dsp
