#include "machine/virtual_keyboard.h"

#include <algorithm>

#include <zlib.h>

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#define STBI_ONLY_JPEG
#define STBI_NO_STDIO
#include "third_party/stb_image.h"

namespace dsp {

VirtualKeyboard::VirtualKeyboard(const unsigned char* picture, size_t picture_size, int width, int height,
                                 const VkbKeyDef* keys, int key_count, Format format)
    : picture_data_(picture),
      format_(format),
      picture_size_(picture_size),
      width_(width),
      height_(height),
      keys_(keys),
      key_count_(key_count),
      latched_(size_t(key_count), false) {}

void VirtualKeyboard::build() {
    if (format_ == Format::Jpeg) {
        int w = 0, h = 0, n = 0;
        unsigned char* px = stbi_load_from_memory(picture_data_, int(picture_size_), &w, &h, &n, 4);
        base_.assign(size_t(width_) * size_t(height_), 0xff404040u);
        if (px != nullptr && crop_x_ + width_ <= w && crop_y_ + height_ <= h) {
            for (int y = 0; y < height_; y++) {
                for (int x = 0; x < width_; x++) {
                    const unsigned char* p = px + (size_t(y + crop_y_) * size_t(w) + size_t(x + crop_x_)) * 4;
                    int r = p[0], g = p[1], b = p[2], a = 255;
                    if (key_red_) {
                        // How much redder than the other channels: 60 levels
                        // or less is the (dark grey) machine, 120 or more the
                        // backdrop; in between is the anti-aliased outline,
                        // whose red spill is removed.
                        const int excess = r - std::max(g, b);
                        if (excess > 60) {
                            a = std::max(0, 255 - (excess - 60) * 255 / 60);
                            r = std::max(g, b);
                        }
                    }
                    base_[size_t(y) * size_t(width_) + size_t(x)] =
                        uint32_t(a) << 24 | uint32_t(r) << 16 | uint32_t(g) << 8 | uint32_t(b);
                }
            }
        }
        stbi_image_free(px);
        image_ = base_;
        return;
    }
    const size_t stride = size_t(width_) * 4;
    std::vector<uint8_t> rgba(stride * size_t(height_));
    uLongf len = uLongf(rgba.size());
    if (uncompress(rgba.data(), &len, picture_data_, uLong(picture_size_)) != Z_OK || len != rgba.size()) {
        rgba.assign(rgba.size(), 0x60);
    } else {
        for (size_t i = stride; i < rgba.size(); i++) rgba[i] = uint8_t(rgba[i] + rgba[i - stride]);
    }
    base_.resize(size_t(width_) * size_t(height_));
    for (size_t i = 0; i < base_.size(); i++) {
        const uint8_t* p = &rgba[i * 4];
        base_[i] = uint32_t(p[3]) << 24 | uint32_t(p[0]) << 16 | uint32_t(p[1]) << 8 | p[2];
    }
    image_ = base_;
}

void VirtualKeyboard::set_visible(bool visible) {
    if (visible && base_.empty()) build();
    if (!visible) release_all();
    visible_ = visible;
    if (visible) compose();
}

void VirtualKeyboard::toggle_key(bool down) {
    if (down && !toggle_down_) set_visible(!visible_);
    toggle_down_ = down;
}

void VirtualKeyboard::release_all() {
    pressed_ = -1;
    latched_.assign(latched_.size(), false);
    button_down_ = false;
}

MachineOverlay VirtualKeyboard::overlay() const {
    MachineOverlay o;
    if (!visible_ || image_.empty()) return o;
    o.pixels = image_.data();
    o.width = width_;
    o.height = height_;
    o.serial = serial_;
    return o;
}

int VirtualKeyboard::hit(int x, int y) const {
    for (int i = 0; i < key_count_; i++) {
        const VkbKeyDef& k = keys_[i];
        if (x >= k.x1 && x < k.x2 && y >= k.y1 && y < k.y2) return i;
    }
    return -1;
}

bool VirtualKeyboard::down(int i) const { return i == pressed_ || latched_[size_t(i)]; }

bool VirtualKeyboard::is_down(int code) const {
    if (!visible_) return false;
    for (int i = 0; i < key_count_; i++) {
        if (keys_[i].code == code && down(i)) return true;
    }
    return false;
}

bool VirtualKeyboard::key_centre(int code, int* x, int* y) const {
    for (int i = 0; i < key_count_; i++) {
        if (keys_[i].code != code) continue;
        *x = (keys_[i].x1 + keys_[i].x2) / 2;
        *y = (keys_[i].y1 + keys_[i].y2) / 2;
        return true;
    }
    return false;
}

void VirtualKeyboard::input(const MachineInputs& inputs) {
    if (!visible_) return;
    const bool button = inputs.overlay_pointer && inputs.overlay_button;
    const int before_pressed = pressed_;
    const std::vector<bool> before_latched = latched_;
    if (button && !button_down_) {
        const int k = hit(inputs.overlay_x, inputs.overlay_y);
        if (k >= 0) {
            if (keys_[k].flags & (kModifier | kLock)) {
                latched_[size_t(k)] = !latched_[size_t(k)];
            } else {
                pressed_ = k;
            }
        }
    }
    if (!button && pressed_ >= 0) {
        pressed_ = -1;
        // A clicked key consumes the latched modifiers; locks stay.
        for (int i = 0; i < key_count_; i++) {
            if (keys_[i].flags & kModifier) latched_[size_t(i)] = false;
        }
    }
    button_down_ = button;
    if (before_pressed != pressed_ || before_latched != latched_) compose();
}

void VirtualKeyboard::compose() {
    image_ = base_;
    // A key that is down sinks: its cap moves down a few pixels and gets
    // darker, the gap it leaves at the top shows the shadowed surround.
    const int travel = travel_ > 0 ? travel_ : height_ / 90 + 2;
    for (int i = 0; i < key_count_; i++) {
        // An ordinary key drawn in several pieces (the QL's L-shaped ENTER)
        // sinks as a whole.
        bool sunk = down(i);
        if (!sunk && pressed_ >= 0 && keys_[i].flags == 0 && keys_[pressed_].code == keys_[i].code) sunk = true;
        if (!sunk) continue;
        const VkbKeyDef& k = keys_[i];
        for (int y = k.y2 - 1; y >= k.y1; y--) {
            // The whole cap rectangle moves down; the band it uncovers at the
            // top shows what lay just above it (socket rim / background).
            const int sy = y - travel < 0 ? 0 : y - travel;
            for (int x = k.x1; x < k.x2; x++) {
                const uint32_t p = base_[size_t(sy) * size_t(width_) + size_t(x)];
                const uint32_t r = ((p >> 16) & 0xff) * 13 / 16, g = ((p >> 8) & 0xff) * 13 / 16,
                               b = (p & 0xff) * 13 / 16;
                image_[size_t(y) * size_t(width_) + size_t(x)] = (p & 0xff000000u) | r << 16 | g << 8 | b;
            }
        }
    }
    serial_++;
}

}  // namespace dsp
