#pragma once

#include <array>
#include <cstdint>

namespace dsp {

// Game Boy / Game Boy Color PPU, ported from gb.pas's update_video_gb /
// update_video_gbc family of procedures (background, window, sprites,
// priorities). The original renders through several compositing passes
// tailored to its generic screen-blit engine (see the long comment at the
// top of gb.pas explaining the draw order); this is a from-scratch
// single-pass-per-pixel reimplementation that resolves to the exact same
// priorities (verified against that comment and each pass's logic), simpler
// because it writes straight into one framebuffer instead of layering blits.
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
    void set_scy(uint8_t v) { scy_ = v; }
    void set_scx(uint8_t v) { scx_ = v; }
    void set_wy(uint8_t v) { wy_ = v; }
    void set_wx(uint8_t v) { wx_ = v; }
    void set_bgp(uint8_t v) { bgp_ = v; }
    void set_obp0(uint8_t v) { obp0_ = v; }
    void set_obp1(uint8_t v) { obp1_ = v; }
    uint8_t vbk() const { return uint8_t(0xfe | vbk_); }
    void set_vbk(uint8_t v) { vbk_ = v & 1; }

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
    void render_scanline(int ly, uint32_t* out) const;

    // Number of sprites visible on this line (0-10), used by the driver to
    // budget Mode 3's extra duration (8 T-states/sprite, matching
    // get_active_sprites' gb_0.sprites_time).
    int count_sprites_on_line(int ly) const;

private:
    struct SpriteHit {
        uint8_t oam_index;
        uint8_t y, x, tile, attr;
    };
    int gather_sprites(int ly, SpriteHit* out) const;
    uint32_t dmg_color(uint8_t palette, uint8_t index) const;
    uint32_t cgb_color(const std::array<uint16_t, 32>& pal, uint8_t entry) const;

    bool is_cgb_ = false;
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

    static const std::array<uint32_t, 4> kPaletteGreen;
};

}  // namespace dsp
