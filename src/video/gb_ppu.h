#pragma once

#include <array>
#include <cstdint>

namespace dsp {

// Game Boy / Game Boy Color PPU, ported from gb.pas's update_video_gb /
// update_video_gbc family of procedures (background, window, sprites,
// priorities).
//
// DMG is a single-pass-per-pixel rewrite of update_video_gb that resolves
// the same priorities into one 160-wide ARGB row.
//
// CGB follows update_video_gbc's multi-pass order and `bg_prio` bits
// (bit 0 = sprite, bit 1 = window, bit 2/value 4 = BG attr.7 over sprites):
//   1. Clear the line to paleta[0] (black in the CGB 32k palette)
//   2. Always draw BG pass 0 (LCDC.0 does NOT disable the background)
//   3. Window pass 0 if LCDC.5 (opaque, including colour 0)
//   4. Sprites with attr bit 7 set (behind) if LCDC.1 and not OAM DMA
//   5. If LCDC.0: BG/window pass 1 (master sprite-vs-BG priority)
//   6. Sprites with attr bit 7 clear (in front)
// Attr bit 7 forces BG over sprites even when the sprite is "in front"
// (007 TWNI intro); that bit is applied in pass 0, independent of LCDC.0.
class GbPpu {
public:
    static constexpr int kScreenWidth = 160;
    static constexpr int kScreenHeight = 144;

    void reset(bool is_cgb);

    // VRAM ($8000-$9fff), banked (bank 1 only exists/matters on CGB).
    uint8_t vram_read(uint16_t address) const { return vram_read_bank(vbk_, address); }
    void vram_write(uint16_t address, uint8_t value);
    uint8_t vram_read_bank(int bank, uint16_t address) const;

    // OAM ($fe00-$fe9f).
    uint8_t oam_read(uint8_t address) const { return oam_[address]; }
    void oam_write(uint8_t address, uint8_t value) { oam_[address] = value; }

    // Registers.
    void set_lcdc(uint8_t v) { lcdc_ = v; }
    uint8_t lcdc() const { return lcdc_; }
    void set_scy(uint8_t v);
    // Mid-line SCY write: fills scroll_y[pos .. sample) like gb.pas $FF42.
    // `sample` is `(contador shr speed) div 4`.
    void write_scy_mid_line(uint8_t v, int sample);
    uint8_t scy() const { return scy_; }
    void set_scx(uint8_t v) { scx_ = v; }
    uint8_t scx() const { return scx_; }
    void set_wy(uint8_t v) { wy_ = v; }
    void set_wx(uint8_t v) { wx_ = v; }
    void set_bgp(uint8_t v) { bgp_ = v; }
    void set_obp0(uint8_t v) { obp0_ = v; }
    void set_obp1(uint8_t v) { obp1_ = v; }
    uint8_t vbk() const { return uint8_t(0xfe | vbk_); }
    void set_vbk(uint8_t v) { vbk_ = v & 1; }
    void set_oam_dma(bool busy) { oam_dma_ = busy; }

    // CGB background/sprite palette RAM ($68-$6b).
    uint8_t bg_pal_index() const { return bgp_index_; }
    void set_bg_pal_index(uint8_t v) { bgp_index_ = v & 0x3f; bgp_inc_ = (v & 0x80) != 0; }
    uint8_t read_bg_pal_data() const;
    void write_bg_pal_data(uint8_t v, bool locked);
    uint8_t obj_pal_index() const { return obp_index_; }
    void set_obj_pal_index(uint8_t v) { obp_index_ = v & 0x3f; obp_inc_ = (v & 0x80) != 0; }
    uint8_t read_obj_pal_data() const;
    void write_obj_pal_data(uint8_t v, bool locked);

    void reset_window_line() { window_line_ = 0; }
    // Called once after rendering a visible line where the window was
    // actually drawn (mirrors gb.pas incrementing window_y_draw).
    void advance_window_line() { window_line_++; }
    bool window_visible_on(int ly) const;

    // Renders one 160-pixel scanline into `out` (already positioned at the
    // start of the destination row) as packed 0xAARRGGBB pixels.
    // Not const: finalises the mid-line SCY table the way update_video_* does.
    void render_scanline(int ly, uint32_t* out);

    // Number of sprites visible on this line (0-10), used by the driver to
    // budget Mode 3's extra duration. CGB charges `8 shr speed` per sprite
    // (get_active_sprites_gbc); DMG charges 8 (get_active_sprites).
    int count_sprites_on_line(int ly) const;
    int sprite_mode3_penalty(int ly, int speed) const;

private:
    struct SpriteHit {
        uint8_t oam_index;
        uint8_t y, x, tile, attr;
    };
    int gather_sprites(int ly, SpriteHit* out) const;
    uint32_t dmg_color(uint8_t palette, uint8_t index) const;
    uint32_t cgb_color(const std::array<uint16_t, 32>& pal, uint8_t entry) const;
    uint8_t scy_for_map_column(int map_col) const;
    void finish_scroll_y();
    void render_scanline_dmg(int ly, uint32_t* out) const;
    void render_scanline_cgb(int ly, uint32_t* out) const;
    void cgb_tile_pixel(uint16_t map_base, int pixel_x, int pixel_y,
                        uint8_t* index, uint8_t* attr) const;
    void draw_cgb_sprites(int ly, uint32_t* out, uint8_t* bg_prio, uint8_t pri) const;

    bool is_cgb_ = false;
    bool oam_dma_ = false;
    std::array<std::array<uint8_t, 0x2000>, 2> vram_{};
    std::array<uint8_t, 0xa0> oam_{};

    uint8_t lcdc_ = 0x91, scy_ = 0, scx_ = 0, wy_ = 0, wx_ = 0;
    uint8_t bgp_ = 0xfc, obp0_ = 0xff, obp1_ = 0xff;
    uint8_t vbk_ = 0;
    uint8_t bgp_index_ = 0, obp_index_ = 0;
    bool bgp_inc_ = false, obp_inc_ = false;
    std::array<uint16_t, 32> bgc_pal_{};  // CGB background palette RAM, BGR555
    std::array<uint16_t, 32> spc_pal_{};  // CGB sprite palette RAM, BGR555
    int window_line_ = 0;

    // Mid-line SCY samples (gb.pas scroll_y[0..255], filled up to index 113).
    std::array<uint8_t, 256> scroll_y_{};
    int scroll_y_pos_ = 0;

    static const std::array<uint32_t, 4> kPaletteGreen;
};

}  // namespace dsp
