#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "core/machine.h"
#include "cpu/z80.h"
#include "machine/taito_68705.h"
#include "sound/sn76496.h"
#include "video/gfx.h"

namespace dsp {

// Return of the Invaders (Taito, 1985), from returnofinvaders_hw.pas.
class Retofinv : public Machine {
public:
    static constexpr int kScreenWidth = 224;
    static constexpr int kScreenHeight = 288;
    static constexpr double kFramesPerSecond = 60.0;
    static constexpr int kScanlines = 224;
    static constexpr uint32_t kCpuClock = 18432000 / 6;

    Retofinv();

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

    const char* title() const override { return "Return of the Invaders"; }

private:
    uint8_t main_read(uint16_t address);
    void main_write(uint16_t address, uint8_t value);
    uint8_t sub_read(uint16_t address);
    void sub_write(uint16_t address, uint8_t value);
    uint8_t sound_read(uint16_t address);
    void sound_write(uint16_t address, uint8_t value);
    void on_sound_cycles(int cycles);

    void decode_graphics(const std::vector<uint8_t>& char_rom,
                         const std::vector<uint8_t>& tile_rom,
                         const std::vector<uint8_t>& sprite_rom);
    void build_palette(const std::vector<uint8_t>& prom, const std::vector<uint8_t>& clut);
    void update_video();
    void draw_bg();
    void draw_fg();
    void draw_sprites();

    Z80 main_cpu_;
    Z80 sub_cpu_;
    Z80 sound_cpu_;
    Taito68705 mcu_;
    SN76496 sn0_;
    SN76496 sn1_;

    std::array<uint8_t, 0x10000> memory_{};
    std::array<uint8_t, 0x2000> sub_rom_{};
    std::array<uint8_t, 0x10000> sound_mem_{};

    std::array<uint32_t, 256> palette_{};
    std::array<uint8_t, 0x200> fg_lut_{};
    std::array<uint8_t, 0x800> bg_lut_{};
    std::array<uint8_t, 0x800> sprite_lut_{};

    GfxSet chars_;
    GfxSet tiles_;
    GfxSet sprites_;

    std::vector<uint32_t> work_;
    std::vector<uint32_t> framebuffer_;

    uint8_t fg_bank_ = 0;
    uint8_t bg_bank_ = 0;
    bool flip_screen_ = false;
    bool main_irq_enable_ = false;
    bool sub_irq_enable_ = false;
    bool sub_enabled_ = false;
    bool sound_enabled_ = false;
    bool mcu_enabled_ = false;

    uint8_t sound_latch_ = 0;
    uint8_t sound_return_ = 0;

    uint8_t in0_ = 0xff;
    uint8_t in1_ = 0xff;
    uint8_t in2_ = 0xcf;
    uint8_t dsw_a_ = 0x6f;
    uint8_t dsw_b_ = 0x00;
    uint8_t dsw_c_ = 0xff;

    int64_t audio_accumulator_ = 0;
    std::vector<int16_t> audio_;
};

}  // namespace dsp
