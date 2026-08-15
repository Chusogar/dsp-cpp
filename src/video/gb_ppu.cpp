#include "video/gb_ppu.h"

#include <algorithm>

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

void GbPpu::render_scanline(int ly, uint32_t* out) const {
    bool display_on = (lcdc_ & 0x80) != 0;
    if (!display_on) {
        uint32_t blank = is_cgb_ ? cgb_color(bgc_pal_, 0) : kPaletteGreen[0];
        for (int x = 0; x < kScreenWidth; x++) out[x] = blank;
        return;
    }

    // --- Background / window: effective color index (0-3) and, for CGB,
    // tile attributes (palette/priority) at every screen x. ---
    std::array<uint8_t, kScreenWidth> bg_index{};
    std::array<uint8_t, kScreenWidth> bg_attr{};  // CGB: bit7 = BG-over-OBJ priority tile bit
    bool window_here = window_visible_on(ly);
    bool bg_enabled = is_cgb_ || (lcdc_ & 1) != 0;  // DMG: LCDC.0 disables BG; CGB: BG always on

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
                map_y = (ly + scy_) & 0xff;
                map_base = uint16_t(0x1800 + ((lcdc_ & 0x08) != 0 ? 0x400 : 0));
            }
            uint16_t map_addr = uint16_t(map_base + (map_y / 8) * 32 + (map_x / 8));
            uint8_t tile_num = vram_read_bank(0, map_addr);
            uint8_t attr = is_cgb_ ? vram_read_bank(1, map_addr) : 0;
            bool flip_x = (attr & 0x20) != 0, flip_y = (attr & 0x40) != 0;
            int row = flip_y ? (7 - (map_y & 7)) : (map_y & 7);
            int col = flip_x ? (7 - (map_x & 7)) : (map_x & 7);
            uint16_t tile_addr;
            if ((lcdc_ & 0x10) != 0) {
                tile_addr = uint16_t(tile_num * 16);
            } else {
                tile_addr = uint16_t(0x1000 + int8_t(tile_num) * 16);
            }
            int bank = (attr & 0x08) != 0 ? 1 : 0;
            uint8_t lo = vram_read_bank(bank, uint16_t(tile_addr + row * 2));
            uint8_t hi = vram_read_bank(bank, uint16_t(tile_addr + row * 2 + 1));
            uint8_t bit = uint8_t(7 - col);
            uint8_t idx = uint8_t(((lo >> bit) & 1) | (((hi >> bit) & 1) << 1));
            bg_index[size_t(x)] = idx;
            bg_attr[size_t(x)] = attr;
        }
    }

    // --- Sprites ---
    std::array<uint8_t, kScreenWidth> obj_index{};
    std::array<uint8_t, kScreenWidth> obj_attr{};
    std::array<bool, kScreenWidth> obj_present{};
    if ((lcdc_ & 2) != 0) {
        SpriteHit hits[10];
        int count = gather_sprites(ly, hits);
        int size = (lcdc_ & 4) != 0 ? 16 : 8;
        // Draw lowest priority first so higher-priority sprites overwrite.
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
            int bank = is_cgb_ && (s.attr & 0x08) != 0 ? 1 : 0;
            uint16_t tile_addr = uint16_t(tile * 16 + row_in_tile * 2);
            uint8_t lo = vram_read_bank(bank, tile_addr);
            uint8_t hi = vram_read_bank(bank, uint16_t(tile_addr + 1));
            for (int col = 0; col < 8; col++) {
                int px = sx + col;
                if (px < 0 || px >= kScreenWidth) continue;
                int bit = flip_x ? col : (7 - col);
                uint8_t idx = uint8_t(((lo >> bit) & 1) | (((hi >> bit) & 1) << 1));
                if (idx == 0) continue;  // sprite color 0 is always transparent
                obj_index[size_t(px)] = idx;
                obj_attr[size_t(px)] = s.attr;
                obj_present[size_t(px)] = true;
            }
        }
    }

    // --- Resolve final colour per pixel ---
    bool master_priority = !is_cgb_ || (lcdc_ & 1) != 0;
    for (int x = 0; x < kScreenWidth; x++) {
        uint8_t bidx = bg_enabled ? bg_index[size_t(x)] : 0;
        bool bg_tile_priority = is_cgb_ && (bg_attr[size_t(x)] & 0x80) != 0 && master_priority;

        if (obj_present[size_t(x)] && !bg_tile_priority) {
            bool obj_behind_bg = (obj_attr[size_t(x)] & 0x80) != 0;
            bool obj_loses = master_priority && obj_behind_bg && bidx != 0;
            if (!obj_loses) {
                uint8_t oidx = obj_index[size_t(x)];
                if (is_cgb_) {
                    out[x] = cgb_color(spc_pal_, uint8_t((obj_attr[size_t(x)] & 7) * 4 + oidx));
                } else {
                    uint8_t pal = (obj_attr[size_t(x)] & 0x10) != 0 ? obp1_ : obp0_;
                    out[x] = dmg_color(pal, oidx);
                }
                continue;
            }
        }
        if (is_cgb_) {
            uint8_t pal_num = uint8_t(bg_attr[size_t(x)] & 7);
            out[x] = bg_enabled ? cgb_color(bgc_pal_, uint8_t(pal_num * 4 + bidx))
                                : cgb_color(bgc_pal_, 0);
        } else {
            out[x] = dmg_color(bgp_, bidx);
        }
    }
}

}  // namespace dsp
