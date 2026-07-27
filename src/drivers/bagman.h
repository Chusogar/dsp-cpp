#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "core/machine.h"
#include "cpu/z80.h"
#include "machine/bagman_pal.h"
#include "sound/ay8910.h"
#include "video/gfx.h"

namespace dsp {

// Bagman (Valadon Automation, 1982), ported from bagman_hw.pas.
class Bagman : public Machine {
public:
    static constexpr int kScreenWidth = 224;
    static constexpr int kScreenHeight = 256;
    static constexpr double kFramesPerSecond = 60.60606060;
    static constexpr int kScanlines = 264;
    static constexpr uint32_t kCpuClock = 3072000;
    static constexpr uint32_t kAyClock = 1536000;

    Bagman();

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
    int sample_rate() const override { return AY8910::kSampleRate; }

    const char* title() const override { return "Bagman"; }

private:
    uint8_t read_byte(uint16_t address);
    void write_byte(uint16_t address, uint8_t value);
    uint8_t read_port(uint16_t port);
    void write_port(uint16_t port, uint8_t value);
    void on_cycles(int cycles);

    void decode_graphics(const std::vector<uint8_t>& char_rom,
                         const std::vector<uint8_t>& sprite_rom);
    void build_palette(const std::vector<uint8_t>& prom);
    void update_video();
    void draw_tile(int offset);
    void draw_sprite(int index);

    Z80 cpu_;
    AY8910 psg_;
    BagmanPal pal_;

    std::array<uint8_t, 0x10000> memory_{};
    std::array<bool, 0x400> dirty_{};
    std::array<uint32_t, 256> palette_{};

    GfxSet chars_;         // gfx 0: characters, bank 0
    GfxSet sprites_;       // gfx 1: 16x16 sprites
    GfxSet chars_bank1_;   // gfx 2: characters, bank 1

    std::array<uint32_t, 256 * 256> tilemap_{};    // background layer
    std::array<uint32_t, 256 * 256> composite_{};  // background + sprites
    std::vector<uint32_t> framebuffer_;

    bool irq_enable_ = true;
    bool video_enable_ = true;
    bool flip_screen_ = false;

    uint8_t in0_ = 0xff;
    uint8_t in1_ = 0xff;
    uint8_t dsw_ = 0xfe;

    int64_t audio_accumulator_ = 0;
    std::vector<int16_t> audio_;
};

}  // namespace dsp
