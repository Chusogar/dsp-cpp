#pragma once

#include "core/machine.h"
#include "cpu/z80.h"
#include "video/gfx.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace dsp {

// Galaxian hardware (Namco / Midway, 1979) — port of galaxian_hw.pas (game 0).
// Memory map, video (tiles + column scroll + 8 sprites + bullets + stars) and
// NMI timing match the Pascal original. Discrete samples are stubbed silent.
class Galaxian : public Machine {
public:
    static constexpr int kScreenWidth = 224;
    static constexpr int kScreenHeight = 256;
    static constexpr double kFramesPerSecond = 60.60606060;
    static constexpr int kScanlines = 256;
    static constexpr uint32_t kCpuClock = 3072000;

    Galaxian();

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
    int sample_rate() const override { return 44100; }

    const char* title() const override { return "Galaxian"; }

private:
    uint8_t read_byte(uint16_t address);
    void write_byte(uint16_t address, uint8_t value);
    void on_cycles(int cycles);

    bool load_roms(const std::string& rom_path, std::string* error);
    void decode_graphics(const std::vector<uint8_t>& gfx_rom);
    void build_palette(const std::vector<uint8_t>& prom);

    void update_video();
    void draw_tile(int offset);
    void draw_sprite(int index);
    void draw_bullets();
    void draw_stars();

    Z80 cpu_;

    std::array<uint8_t, 0x10000> memory_{};
    std::array<uint8_t, 0x400> videoram_{};
    std::array<uint8_t, 0x40> attributes_{};
    std::array<uint8_t, 0x20> sprites_{};
    std::array<uint8_t, 0x20> bullets_{};
    std::array<bool, 0x400> dirty_{};

    std::array<uint32_t, 64> palette_{};
    GfxSet chars_;
    GfxSet sprites_gfx_;

    std::array<uint32_t, 256 * 256> tilemap_{};
    std::array<uint32_t, 256 * 256> composite_{};
    std::vector<uint32_t> framebuffer_;

    bool nmi_enable_ = false;
    bool stars_enable_ = false;
    uint32_t stars_scroll_ = 0;

    uint8_t in0_ = 0;
    uint8_t in1_ = 0;
    uint8_t dsw_a_ = 0;
    uint8_t dsw_b_ = 0;
    uint8_t dsw_c_ = 0;

    int64_t audio_accumulator_ = 0;
    std::vector<int16_t> audio_;
};

}  // namespace dsp
