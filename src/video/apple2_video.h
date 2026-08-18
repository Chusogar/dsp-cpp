#pragma once

#include <cstdint>

namespace dsp {

// Apple II / IIe video: 40/80-column text, lo-res, hi-res and double hi-res.
// Output is a 560×384 ARGB8888 framebuffer (2×2 for 40-column / hi-res so the
// aspect matches 80-column / double hi-res).
class Apple2Video {
public:
    static constexpr int kWidth = 560;
    static constexpr int kHeight = 384;
    static constexpr int kTextRows = 24;
    static constexpr int kTextCols40 = 40;
    static constexpr int kTextCols80 = 80;

    bool text = true;
    bool mixed = false;
    bool page2 = false;
    bool hires = false;
    bool col80 = false;
    bool altcharset = false;
    bool dhires = false;
    bool iie = false;
    int flash_phase = 0;

    void render(uint32_t* dest, const uint8_t* main, const uint8_t* aux, const uint8_t* chargen,
                int chargen_size) const;

    static uint16_t text_address(int row, int col, bool page2);
    static uint16_t hires_address(int y, int col, bool page2);

private:
    void put(uint32_t* dest, int x, int y, uint32_t color, int xscale) const;
    void draw_text_row(uint32_t* dest, int row, const uint8_t* main, const uint8_t* aux,
                       const uint8_t* chargen, int chargen_size, bool col80) const;
    void draw_lores(uint32_t* dest, const uint8_t* ram, int rows) const;
    void draw_hires(uint32_t* dest, const uint8_t* ram, int rows) const;
    void draw_dhires(uint32_t* dest, const uint8_t* main, const uint8_t* aux, int rows) const;
    uint8_t glyph_row(const uint8_t* chargen, int chargen_size, uint8_t code, int row,
                      bool* invert) const;
};

}  // namespace dsp
