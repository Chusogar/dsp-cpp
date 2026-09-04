#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "core/machine.h"
#include "cpu/z80.h"
#include "sound/sn76496.h"
#include "video/gfx.h"

namespace dsp {

// Mr. Do! (Universal, 1982), ported from mrdo_hw.pas.
// Z80 @ 4.1 MHz driving two SN76496 chips and a dual tilemap + sprite video board.
class MrDo : public Machine {
public:
    static constexpr int kScreenWidth = 192;
    static constexpr int kScreenHeight = 240;
    static constexpr double kFramesPerSecond = 59.94323742;
    static constexpr int kScanlines = 262;
    static constexpr uint32_t kCpuClock = 4100000;

    MrDo();

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

    const char* title() const override { return "Mr. Do!"; }

private:
    uint8_t read_byte(uint16_t address);
    void write_byte(uint16_t address, uint8_t value);
    void on_cycles(int cycles);

    void decode_graphics(const std::vector<uint8_t>& char1_rom,
                         const std::vector<uint8_t>& char2_rom,
                         const std::vector<uint8_t>& sprite_rom);
    void build_palette(const std::vector<uint8_t>& prom);
    void update_video();
    void draw_bg_tile(int offset);
    void draw_fg_tile(int offset);
    void draw_sprite(int index);
    void blit_scrolled(const std::array<uint32_t, 256 * 256>& source, bool transparent);
    void blit_layer(const std::array<uint32_t, 256 * 256>& source, bool transparent);

    Z80 cpu_;
    SN76496 sn0_;
    SN76496 sn1_;

    std::array<uint8_t, 0x10000> memory_{};
    std::array<bool, 0x400> dirty_bg_{};
    std::array<bool, 0x400> dirty_fg_{};
    std::array<uint32_t, 256> palette_{};
    // Sprite colour lookup: index is ((attr & 0x0f) << 2) | pen.
    std::array<uint8_t, 0x40> sprite_lut_{};

    GfxSet chars_fg_;  // gfx 0: foreground 8x8
    GfxSet chars_bg_;  // gfx 1: background 8x8
    GfxSet sprites_;   // gfx 2: 16x16 sprites

    // Work surfaces matching the five screens in mrdo_hw.pas.
    std::array<uint32_t, 256 * 256> bg_opaque_{};
    std::array<uint32_t, 256 * 256> fg_opaque_{};
    std::array<uint32_t, 256 * 256> bg_trans_{};
    std::array<uint32_t, 256 * 256> fg_trans_{};
    std::array<uint32_t, 256 * 256> composite_{};
    std::vector<uint32_t> framebuffer_;

    uint8_t scroll_x_ = 0;
    uint8_t scroll_y_ = 0;
    bool flip_screen_ = false;

    uint8_t in0_ = 0xff;
    uint8_t in1_ = 0xff;
    uint8_t dsw_a_ = 0xdf;
    uint8_t dsw_b_ = 0xff;

    int64_t audio_accumulator_ = 0;
    std::vector<int16_t> audio_;
};

}  // namespace dsp
