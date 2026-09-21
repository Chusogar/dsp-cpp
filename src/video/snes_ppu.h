#pragma once

#include <array>
#include <cstdint>

namespace dsp {

// SNES S-PPU with main/sub color math and improved Mode 7.
class SnesPpu {
public:
    static constexpr int kWidth = 256;
    static constexpr int kHeight = 224;
    static constexpr int kVramWords = 0x8000;

    void reset();
    void write(uint16_t reg, uint8_t value);
    uint8_t read(uint16_t reg);
    void render_line(int line, uint32_t* dst);

    bool forced_blank() const { return (inidisp_ & 0x80) != 0; }
    uint8_t brightness() const { return inidisp_ & 0x0f; }
    uint8_t bgmode() const { return bgmode_; }
    uint8_t tm() const { return tm_; }
    uint8_t ts() const { return ts_; }
    uint8_t cgwsel() const { return cgwsel_; }
    uint8_t cgadsub() const { return cgadsub_; }
    int16_t m7a() const { return m7a_; }
    int16_t m7d() const { return m7d_; }
    int16_t m7hofs() const { return m7hofs_; }
    int16_t m7vofs() const { return m7vofs_; }
    int16_t m7x() const { return m7x_; }
    int16_t m7y() const { return m7y_; }

    const std::array<uint16_t, kVramWords>& vram() const { return vram_; }

private:
    struct Bg {
        uint16_t map_base = 0;
        uint16_t chr_base = 0;
        uint8_t map_size = 0;
        uint16_t hofs = 0, vofs = 0;
        bool tile16 = false;
    };
    struct Sample {
        uint16_t color = 0;
        uint8_t prio = 0;
        uint8_t source = 0;
    };

    uint16_t tilemap_entry(const Bg& bg, int tx, int ty) const;
    uint8_t bg_priority(int layer, bool tile_prio) const;
    uint8_t obj_priority(int sprite_prio) const;
    void draw_bg_line(int bg_index, int depth, int line, Sample* out, uint8_t layer_mask);
    void draw_sprite_line(int line, Sample* out, uint8_t layer_mask);
    void draw_mode7_line(int line, Sample* out, uint8_t layer_mask);
    uint16_t apply_color_math(const Sample& main, const Sample& sub) const;
    uint32_t colour(uint16_t bgr15) const;
    uint16_t vram_step() const {
        static const uint16_t kSteps[4] = {1, 32, 128, 128};
        return kSteps[vmain_ & 3];
    }
    static int clip13(int n) {
        n &= 0x1fff;
        if (n & 0x1000) n |= ~0x1fff;
        return n;
    }

    std::array<uint16_t, kVramWords> vram_{};
    std::array<uint16_t, 256> cgram_{};
    std::array<uint8_t, 544> oam_{};

    uint8_t inidisp_ = 0x80;
    uint8_t bgmode_ = 0;
    uint8_t obsel_ = 0;
    std::array<Bg, 4> bg_{};
    uint8_t tm_ = 0, ts_ = 0;
    uint8_t tmw_ = 0, tsw_ = 0;
    uint8_t cgwsel_ = 0;
    uint8_t cgadsub_ = 0;
    uint16_t coldata_ = 0;

    uint8_t vmain_ = 0;
    uint16_t vmadd_ = 0;
    uint16_t vram_latch_ = 0;
    uint8_t cgadd_ = 0;
    bool cg_high_ = false;
    uint8_t cg_low_ = 0;
    uint16_t oamadd_ = 0;
    bool oam_high_ = false;
    uint8_t oam_low_ = 0;

    int16_t m7a_ = 0x100, m7b_ = 0, m7c_ = 0, m7d_ = 0x100;
    int16_t m7x_ = 0, m7y_ = 0, m7hofs_ = 0, m7vofs_ = 0;
    uint8_t m7_prev_ = 0;
    uint8_t m7sel_ = 0;
    uint8_t scroll_prev_ = 0;
    uint8_t scroll_prev_h_ = 0;
    uint8_t ppu_open_bus_ = 0;
};

}  // namespace dsp
