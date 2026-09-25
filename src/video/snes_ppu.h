#pragma once

#include <array>
#include <cstdint>

namespace dsp {

// SNES S-PPU1/S-PPU2, rendered a scanline at a time.
//
// Layers are resolved with the per-mode priority order, sprites with their
// real OAM layout (32 per line, 34 tiles per line, priority rotation), the
// two windows mask layers on the main and sub screens, and colour math
// follows CGWSEL/CGADSUB (sub screen or fixed colour addend, halving,
// clip-to-black / prevent-math regions). Mode 7 implements the hardware
// matrix arithmetic, the three out-of-bounds modes and EXTBG.
class SnesPpu {
public:
    static constexpr int kWidth = 256;
    static constexpr int kHeight = 224;
    static constexpr int kVramWords = 0x8000;

    void reset();
    void write(uint16_t reg, uint8_t value);
    uint8_t read(uint16_t reg);
    // `line` is the V counter (1..224 are the visible lines).
    void render_line(int line, uint32_t* dst);
    void start_vblank();
    // VRAM only accepts writes during vertical blank or forced blank; the
    // address still advances when a write is dropped.
    void set_vram_open(bool open) { vram_open_ = open; }

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
    // One layer's pixel: `z` is its place in the priority order (0 = none).
    struct Pixel {
        uint16_t color = 0;
        uint8_t z = 0;
        uint8_t layer = 5;   // 0-3 BG1-4, 4 OBJ, 5 backdrop
        bool math = true;    // OBJ palettes 0-3 never take part in colour math
    };

    void render_bg(int bg, int depth, int line, std::array<Pixel, kWidth>& out,
                   const uint8_t (&z)[2]);
    void render_mode7(int line, std::array<Pixel, kWidth>& bg1, std::array<Pixel, kWidth>& bg2,
                      const uint8_t (&z1)[2], const uint8_t (&z2)[2]);
    void render_sprites(int line, std::array<Pixel, kWidth>& out, const uint8_t (&z)[4]);
    uint16_t tilemap_entry(const Bg& bg, int tx, int ty) const;
    bool window(int layer, int x) const;   // layer 0-3 BG, 4 OBJ, 5 colour
    uint16_t vram_address() const;
    uint32_t colour(uint16_t bgr15) const;
    uint16_t direct_color(uint8_t pixel, int pal) const;
    uint16_t vram_step() const {
        static const uint16_t kSteps[4] = {1, 32, 128, 128};
        return kSteps[vmain_ & 3];
    }
    static int sext13(int n) {
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
    uint8_t mosaic_ = 0;
    std::array<Bg, 4> bg_{};
    uint8_t tm_ = 0, ts_ = 0;
    uint8_t tmw_ = 0, tsw_ = 0;
    uint8_t wsel_[3] = {};        // W12SEL, W34SEL, WOBJSEL
    uint8_t wh_[4] = {};          // WH0-WH3
    uint8_t wbglog_ = 0, wobjlog_ = 0;
    uint8_t cgwsel_ = 0;
    uint8_t cgadsub_ = 0;
    uint16_t coldata_ = 0;
    uint8_t setini_ = 0;

    uint8_t vmain_ = 0;
    uint16_t vmadd_ = 0;
    uint16_t vram_latch_ = 0;
    uint8_t cgadd_ = 0;
    bool cg_high_ = false;
    uint8_t cg_low_ = 0;
    bool cg_read_high_ = false;
    uint16_t oam_reload_ = 0;   // OAMADD ($2102/$2103), in words
    uint16_t oam_addr_ = 0;     // internal byte address
    bool oam_priority_ = false; // $2103 bit 7: priority rotation
    uint8_t oam_low_ = 0;
    bool range_over_ = false, time_over_ = false;

    int16_t m7a_ = 0x100, m7b_ = 0, m7c_ = 0, m7d_ = 0x100;
    int16_t m7x_ = 0, m7y_ = 0, m7hofs_ = 0, m7vofs_ = 0;
    uint8_t m7_prev_ = 0;
    uint8_t m7sel_ = 0;
    uint8_t scroll_prev_ = 0;
    uint8_t scroll_prev_h_ = 0;
    uint8_t ppu1_open_bus_ = 0, ppu2_open_bus_ = 0;
    int mosaic_start_ = 1;
    bool vram_open_ = true;
};

}  // namespace dsp
