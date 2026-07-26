#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "cpu/z80.h"
#include "machine/bagman_pal.h"
#include "sound/ay8910.h"
#include "video/gfx.h"

namespace dsp {

// Player inputs, one entry per player.
struct InputState {
    bool up = false;
    bool down = false;
    bool left = false;
    bool right = false;
    bool button = false;
    bool start = false;
};

// Bagman (Valadon Automation, 1982), ported from bagman_hw.pas.
class Bagman {
public:
    static constexpr int kScreenWidth = 224;
    static constexpr int kScreenHeight = 256;
    static constexpr double kFramesPerSecond = 60.60606060;
    static constexpr int kScanlines = 264;
    static constexpr uint32_t kCpuClock = 3072000;
    static constexpr uint32_t kAyClock = 1536000;

    Bagman();

    // `rom_path` is a directory or a zip archive holding the bagman ROM set.
    bool init(const std::string& rom_path, std::string* error);
    void reset();

    // Runs a full frame and renders it into the internal framebuffer.
    void run_frame();

    void set_inputs(const InputState& player1, const InputState& player2, bool coin1, bool coin2);
    void set_dip_switches(uint8_t value) { dsw_ = value; }
    uint8_t dip_switches() const { return dsw_; }

    // ARGB8888 framebuffer, kScreenWidth * kScreenHeight pixels.
    const uint32_t* framebuffer() const { return framebuffer_.data(); }

    // Consumes the audio samples generated so far (mono, 44100 Hz, signed 16 bit).
    void drain_audio(std::vector<int16_t>& out);

    const std::vector<std::string>& warnings() const { return warnings_; }

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
    std::vector<std::string> warnings_;
};

}  // namespace dsp
