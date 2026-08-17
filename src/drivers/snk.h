#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "core/machine.h"
#include "cpu/z80.h"
#include "sound/ym3812.h"
#include "video/gfx.h"

namespace dsp {

// SNK three-Z80 hardware, ported from snk_hw.pas:
//   ikari  — Ikari Warriors (216x288, 16x16 + 32x32 sprites, two YM3526)
//   athena — Athena (288x216, TNK3 video + Athena sprites, two YM3526)
//   tnk3   — TNK III (288x216 rotated 270°, one YM3526)
//   aso    — ASO / Alpha Mission (288x216 rotated 270°, one YM3526)
class Snk : public Machine {
public:
    enum class Game { Ikari, Athena, Tnk3, Aso };

    static constexpr double kFramesPerSecond = 60.0;
    static constexpr int kScanlines = 224;
    static constexpr int kCpuSync = 5;
    static constexpr uint32_t kMainClock = 3350000;
    static constexpr uint32_t kSubClock = 3350000;
    static constexpr uint32_t kSoundClock = 4000000;
    static constexpr uint32_t kYmClock = 4000000;
    static constexpr int kSoundTimerCycles = 120;
    static constexpr int kWorkWidth = 512;
    static constexpr int kWorkHeight = 512;
    static constexpr uint32_t kTransparent = 0;
    static constexpr uint32_t kShadowRgb = 0xff000000u;

    explicit Snk(Game game);

    bool init(const std::string& rom_path, std::string* error) override;
    void reset() override;
    void run_frame() override;

    void set_inputs(const MachineInputs& inputs) override;
    void set_dip_switch(int bank, uint8_t value) override;

    const uint32_t* framebuffer() const override { return framebuffer_.data(); }
    int screen_width() const override { return screen_width_; }
    int screen_height() const override { return screen_height_; }
    double frames_per_second() const override { return kFramesPerSecond; }

    void drain_audio(std::vector<int16_t>& out) override;
    int sample_rate() const override { return YM3812::kSampleRate; }

    const char* title() const override;

private:
    bool is_ikari() const { return game_ == Game::Ikari; }
    bool is_athena() const { return game_ == Game::Athena; }
    bool is_tnk3() const { return game_ == Game::Tnk3; }
    bool is_aso() const { return game_ == Game::Aso; }
    bool uses_two_ym() const { return is_ikari() || is_athena(); }
    bool rotate_final() const { return is_tnk3() || is_aso(); }

    uint8_t main_read(uint16_t address);
    void main_write(uint16_t address, uint8_t value);
    uint8_t sub_read(uint16_t address);
    void sub_write(uint16_t address, uint8_t value);
    uint8_t sound_read(uint16_t address);
    void sound_write(uint16_t address, uint8_t value);

    uint8_t ikari_main_read(uint16_t address);
    void ikari_main_write(uint16_t address, uint8_t value);
    uint8_t ikari_sub_read(uint16_t address);
    void ikari_sub_write(uint16_t address, uint8_t value);

    uint8_t athena_main_read(uint16_t address);
    void athena_main_write(uint16_t address, uint8_t value);
    uint8_t tnk3_main_read(uint16_t address);
    uint8_t athena_sub_read(uint16_t address);
    void athena_sub_write(uint16_t address, uint8_t value);

    uint8_t aso_main_read(uint16_t address);
    void aso_main_write(uint16_t address, uint8_t value);
    uint8_t aso_sub_read(uint16_t address);
    void aso_sub_write(uint16_t address, uint8_t value);

    uint8_t ikari_sound_read(uint16_t address);
    void ikari_sound_write(uint16_t address, uint8_t value);
    uint8_t tnk3_sound_read(uint16_t address);
    void tnk3_sound_write(uint16_t address, uint8_t value);
    uint8_t aso_sound_read(uint16_t address);
    void aso_sound_write(uint16_t address, uint8_t value);

    void on_sound_cycles(int cycles);
    void fire_sound_irq();
    void write_sound_latch(uint8_t value);

    uint8_t hardflags_check(int index) const;
    uint8_t hardflags_check8(int first) const;

