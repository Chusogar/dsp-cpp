#pragma once

#include "core/machine.h"
#include "cpu/z80.h"
#include "sound/ym2203.h"
#include "video/gfx.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace dsp {

// Capcom Commando (commando_hw.pas) — dual Z80, dual YM2203, encrypted opcodes.
class Commando : public Machine {
public:
    static constexpr int kScreenWidth = 224;
    static constexpr int kScreenHeight = 256;
    static constexpr double kFramesPerSecond = 12000000.0 / 2.0 / 384.0 / 262.0;
    static constexpr int kScanlines = 262;
    static constexpr uint32_t kMainClock = 3000000;
    static constexpr uint32_t kSoundClock = 3000000;
    static constexpr uint32_t kYmClock = 1500000;

    Commando();

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

    const char* title() const override { return "Commando"; }

private:
    uint8_t main_read(uint16_t address);
    uint8_t main_opcode(uint16_t address);
    void main_write(uint16_t address, uint8_t value);
    uint8_t sound_read(uint16_t address);
    void sound_write(uint16_t address, uint8_t value);
    void on_sound_cycles(int cycles);

    bool load_roms(const std::string& rom_path, std::string* error);
    void decode_graphics();
    void build_palette(const std::vector<uint8_t>& prom);

    void update_video();
    void draw_bg_tile(int offset);
    void draw_fg_char(int offset);
    void draw_sprites();

    Z80 main_cpu_;
    Z80 sound_cpu_;
    YM2203 ym0_;
    YM2203 ym1_;

    std::array<uint8_t, 0x10000> memory_{};
    std::array<uint8_t, 0xc000> opcodes_{};  // decrypted opcode stream
    std::array<uint8_t, 0x10000> mem_snd_{};
    std::array<uint8_t, 0x200> sprite_buffer_{};
    std::array<uint32_t, 256> palette_{};

    GfxSet chars_;
    GfxSet sprites_;
    GfxSet tiles_;

    std::vector<uint8_t> gfx_char_;
    std::vector<uint8_t> gfx_spr_;
    std::vector<uint8_t> gfx_tile_;

    // Tilemaps: bg 512x512 (16x16 tiles), fg 256x256 (8x8)
    std::vector<uint32_t> layer_bg_;
    std::vector<uint32_t> layer_fg_;
    std::vector<uint32_t> composite_;
    std::vector<uint32_t> framebuffer_;

    std::array<bool, 0x400> bg_dirty_{};
    std::array<bool, 0x400> fg_dirty_{};

    uint16_t scroll_x_ = 0;
    uint16_t scroll_y_ = 0;
    uint8_t sound_command_ = 0;
    bool sound_reset_ = false;
    bool flip_screen_ = false;

    uint8_t in0_ = 0xff;
    uint8_t in1_ = 0xff;
    uint8_t in2_ = 0xff;
    uint8_t dsw_a_ = 0xff;
    uint8_t dsw_b_ = 0x1f;

    int64_t audio_accum_ = 0;
    int sound_irq_counter_ = 0;
    std::vector<int16_t> audio_;
};

}  // namespace dsp
