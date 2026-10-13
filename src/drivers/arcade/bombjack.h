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

// Bomb Jack (Tehkan, 1984) and Calorie Kun vs Moguranian (Sega, 1986), ported
// from dsp-emulator bombjack_hw.pas (MAME bombjack.cpp / calorie.cpp).
//
// A Z80 with a 32x32 text layer, a background picked among 16 pictures stored
// as tile maps in ROM, 16x16 and 32x32 sprites and 128 colours of 4-bit RGB.
// A second Z80 plays three AY-3-8910s and takes commands through a latch that
// clears when read. Bomb Jack: 4 MHz main CPU with a VBLANK NMI, big sprites
// chosen by a range of sprite numbers, vertical monitor. Calorie Kun: Sega
// 317-000x encrypted opcodes, IRQs instead of NMIs, big sprites flagged per
// sprite, horizontal monitor.
class BombJack : public Machine {
public:
    MachineType machine_type() const override { return MachineType::Arcade; }
    enum class Game { BombJack, CalorieKun };

    static constexpr uint32_t kMainClock = 4000000;
    static constexpr uint32_t kSoundClock = 3000000;
    static constexpr int kScanlines = 264;
    static constexpr double kFramesPerSecond = 60.0;

    explicit BombJack(Game game = Game::BombJack);

    bool init(const std::string& rom_path, std::string* error) override;
    void reset() override;
    void run_frame() override;

    void set_inputs(const MachineInputs& inputs) override;
    void set_dip_switch(int bank, uint8_t value) override;

    const uint32_t* framebuffer() const override { return framebuffer_.data(); }
    int screen_width() const override { return rotated() ? 224 : 256; }
    int screen_height() const override { return rotated() ? 256 : 224; }
    double frames_per_second() const override { return kFramesPerSecond; }

    void drain_audio(std::vector<int16_t>& out) override;
    int sample_rate() const override { return AY8910::kSampleRate; }

    const char* title() const override {
        return game_ == Game::CalorieKun ? "Calorie Kun vs Moguranian" : "Bomb Jack";
    }

    // Test hooks.
    uint16_t main_pc() const { return main_cpu_.pc(); }
    int sound_commands() const { return sound_commands_; }

private:
    bool rotated() const { return game_ == Game::BombJack; }
    uint8_t main_read(uint16_t address);
    void main_write(uint16_t address, uint8_t value);
    uint8_t map_read(uint16_t offset);
    void map_write(uint16_t offset, uint8_t value);
    uint8_t sound_read(uint16_t address);
    void sound_write(uint16_t address, uint8_t value);
    uint8_t sound_in(uint16_t port);
    void sound_out(uint16_t port, uint8_t value);
    void on_sound_cycles(int cycles);
    void update_video();

    Game game_;
    Z80 main_cpu_{kMainClock};
    Z80 sound_cpu_{kSoundClock};
    AY8910 ay_[3] = {AY8910(1500000), AY8910(1500000), AY8910(1500000)};

    std::vector<uint8_t> rom_;      // 64 KB address space image (data view)
    std::vector<uint8_t> opcodes_;  // Calorie Kun: decrypted opcodes of 0000-7fff
    std::vector<uint8_t> sound_rom_;
    std::vector<uint8_t> bg_map_;   // background tile maps (16 pictures of 0x200 bytes)
    std::array<uint8_t, 0x1000> ram_{};
    std::array<uint8_t, 0x800> screen_ram_{};  // 0x400 codes + 0x400 attributes
    std::array<uint8_t, 0x80> sprite_ram_{};
    std::array<uint8_t, 0x100> pal_ram_{};
    std::array<uint8_t, 0x800> sound_ram_{};
    std::array<uint32_t, 0x80> palette_{};

    GfxSet chars_, tiles_, sprites_, big_sprites_;
    int big_mask_ = 0x1f;
    std::vector<uint32_t> screen_;  // 256x256
    std::vector<uint32_t> framebuffer_;

    uint8_t in0_ = 0, in1_ = 0, in2_ = 0xf0, dsw_a_ = 0, dsw_b_ = 0;
    uint8_t sound_latch_ = 0, background_ = 0xff;
    uint8_t sprite_control_[2] = {0, 0};
    bool background_on_ = false, nmi_enabled_ = false, flip_ = false;
    double main_debt_ = 0, sound_debt_ = 0;
    int64_t audio_accum_ = 0;
    int sound_commands_ = 0;
    std::vector<int16_t> audio_;
};

}  // namespace dsp
