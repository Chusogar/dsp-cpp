#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <vector>

#include "video/gfx.h"

namespace dsp {

// Konami 051316 zoom/ROZ chip (MAME k051316_device + tilemap_t::draw_roz).
class K051316 {
public:
    using Callback = std::function<void(uint16_t& code, uint16_t& color, uint16_t& priority_mask)>;

    enum class Bpp { Bpp4, Bpp7 };

    static constexpr int kMapW = 32;
    static constexpr int kMapH = 32;
    static constexpr int kTile = 16;
    static constexpr int kLayerW = kMapW * kTile;  // 512
    static constexpr int kLayerH = kMapH * kTile;  // 512

    K051316(Callback cb, std::vector<uint8_t> rom, Bpp bpp = Bpp::Bpp7);

    void reset();
    uint8_t read(uint16_t address);
    void write(uint16_t address, uint8_t value);
    void control_w(uint8_t offset, uint8_t value);
    void set_wraparound(bool enable) { wrap_ = enable; }
    bool wraparound() const { return wrap_; }
    uint8_t ram_at(uint16_t address) const { return ram_[address & 0x7ff]; }
    void set_offsets(int dx, int dy) {
        dx_ = dx;
        dy_ = dy;
    }
    void control_snapshot(uint8_t out[16]) const {
        for (int i = 0; i < 16; i++) out[i] = control_[size_t(i)];
    }
    uint8_t rom_read(uint16_t address) const;

    // Draw zoom layer into dest (palette indices). Transparent pen 0 is skipped.
    // crop_x/crop_y are the MAME cliprect origin (ajax visarea is 8,16).
    void draw(uint16_t* dest, int dest_w, int dest_h, int crop_x, int crop_y);

    void clean_video_buffer();
    // Debug: expose layer pens (512x512)
    const uint16_t* layer_data() const { return layer_.data(); }
    int layer_w() const { return kLayerW; }
    int layer_h() const { return kLayerH; }

private:
    void rebuild_layer();

    Callback callback_;
    std::vector<uint8_t> rom_;
    Bpp bpp_;
    GfxSet gfx_;
    uint32_t tile_mask_ = 0;
    int color_shift_ = 4;  // <<4 for BPP4, <<7 conceptually; Ajax uses color shl 7? Pascal color_type
    int pixels_per_byte_ = 1;

    std::array<uint8_t, 0x800> ram_{};
    std::array<uint8_t, 0x10> control_{};
    std::array<bool, 0x400> dirty_{};
    std::vector<uint16_t> layer_;  // 512x512 pens
    bool layer_dirty_ = true;
    bool wrap_ = false;
    bool flipx_enabled_ = false;
    bool flipy_enabled_ = false;
    int dx_ = 0;
    int dy_ = 0;
};

}  // namespace dsp
