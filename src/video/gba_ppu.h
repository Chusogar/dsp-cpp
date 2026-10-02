#pragma once

#include <array>
#include <cstdint>

namespace dsp {

// Game Boy Advance LCD controller: tiled modes 0-2 (text and affine
// backgrounds), bitmap modes 3-5, 128 sprites (regular and affine, 4/8 bpp,
// 1D/2D tile mapping, semi-transparent and window sprites), windows 0/1/OBJ,
// alpha blending and brightness effects, mosaic. Rendered one scanline at a
// time from the live registers, as the hardware draws during H-draw.
class GbaPpu {
public:
    static constexpr int kWidth = 240;
    static constexpr int kHeight = 160;

    GbaPpu(const uint8_t* io, const uint8_t* palette, const uint8_t* vram, const uint8_t* oam)
        : io_(io), pal_(palette), vram_(vram), oam_(oam) {}

    void reset();
    // Draws `line` (0-159) into `row` (240 ARGB pixels).
    void render_line(int line, uint32_t* row);
    // Affine background reference points: latched from BGxX/BGxY on a write
    // and at the start of each frame, advanced by PB/PD after every line.
    void latch_reference(int bg) ;
    void start_frame();
    void end_line();

private:
    uint16_t reg(uint32_t offset) const { return uint16_t(io_[offset] | (io_[offset + 1] << 8)); }
    uint16_t palette(int index) const {
        return uint16_t(pal_[index * 2] | (pal_[index * 2 + 1] << 8)) & 0x7FFF;
    }
    void draw_text_bg(int bg, int line);
    void draw_affine_bg(int bg);
    void draw_bitmap_bg(int mode);
    void draw_sprites(int line);

    const uint8_t* io_;
    const uint8_t* pal_;
    const uint8_t* vram_;
    const uint8_t* oam_;

    static constexpr uint16_t kTransparent = 0x8000;
    // Per-line layer buffers: 15-bit colour or kTransparent.
    std::array<std::array<uint16_t, kWidth>, 4> bg_{};
    std::array<uint16_t, kWidth> obj_color_{};
    std::array<uint8_t, kWidth> obj_prio_{};
    std::array<uint8_t, kWidth> obj_alpha_{};   // semi-transparent sprite pixel
    std::array<uint8_t, kWidth> obj_window_{};  // inside the OBJ window
    std::array<int32_t, 2> ref_x_{}, ref_y_{};  // BG2/BG3 internal references
};

}  // namespace dsp
