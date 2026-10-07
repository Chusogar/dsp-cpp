// PlayStation GPU (GP0/GP1, 1 MiB VRAM, basic drawing).
// Ported/adapted from ProjectPSX (MIT) by Pedro Cortés:
//   https://github.com/BluestormDNA/ProjectPSX
#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "machine/psx_timers.h"

namespace dsp {

class PsxGpu {
public:
    static constexpr int kVramW = 1024;
    static constexpr int kVramH = 512;

    PsxGpu();
    void reset();

    uint32_t load_gpuread();
    uint32_t load_gpustat() const;
    void write(uint32_t addr, uint32_t value);
    void write_gp0(uint32_t value);
    void write_gp1(uint32_t value);
    void process_dma(const uint32_t* data, int words);

    // Advances video timing; returns true on VBlank (frame complete).
    bool tick(int cycles);
    PsxGpuSync blanks_and_dot() const;

    int display_width() const;
    int display_height() const;
    bool depth24() const { return depth24_; }
    bool display_disabled() const { return display_disabled_; }
    uint16_t disp_vram_x() const { return disp_vram_x_; }
    uint16_t disp_vram_y() const { return disp_vram_y_; }
    uint16_t disp_y1() const { return disp_y1_; }
    uint16_t disp_y2() const { return disp_y2_; }

    // Blit visible display area to ARGB8888 (host framebuffer).
    void blit_display(uint32_t* dst, int dst_w, int dst_h) const;

    const uint32_t* vram8888() const { return vram8888_.data(); }
    const uint16_t* vram1555() const { return vram1555_.data(); }

private:
    enum class Mode { Command, Vram };

    struct Point2D {
        int16_t x = 0;
        int16_t y = 0;
    };
    struct TextureData {
        uint16_t val = 0;
        uint8_t x() const { return uint8_t(val); }
        uint8_t y() const { return uint8_t(val >> 8); }
    };
    struct Color {
        uint32_t val = 0;
        uint8_t r() const { return uint8_t(val); }
        uint8_t g() const { return uint8_t(val >> 8); }
        uint8_t b() const { return uint8_t(val >> 16); }
        uint8_t m() const { return uint8_t(val >> 24); }
        void set_r(uint8_t v) { val = (val & 0xFFFFFF00u) | v; }
        void set_g(uint8_t v) { val = (val & 0xFFFF00FFu) | (uint32_t(v) << 8); }
        void set_b(uint8_t v) { val = (val & 0xFF00FFFFu) | (uint32_t(v) << 16); }
        void set_m(uint8_t v) { val = (val & 0x00FFFFFFu) | (uint32_t(v) << 24); }
    };
    struct Primitive {
        bool shaded = false;
        bool textured = false;
        bool semi_transparent = false;
        bool raw_textured = false;
        int depth = 0;
        int semi_mode = 0;
        Point2D clut{};
        Point2D texture_base{};
    };
    struct VramTransfer {
        int x = 0, y = 0;
        uint16_t w = 0, h = 0;
        int origin_x = 0, origin_y = 0;
        int half_words = 0;
    };

    void init_color_lut();
    void decode_gp0(uint32_t value);
    void decode_gp0_dma(const uint32_t* data, int words);
    void execute_gp0(uint32_t opcode, const uint32_t* buffer);
    void write_to_vram(uint32_t value);
    uint32_t read_from_vram();
    void step_vram_transfer();
    void draw_vram_pixel(uint16_t color1555);

    void gp0_fill_rect(const uint32_t* buffer);
    void gp0_render_polygon(const uint32_t* buffer);
    void gp0_render_line(const uint32_t* buffer);
    void gp0_render_rectangle(const uint32_t* buffer);
    void gp0_copy_vram_vram(const uint32_t* buffer);
    void gp0_copy_cpu_vram(const uint32_t* buffer);
    void gp0_copy_vram_cpu(const uint32_t* buffer);

    void rasterize_tri(Point2D v0, Point2D v1, Point2D v2,
                       TextureData t0, TextureData t1, TextureData t2,
                       uint32_t c0, uint32_t c1, uint32_t c2, const Primitive& p);
    void rasterize_line(uint32_t v1, uint32_t v2, uint32_t color1, uint32_t color2, bool transparent);
    void rasterize_rect(Point2D origin, Point2D size, TextureData texture, uint32_t bgr, const Primitive& p);

