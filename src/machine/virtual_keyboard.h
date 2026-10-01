#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "core/machine.h"

namespace dsp {

// One clickable key of an on-screen keyboard picture. `code` is the
// driver's own identifier (typically matrix row * 8 + bit); the rectangle is
// the key cap in picture pixels.
struct VkbKeyDef {
    int16_t code;
    uint8_t flags;
    int16_t x1, y1, x2, y2;
};

// On-screen keyboard shown as a high-resolution overlay (see
// Machine::screen_overlay()): a picture generated offline (tools/gen_*_vkb.py,
// RGBA rows stored as byte deltas of the row above, zlib) plus its key table.
// The mouse clicks keys; a held key sinks into the picture. Modifier keys
// latch until the next ordinary key, lock keys toggle.
class VirtualKeyboard {
public:
    static constexpr uint8_t kModifier = 1;
    static constexpr uint8_t kLock = 2;

    VirtualKeyboard(const unsigned char* picture, size_t picture_size, int width, int height,
                    const VkbKeyDef* keys, int key_count);

    bool visible() const { return visible_; }
    void set_visible(bool visible);
    // Edge-detects the toggle key (F11 in the drivers).
    void toggle_key(bool down);
    // Mouse over the overlay (MachineInputs::overlay_*).
    void input(const MachineInputs& inputs);
    void release_all();

    MachineOverlay overlay() const;
    int width() const { return width_; }
    int height() const { return height_; }
    const std::vector<uint32_t>& picture() const { return image_; }

    // Calls f(code) for every key that is down (held, latched or locked).
    template <typename F>
    void for_each_down(F f) const {
        if (!visible_) return;
        for (int i = 0; i < key_count_; i++) {
            if (down(i)) f(keys_[i].code);
        }
    }
    bool is_down(int code) const;
    bool key_centre(int code, int* x, int* y) const;

private:
    void build();
    void compose();
    int hit(int x, int y) const;
    bool down(int i) const;

    const unsigned char* picture_data_;
    size_t picture_size_;
    int width_, height_;
    const VkbKeyDef* keys_;
    int key_count_;

    std::vector<uint32_t> base_;
    std::vector<uint32_t> image_;
    uint32_t serial_ = 1;
    bool visible_ = false;
    bool toggle_down_ = false;
    bool button_down_ = false;
    int pressed_ = -1;
    std::vector<bool> latched_;
};

}  // namespace dsp
