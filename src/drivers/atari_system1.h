#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "core/machine.h"
#include "cpu/m6502.h"
#include "cpu/m68000.h"
#include "machine/slapstic.h"
#include "sound/pokey.h"
#include "sound/ym2151.h"
#include "machine/via6522.h"
#include "sound/tms5220.h"

namespace dsp {

// Atari System 1 (Marble Madness, Peter Pack Rat, Indiana Jones, …)
// from atari_system1.pas — M68000 + M6502, YM2151, Pokey, Slapstic.
class AtariSystem1 : public Machine {
public:
    static constexpr int kScreenW = 336;
    static constexpr int kScreenH = 240;
    static constexpr int kScanlines = 262;
    static constexpr uint32_t kMasterClock = 14318180;
    static constexpr uint32_t kCpuClock = kMasterClock / 2;   // 7.159 MHz
    static constexpr uint32_t kAudioClock = kMasterClock / 8; // 1.789 MHz
    static constexpr uint32_t kYmClock = kMasterClock / 4;    // 3.579 MHz
    static constexpr double kFps = 59.922743;
    static constexpr int kSampleRate = YM2151::kSampleRate;

    enum class Game { PeterPak, Indy, Marble };

    explicit AtariSystem1(Game game = Game::Marble);

    bool init(const std::string& rom_path, std::string* error) override;
    void reset() override;
    void run_frame() override;
    void set_inputs(const MachineInputs& inputs) override;
    void set_dip_switch(int bank, uint8_t value) override;
    const uint32_t* framebuffer() const override { return framebuffer_.data(); }
    int screen_width() const override { return kScreenW; }
    int screen_height() const override { return kScreenH; }
    double frames_per_second() const override { return kFps; }
    void drain_audio(std::vector<int16_t>& out) override;
    int sample_rate() const override { return kSampleRate; }
    const char* title() const override;

private:
    uint16_t cpu_read(uint32_t addr);
    void cpu_write(uint32_t addr, uint16_t value);
    uint8_t snd_read(uint16_t addr);
    void snd_write(uint16_t addr, uint8_t value);
    void on_main_cycles(int cycles);
    void on_snd_cycles(int cycles);
    void update_video();
    void set_color(uint16_t index, uint16_t value);

    Game game_;
    M68000 main_cpu_;
    M6502 snd_cpu_;
    YM2151 ym_;
    Pokey pokey_;
    Slapstic slapstic_;  // dsp-cpp slapstic (needs main_cpu_)
    Via6522 via_;
    Tms5220 tms_;

    // Program ROM (word-addressed image, 512 KB max)
    std::array<uint16_t, 0x40000> rom_{};
    std::array<std::array<uint16_t, 0x1000>, 4> slapstic_rom_{};
    std::array<uint16_t, 0x1000> ram_{};
    std::array<uint16_t, 0x80000> ram2_{};  // playfield / MO
    std::array<uint16_t, 0x2000> ram3_{};
    std::array<uint16_t, 0x400> palette_{};
    std::array<uint8_t, 0x800> eeprom_{};
    std::array<uint8_t, 0x10000> snd_rom_{};
    std::array<uint8_t, 0x1000> snd_ram_{};

    // GFX tiles (decoded 8x8, 4bpp, up to 64K tiles)
    std::vector<uint8_t> tiles_;  // [tile][64] nibbles as bytes
    int tile_count_ = 0;
    std::array<uint16_t, 256> playfield_lookup_{};

    std::array<uint32_t, kScreenW * kScreenH> framebuffer_{};
    std::array<uint32_t, 512> argb_pal_{};

    uint16_t scroll_x_ = 0, scroll_y_ = 0, scroll_y_latch_ = 0;
    uint16_t bankselect_ = 0;
    uint8_t playfield_tile_bank_ = 0;
    uint8_t rom_bank_ = 0;
    uint8_t vblank_ = 0x10;
    uint8_t in0_ = 0xff, in1_ = 0xff, in2_ = 0x87;
    uint8_t sound_latch_ = 0, main_latch_ = 0;
    bool sound_pending_ = false, main_pending_ = false;
    bool write_eeprom_ = false;
    int line_ = 0;

    std::vector<int16_t> audio_;
    int64_t audio_acc_ = 0;
    int snd_cycle_acc_ = 0;
};

}  // namespace dsp
