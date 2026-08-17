#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "core/machine.h"
#include "cpu/nec_v30.h"
#include "cpu/z80.h"
#include "sound/ym2151.h"
#include "video/gfx.h"

namespace dsp {

// Irem M72 hardware, ported from m72_hw.pas.
// NEC V30 at 8 MHz, sound Z80 at 3.579545 MHz, YM2151; R-Type 2 / Hammerin'
// Harry also have a DAC sample ROM.
class M72 : public Machine {
public:
    enum class Game { Rtype, Hharry, Rtype2 };

    static constexpr int kScreenWidth = 384;
    static constexpr int kScreenHeight = 256;
    static constexpr double kFramesPerSecond = 55.017606;
    static constexpr int kScanlines = 284;
    static constexpr uint32_t kMainClock = 8000000;
    static constexpr uint32_t kSoundClock = 3579545;
    static constexpr int kTileMapSize = 512;
    static constexpr int kSpriteMapWidth = 1024;
    static constexpr int kSpriteMapHeight = 512;
    static constexpr uint32_t kTransparent = 0;

    explicit M72(Game game);

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

    const char* title() const override;

private:
    uint8_t main_read(uint32_t address);
    void main_write(uint32_t address, uint8_t value);
    uint16_t main_in_word(uint32_t port);
    void main_out_word(uint32_t port, uint16_t value);
    uint8_t main_in_byte(uint16_t port);
    void main_out_byte(uint16_t port, uint8_t value);

    uint8_t sound_read(uint16_t address);
    void sound_write(uint16_t address, uint8_t value);
    uint8_t sound_in(uint16_t port);
    void sound_out(uint16_t port, uint8_t value);

    void on_sound_cycles(int cycles);
    void sound_irq_ack();
    void set_sound_reset(bool held);

    void change_color1(uint16_t num);
    void change_color2(uint16_t num);
    void update_video();
    void paint_video(int line_from, int line_to);
    void draw_sprites();
    void draw_tile(std::vector<uint32_t>& dest, const GfxSet& gfx, int x, int y, int code,
                   int color, bool flipx, bool flipy, bool transparent);
    void clear_tile(std::vector<uint32_t>& dest, int x, int y);
    void blit_layer(const std::vector<uint32_t>& src, int src_w, int src_h, int scroll_x,
                    int scroll_y, bool opaque);
    void crop_to_screen(int line_from, int line_to);

    bool has_dac() const { return game_ != Game::Rtype; }
    uint32_t rom_mask() const { return game_ == Game::Rtype ? 0x3ffff : 0x7ffff; }

    Game game_;
    NecV30 main_cpu_;
    Z80 sound_cpu_;
    YM2151 ym_;

    std::vector<uint8_t> rom_;
    std::array<uint8_t, 0x4000> ram_{};
    std::array<uint8_t, 0x400> spriteram_{};
    std::array<uint8_t, 0x400> sprite_buffer_{};
    std::array<uint8_t, 0xc00> palette1_{};
    std::array<uint8_t, 0xc00> palette2_{};
    std::array<uint8_t, 0x4000> videoram1_{};
    std::array<uint8_t, 0x4000> videoram2_{};
    std::array<uint8_t, 0x10000> mem_snd_{};
    std::array<uint8_t, 0x20000> mem_dac_{};

    GfxSet chars0_;
    GfxSet chars1_;
    GfxSet sprites_;

    std::array<uint32_t, 512> palette_{};
    std::vector<uint32_t> layer_bg_lo_;
    std::vector<uint32_t> layer_bg_hi_;
    std::vector<uint32_t> layer_fg_lo_;
    std::vector<uint32_t> layer_fg_hi_;
    std::vector<uint32_t> composite_;
    std::vector<uint32_t> framebuffer_;
    std::array<bool, 0x1000> dirty_fg_{};
    std::array<bool, 0x1000> dirty_bg_{};
    std::array<bool, 16> dirty_color_{};

    uint8_t sound_latch_ = 0;
    uint8_t snd_irq_vector_ = 0xff;
    bool sound_irq_timer_ = false;
    bool sound_reset_held_ = false;
    uint16_t m72_raster_irq_position_ = 0;
    uint16_t scroll_x1_ = 0, scroll_y1_ = 0, scroll_x2_ = 0, scroll_y2_ = 0;
    bool video_off_ = true;
    uint32_t sample_addr_ = 0;
    std::array<uint8_t, 6> irq_base_{};
    uint8_t irq_pos_ = 0;
    int16_t dac_sample_ = 0;

    uint16_t in0_ = 0xffff;
    uint16_t in1_ = 0xffff;
    uint16_t dsw_ = 0xfdfb;

    int nmi_counter_ = 0;
    int nmi_period_ = 0;
    int64_t audio_accumulator_ = 0;
    std::vector<int16_t> audio_;
};

}  // namespace dsp
