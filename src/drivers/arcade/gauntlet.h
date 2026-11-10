#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "core/machine.h"

struct gt_core;

namespace dsp {

// Gauntlet (Atari, 1985): 68010 behind a SLAPSTIC, 6502 with a YM2151, a
// POKEY and a TMS5220.
//
// The machine is the lean C core of src/lean/gauntlet (gauntlet_core.c,
// ported from this driver's gauntlet_hw.pas design and tuned to run on the
// ESP32 and RP2350 boards): Musashi for the 68010 with the program's busy
// loop fast-forwarded exactly, the sound 6502 compiled inside the machine
// with its polling loop fast-forwarded, the tiles packed to 4 bpp rows and the
// picture composed line by line from the video RAM (no planes kept). This
// class loads the ROM sets and turns the core's lines into the frame buffer.
class Gauntlet : public Machine {
public:
    MachineType machine_type() const override { return MachineType::Arcade; }
    static constexpr int kScreenWidth = 336;
    static constexpr int kScreenHeight = 240;
    static constexpr double kFramesPerSecond = 59.922743;
    static constexpr int kSampleRate = 44100;

    Gauntlet();
    ~Gauntlet() override;

    bool init(const std::string& rom_path, std::string* error) override;
    void reset() override;
    void run_frame() override;

    void set_inputs(const MachineInputs& inputs) override;
    void set_dip_switch(int bank, uint8_t value) override;

    const uint32_t* framebuffer() const override;
    PenRows pen_rows() const override;
    int screen_width() const override { return kScreenWidth; }
    int screen_height() const override { return kScreenHeight; }
    double frames_per_second() const override { return kFramesPerSecond; }
    void set_draw_enabled(bool enabled) override { draw_ = enabled; }

    void drain_audio(std::vector<int16_t>& out) override;
    int sample_rate() const override { return kSampleRate; }

    const char* title() const override { return "Gauntlet"; }

    // Writes the images the core runs from (lean_*.bin) to `dir`: a ROM
    // "set" of just those files is read in place on the boards (from flash),
    // with no copy in RAM (tools/lean_roms).
    bool write_prepared(const std::string& dir, std::string* error) const;

    // Cycles the core fast-forwarded (68010, 6502), for the tests.
    uint32_t debug_skipped_main() const;
    uint32_t debug_skipped_sound() const;

private:
    bool load_roms(const std::string& rom_path, std::string* error);
    static void on_vblank(void* user, gt_core* core);
    static void rows(const void* source, int y, int n, uint16_t* pens);
    void compose() const;

    struct Lean;                      // the C core and its renderer
    std::unique_ptr<Lean> lean_;
    // The images (in place on a board when the set is prepared, else here):
    const uint8_t* main_ = nullptr;   // 0x80000 bytes, big endian words
    const uint8_t* sound_ = nullptr;  // 0xc000 bytes: 6502 $4000-$FFFF
    const uint8_t* chars_ = nullptr;  // 0x4000 bytes, 2bpp
    const uint32_t* tiles_ = nullptr; // 0x2000 tiles x 8 rows, 4bpp
    std::vector<uint8_t> main_rom_, sound_rom_, char_rom_, info_;
    std::vector<uint32_t> tile_rows_;
    int slapstic_ = 104;
    uint32_t palette_[0x400] = {};    // ARGB, at the start of the vertical blank
    bool ready_ = false;
    bool draw_ = true;
    bool service_ = false;
    mutable std::vector<uint32_t> framebuffer_;  // made when asked for
    std::vector<int16_t> audio_;
    std::vector<int16_t> frame_audio_;
};

}  // namespace dsp
