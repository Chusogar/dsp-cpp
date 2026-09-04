#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "core/machine.h"
#include "cpu/m68000.h"
#include "cpu/z80.h"
#include "sound/ym2203.h"
#include "sound/okim6295.h"
#include "video/gfx.h"

namespace dsp {

// Shadow Warriors / Ninja Gaiden (Tecmo, 1988), ported from shadow_warriors_hw.pas.
// ROM set is MAME `shadoww`. 68000 main CPU (9.216 MHz), Z80 sound CPU (4 MHz)
// driving two YM2203s and an OKI MSM6295, three scroll layers (text/bg/fg)
// plus sprites composed into a 256x224 screen with blend effects.
class ShadowWarriors : public Machine {
public:
    static constexpr int kScreenWidth = 256;
    static constexpr int kScreenHeight = 224;
    static constexpr int kWorkSize = 512;
    static constexpr double kFramesPerSecond = 59.169998;
    static constexpr int kScanlines = 256;
    static constexpr uint32_t kMainClock = 18432000 / 2;
    static constexpr uint32_t kSoundClock = 4000000;
    static constexpr uint32_t kOkimClock = 1000000;

    ShadowWarriors();

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
    int sample_rate() const override { return YM2203::kSampleRate; }

    const char* title() const override { return "Shadow Warriors"; }
    bool uses_pointer() const override { return false; }

private:
    uint16_t main_read(uint32_t address);
    void main_write(uint32_t address, uint16_t value);
    uint8_t main_read_byte(uint32_t address);
    void main_write_byte(uint32_t address, uint8_t value);
    uint8_t sound_read(uint16_t address);
    void sound_write(uint16_t address, uint8_t value);
    void on_sound_cycles(int cycles);

    bool load_roms(const std::string& rom_path, std::string* error);
    void decode_graphics(const std::vector<uint8_t>& char_rom,
                         const std::vector<uint8_t>& bg_rom,
                         const std::vector<uint8_t>& fg_rom,
                         const std::vector<uint8_t>& sprite_rom);
    void set_palette(int index, uint16_t value);
    void update_video();
    void draw_tilemap();
    void scroll_layer(const std::vector<uint32_t>& layer, int layer_width,
                      uint16_t scroll_x, uint16_t scroll_y, int mask_x, int mask_y);
    void draw_sprites(int priority);

    M68000 main_cpu_;
    Z80 sound_cpu_;
    YM2203 ym0_;
    YM2203 ym1_;
    OKIM6295 okim_;

    std::vector<uint16_t> rom_;
    std::array<uint16_t, 0x2000> ram_{};
    std::array<uint16_t, 0x800> video_ram1_{};
    std::array<uint16_t, 0x1000> video_ram2_{};
    std::array<uint16_t, 0x1000> video_ram3_{};
    std::array<uint16_t, 0x1000> sprite_ram_{};
    // 0x78000-0x79fff → 0x2000 bytes = 0x1000 words (Pascal buffer_paleta).
    std::array<uint16_t, 0x1000> palette_ram_{};
    std::array<uint8_t, 0x10000> mem_snd_{};

    GfxSet gfx_char_;
    GfxSet gfx_bg_;
    GfxSet gfx_fg_;
    GfxSet gfx_sprite_;
    // Full palette RAM is 0x1000 words; sprites use 0x000-0x0ff (opaque) and
    // 0x400-0x4ff (blend), tiles use 0x100/0x200/0x300 banks.
    std::array<uint32_t, 0x1000> palette_{};
    std::vector<uint32_t> text_layer_;
    std::vector<uint32_t> bg_layer_;
    std::vector<uint32_t> fg_layer_;
    std::vector<uint32_t> composite_;
    std::vector<uint32_t> framebuffer_;

    uint16_t scroll_x_txt_ = 0;
    uint16_t scroll_y_txt_ = 0;
    uint8_t scroll_y_txt_off_ = 0;
    uint16_t scroll_x_bg_ = 0;
    uint16_t scroll_y_bg_ = 0;
    uint8_t scroll_y_bg_off_ = 0;
    uint16_t scroll_x_fg_ = 0;
    uint16_t scroll_y_fg_ = 0;
    uint8_t scroll_y_fg_off_ = 0;

    bool flip_main_ = false;
    uint8_t sound_latch_ = 0;

    uint8_t in0_ = 0xff;
    uint16_t in1_ = 0xffff;
    uint16_t dsw_a_ = 0xffff;

    int64_t audio_accumulator_ = 0;
    int64_t oki_accumulator_ = 0;
    int32_t oki_last_ = 0;
    std::vector<int16_t> audio_;
};

}  // namespace dsp