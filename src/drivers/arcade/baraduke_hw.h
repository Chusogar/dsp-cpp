#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "core/machine.h"
#include "cpu/hd63701.h"
#include "cpu/m6809.h"
#include "sound/namco_cus30.h"
#include "video/gfx.h"

namespace dsp {

// Namco Baraduke hardware (Baraduke / Alien Sector, Metro-Cross), ported from
// baraduke_hw.pas. Main M6809 + HD63701V MCU (CUS60), shared Namco CUS30 sound.
class BaradukeHw : public Machine {
public:
    enum class Game { Baraduke, MetroCross };

    static constexpr int kScreenWidth = 288;
    static constexpr int kScreenHeight = 224;
    static constexpr double kFramesPerSecond = 60.606060606;
    static constexpr int kScanlines = 264;
    static constexpr uint32_t kMasterClock = 49152000;
    static constexpr uint32_t kMainClock = kMasterClock / 32;  // 1.536 MHz
    static constexpr uint32_t kMcuClock = kMasterClock / 8;    // 6.144 MHz

    explicit BaradukeHw(Game game);

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
    int sample_rate() const override { return NamcoCus30::kSampleRate; }

    const char* title() const override;

private:
    static constexpr int kTileW = 512;
    static constexpr int kTileH = 256;

    uint8_t main_read(uint16_t address);
    void main_write(uint16_t address, uint8_t value);
    uint8_t mcu_read(uint16_t address);
    void mcu_write(uint16_t address, uint8_t value);
    uint8_t in_port1();
    void out_port1(uint8_t value);
    void on_mcu_cycles(int cycles);

    bool load_roms(const std::string& rom_path, std::string* error);
    void convert_chars(const std::vector<uint8_t>& rom);
    void convert_tiles(std::vector<uint8_t> rom);
    void convert_sprites(const std::vector<uint8_t>& rom, int count);
    void build_palette(const std::vector<uint8_t>& prom);

    void update_video();
    void draw_text_layer();
    void draw_tile_layer(int layer, bool transparent);
    void scroll_layer_to_screen(const std::vector<uint32_t>& layer, int scroll_x, int scroll_y);
    void draw_sprites(int priority);
    void copy_sprites_hw();

    Game game_;
    M6809 main_cpu_;
    HD63701 mcu_;
    NamcoCus30 cus30_;

    std::array<uint8_t, 0x10000> memory_{};
    std::array<uint8_t, 0x10000> mem_snd_{};

    GfxSet chars_;     // gfx 0
    GfxSet tiles0_;    // gfx 1
    GfxSet tiles1_;    // gfx 2
    GfxSet sprites_;   // gfx 3

    std::array<uint32_t, 0x800> palette_{};
    std::vector<uint32_t> text_layer_;
    std::vector<uint32_t> tile_layer0_;
    std::vector<uint32_t> tile_layer1_;
    std::vector<uint32_t> framebuffer_;

    std::array<bool, 0x400> text_dirty_{};
    std::array<bool, 0x800> tile0_dirty_{};
    std::array<bool, 0x800> tile1_dirty_{};

    uint8_t in0_ = 0x1f;
    uint8_t in1_ = 0x1f;
    uint8_t in2_ = 0x1f;
    uint8_t dsw_a_ = 0xff;
    uint8_t dsw_b_ = 0xff;
    uint8_t dsw_c_ = 0xff;

    uint8_t inputport_selected_ = 0;
    uint16_t counter_ = 0;
    uint16_t scroll_x0_ = 0;
    uint16_t scroll_x1_ = 0;
    uint8_t scroll_y0_ = 0;
    uint8_t scroll_y1_ = 0;
    bool prio_ = false;
    bool copy_sprites_ = false;
    int spritex_add_ = 0;
    int spritey_add_ = 0;

    int64_t audio_accumulator_ = 0;
    std::vector<int16_t> audio_;
};

}  // namespace dsp
