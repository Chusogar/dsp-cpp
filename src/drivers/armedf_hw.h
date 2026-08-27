#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "core/machine.h"
#include "cpu/m68000.h"
#include "cpu/z80.h"
#include "sound/dac.h"
#include "sound/ym3812.h"
#include "video/gfx.h"

namespace dsp {

// Nichibutsu "Armed F" hardware family, ported from armedf_hw.pas: an M68000
// main CPU, a Z80 sound CPU driving a YM3812/YM3526 plus two DACs, a fixed
// 8x8 text layer, two independently scrolling 16x16 tile layers, and 16x16
// sprites drawn in three interleaved priority passes. Three of the four
// games on this board (all but Armed F itself) additionally carry an
// NB1414M4 message coprocessor: a tiny DMA engine with its own internal ROM
// that stamps pre-built strings (coin/credit prompts, scores) into the text
// layer on command.
class ArmedfHw : public Machine {
public:
    enum class Game { ArmedF, TerraForce, CrazyClimber2, Legion };

    static constexpr uint32_t kMainClock = 8000000;
    static constexpr uint32_t kSoundClock = 4000000;
    static constexpr int kScanlines = 256;
    static constexpr double kFramesPerSecond = 59.082012;

    explicit ArmedfHw(Game game);

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
    bool uses_nb1414() const { return game_ != Game::ArmedF; }

    // --- Main CPU memory map (armedf_getword/putword and the shared
    // terraf_getword/putword variant used by the other three games) ---
    uint16_t main_read(uint32_t addr);
    void main_write(uint32_t addr, uint16_t value);
    void write_palette(int index, uint16_t value);

    // --- Sound CPU ---
    uint8_t sound_read(uint16_t addr);
    void sound_write(uint16_t addr, uint8_t value);
    uint8_t sound_in(uint16_t port);
    void sound_out(uint16_t port, uint8_t value);
    void on_sound_cycles(int cycles);

    // --- Loading ---
    bool load_roms(const std::string& rom_path, std::string* error);

    // --- NB1414M4 message coprocessor ---
    void nb_exec();
    void nb_dma(int src, int dst, int size, bool condition);
    void nb_fill(int dst, uint8_t tile, uint8_t pal);
    void nb_kozure_score_msg(int dst, int src_base);
    void nb_insert_coin_msg();
    void nb_credit_msg();
    void nb_cmd_0200(uint8_t command);
    void nb_cmd_0600(uint8_t is2p);
    void nb_cmd_0e00(uint8_t command);

    // --- Video ---
    void render_frame();
    int text_pos(int x, int y) const;  // per-game text RAM addressing
    void draw_text_layer();
    void draw_tile_layer(const std::array<uint16_t, 0x800>& ram, const GfxSet& gfx, int color_base,
                         int scroll_x, int scroll_y, std::vector<uint32_t>& canvas);
    void draw_sprites(int priority);
    static std::vector<uint8_t> rotate_ccw(const GfxSet& src, int count, int size);

    Game game_;
    int screen_width_ = 320, screen_height_ = 240;
    int crop_x_ = 0, crop_y_ = 0;

    M68000 main_cpu_;
    Z80 sound_cpu_;
    YM3812 ym_;
    Dac dac0_, dac1_;

    GfxSet chars_gfx_;
    GfxSet bg_gfx_;
    GfxSet fg_gfx_;
    GfxSet sprites_gfx_;
    std::vector<uint8_t> sprites_rotated_;

    std::array<uint16_t, 0x30000> rom_{};
    std::array<uint16_t, 0x6400> ram_{};
    std::array<uint8_t, 0x1000> ram_txt_{};
    std::array<uint16_t, 0x800> ram_bg_{}, ram_fg_{}, ram_clut_{}, ram_sprites_{};
    std::array<uint16_t, 0x800> sprite_buffer_{};  // latched copy used while drawing a frame
    std::array<uint16_t, 0x800> palette_raw_{};
    std::array<uint32_t, 0x801> palette_{};  // +1: index 0x800 is the sprite CLUT's "transparent" slot
    std::array<uint8_t, 0x4000> nb_rom_{};
    std::array<uint8_t, 0x10000> sound_ram_{};

    std::vector<uint32_t> bg_canvas_, fg_canvas_;  // 512x256 each
    std::vector<uint32_t> fg_text_;                // 512x256, maskable text overlay ("screen2")
    std::vector<uint32_t> composite_;              // 512x512

    uint16_t video_reg_ = 0;
    uint16_t scroll_fg_x_ = 0, scroll_fg_y_ = 0, scroll_bg_x_ = 0, scroll_bg_y_ = 0;
    uint16_t in0_ = 0xffff, in1_ = 0xffff;
    uint8_t dsw_a_ = 0xff, dsw_b_ = 0xff;
    uint8_t sound_latch_ = 0;
    uint8_t frame_counter_ = 0;
    int irq_level_ = 1;
    int sprite_offset_ = 0x80;
    int sprite_count_ = 0x200;

    std::vector<uint32_t> framebuffer_;
    std::vector<int16_t> audio_;
    int64_t audio_accumulator_ = 0;
    int64_t sound_irq_accumulator_ = 0;
    int main_cycles_per_line_ = 0;
};

}  // namespace dsp