    void write_bg_byte(int offset, uint8_t value);
    void write_txt_byte(int offset, uint8_t value);

    bool load_roms(const std::string& rom_path, std::string* error);
    void decode_ikari(const std::vector<uint8_t>& chars, const std::vector<uint8_t>& tiles,
                      const std::vector<uint8_t>& sp16, const std::vector<uint8_t>& sp32);
    void decode_tnk_family(const std::vector<uint8_t>& chars, const std::vector<uint8_t>& tiles,
                           const std::vector<uint8_t>& sp16, int char_total, int sprite_total,
                           bool duplicate_chars);
    void build_ikari_palette(const std::vector<uint8_t>& prom);
    void build_tnk_palette(const std::vector<uint8_t>& prom);

    void update_video();
    void update_video_ikari();
    void update_video_tnk3();
    void update_video_aso();
    void draw_text_ikari();
    void draw_text_tnk3(bool use_txt_offset);
    void draw_bg_ikari();
    void draw_bg_tnk3();
    void draw_bg_aso();
    void draw_sprites16_ikari(int bank);
    void draw_sprites32_ikari();
    void draw_sprites_athena();
    void draw_sprites_tnk3();
    void blit_scrolled_bg(int scroll_x, int scroll_y);
    void blit_text(int width, int height);
    void blit_sprite_shadow(const GfxSet& gfx, int code, int color, bool flip_x, bool flip_y,
                            int dest_x, int dest_y, int wrap);
    void put_tile(const GfxSet& gfx, std::vector<uint32_t>& dest, int dest_w, int x, int y, int code,
                  int color, bool transparent, int trans_pen);
    void copy_final(int src_w, int src_h);

    Game game_;
    int screen_width_ = 216;
    int screen_height_ = 288;

    Z80 main_cpu_;
    Z80 sub_cpu_;
    Z80 sound_cpu_;
    YM3812 ym0_;
    YM3812 ym1_;

    std::array<uint8_t, 0x10000> memory_{};
    std::array<uint8_t, 0x10000> sub_memory_{};
    std::array<uint8_t, 0x10000> sound_memory_{};
    std::array<uint8_t, 0x800> txt_ram_{};
    std::array<uint8_t, 0x800> sprite_ram_{};
    std::array<uint8_t, 0x2000> bg_ram_{};
    std::array<bool, 0x800> dirty_txt_{};
    std::array<bool, 0x1000> dirty_bg_{};

    GfxSet chars_;
    GfxSet tiles_;
    GfxSet sprites16_;
    GfxSet sprites32_;

    std::array<uint32_t, 2048> palette_{};
    std::vector<uint32_t> text_;
    std::vector<uint32_t> background_;
    std::vector<uint32_t> composite_;
    std::vector<uint32_t> framebuffer_;

    uint16_t scroll_x_ = 0;
    uint16_t scroll_y_ = 0;
    uint16_t sp16_scroll_x_ = 0;
    uint16_t sp16_scroll_y_ = 0;
    uint16_t sp32_scroll_x_ = 0;
    uint16_t sp32_scroll_y_ = 0;
    uint16_t hf_pos_x_ = 0;
    uint16_t hf_pos_y_ = 0;
    uint16_t txt_offset_ = 0;
    uint16_t bg_offset_ = 0;
    uint16_t bg_pal_offset_ = 0;
    bool flip_screen_ = false;

    uint8_t sound_status_ = 0;
    uint8_t sound_latch_ = 0;
    bool sound_timer_enabled_ = false;
    int sound_timer_cycles_ = 0;

    uint8_t in0_ = 0xfe;
    uint8_t in1_ = 0xbf;
    uint8_t in2_ = 0xbf;
    uint8_t in3_ = 0xff;
    uint8_t dsw_a_ = 0x3b;
    uint8_t dsw_b_ = 0x4b;
    uint8_t dsw_c_ = 0x34;
    uint8_t rot_cont_ = 0;
    uint8_t rot_nibble_ = 0xb0;

    int64_t audio_accumulator_ = 0;
    std::vector<int16_t> audio_;
};

}  // namespace dsp
