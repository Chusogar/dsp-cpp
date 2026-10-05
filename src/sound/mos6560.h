#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <vector>

namespace dsp {

// MOS 6560 (NTSC) / 6561 (PAL) VIC — video + sound. Ported from MAME mos6560.
class Mos6560 {
public:
    static constexpr int kSampleRate = 44100;

    static constexpr int kNtscLines = 261;
    static constexpr int kPalLines = 312;
    static constexpr int kNtscCyclesPerLine = 65;
    static constexpr int kPalCyclesPerLine = 71;

    static constexpr int kNtscXSize = 4 + 201;
    static constexpr int kNtscYSize = 10 + 251;
    static constexpr int kPalXSize = 20 + 229;
    static constexpr int kPalYSize = 10 + 302;

    static constexpr int kNtscVisX = 4;
    static constexpr int kNtscVisY = 10;
    static constexpr int kNtscVisW = 200;
    static constexpr int kNtscVisH = 248;

    static constexpr int kPalVisX = 20;
    static constexpr int kPalVisY = 10;
    static constexpr int kPalVisW = 224;
    static constexpr int kPalVisH = 296;

    static constexpr uint32_t kNtscClock = 14318181u / 14u;  // ≈ 1022727
    static constexpr uint32_t kPalClock = 4433618u / 4u;     // ≈ 1108404

    enum class Variant { Ntsc6560, Pal6561 };

    using MemRead = std::function<uint8_t(uint16_t)>;
    using ColorRead = std::function<uint8_t(uint16_t)>;

    explicit Mos6560(Variant variant = Variant::Pal6561);

    void set_mem_read(MemRead r) { mem_ = std::move(r); }
    void set_color_read(ColorRead r) { color_ = std::move(r); }
    void set_pot_read(std::function<uint8_t()> x, std::function<uint8_t()> y) {
        pot_x_ = std::move(x);
        pot_y_ = std::move(y);
    }

    void reset();
    uint8_t read(uint8_t reg);
    void write(uint8_t reg, uint8_t value);

    // Advance one raster line (0 .. lines-1). Draws that line into the
    // internal full-size bitmap when fb drawing is enabled.
    void update_line(int line);

    // Copy the visible MAME crop into `dst` (vis_w * vis_h ARGB pixels).
    void blit_visible(uint32_t* dst) const;

    // One mono sample at kSampleRate.
    int16_t update();

    uint8_t bus_r() const { return last_data_; }
    int raster_line() const { return rasterline_; }
    int lines() const { return total_lines_; }
    int cycles_per_line() const { return cycles_per_line_; }
    int vis_width() const { return vis_w_; }
    int vis_height() const { return vis_h_; }
    int total_width() const { return total_xsize_; }
    int total_height() const { return total_ysize_; }
    Variant variant() const { return variant_; }
    uint32_t clock() const { return clock_; }

    static constexpr uint32_t kPalette[16] = {
        0xFF000000, 0xFFFFFFFF, 0xFFF00000, 0xFF00F0F0, 0xFF600060, 0xFF00A000,
        0xFF0000F0, 0xFFD0D000, 0xFFC0A000, 0xFFFFA000, 0xFFF08080, 0xFF00FFFF,
        0xFFFF00FF, 0xFF00FF00, 0xFF00A0FF, 0xFFFFFF00,
    };

private:
    uint8_t read_videoram(uint16_t offset);
    uint8_t read_colorram(uint16_t offset);
    void draw_character(int ybegin, int yend, int ch, int yoff, int xoff, const uint16_t* color);
    void draw_character_multi(int ybegin, int yend, int ch, int yoff, int xoff,
                              const uint16_t* color);
    void drawlines(int first, int last);
    void soundport_w(int offset, uint8_t data);
    void sound_start();
    void put_pix(int y, int x, uint32_t argb);

    Variant variant_;
    uint32_t clock_;
    MemRead mem_;
    ColorRead color_;
    std::function<uint8_t()> pot_x_;
    std::function<uint8_t()> pot_y_;

    std::array<uint8_t, 16> reg_{};
    std::vector<uint32_t> bitmap_;

    int rasterline_ = 0;
    int lastline_ = 0;

    int charheight_ = 8;
    int matrix8x16_ = 0;
    int inverted_ = 0;
    int chars_x_ = 0;
    int chars_y_ = 0;
    int xsize_ = 0;
    int ysize_ = 0;
    int xpos_ = 0;
    int ypos_ = 0;
    int chargenaddr_ = 0;
    int videoaddr_ = 0;

    uint16_t backgroundcolor_ = 0;
    uint16_t framecolor_ = 0;
    uint16_t helpercolor_ = 0;
    uint16_t mono_[2]{};
    uint16_t monoinverted_[2]{};
    uint16_t multi_[4]{};
    uint16_t multiinverted_[4]{};

    int total_xsize_ = 0;
    int total_ysize_ = 0;
    int total_lines_ = 0;
    int cycles_per_line_ = 0;
    int vis_x_ = 0, vis_y_ = 0, vis_w_ = 0, vis_h_ = 0;

    uint8_t last_data_ = 0;

    // Sound
    int tone1pos_ = 0, tone2pos_ = 0, tone3pos_ = 0;
    int tonesize_ = 0;
    int tone1samples_ = 1, tone2samples_ = 1, tone3samples_ = 1;
    int noisesize_ = 0;
    int noisepos_ = 0;
    int noisesamples_ = 1;
    std::vector<int16_t> tone_;
    std::vector<int8_t> noise_;
};

}  // namespace dsp
