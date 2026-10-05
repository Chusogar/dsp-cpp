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

// Sanritsu / Sega Bank Panic hardware, ported from bankpanic_hw.pas.
// Games: Bank Panic, Combat Hawk.
class BankPanicHw : public Machine {
public:
    enum class Game { BankPanic, CombatHawk };

    static constexpr int kVisWidth = 224;
    static constexpr int kVisHeight = 224;
    static constexpr int kWorkSize = 256;
    static constexpr double kFramesPerSecond = 61.034091;
    static constexpr int kScanlines = 256;
    static constexpr uint32_t kCpuClock = 15468480 / 6;

    explicit BankPanicHw(Game game);

    bool init(const std::string& rom_path, std::string* error) override;
    void reset() override;
    void run_frame() override;

    void set_inputs(const MachineInputs& inputs) override;
    void set_dip_switch(int bank, uint8_t value) override;

    const uint32_t* framebuffer() const override { return framebuffer_.data(); }
    int screen_width() const override { return kVisWidth; }
    int screen_height() const override { return kVisHeight; }
    double frames_per_second() const override { return kFramesPerSecond; }

    void drain_audio(std::vector<int16_t>& out) override;
    int sample_rate() const override { return SN76496::kSampleRate; }

    const char* title() const override;

private:
    uint8_t read_byte(uint16_t address);
    void write_byte(uint16_t address, uint8_t value);
    uint8_t read_port(uint16_t port);
    void write_port(uint16_t port, uint8_t value);
    void on_cycles(int cycles);

    bool load_roms(const std::string& rom_path, std::string* error);
    void decode_chars(const std::vector<uint8_t>& rom);
    void decode_tiles(const std::vector<uint8_t>& rom);
    void build_palette(const std::vector<uint8_t>& prom);

    void update_video();
    void draw_fg_layer(bool masked);
    void draw_bg_layer(bool masked);
    void blit_tile(std::vector<uint32_t>& dest, const GfxSet& gfx,
                   const std::array<uint8_t, 512>& lut, int x, int y, int code,
                   int color_base, bool flip_x, bool masked, uint8_t mask);

    Game game_;
    Z80 cpu_;
    SN76496 sn0_;
    SN76496 sn1_;
    SN76496 sn2_;

    std::array<uint8_t, 0x10000> memory_{};
    GfxSet chars_;   // FG, gfx 0
    GfxSet tiles_;   // BG, gfx 1
    std::array<uint32_t, 32> palette_{};
    std::array<uint8_t, 512> fg_lut_{};
    std::array<uint8_t, 512> bg_lut_{};

    std::vector<uint32_t> fg_layer_;
    std::vector<uint32_t> bg_layer_;
    std::vector<uint32_t> work_;
    std::vector<uint32_t> framebuffer_;

    std::array<bool, 0x400> fg_dirty_{};
    std::array<bool, 0x400> bg_dirty_{};

    uint8_t in0_ = 0;
    uint8_t in1_ = 0;
    uint8_t in2_ = 0;
    uint8_t dsw_ = 0xc0;

    bool priority_ = false;
    bool display_on_ = true;
    bool nmi_vblank_ = false;
    uint8_t color_hi_ = 0;
    uint8_t scroll_x_ = 0;

    int64_t audio_accumulator_ = 0;
    std::vector<int16_t> audio_;
};

}  // namespace dsp
