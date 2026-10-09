#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "core/machine.h"
#include "cpu/mcs51.h"
#include "cpu/z80.h"
#include "sound/ym2203.h"
#include "video/gfx.h"

namespace dsp {

// Black Tiger (Capcom, 1987) — port of dsp-emulator blacktiger_hw.pas, with
// the tilemap layouts, split priorities and sprite rules of MAME's blktiger.cpp.
//
// Main Z80 @ 6 MHz with 16 banked 16 KB ROM pages, sound Z80 @ 3.58 MHz with
// two YM2203 (the first one drives the sound IRQ) and an i8751 protection MCU
// talking to the main CPU through a pair of latches on I/O port 7.
// Video: 8x8 text layer, a 16x16 scrolling background (128x64 or 64x128 tiles,
// selectable, split in front/behind halves by colour) and 128 buffered 16x16
// sprites; 1024 colours in xBRG_444 format.
class BlackTiger : public Machine {
public:
    MachineType machine_type() const override { return MachineType::Arcade; }
    static constexpr int kScreenWidth = 256;
    static constexpr int kScreenHeight = 224;
    static constexpr int kScanlines = 262;
    static constexpr double kFramesPerSecond = 24000000.0 / 4.0 / 384.0 / 262.0;  // 59.637 Hz
    static constexpr uint32_t kMainClock = 6000000;
    static constexpr uint32_t kSoundClock = 3579545;
    static constexpr uint32_t kMcuClock = 8000000;

    BlackTiger();

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
    int sample_rate() const override { return YM2203::kSampleRate; }

    const char* title() const override { return "Black Tiger"; }

    // Test hooks.
    uint16_t main_pc() const { return main_cpu_.pc(); }
    uint8_t main_byte(uint16_t address) { return main_read(address); }
    int mcu_reads() const { return mcu_reads_; }
    int sound_irqs() const { return sound_irqs_; }

private:
    uint8_t main_read(uint16_t address);
    void main_write(uint16_t address, uint8_t value);
    uint8_t main_in(uint8_t port);
    void main_out(uint8_t port, uint8_t value);
    uint8_t sound_read(uint16_t address);
    void sound_write(uint16_t address, uint8_t value);
    void on_sound_cycles(int cycles);
    void set_color(int index);
    void update_video();

    Z80 main_cpu_{kMainClock};
    Z80 sound_cpu_{kSoundClock};
    Mcs51 mcu_{kMcuClock};
    YM2203 ym0_{kSoundClock};
    YM2203 ym1_{kSoundClock};

    std::array<uint8_t, 0x8000> rom_{};                      // 0000-7fff
    std::array<std::array<uint8_t, 0x4000>, 16> banks_{};    // 8000-bfff
    std::array<uint8_t, 0x4000> scroll_ram_{};               // c000-cfff, 4 banks
    std::array<uint8_t, 0x800> tx_ram_{};                    // d000-d7ff
    std::array<uint8_t, 0x800> pal_ram_{};                   // d800-dfff
    std::array<uint8_t, 0x2000> wram_{};                     // e000-ffff (sprites fe00)
    std::array<uint8_t, 0x200> sprite_buf_{};
    std::array<uint8_t, 0x8000> sound_rom_{};
    std::array<uint8_t, 0x800> sound_ram_{};
    std::array<uint32_t, 0x400> palette_{};

    GfxSet chars_, tiles_, sprites_;
    std::vector<uint32_t> screen_;  // 256x256 composition (visible rows 16..239)
    std::vector<uint32_t> framebuffer_;

    uint8_t bank_ = 0, soundlatch_ = 0, i8751_latch_ = 0, z80_latch_ = 0;
    uint16_t scroll_x_ = 0, scroll_y_ = 0, scroll_bank_ = 0;
    bool screen_layout_ = false;  // true: 128x64 tiles (8x4 pages), false: 64x128
    bool ch_on_ = true, bg_on_ = true, obj_on_ = true, flip_screen_ = false;
    bool sound_reset_ = false;

    uint8_t in0_ = 0xff, in1_ = 0xff, in2_ = 0xff;
    uint8_t dsw_a_ = 0xff, dsw_b_ = 0x6f;

    double main_debt_ = 0, sound_debt_ = 0, mcu_debt_ = 0;
    int64_t audio_accum_ = 0;
    std::vector<int16_t> audio_;
    int mcu_reads_ = 0;
    int sound_irqs_ = 0;
};

}  // namespace dsp