    void gp0_e1(uint32_t val);
    void gp0_e2(uint32_t val);
    void gp0_e3(uint32_t val);
    void gp0_e4(uint32_t val);
    void gp0_e5(uint32_t val);
    void gp0_e6(uint32_t val);

    void gp1_reset();
    void gp1_display_mode(uint32_t value);
    void gp1_info(uint32_t value);

    int get_texel(int x, int y, Point2D clut, Point2D base, int depth) const;
    int handle_semi(int x, int y, int color, int mode) const;
    int get_rgb_color(uint32_t value) const;
    static int orient2d(Point2D a, Point2D b, Point2D c);
    static bool is_top_left(Point2D a, Point2D b);
    static int interpolate(int w0, int w1, int w2, int t0, int t1, int t2, int area);
    static int16_t signed11(uint32_t n);
    static uint8_t clamp_ff(int v);
    static uint8_t clamp_zero(int v);
    int mask_texel(int axis, int pre, int post) const;

    void set_pixel(int x, int y, int color);
    int get_pixel888(int x, int y) const;
    uint16_t get_pixel555(int x, int y) const;

    std::array<uint32_t, kVramW * kVramH> vram8888_{};
    std::array<uint16_t, kVramW * kVramH> vram1555_{};
    std::array<uint32_t, 65536> color_lut_{};

    uint32_t gpuread_ = 0;
    uint32_t command_ = 0;
    int command_size_ = 0;
    std::array<uint32_t, 16> command_buffer_{};
    int pointer_ = 0;
    Mode mode_ = Mode::Command;
    VramTransfer xfer_{};

    int scan_line_ = 0;
    int video_cycles_ = 0;
    int horizontal_timing_ = 3413;
    int vertical_timing_ = 263;

    uint8_t texture_x_base_ = 0;
    uint8_t texture_y_base_ = 0;
    uint8_t transparency_mode_ = 0;
    uint8_t texture_depth_ = 0;
    bool dithered_ = false;
    bool draw_to_display_ = false;
    int mask_while_drawing_ = 0;
    bool check_mask_ = false;
    bool interlace_field_ = false;
    bool reverse_flag_ = false;
    bool texture_disabled_ = false;
    bool texture_disable_allowed_ = false;
    uint8_t hres2_ = 0;
    uint8_t hres1_ = 0;
    bool vres480_ = false;
    bool pal_ = false;
    bool depth24_ = false;
    bool vertical_interlace_ = false;
    bool display_disabled_ = true;
    bool interrupt_requested_ = false;
    bool dma_request_ = false;
    bool ready_command_ = true;
    bool ready_vram_to_cpu_ = false;
    bool ready_dma_ = true;
    uint8_t dma_direction_ = 0;
    bool odd_line_ = false;

    uint32_t draw_mode_bits_ = 0xFFFFFFFFu;
    uint32_t display_mode_bits_ = 0xFFFFFFFFu;
    uint32_t display_v_range_ = 0xFFFFFFFFu;
    uint32_t display_h_range_ = 0xFFFFFFFFu;
    uint32_t texture_window_bits_ = 0xFFFFFFFFu;
    int pre_mask_x_ = 0, pre_mask_y_ = 0, post_mask_x_ = 0, post_mask_y_ = 0;

    uint16_t draw_left_ = 0, draw_right_ = 0, draw_top_ = 0, draw_bottom_ = 0;
    int16_t draw_x_off_ = 0, draw_y_off_ = 0;
    uint16_t disp_vram_x_ = 0, disp_vram_y_ = 0;
    uint16_t disp_x1_ = 0x200, disp_x2_ = 0xC00;
    uint16_t disp_y1_ = 0x10, disp_y2_ = 0x100;

    TextureData texture_data_{};
    mutable Color color0_, color1_, color2_;
    Point2D min_{}, max_{};

    static const int kResolutions[5];
    static const int kDotDiv[5];
    static const uint8_t kCommandSize[256];
};

}  // namespace dsp
