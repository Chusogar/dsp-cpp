#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "core/machine.h"
#include "cpu/m68000.h"
#include "cpu/z80.h"
#include "machine/eeprom93c46.h"
#include "sound/okim6295.h"
#include "sound/qsound.h"
#include "sound/ym2151.h"
#include "video/gfx.h"

namespace dsp {

// Capcom CPS1, ported from cps1_hw.pas.
class Cps1 : public Machine {
public:
    enum class Game {
        Ghouls,
        Ffight,
        Kod,
        Sf2,
        Strider,
        Wonder3,
        Captcomm,
        Knights,
        Sf2ce,
        Dino,
        Punisher,
        Willow,
        Ca1941,
        Nemo,
    };

    static constexpr int kScreenWidth = 384;
    static constexpr int kScreenHeight = 224;
    static constexpr double kFramesPerSecond = 59.61;
    static constexpr int kScanlines = 262;
    static constexpr int kWorkSize = 512;
    static constexpr uint32_t kZ80ClockYm = 3579545;
    static constexpr uint32_t kZ80ClockQsound = 8000000;
    static constexpr uint32_t kMainClockSlow = 10000000;
    static constexpr uint32_t kMainClockFast = 12000000;

    explicit Cps1(Game game);

    static constexpr uint8_t kGfxSprites = 1 << 0;
    static constexpr uint8_t kGfxScroll1 = 1 << 1;
    static constexpr uint8_t kGfxScroll2 = 1 << 2;
    static constexpr uint8_t kGfxScroll3 = 1 << 3;
    static constexpr uint8_t kGfxStars = 1 << 4;

    struct CpsB {
        uint16_t layerctrl = 0x1ff;
        uint16_t palctrl = 0x1ff;
        uint16_t testaddr = 0x1ff;
        uint16_t testval = 0;
        uint16_t mula = 0x1ff;
        uint16_t mulb = 0x1ff;
        uint16_t mull = 0x1ff;
        uint16_t mulh = 0x1ff;
        uint8_t mask_sc1 = 0;
        uint8_t mask_sc2 = 0;
        uint8_t mask_sc3 = 0;
        uint8_t mask_sc4 = 0;
        uint16_t pri_mask1 = 0x1ff;
        uint16_t pri_mask2 = 0x1ff;
        uint16_t pri_mask3 = 0x1ff;
        uint16_t pri_mask4 = 0x1ff;
    };
    struct BankRange {
        uint8_t type = 0;
        uint32_t start = 0;
        uint32_t end = 0;
        uint8_t num_bank = 0;
    };
    struct BankMap {
        std::array<uint32_t, 4> lbank{};
        std::array<BankRange, 7> ranges{};
    };

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
    int sample_rate() const override { return YM2151::kSampleRate; }

    const char* title() const override;

    uint32_t debug_pc() const { return main_cpu_.pc(); }
    uint16_t debug_layer() const { return cps1_layer_; }
    uint16_t debug_palctrl() const { return cps1_palcltr_; }
    uint16_t debug_videocontrol() const { return video_control_; }
    uint32_t debug_pal_base() const { return cps1_pal_; }
    uint32_t debug_scroll1_base() const { return cps1_scroll1_; }
    uint32_t debug_scroll2_base() const { return cps1_scroll2_; }
    uint32_t debug_scroll3_base() const { return cps1_scroll3_; }
    uint32_t debug_sprite_base() const { return cps1_sprites_; }
    uint16_t debug_scroll_x1() const { return scroll_x1_; }
    uint16_t debug_scroll_y1() const { return scroll_y1_; }
    uint16_t debug_scroll_x2() const { return scroll_x2_; }
    uint16_t debug_scroll_y2() const { return scroll_y2_; }
    uint16_t debug_scroll_x3() const { return scroll_x3_; }
    uint16_t debug_scroll_y3() const { return scroll_y3_; }

    uint16_t debug_sprite_word(int index) const {
        return sprite_buffer_[size_t(index) & 0x3ff];
    }
    uint16_t debug_vram_word(uint32_t word) const { return vram_[word % 0x18000]; }
    const uint8_t* debug_char0(int code) const { return chars0_.element(code); }
    const uint8_t* debug_char1(int code) const { return chars1_.element(code); }
    const uint8_t* debug_tile16(int code) const { return tiles16_.element(code); }

private:
    bool uses_qsound() const { return game_ == Game::Dino || game_ == Game::Punisher; }
    bool rotate_final() const { return game_ == Game::Ca1941; }
    uint32_t main_clock() const {
        return (game_ == Game::Sf2ce || uses_qsound()) ? kMainClockFast : kMainClockSlow;
    }
    uint32_t sound_clock() const { return uses_qsound() ? kZ80ClockQsound : kZ80ClockYm; }

    uint16_t main_read(uint32_t address);
    void main_write(uint32_t address, uint16_t value);
    uint16_t read_io(uint16_t dir) const;
    void write_io(uint16_t dir, uint16_t value);
    uint8_t sound_read(uint16_t address);
    void sound_write(uint16_t address, uint8_t value);
    uint8_t qsound_z80_read(uint16_t address);
    uint8_t qsound_z80_opcode(uint16_t address);
    void qsound_z80_write(uint16_t address, uint8_t value);
    void on_sound_cycles(int cycles);
    void test_buffers(uint32_t address);
    void calc_mask(uint16_t mask, int index);

