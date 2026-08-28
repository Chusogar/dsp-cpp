#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <vector>

#include "video/gfx.h"

namespace dsp {

// Konami 051316 zoom/ROZ chip, ported from k051316.pas (+ true affine sampling).
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
    uint8_t rom_read(uint16_t address) const;

    // Draw zoom layer into dest (palette indices). Transparent pen 0 is skipped.
    void draw(uint16_t* dest, int dest_w, int dest_h, int crop_x, int crop_y);

    void clean_video_buffer();

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
};

}  // namespace dsp
