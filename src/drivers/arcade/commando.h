#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "core/machine.h"
#include "cpu/z80.h"
#include "sound/ym2203.h"
#include "video/gfx.h"

namespace dsp {

// Capcom "Commando" hardware, ported from commando_hw.pas: a Z80 main CPU
// whose opcode fetches run through a fixed bit-swap (data reads see the
// plain ROM), a second Z80 driving two YM2203s off a periodic timer IRQ, a
// 16x16 scrolling background layer, an 8x8 transparent text layer, and
// bank-switched 16x16 sprites.
class Commando : public Machine {
public:
    static constexpr uint32_t kCpuClock = 3000000;
    static constexpr int kScreenWidth = 224;
    static constexpr int kScreenHeight = 256;
    static constexpr int kScanlines = 262;
    static constexpr double kFramesPerSecond = 12000000.0 / 2.0 / 384.0 / 262.0;

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
    uint8_t main_read(uint16_t addr);
    uint8_t main_read_opcode(uint16_t addr);
    void main_write(uint16_t addr, uint8_t value);
    uint8_t sound_read(uint16_t addr);
    void sound_write(uint16_t addr, uint8_t value);
    void on_sound_cycles(int cycles);

    void render_frame();
    static uint32_t wrap(int v, int m) { return uint32_t(((v % m) + m) % m); }
    static std::vector<uint8_t> rotate_ccw(const GfxSet& src, int count, int size);

    Z80 main_cpu_;
    Z80 sound_cpu_;
    YM2203 ym0_;
    YM2203 ym1_;

    GfxSet chars_;
    GfxSet sprites_;
    GfxSet tiles_;

    // Commando's char/sprite/tile ROMs are decoded then rotated 90 degrees
    // counter-clockwise at load time on real hardware (the vertical
    // monitor); GfxSet has no such hook, so we decode normally and rotate
    // the pixel data ourselves into these buffers.
    std::vector<uint8_t> chars_rotated_;
    std::vector<uint8_t> sprites_rotated_;
    std::vector<uint8_t> tiles_rotated_;

    std::array<uint8_t, 0xc000> rom_data_{};  // plain ROM, used for data reads
    std::array<uint8_t, 0xc000> rom_opcode_{};  // bit-swapped ROM, used for opcode fetches
    std::array<uint8_t, 0x2000> ram_{};         // $e000-$ffff
    std::array<uint8_t, 0x800> char_ram_{};     // $d000-$d7ff
    std::array<uint8_t, 0x800> tile_ram_{};     // $d800-$dfff
    std::array<uint8_t, 0x200> sprite_ram_{};   // last page of ram_, mirrored each vblank
    std::array<uint8_t, 0x4800> sound_ram_{};   // $0000-$47ff (ROM + RAM view for the sound CPU)
    std::array<uint32_t, 256> palette_{};
    std::array<uint8_t, 64> sprite_lut_{};
    std::array<uint8_t, 64> char_lut_{};

    std::vector<uint32_t> bg_canvas_;    // 512x512
    std::vector<uint32_t> char_canvas_;  // 256x256

    int scroll_x_ = 0, scroll_y_ = 0;
    bool flip_screen_ = false;
    bool sound_reset_held_ = true;
    uint8_t sound_command_ = 0;
    uint8_t in0_ = 0xff, in1_ = 0xff, in2_ = 0xff;
    uint8_t dsw_a_ = 0xff, dsw_b_ = 0xff;

    std::vector<uint32_t> framebuffer_;
    std::vector<int16_t> audio_;
    int64_t audio_accumulator_ = 0;
    int64_t sound_irq_accumulator_ = 0;
    int32_t last_sample_ = 0;
    int main_cycles_per_line_ = 0;
};

}  // namespace dsp
