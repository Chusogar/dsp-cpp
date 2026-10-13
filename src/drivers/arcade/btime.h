#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "core/machine.h"
#include "cpu/m6502.h"
#include "sound/ay8910.h"
#include "video/gfx.h"

namespace dsp {

// BurgerTime (Data East, 1982), Lock'n'Chase (Data East, 1981) and Minky
// Monkey (Technos, 1982), ported from dsp-emulator burgertime_hw.pas (MAME
// btime.cpp).
//
// A 6502 at 1.5 MHz with a 32x32 layer of 3 bpp characters, 8 sprites of
// 16x16 that live in the character RAM and, on BurgerTime, a background of
// 16x16 tiles picked from a ROM map. The character RAM has a second window
// with rows and columns swapped. A 500 kHz sound 6502 drives two AY-3-8910,
// gets commands through a latch and IRQ and has an NMI clocked every 16
// scanlines that the first AY's port A gates. The monitor is vertical; the
// graphics are stored turned, so they are decoded rotated (like the original).
//
// BurgerTime encrypts an opcode by swapping bits when it sits at an address
// matching xxxx xxx1 xxxx x1xx and the previous instruction wrote to memory;
// its 16 colours are written by the program. Lock'n'Chase and Minky Monkey
// swap bits 5 and 6 of every opcode and take their 32 colours from a PROM;
// Minky Monkey adds a protection device that adds BCD scores and searches a
// table.
class BurgerTime : public Machine {
public:
    MachineType machine_type() const override { return MachineType::Arcade; }
    enum class Game { BurgerTime, LockNChase, MinkyMonkey };

    static constexpr uint32_t kMainClock = 1500000;
    static constexpr uint32_t kSoundClock = 500000;
    static constexpr int kScanlines = 272;
    static constexpr double kFramesPerSecond = 57.444855;
    static constexpr int kSize = 240;  // 240x240 visible

    explicit BurgerTime(Game game = Game::BurgerTime);

    bool init(const std::string& rom_path, std::string* error) override;
    void reset() override;
    void run_frame() override;

    void set_inputs(const MachineInputs& inputs) override;
    void set_dip_switch(int bank, uint8_t value) override;

    const uint32_t* framebuffer() const override { return framebuffer_.data(); }
    int screen_width() const override { return kSize; }
    int screen_height() const override { return kSize; }
    double frames_per_second() const override { return kFramesPerSecond; }

    void drain_audio(std::vector<int16_t>& out) override;
    int sample_rate() const override { return AY8910::kSampleRate; }

    const char* title() const override;

    // Test hooks.
    uint16_t main_pc() const { return main_cpu_.pc(); }
    int sound_commands() const { return sound_commands_; }

private:
    bool btime() const { return game_ == Game::BurgerTime; }
    static uint16_t mirror_index(uint16_t address);  // rows and columns swapped
    uint8_t opcode_read(uint16_t address);
    uint8_t main_read(uint16_t address);
    void main_write(uint16_t address, uint8_t value);
    uint8_t lnc_read(uint16_t address);
    void lnc_write(uint16_t address, uint8_t value);
    uint8_t protection_read(uint16_t address);
    void protection_write(uint16_t address, uint8_t value);
    uint8_t sound_read(uint16_t address);
    void sound_write(uint16_t address, uint8_t value);
    void on_sound_cycles(int cycles);
    void set_color(int index, uint8_t value);
    void update_video();

    Game game_;
    M6502 main_cpu_{kMainClock};
    M6502 sound_cpu_{kSoundClock};
    AY8910 ay0_{1500000}, ay1_{1500000};

    std::array<uint8_t, 0x10000> memory_{};  // main RAM and ROM image
    std::array<uint8_t, 0x1000> sound_rom_{};
    std::array<uint8_t, 0x400> sound_ram_{};
    std::array<uint8_t, 0x400> video_ram_{}, color_ram_{};
    std::array<uint8_t, 0x800> bg_map_{};
    std::array<uint8_t, 0x10> palette_ram_{};
    std::array<uint32_t, 0x20> palette_{};

    GfxSet chars_, sprites_, tiles_;
    std::vector<uint32_t> screen_;  // 256x256
    std::vector<uint32_t> framebuffer_;

    uint8_t in0_ = 0xff, in1_ = 0xff, in2_ = 0xff, dsw_a_ = 0x3f, dsw_b_ = 0xeb;
    uint8_t sound_latch_ = 0, bg_control_ = 0, charbank_ = 1;
    bool sound_nmi_enabled_ = false, had_written_ = false;
    // Minky Monkey protection.
    uint8_t prot_status_ = 0, prot_ret_ = 0, prot_command_ = 0, prot_value_ = 0;
    double main_debt_ = 0, sound_debt_ = 0;
    int64_t audio_accum_ = 0;
    int sound_commands_ = 0;
    std::vector<int16_t> audio_;
};

}  // namespace dsp
