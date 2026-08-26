#pragma once

#include "core/machine.h"
#include "cpu/m68000.h"
#include "cpu/z80.h"
#include "sound/dac.h"
#include "sound/ym3812.h"
#include "machine/nb1414_m4.h"
#include "video/gfx.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace dsp {

// Nichibutsu Armed Formation hardware (armedf_hw.pas).
// Main M68000 @ 8 MHz + sound Z80 @ 4 MHz, YM3812 + dual 8-bit DACs.
// Games: Armed F, Terra Force, Crazy Climber 2, Legion (Armed F first).
class ArmedF : public Machine {
public:
    enum class Game {
        ArmedF,
        Terraf,
        Cclimbr2,
        Legion,
    };

    static constexpr int kScreenWidth = 320;
    static constexpr int kScreenHeight = 240;
    static constexpr double kFramesPerSecond = 59.082012;
    static constexpr int kScanlines = 256;
    static constexpr uint32_t kMainClock = 8000000;
    static constexpr uint32_t kSoundClock = 4000000;
    static constexpr uint32_t kYmClock = 4000000;

    explicit ArmedF(Game game = Game::ArmedF);

    bool init(const std::string& rom_path, std::string* error) override;
    void reset() override;
    void run_frame() override;

    void set_inputs(const MachineInputs& inputs) override;
    void set_dip_switch(int bank, uint8_t value) override;

    const uint32_t* framebuffer() const override { return framebuffer_.data(); }
    int screen_width() const override { return display_width_; }
    int screen_height() const override { return display_height_; }
    double frames_per_second() const override { return kFramesPerSecond; }

    void drain_audio(std::vector<int16_t>& out) override;
    int sample_rate() const override { return YM3812::kSampleRate; }

    const char* title() const override;

private:
    uint16_t main_read_word(uint32_t address);
    void main_write_word(uint32_t address, uint16_t value);
    uint8_t main_read_byte(uint32_t address);
    void main_write_byte(uint32_t address, uint8_t value);

    uint8_t sound_read(uint16_t address);
    void sound_write(uint16_t address, uint8_t value);
    uint8_t sound_in(uint16_t port);
    void sound_out(uint16_t port, uint8_t value);
    void on_sound_cycles(int cycles);

    bool load_roms(const std::string& rom_path, std::string* error);
    void decode_graphics();
    void set_color(int pos, uint16_t data);

    void update_video();
    void draw_text_layer(bool opaque);
    void draw_tile_layer(const std::array<uint16_t, 0x800>& ram, const GfxSet& gfx,
                         int color_base, int scroll_x, int scroll_y);
    void draw_sprites(int priority);

    Game game_;
    M68000 main_cpu_;
    Z80 sound_cpu_;
    YM3812 ym_;
    Dac dac0_;
    Dac dac1_;
    Nb1414M4 nb1414_;

    // 68000 program ROM as bytes (big-endian word layout).
    std::vector<uint8_t> rom_;
    std::array<uint16_t, 0x6400> ram_{};
    std::array<uint16_t, 0x800> ram_sprites_{};
    std::array<uint16_t, 0x800> ram_bg_{};
    std::array<uint16_t, 0x800> ram_fg_{};
    std::array<uint16_t, 0x800> ram_clut_{};
    std::array<uint8_t, 0x1000> ram_txt_{};
    std::array<uint16_t, 0x800> palette_ram_{};
    std::array<uint32_t, 0x800> palette_{};

    std::array<uint8_t, 0x10000> mem_snd_{};

    // Sprite RAM snapshot (copied at end of frame, used next frame — MAME/Pascal).
    std::array<uint16_t, 0x800> sprite_buffer_{};

    GfxSet chars_;
    GfxSet tiles_bg_;
    GfxSet tiles_fg_;
    GfxSet sprites_;

    // Work surfaces (Pascal screens 1–5 simplified).
    std::vector<uint32_t> layer_txt_op_;   // 512x256
    std::vector<uint32_t> layer_txt_tr_;   // 512x256 transparent
    std::vector<uint32_t> layer_bg_;       // 1024x512
    std::vector<uint32_t> layer_fg_;       // 1024x512
    std::vector<uint32_t> composite_;      // 512x512
    std::vector<uint32_t> framebuffer_;

    uint16_t video_reg_ = 0;
    uint16_t scroll_fg_x_ = 0;
    uint16_t scroll_fg_y_ = 0;
    uint16_t scroll_bg_x_ = 0;
    uint16_t scroll_bg_y_ = 0;
    uint8_t sound_latch_ = 0;
    uint8_t irq_level_ = 1;
    uint8_t sprite_offset_ = 0x80;
    int sprite_num_ = 0x1ff;
    bool rotate_270_ = false;
    int display_width_ = kScreenWidth;
    int display_height_ = kScreenHeight;
    uint8_t frame_counter_ = 0;
    uint16_t prev_video_reg_ = 0;

    uint16_t in0_ = 0xffff;
    uint16_t in1_ = 0xffff;
    uint16_t dsw_a_ = 0xffff;
    uint16_t dsw_b_ = 0xffff;

    int64_t audio_accum_ = 0;
    int sound_irq_counter_ = 0;
    std::vector<int16_t> audio_;

    // Raw GFX ROMs kept for decode.
    std::vector<uint8_t> gfx_char_;
    std::vector<uint8_t> gfx_bg_;
    std::vector<uint8_t> gfx_fg_;
    std::vector<uint8_t> gfx_spr_;
};

}  // namespace dsp
