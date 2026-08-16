#include "video/gb_ppu.h"

#include <algorithm>
#include <cmath>

namespace dsp {

// Classic DMG "green LCD" 4-shade palette, from gb.pas's color_pal[0].
const std::array<uint32_t, 4> GbPpu::kPaletteGreen = {
    0xff9bbc0fu,
    0xff8bac0fu,
    0xff306230u,
    0xff0f380fu,
};

void GbPpu::reset(bool is_cgb) {
    is_cgb_ = is_cgb;
    oam_dma_ = false;
    for (auto& bank : vram_) bank.fill(0);
    oam_.fill(0);
    lcdc_ = 0x91;
    scy_ = scx_ = wy_ = wx_ = 0;
    bgp_ = 0xfc;
    obp0_ = obp1_ = 0xff;
    vbk_ = 0;
    bgp_index_ = obp_index_ = 0;
    bgp_inc_ = obp_inc_ = false;
    bgc_pal_.fill(0x7fff);
    spc_pal_.fill(0);
    window_line_ = 0;
    scroll_y_.fill(0);
    scroll_y_pos_ = 0;
}

void GbPpu::set_scy(uint8_t v) {
    scy_ = v;
    scroll_y_.fill(v);
    scroll_y_pos_ = 0;
}

void GbPpu::write_scy_mid_line(uint8_t v, int sample) {
    if (sample < 0) sample = 0;
    if (sample > 256) sample = 256;
    for (int i = scroll_y_pos_; i < sample && i < 256; i++) scroll_y_[size_t(i)] = v;
    scroll_y_pos_ = sample > 255 ? 255 : sample;
    scy_ = v;
}

void GbPpu::finish_scroll_y() {
    for (int i = scroll_y_pos_; i <= 113 && i < 256; i++) scroll_y_[size_t(i)] = scy_;
    scroll_y_pos_ = 0;
}

uint8_t GbPpu::scy_for_map_column(int map_col) const {
    int idx = int(std::lround(double(map_col) * 3.5625));
    if (idx < 0) idx = 0;
    if (idx > 255) idx = 255;
    return scroll_y_[size_t(idx)];
}

void GbPpu::vram_write(uint16_t address, uint8_t value) { vram_[vbk_][address & 0x1fff] = value; }
uint8_t GbPpu::vram_read_bank(int bank, uint16_t address) const {
    return vram_[bank & 1][address & 0x1fff];
}

uint8_t GbPpu::read_bg_pal_data() const {
    uint16_t entry = bgc_pal_[bgp_index_ >> 1];
    return (bgp_index_ & 1) != 0 ? uint8_t(entry >> 8) : uint8_t(entry & 0xff);
}
void GbPpu::write_bg_pal_data(uint8_t v, bool locked) {
    if (!locked) {
        uint16_t& entry = bgc_pal_[bgp_index_ >> 1];
        entry = (bgp_index_ & 1) != 0 ? uint16_t((entry & 0x00ff) | (v << 8))
                                      : uint16_t((entry & 0xff00) | v);
    }
    if (bgp_inc_) bgp_index_ = (bgp_index_ + 1) & 0x3f;
}
uint8_t GbPpu::read_obj_pal_data() const {
    uint16_t entry = spc_pal_[obp_index_ >> 1];
    return (obp_index_ & 1) != 0 ? uint8_t(entry >> 8) : uint8_t(entry & 0xff);
}
void GbPpu::write_obj_pal_data(uint8_t v, bool locked) {
    if (!locked) {
        uint16_t& entry = spc_pal_[obp_index_ >> 1];
        entry = (obp_index_ & 1) != 0 ? uint16_t((entry & 0x00ff) | (v << 8))
                                      : uint16_t((entry & 0xff00) | v);
    }
    if (obp_inc_) obp_index_ = (obp_index_ + 1) & 0x3f;
}

bool GbPpu::window_visible_on(int ly) const {
    return (lcdc_ & 0x20) != 0 && ly >= wy_ && wx_ <= 166 && wx_ != 0;
}

uint32_t GbPpu::dmg_color(uint8_t palette, uint8_t index) const {
    uint8_t shade = uint8_t((palette >> (index * 2)) & 3);
    return kPaletteGreen[shade];
}

uint32_t GbPpu::cgb_color(const std::array<uint16_t, 32>& pal, uint8_t entry) const {
    uint16_t v = pal[entry & 0x1f];
    uint8_t r = uint8_t((v & 0x1f) << 3);
    uint8_t g = uint8_t(((v >> 5) & 0x1f) << 3);
    uint8_t b = uint8_t(((v >> 10) & 0x1f) << 3);
    return 0xff000000u | (uint32_t(r) << 16) | (uint32_t(g) << 8) | b;
}

int GbPpu::gather_sprites(int ly, SpriteHit* out) const {
    int size = (lcdc_ & 4) != 0 ? 16 : 8;
    int count = 0;
    for (int i = 0; i < 40 && count < 10; i++) {
        uint8_t y = oam_[size_t(i) * 4 + 0];
        if (y == 0 || y >= 160) continue;
        int line_in_sprite = ly - (int(y) - 16);
        if (line_in_sprite < 0 || line_in_sprite >= size) continue;
        out[count].oam_index = uint8_t(i);
        out[count].y = y;
        out[count].x = oam_[size_t(i) * 4 + 1];
        out[count].tile = oam_[size_t(i) * 4 + 2];
        out[count].attr = oam_[size_t(i) * 4 + 3];
        count++;
    }
    if (!is_cgb_) {
        // DMG: sorted by X (lower X drawn on top), ties keep OAM order.
        std::stable_sort(out, out + count,
                         [](const SpriteHit& a, const SpriteHit& b) { return a.x < b.x; });
    }
    return count;
}

int GbPpu::count_sprites_on_line(int ly) const {
    if ((lcdc_ & 2) == 0) return 0;
    SpriteHit hits[10];
    return gather_sprites(ly, hits);
}

int GbPpu::sprite_mode3_penalty(int ly, int speed) const {
    SpriteHit hits[10];
    int count = gather_sprites(ly, hits);
    int per = is_cgb_ ? (8 >> (speed & 1)) : 8;
    return count * per;
}

void GbPpu::render_scanline(int ly, uint32_t* out) {
    finish_scroll_y();
    if (is_cgb_) render_scanline_cgb(ly, out);
    else render_scanline_dmg(ly, out);
}

void GbPpu::cgb_tile_pixel(uint16_t map_base, int pixel_x, int pixel_y,
                           uint8_t* index, uint8_t* attr) const {
    pixel_x &= 0xff;
    pixel_y &= 0xff;
    uint16_t map_addr = uint16_t((map_base + (pixel_y / 8) * 32 + (pixel_x / 8)) & 0x1fff);
    uint8_t tile_num = vram_read_bank(0, map_addr);
    uint8_t a = vram_read_bank(1, map_addr);
    bool flip_x = (a & 0x20) != 0, flip_y = (a & 0x40) != 0;
    int row = flip_y ? (7 - (pixel_y & 7)) : (pixel_y & 7);
    int col = flip_x ? (7 - (pixel_x & 7)) : (pixel_x & 7);
    int n2 = ((lcdc_ & 0x10) == 0) ? int(int8_t(tile_num)) : int(tile_num);
    uint16_t tile_base = uint16_t(((lcdc_ & 0x10) == 0) ? 0x1000 : 0);
    int bank = (a & 0x08) != 0 ? 1 : 0;
    uint16_t addr = uint16_t((n2 * 16 + int(tile_base) + row * 2) & 0x1fff);
    uint8_t lo = vram_read_bank(bank, addr);
    uint8_t hi = vram_read_bank(bank, uint16_t((addr + 1) & 0x1fff));
    uint8_t bit = uint8_t(7 - col);
    *index = uint8_t(((lo >> bit) & 1) | (((hi >> bit) & 1) << 1));
    *attr = a;
}

void GbPpu::draw_cgb_sprites(int ly, uint32_t* out, uint8_t* bg_prio, uint8_t pri) const {
    SpriteHit hits[10];
    int count = gather_sprites(ly, hits);
    int size = (lcdc_ & 4) != 0 ? 16 : 8;
    // OAM reverse order so earlier sprites end up on top, matching
    // draw_sprites_gbc's `for f:=9 downto 0`.
    for (int i = count - 1; i >= 0; i--) {
        const SpriteHit& s = hits[size_t(i)];
        if ((s.attr & 0x80) != pri) continue;
        int sx = int(s.x) - 8;
        if (s.x == 0 || s.x >= 168) continue;
        bool flip_x = (s.attr & 0x20) != 0, flip_y = (s.attr & 0x40) != 0;
        int line_in_sprite = ly - (int(s.y) - 16);
        int row = flip_y ? (size - 1 - line_in_sprite) : line_in_sprite;
        uint8_t tile = s.tile;
        if (size == 16) {
            if (flip_y) tile = uint8_t((tile & 0xfe) + ((~uint8_t(line_in_sprite >> 3)) & 1));
            else tile = uint8_t((tile & 0xfe) + ((line_in_sprite >> 3) & 1));
            row &= 7;
        }
        int bank = (s.attr & 0x08) != 0 ? 1 : 0;
        uint16_t tile_addr = uint16_t(tile * 16 + row * 2);
        uint8_t lo = vram_read_bank(bank, tile_addr);
        uint8_t hi = vram_read_bank(bank, uint16_t(tile_addr + 1));
        uint8_t pal = uint8_t((s.attr & 7) * 4);
        for (int col = 0; col < 8; col++) {
            int px = sx + col;
            int bit = flip_x ? col : (7 - col);
            uint8_t idx = uint8_t(((lo >> bit) & 1) | (((hi >> bit) & 1) << 1));
            if (idx == 0) continue;
            uint8_t prio_i = uint8_t(px & 0xff);
            // Attr bit 7 on a non-zero BG pixel (bg_prio bit 2) hides the sprite.
            if ((bg_prio[prio_i] & 4) != 0) continue;
            if (px >= 0 && px < kScreenWidth) {
                out[px] = cgb_color(spc_pal_, uint8_t((idx + pal) & 0x1f));
            }
            bg_prio[prio_i] = uint8_t(bg_prio[prio_i] | 1);
        }
    }
}

void GbPpu::render_scanline_cgb(int ly, uint32_t* out) const {
    // paleta[0] after gb_set_pal's CGB 32k table is RGB(0,0,0).
    const uint32_t blank = 0xff000000u;
    for (int x = 0; x < kScreenWidth; x++) out[x] = blank;
    if ((lcdc_ & 0x80) == 0) return;

    std::array<uint8_t, 256> bg_prio{};
    std::array<uint8_t, kScreenWidth> bg_index{};
    std::array<uint8_t, kScreenWidth> bg_attr{};
    uint16_t bg_map = uint16_t(0x1800 + ((lcdc_ & 0x08) ? 0x400 : 0));

    // Pass 0: background. Always drawn; LCDC.0 is master priority, not BG enable.
    for (int x = 0; x < kScreenWidth; x++) {
        int bg_x = (x + scx_) & 0xff;
        int map_col = bg_x / 8;
        int map_y = (ly + scy_for_map_column(map_col)) & 0xff;
        uint8_t idx = 0, attr = 0;
        cgb_tile_pixel(bg_map, bg_x, map_y, &idx, &attr);
        bg_index[size_t(x)] = idx;
        bg_attr[size_t(x)] = attr;
        uint8_t pal = uint8_t((attr & 7) * 4);
        out[x] = cgb_color(bgc_pal_, uint8_t(idx + pal));
        if ((attr & 0x80) != 0 && idx != 0) bg_prio[size_t(x)] = uint8_t(bg_prio[size_t(x)] | 4);
    }

    bool window_here = window_visible_on(ly);
    uint16_t win_map = uint16_t(0x1800 + ((lcdc_ & 0x40) ? 0x400 : 0));
    if (window_here) {
        int win_origin = int(wx_) - 7;
        for (int x = 0; x < kScreenWidth; x++) {
            if (x < win_origin) continue;
            int map_x = x - win_origin;
            uint8_t idx = 0, attr = 0;
            cgb_tile_pixel(win_map, map_x, window_line_, &idx, &attr);
            uint8_t pal = uint8_t((attr & 7) * 4);
            out[x] = cgb_color(bgc_pal_, uint8_t(idx + pal));
            bg_prio[size_t(x)] = uint8_t(bg_prio[size_t(x)] | 2);
            bg_index[size_t(x)] = idx;
            bg_attr[size_t(x)] = attr;
        }
    }

    bool sprites_on = ((lcdc_ & 2) != 0) && !oam_dma_;
    if (sprites_on) draw_cgb_sprites(ly, out, bg_prio.data(), 0x80);

    // LCDC.0: second pass, BG/window cover sprites (colour 0 transparent).
    if ((lcdc_ & 1) != 0) {
        for (int x = 0; x < kScreenWidth; x++) {
            bool in_window = window_here && x >= int(wx_) - 7;
            uint8_t idx = bg_index[size_t(x)];
            uint8_t attr = bg_attr[size_t(x)];
            uint8_t pal = uint8_t((attr & 7) * 4);
            if (in_window) {
                // Window pass 1: non-zero always covers; colour 0 only if
                // nothing is underneath (bg_prio == 0). After pass 0 the
                // window bit is already set, so colour 0 never restores
                // over a sprite — sprites show through window colour 0.
                if (idx != 0) out[x] = cgb_color(bgc_pal_, uint8_t(idx + pal));
                else if (bg_prio[size_t(x)] == 0) out[x] = cgb_color(bgc_pal_, pal);
            } else if (idx != 0 && (bg_prio[size_t(x)] & 2) == 0) {
                out[x] = cgb_color(bgc_pal_, uint8_t(idx + pal));
            }
        }
    }

    if (sprites_on) draw_cgb_sprites(ly, out, bg_prio.data(), 0);
}

void GbPpu::render_scanline_dmg(int ly, uint32_t* out) const {
    bool display_on = (lcdc_ & 0x80) != 0;
    if (!display_on) {
        uint32_t blank = kPaletteGreen[0];
        for (int x = 0; x < kScreenWidth; x++) out[x] = blank;
        return;
    }

    std::array<uint8_t, kScreenWidth> bg_index{};
    bool window_here = window_visible_on(ly);
    bool bg_enabled = (lcdc_ & 1) != 0;

    if (bg_enabled) {
        for (int x = 0; x < kScreenWidth; x++) {
            bool use_window = window_here && x >= wx_ - 7;
            int map_x, map_y;
            uint16_t map_base;
            if (use_window) {
                map_x = x - (wx_ - 7);
                map_y = window_line_;
                map_base = uint16_t(0x1800 + ((lcdc_ & 0x40) != 0 ? 0x400 : 0));
            } else {
                map_x = (x + scx_) & 0xff;
                map_y = (ly + scy_for_map_column(map_x / 8)) & 0xff;
                map_base = uint16_t(0x1800 + ((lcdc_ & 0x08) != 0 ? 0x400 : 0));
            }
            uint16_t map_addr = uint16_t(map_base + (map_y / 8) * 32 + (map_x / 8));
            uint8_t tile_num = vram_read_bank(0, map_addr);
            int row = map_y & 7;
            int col = map_x & 7;
            uint16_t tile_addr;
            if ((lcdc_ & 0x10) != 0) {
                tile_addr = uint16_t(tile_num * 16);
            } else {
                tile_addr = uint16_t(0x1000 + int8_t(tile_num) * 16);
            }
            uint8_t lo = vram_read_bank(0, uint16_t(tile_addr + row * 2));
            uint8_t hi = vram_read_bank(0, uint16_t(tile_addr + row * 2 + 1));
            uint8_t bit = uint8_t(7 - col);
            uint8_t idx = uint8_t(((lo >> bit) & 1) | (((hi >> bit) & 1) << 1));
            bg_index[size_t(x)] = idx;
        }
    }

    std::array<uint8_t, kScreenWidth> obj_index{};
    std::array<uint8_t, kScreenWidth> obj_attr{};
    std::array<bool, kScreenWidth> obj_present{};
    if ((lcdc_ & 2) != 0 && !oam_dma_) {
        SpriteHit hits[10];
        int count = gather_sprites(ly, hits);
        int size = (lcdc_ & 4) != 0 ? 16 : 8;
        for (int i = count - 1; i >= 0; i--) {
            const SpriteHit& s = hits[size_t(i)];
            int sx = int(s.x) - 8;
            if (sx <= -8 || sx >= kScreenWidth) continue;
            bool flip_x = (s.attr & 0x20) != 0, flip_y = (s.attr & 0x40) != 0;
            int line_in_sprite = ly - (int(s.y) - 16);
            int row = flip_y ? (size - 1 - line_in_sprite) : line_in_sprite;
            uint8_t tile = s.tile;
            if (size == 16) tile = uint8_t((tile & 0xfe) + (row >= 8 ? 1 : 0));
            int row_in_tile = row & 7;
            uint16_t tile_addr = uint16_t(tile * 16 + row_in_tile * 2);
            uint8_t lo = vram_read_bank(0, tile_addr);
            uint8_t hi = vram_read_bank(0, uint16_t(tile_addr + 1));
            for (int col = 0; col < 8; col++) {
                int px = sx + col;
                if (px < 0 || px >= kScreenWidth) continue;
                int bit = flip_x ? col : (7 - col);
                uint8_t idx = uint8_t(((lo >> bit) & 1) | (((hi >> bit) & 1) << 1));
                if (idx == 0) continue;
                obj_index[size_t(px)] = idx;
                obj_attr[size_t(px)] = s.attr;
                obj_present[size_t(px)] = true;
            }
        }
    }

    for (int x = 0; x < kScreenWidth; x++) {
        uint8_t bidx = bg_enabled ? bg_index[size_t(x)] : 0;
        if (obj_present[size_t(x)]) {
            bool obj_behind_bg = (obj_attr[size_t(x)] & 0x80) != 0;
            bool obj_loses = obj_behind_bg && bidx != 0;
            if (!obj_loses) {
                uint8_t oidx = obj_index[size_t(x)];
                uint8_t pal = (obj_attr[size_t(x)] & 0x10) != 0 ? obp1_ : obp0_;
                out[x] = dmg_color(pal, oidx);
                continue;
            }
        }
        out[x] = dmg_color(bgp_, bidx);
    }
}

}  // namespace dsp