    int gfx_bank(uint8_t type, uint16_t nchar) const;
    void pal_calc();
    void update_video();
    void draw_sprites();
    void draw_layer(int nlayer, bool sprite_next);
    void draw_stars();
    void draw_tile(const GfxSet& gfx, std::vector<uint32_t>& dest, int dest_w, int dest_h, int x,
                   int y, int code, int color, bool flipx, bool flipy, const bool* trans_alt);
    void clear_tile(std::vector<uint32_t>& dest, int dest_w, int dest_h, int x, int y, int size);
    void blit_scrolled(const std::vector<uint32_t>& src, int src_w, int src_h, int scroll_x,
                       int scroll_y);
    void blit_rowscroll();
    void copy_final();

    bool load_roms(const std::string& rom_path, std::string* error);
    void decode_graphics(const std::vector<uint8_t>& gfx, int chars, int tiles16, int tiles32);
    void install_sound_rom(const std::vector<uint8_t>& sound);
    static void cps1_gfx_decode(std::vector<uint8_t>& data);
    static void poner_roms_word(const std::vector<uint8_t>& bytes, std::vector<uint16_t>& rom);

    Game game_;
    int nbank_ = 0;
    int cps_b_index_ = 0;
    int screen_width_ = kScreenWidth;
    int screen_height_ = kScreenHeight;

    M68000 main_cpu_;
    Z80 sound_cpu_;
    YM2151 ym_;
    OKIM6295 oki_;
    QSound qsound_;
    Eeprom93C46 eeprom_;

    std::vector<uint16_t> rom_;
    std::array<uint16_t, 0x8000> ram_{};
    std::array<uint16_t, 0x18000> vram_{};
    std::array<uint16_t, 0x400> sprite_buffer_{};
    std::array<uint8_t, 0x8000> sound_rom_{};
    std::array<uint8_t, 0x800> sound_ram_{};
    std::array<std::array<uint8_t, 0x4000>, 6> snd_bank_{};
    std::array<uint8_t, 0x1000> qram1_{};
    std::array<uint8_t, 0x1000> qram2_{};
    std::vector<uint8_t> qsnd_opcode_;
    std::vector<uint8_t> qsnd_data_;
    std::array<uint8_t, 0x8000> stars_{};

    GfxSet chars0_;
    GfxSet chars1_;
    GfxSet tiles16_;
    GfxSet tiles32_;

    std::array<uint32_t, 0xc00> palette_{};
    std::array<uint8_t, 0xc00> palette_dirty_{};
    std::array<uint8_t, 0x80> color_dirty_{};
    std::array<std::array<bool, 16>, 4> trans_alt_{};

    std::vector<uint32_t> scroll1_;
    std::vector<uint32_t> scroll2_;
    std::vector<uint32_t> scroll3_;
    std::vector<uint32_t> priority_;
    std::vector<uint32_t> composite_;
    std::vector<uint32_t> framebuffer_;

    uint16_t in0_ = 0xffff;
    uint16_t in1_ = 0xffff;
    uint16_t in2_ = 0xffff;
    uint8_t dsw_a_ = 0xff;
    uint8_t dsw_b_ = 0xff;
    uint8_t dsw_c_ = 0xff;

    uint8_t sound_latch_ = 0;
    uint8_t sound_latch2_ = 0;
    uint8_t sound_bank_ = 0;

    uint32_t cps1_sprites_ = 0xffff;
    uint32_t cps1_scroll1_ = 0xffff;
    uint32_t cps1_scroll2_ = 0xffff;
    uint32_t cps1_scroll3_ = 0xffff;
    uint32_t cps1_rowscroll_ = 0;
    uint32_t cps1_pal_ = 0xffff;
    uint16_t cps1_rowscrollstart_ = 0;
    uint16_t cps1_mula_ = 0;
    uint16_t cps1_mulb_ = 0;
    uint16_t cps1_layer_ = 0;
    uint16_t cps1_palcltr_ = 0;
    uint16_t pri_mask0_ = 0;
    uint16_t pri_mask1_ = 0;
    uint16_t pri_mask2_ = 0;
    uint16_t pri_mask3_ = 0;
    uint16_t video_control_ = 0;
    uint16_t scroll_x1_ = 0, scroll_y1_ = 0;
    uint16_t scroll_x2_ = 0, scroll_y2_ = 0;
    uint16_t scroll_x3_ = 0, scroll_y3_ = 0;
    uint16_t stars_x1_ = 0, stars_y1_ = 0, stars_x2_ = 0, stars_y2_ = 0;
    uint16_t scroll_pri_x_ = 0, scroll_pri_y_ = 0;
    uint32_t cps1_frame_ = 0;
    bool stars_enabled_ = false;
    bool pal_change_ = false;
    bool mask_change_ = false;
    bool sprites_pri_draw_ = false;
    bool rowscroll_ena_ = false;
    bool flip_screen_ = false;

    int64_t audio_accumulator_ = 0;
    int64_t oki_accumulator_ = 0;
    int64_t qsound_accumulator_ = 0;
    int64_t qsound_irq_accumulator_ = 0;
    int32_t last_oki_ = 0;
    std::vector<int16_t> audio_;
};

}  // namespace dsp
