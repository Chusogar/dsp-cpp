#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <vector>

#include "video/gfx.h"

namespace dsp {

// Konami 051960/051937 sprite generator, ported from k051960.pas.
class K051960 {
public:
    static constexpr int kNumSprites = 128;

    using SpriteCallback =
        std::function<void(uint16_t& code, uint16_t& color, uint16_t& priority, uint16_t& shadow)>;
    using IrqCallback = std::function<void(bool state)>;

    K051960(SpriteCallback cb, std::vector<uint8_t> rom, int bpp = 4);

    void reset();
    uint8_t read(uint16_t offset);
    void write(uint16_t offset, uint8_t value);
    uint8_t k051937_read(uint8_t offset);
    void k051937_write(uint8_t offset, uint8_t value);

    void update_sprites();
    void update_line(int line);
    // Draw sprites whose callback priority equals `pri`.
    void draw_sprites(int pri, uint16_t* dest, int dest_w, int dest_h, int crop_x, int crop_y) const;

    void set_irq_callback(IrqCallback cb) { irq_cb_ = std::move(cb); }
    void set_firq_callback(IrqCallback cb) { firq_cb_ = std::move(cb); }
    void set_nmi_callback(IrqCallback cb) { nmi_cb_ = std::move(cb); }

private:
    SpriteCallback callback_;
    IrqCallback irq_cb_;
    IrqCallback firq_cb_;
    IrqCallback nmi_cb_;
    std::vector<uint8_t> rom_;
    GfxSet gfx_;
    uint32_t sprite_mask_ = 0;

    std::array<uint8_t, 0x400> ram_{};
    std::array<uint8_t, 8> k051937_{};
    std::array<uint8_t, 3> spriterombank_{};
    std::array<int, kNumSprites> sorted_list_{};
    bool irq_enabled_ = false;
    uint8_t counter_ = 0;
    bool nmi_enabled_ = false;
    bool spriteflip_ = false;
    bool readroms_ = false;
};

}  // namespace dsp
