#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "core/machine.h"
#include "cpu/m6809.h"
#include "cpu/z80.h"
#include "sound/sn76496.h"
#include "video/gfx.h"

namespace dsp {

// Mikie (Konami, 1984), ported from mikie_hw.pas.
// Main CPU: M6809, sound CPU: Z80 driving two SN76496.
class Mikie : public Machine {
public:
    static constexpr int kScreenWidth = 224;
    static constexpr int kScreenHeight = 256;
    static constexpr double kFramesPerSecond = 60.59;
    static constexpr int kScanlines = 256;
    static constexpr uint32_t kMainClock = 18432000 / 12;
    static constexpr uint32_t kSoundClock = 14318180 / 4;
    static constexpr uint32_t kSn0Clock = 14318180 / 8;
    static constexpr uint32_t kSn1Clock = 14318180 / 4;

    Mikie();

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
    int sample_rate() const override { return SN76496::kSampleRate; }

    const char* title() const override { return "Mikie"; }

private:
    uint8_t main_read(uint16_t address);
    void main_write(uint16_t address, uint8_t value);
    uint8_t sound_read(uint16_t address);
    void sound_write(uint16_t address, uint8_t value);
    void on_sound_cycles(int cycles);

    void decode_graphics(const std::vector<uint8_t>& char_rom,
                         const std::vector<uint8_t>& sprite_rom);
    void build_palette(const std::vector<uint8_t>& prom);
    void update_video();
    void draw_tile(int offset);
    void draw_sprite(int index);

    M6809 main_cpu_;
    Z80 sound_cpu_;
    SN76496 sn0_;
    SN76496 sn1_;

    std::array<uint8_t, 0x10000> memory_{};      // main CPU address space
    std::array<uint8_t, 0x4400> sound_memory_{};  // sound CPU ROM + RAM
    std::array<bool, 0x400> dirty_{};
    std::array<uint32_t, 256> palette_{};
    // Colour lookup tables: index is (colour << 4) | pen, see mikie_hw.pas.
    std::array<uint8_t, 0x800> char_lut_{};
    std::array<uint8_t, 0x800> sprite_lut_{};

    GfxSet chars_;    // gfx 0: 8x8 characters
    GfxSet sprites_;  // gfx 1: 16x16 sprites

    std::array<uint32_t, 256 * 256> background_{};  // opaque tile layer
    std::array<uint32_t, 256 * 256> foreground_{};  // tiles drawn above the sprites
    std::array<uint32_t, 256 * 256> composite_{};
    std::vector<uint32_t> framebuffer_;

    uint8_t palette_bank_ = 0;
    uint8_t sound_latch_ = 0;
    uint8_t sound_irq_trigger_ = 0;
    bool irq_enable_ = false;
    bool flip_screen_ = false;

    uint8_t in0_ = 0xff;  // player 1
    uint8_t in1_ = 0xff;  // player 2
    uint8_t in2_ = 0xff;  // coins and start buttons
    uint8_t dsw_a_ = 0xff;
    uint8_t dsw_b_ = 0x7b;
    uint8_t dsw_c_ = 0xfe;

    uint64_t sound_cycles_ = 0;
    int64_t audio_accumulator_ = 0;
    std::vector<int16_t> audio_;
};

}  // namespace dsp
