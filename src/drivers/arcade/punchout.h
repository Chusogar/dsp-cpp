#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "core/machine.h"
#include "cpu/m6502.h"
#include "cpu/z80.h"
#include "sound/nes_apu.h"
#include "sound/vlm5030.h"

namespace dsp {

// Nintendo Punch-Out!! (1984), after MAME nintendo/punchout.cpp.
//
// Z80 at 4 MHz; a 2A03 (NES CPU + APU) plays music and effects, a VLM5030
// speaks.  Two monitors, stacked in one 256x448 picture:
//  * top: 32x32 tilemap plus the zooming "big sprite" (the opponent)
//    when it is routed to the top monitor;
//  * bottom: 64x32 tilemap with per-row scroll, the big sprite, and the
//    second big sprite (Little Mac).
// Colours come from the pink-labelled PROMs, one 256-entry palette per
// monitor, each with two banks.
class PunchOut : public Machine {
public:
    static constexpr int kScreenWidth = 256;
    static constexpr int kMonitorHeight = 224;  // lines 16-239 of each monitor
    static constexpr int kScreenHeight = kMonitorHeight * 2;
    static constexpr uint32_t kMainClock = 4000000;
    static constexpr uint32_t kSoundClock = 1789773;  // NTSC 2A03
    static constexpr int kLines = 256;
    static constexpr double kFramesPerSecond = 60.0;
    static constexpr int kSampleRate = 44100;

    PunchOut();

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
    const char* title() const override { return "Punch-Out!!"; }
    bool fit_window() const override { return true; }

    uint16_t debug_pc() const { return main_cpu_.pc(); }
    uint16_t debug_sound_pc() const { return sound_cpu_.pc(); }
    bool debug_speaking() const { return vlm_.get_bsy() != 0; }

private:
    uint8_t main_read(uint16_t address);
    void main_write(uint16_t address, uint8_t value);
    uint8_t port_read(uint16_t port);
    void port_write(uint16_t port, uint8_t value);
    uint8_t sound_read(uint16_t address);
    void sound_write(uint16_t address, uint8_t value);
    void on_sound_cycles(int cycles);
    void latch_w(int bit, bool state);

    void build_palettes();
    void render();
    void draw_top(uint32_t* out);
    void draw_bottom(uint32_t* out);
    // Big sprite #1 (zooming, 3bpp, 128x256) into a monitor's pen buffer.
    void draw_big_sprite(uint16_t* pens, int palette);
    // Big sprite #2 (2bpp, 128x256, bottom monitor).
    void draw_big_sprite2(uint16_t* pens);

    Z80 main_cpu_;
    M6502 sound_cpu_;
    NesApu apu_;
    Vlm5030 vlm_;

    std::array<uint8_t, 0xc000> main_rom_{};
    std::array<uint8_t, 0x2000> sound_rom_{};
    std::array<uint8_t, 0x400> nvram_{};
    std::array<uint8_t, 0x800> work_ram_{};
    std::array<uint8_t, 0x800> top_vram_{};   // $D800-$DFFF (incl. control regs at $DFF0)
    std::array<uint8_t, 0x800> spr1_vram_{};  // $E000
    std::array<uint8_t, 0x800> spr2_vram_{};  // $E800
    std::array<uint8_t, 0x1000> bot_vram_{};  // $F000
    std::array<uint8_t, 0x800> sound_ram_{};

    // Decoded graphics: one byte per pixel.
    std::vector<uint8_t> gfx_top_;   // 1024 chars 2bpp
    std::vector<uint8_t> gfx_bot_;   // 1024 chars 2bpp
    std::vector<uint8_t> gfx_spr1_;  // 8192 chars 3bpp
    std::vector<uint8_t> gfx_spr2_;  // 4096 chars 2bpp
    std::vector<uint8_t> proms_;
    // pen -> colour for each monitor and palette bank.
    std::array<std::array<uint32_t, 0x100>, 2> top_pal_{};
    std::array<std::array<uint32_t, 0x100>, 2> bot_pal_{};

    std::vector<uint32_t> framebuffer_;

    uint8_t in0_ = 0, in1_ = 0;
    uint8_t dsw1_ = 0x00, dsw2_ = 0x10;
    uint8_t latch_ = 0;
    uint8_t sound_latch_[2] = {};
    bool nmi_mask_ = false;
    bool sound_reset_ = false;
    int vlm_cycle_acc_ = 0;
    int64_t audio_accumulator_ = 0;
    int apu_cycles_ = 0;
    std::vector<int16_t> audio_;
};

}  // namespace dsp
