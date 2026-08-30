#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <vector>

#include "video/gfx.h"

namespace dsp {

// K053246/K053247 sprite generator (Konami GX series)
class K053246 {
public:
    // code, color (in/out), priority_mask (out 0..3)
    using SpriteCallback = std::function<void(uint32_t& code, uint16_t& color, uint16_t& pri_mask)>;

    K053246(SpriteCallback cb, std::vector<uint8_t> rom);

    void reset();
    void start(int dx = 0, int dy = 0);

    bool is_irq_enabled() const { return (kx46_regs_[5] & 0x10) != 0; }

    uint8_t read(uint8_t offset);
    void write(uint8_t offset, uint8_t value);

    // Direct access to 053247 sprite RAM (word array, 0x800 words)
    uint16_t* ram() { return ram_.data(); }
    const uint16_t* ram() const { return ram_.data(); }

    void set_objcha_line(bool assert_line) { objcha_ = assert_line; }

    void update_sprites();
    // Draw sprites matching priority mask (0..3)
    void draw_sprites(uint16_t* dest, int dest_w, int dest_h, int crop_x, int crop_y, uint8_t prio);

private:
    void draw_single(uint32_t code, int offs, uint16_t color, uint16_t* dest, int dw, int dh,
                     int crop_x, int crop_y);

    SpriteCallback callback_;
    std::vector<uint8_t> rom_;
    GfxSet gfx_;
    uint32_t sprite_mask_ = 0;

    std::array<uint8_t, 8> kx46_regs_{};
    std::array<uint16_t, 32> kx47_regs_{};
    std::array<uint16_t, 0x800> ram_{};
    std::array<uint16_t, 256> sorted_list_{};
    int sprite_count_ = 0;
    bool objcha_ = false;
    int dx_ = 0, dy_ = 0;
};

}  // namespace dsp
