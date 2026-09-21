#pragma once

#include <array>
#include <cstdint>

namespace dsp {

// SNES picture processing unit (the S-PPU1/S-PPU2 pair seen as one block).
//
// Four background layers whose meaning depends on the mode in BGMODE: modes 0
// to 4 give a mix of 2bpp and 4bpp tile planes, and each layer has its own
// tilemap base, character base and scroll. Sprites live in OAM with their own
// 4bpp character data. Colours are 15-bit BGR held in CGRAM.
class SnesPpu {
public:
    static constexpr int kWidth = 256;
    static constexpr int kHeight = 224;
    static constexpr int kVramWords = 0x8000;   // 64 KB addressed as words

    void reset();

    void write(uint16_t reg, uint8_t value);    // $2100-$213F
    uint8_t read(uint16_t reg);

    // Draws one visible scanline into `dst` as ARGB.
    void render_line(int line, uint32_t* dst);

    bool forced_blank() const { return (inidisp_ & 0x80) != 0; }
    uint8_t brightness() const { return inidisp_ & 0x0f; }

    // OAM and VRAM are also reachable by DMA, which writes through the same
    // registers, so nothing extra is needed for it here.
    const std::array<uint16_t, kVramWords>& vram() const { return vram_; }

private:
    struct Bg {
        uint16_t map_base = 0;     // in words
        uint16_t chr_base = 0;     // in words
        uint8_t map_size = 0;      // bits 0-1 of BGnSC: 32x32, 64x32, 32x64, 64x64
        uint16_t hofs = 0, vofs = 0;
        bool tile16 = false;       // 16x16 tiles instead of 8x8
    };

    uint16_t tilemap_entry(const Bg& bg, int tx, int ty) const;
    // Priority of a layer's pixels on the shared 0-15 scale, given the
    // per-tile priority bit. The console interleaves the four planes and the
    // four sprite priorities in a fixed order that depends on the mode, so a
    // layer cannot simply be drawn on top of the previous one.
    uint8_t bg_priority(int layer, bool tile_prio) const;
    uint8_t obj_priority(int sprite_prio) const;
    void draw_bg_line(int bg_index, int depth, int line, uint16_t* out, uint8_t* prio);
    void draw_sprite_line(int line, uint16_t* out, uint8_t* prio);
    // Mode 7 is not a tile plane like the others: one 128x128 map of 8x8
    // characters, sampled through a 2x2 matrix with a movable centre, which
    // is what gives it rotation and scaling.
    void draw_mode7_line(int line, uint16_t* out, uint8_t* prio);
    uint32_t colour(uint16_t bgr15) const;
    // VMAIN bits 0-1 select the step the VRAM address takes after a port
    // access: 1, 32 or 128 words.
    uint16_t vram_step() const {
        static const uint16_t kSteps[4] = {1, 32, 128, 128};
        return kSteps[vmain_ & 3];
    }

    std::array<uint16_t, kVramWords> vram_{};
    std::array<uint16_t, 256> cgram_{};
    std::array<uint8_t, 544> oam_{};

    uint8_t inidisp_ = 0x80;       // starts in forced blank
    uint8_t bgmode_ = 0;
    uint8_t obsel_ = 0;
    std::array<Bg, 4> bg_{};
    uint8_t tm_ = 0, ts_ = 0;      // main/sub screen layer enables

    // VRAM port
    uint8_t vmain_ = 0;
    uint16_t vmadd_ = 0;
    uint16_t vram_latch_ = 0;

    // CGRAM port
    uint8_t cgadd_ = 0;
    bool cg_high_ = false;
    uint8_t cg_low_ = 0;

    // OAM port
    uint16_t oamadd_ = 0;
    bool oam_high_ = false;
    uint8_t oam_low_ = 0;

    // Write-twice scroll registers keep a shared latch.
    // Mode 7 matrix and offsets. Each is written twice into a shared latch.
    int16_t m7a_ = 0x100, m7b_ = 0, m7c_ = 0, m7d_ = 0x100;
    int16_t m7x_ = 0, m7y_ = 0, m7hofs_ = 0, m7vofs_ = 0;
    uint8_t m7_prev_ = 0;
    uint8_t m7sel_ = 0;

    // BGnHOFS needs two latches: one shared with BGnVOFS and one that only
    // the horizontal registers update. Using a single latch mixes the two
    // axes together and both end up with the same value.
    uint8_t scroll_prev_ = 0;
    uint8_t scroll_prev_h_ = 0;
    uint8_t ppu_open_bus_ = 0;
};

}  // namespace dsp
