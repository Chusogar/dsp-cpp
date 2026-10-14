#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "core/machine.h"
#include "cpu/m6809.h"
#include "cpu/z80.h"
#include "sound/okim6295.h"
#include "sound/ym2151.h"
#include "video/gfx.h"

namespace dsp {

// China Gate / Sai Yu Gou Ma Roku (Technos, 1988), ported from dsp-emulator
// chinagate_hw.pas (MAME chinagat.cpp).
//
// Dual HD6309 (6809 emulation mode) @ 6 MHz, Z80 sound @ 3.579545 MHz with
// YM2151 + OKI6295, Technos tilemap/sprites, 256x240.
class ChinaGate : public Machine {
public:
    MachineType machine_type() const override { return MachineType::Arcade; }

    static constexpr int kScreenWidth = 256;
    static constexpr int kScreenHeight = 240;
    static constexpr double kFramesPerSecond = 6000000.0 / 384.0 / 272.0;
    static constexpr int kScanlines = 272;
    static constexpr int kCpuSync = 4;
    static constexpr uint32_t kMainClock = 12000000 / 2;
    static constexpr uint32_t kSubClock = 12000000 / 2;
    static constexpr uint32_t kSoundClock = 3579545;
    static constexpr uint32_t kYmClock = 3579545;
    static constexpr uint32_t kOkiClock = 1056000;

    ChinaGate();

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
    int sample_rate() const override { return YM2151::kSampleRate; }

    const char* title() const override { return "China Gate"; }

private:
    uint8_t main_read(uint16_t address);
    void main_write(uint16_t address, uint8_t value);
    uint8_t sub_read(uint16_t address);
    void sub_write(uint16_t address, uint8_t value);
    uint8_t sound_read(uint16_t address);
    void sound_write(uint16_t address, uint8_t value);
    void on_sound_cycles(int cycles);

    void update_palette(uint16_t offset);
    void update_video();
    void draw_background();
    void draw_foreground();
    void draw_sprites();
    void draw_sprite_element(int code, int color, bool flip_x, bool flip_y, int pos_x, int pos_y);

    M6809 main_cpu_{kMainClock};
    M6809 sub_cpu_{kSubClock};
    Z80 sound_cpu_{kSoundClock};
    YM2151 ym_{kYmClock};
    OKIM6295 oki_{kOkiClock, true};

    std::array<uint8_t, 0x10000> memory_{};
    std::array<uint8_t, 0x10000> sub_fixed_{};  // sub CPU $8000-$ffff
    std::array<uint8_t, 0x10000> sound_memory_{};
    std::array<uint8_t, 0x800> palette_ram_{};
    std::vector<uint8_t> banked_rom_;      // 6 x 16 KB
    std::vector<uint8_t> banked_rom_sub_;  // 6 x 16 KB

    GfxSet chars_, tiles_, sprites_;

    std::array<uint32_t, 0x400> palette_{};
    std::array<uint32_t, 512 * 512> background_{};
    std::array<uint32_t, 256 * 256> composite_{};
    std::vector<uint32_t> framebuffer_;

    uint8_t rom_bank_ = 0;
    uint8_t rom_bank_sub_ = 0;
    uint8_t sound_latch_ = 0;
    uint16_t scroll_x_ = 0;
    uint16_t scroll_y_ = 0;
    bool flip_screen_ = false;

    uint8_t in0_ = 0x0e;  // bit0 = vblank
    uint8_t in1_ = 0xff;
    uint8_t in2_ = 0xff;
    uint8_t dsw_a_ = 0xbf;
    uint8_t dsw_b_ = 0xe7;

    int64_t audio_accumulator_ = 0;
    std::vector<int16_t> audio_;
};

}  // namespace dsp
