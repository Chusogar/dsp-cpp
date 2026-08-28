#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <vector>

#include "video/gfx.h"

namespace dsp {

// Konami 052109 tilemap generator, ported from k052109.pas.
class K052109 {
public:
    static constexpr int kLayerW = 512;
    static constexpr int kLayerH = 256;

    using Callback = std::function<void(int layer, int bank, uint32_t& code, uint16_t& color,
                                        uint16_t& flags, uint16_t& priority)>;

    K052109(Callback cb, std::vector<uint8_t> rom);

    void reset();
    uint8_t read(uint16_t address);
    void write(uint16_t address, uint8_t value);

    void set_rmrd_line(bool assert_line) { rmrd_ = assert_line; }
    bool is_irq_enabled() const { return irq_enabled_; }

    void draw_tiles();
    void draw_layer(int layer, uint16_t* dest, int dest_w, int dest_h, int crop_x, int crop_y) const;
    void clean_video_buffer();

private:
    void update_tile(int layer, int index);
    void calc_scroll_1();
    void calc_scroll_2();

    Callback callback_;
    std::vector<uint8_t> rom_;
    uint32_t char_mask_ = 0;
    GfxSet gfx_;

    std::array<uint8_t, 0x6000> ram_{};
    std::array<std::array<bool, 0x800>, 3> dirty_{};
    std::array<std::vector<uint16_t>, 3> layers_;

    std::array<uint8_t, 4> charrombank_{};
    std::array<uint8_t, 4> charrombank2_{};
    uint8_t romsubbank_ = 0;
    uint8_t scrollctrl_ = 0;
    uint8_t tileflip_enable_ = 0;
    bool irq_enabled_ = false;
    bool rmrd_ = false;
    bool has_extra_video_ram_ = false;

    // scroll_tipo: 0=rowscroll 8px groups, 1=rowscroll 1px, 2=colscroll, 3=global
    std::array<uint8_t, 3> scroll_tipo_{};
    std::array<std::array<uint16_t, 256>, 3> scroll_x_{};
    std::array<std::array<uint16_t, 512>, 3> scroll_y_{};
};

}  // namespace dsp
