#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "core/machine.h"
#include "cpu/hd63701.h"
#include "cpu/z80.h"
#include "sound/ay8910.h"
#include "sound/msm5205.h"
#include "video/gfx.h"

namespace dsp {

// Irem M62 hardware, ported from m62_hw.pas.
// Main Z80, M6803 sound CPU driving two AY-3-8910 and two MSM5205 ADPCM chips.
// Games: Kung-Fu Master, Spelunker, Spelunker II, Lode Runner, Lode Runner II.
class IremM62 : public Machine {
public:
    enum class Game { KungFuMaster, Spelunker, Spelunker2, LodeRunner, LodeRunner2 };

    static constexpr double kFramesPerSecond = 56.338028;
    static constexpr int kScanlines = 284;
    static constexpr uint32_t kKungFuClock = 3072000;
    static constexpr uint32_t kMainClock = 4000000;
    static constexpr uint32_t kSoundClock = 3579545 / 4;
    static constexpr uint32_t kAyClock = 3579545 / 4;
    static constexpr uint32_t kMsmClock = 384000;
    static constexpr int kWorkWidth = 512;
    static constexpr int kWorkHeight = 512;

    explicit IremM62(Game game);

    bool init(const std::string& rom_path, std::string* error) override;
    void reset() override;
    void run_frame() override;

    void set_inputs(const MachineInputs& inputs) override;
    void set_dip_switch(int bank, uint8_t value) override;

    const uint32_t* framebuffer() const override { return framebuffer_.data(); }
    int screen_width() const override { return screen_width_; }
    int screen_height() const override { return kScreenHeight; }
    double frames_per_second() const override { return kFramesPerSecond; }

    void drain_audio(std::vector<int16_t>& out) override;
    int sample_rate() const override { return AY8910::kSampleRate; }

    const char* title() const override;

private:
    static constexpr int kScreenHeight = 256;

    bool is_kungfu() const { return game_ == Game::KungFuMaster; }
    bool is_spelunker() const {
        return game_ == Game::Spelunker || game_ == Game::Spelunker2;
    }
    bool is_ldrun() const {
        return game_ == Game::LodeRunner || game_ == Game::LodeRunner2;
    }

    uint32_t main_clock() const { return is_kungfu() ? kKungFuClock : kMainClock; }

    uint8_t main_read(uint16_t address);
    void main_write(uint16_t address, uint8_t value);
    uint8_t main_in(uint16_t port);
    void main_out(uint16_t port, uint8_t value);
    uint8_t sound_read(uint16_t address);
    void sound_write(uint16_t address, uint8_t value);
    void on_sound_cycles(int cycles);

    void out_port1(uint8_t value);
    void out_port2(uint8_t value);
    uint8_t in_port1();
    uint8_t ay0_port_a_read();
    void ay0_port_b_write(uint8_t value);

    bool load_roms(const std::string& rom_path, std::string* error);
    void decode_chars(const std::vector<uint8_t>& rom, int count);
    void decode_spelunker_chars(const std::vector<uint8_t>& rom);
    void decode_sprites(const std::vector<uint8_t>& rom, int count);
    void decode_tiles(const std::vector<uint8_t>& rom, int count);
    void build_palette(const std::vector<uint8_t>& prom);
    void build_palette_spl2(const std::vector<uint8_t>& prom);

    void update_video();
    void update_video_kungfum();
    void update_video_ldrun();
    void update_video_spelunker();
    void draw_tile_8(int dest_x, int dest_y, int code, int color, bool flip_x, bool opaque,
                     std::array<uint32_t, kWorkWidth * kWorkHeight>& dest);
    void draw_tile_12(int dest_x, int dest_y, int code, int color,
                      std::array<uint32_t, kWorkWidth * kWorkHeight>& dest);
    void draw_sprite_tile(int code, int color, bool flip_x, bool flip_y, int pos_x, int pos_y);
    void draw_sprites(int pos, int col, uint8_t col_mask, uint8_t pri_mask, uint8_t pri);
    uint16_t calc_nchar_sp(uint8_t color) const;

    Game game_;
    int screen_width_ = 256;
    int crop_x_ = 128;
    int crop_y_ = 0;

    Z80 main_cpu_;
    HD63701 sound_cpu_;
    AY8910 ay0_;
    AY8910 ay1_;
    MSM5205 msm0_;
    MSM5205 msm1_;

    std::array<uint8_t, 0x10000> memory_{};
    std::array<uint8_t, 0x10000> sound_memory_{};
    std::array<std::array<uint8_t, 0x2000>, 4> mem_rom_{};
    std::array<std::array<uint8_t, 0x1000>, 16> mem_rom2_{};
    std::array<uint8_t, 0x20> sprite_height_{};

    GfxSet chars_;    // gfx 0
    GfxSet sprites_;  // gfx 1
    GfxSet tiles_;    // gfx 2, Spelunker background

    std::array<uint32_t, 768> palette_{};
    std::array<uint32_t, kWorkWidth * kWorkHeight> layer1_{};
    std::array<uint32_t, kWorkWidth * kWorkHeight> layer3_{};
    std::array<uint32_t, kWorkWidth * kWorkHeight> composite_{};
    std::vector<uint32_t> framebuffer_;

    uint8_t sound_command_ = 0;
    uint8_t val_port1_ = 0;
    uint8_t val_port2_ = 0;
    uint8_t ldrun_color_ = 0x0c;
    uint8_t sprites_sp_ = 1;
    uint8_t rom_bank_ = 0;
    uint8_t rom_bank2_ = 0;
    uint8_t pal_bank_ = 0;
    uint8_t ldrun2_banksw_ = 0;
    uint8_t old_bank_ = 0;
    uint16_t scroll_x_ = 0;
    uint16_t scroll_y_ = 0;

    uint8_t in0_ = 0xff;
    uint8_t in1_ = 0xff;
    uint8_t in2_ = 0xff;
    uint8_t dsw_a_ = 0xff;
    uint8_t dsw_b_ = 0xfd;

    int64_t audio_accumulator_ = 0;
    int64_t msm_accumulator_ = 0;
    std::vector<int16_t> audio_;
};

}  // namespace dsp
