#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace dsp {

// MAME style graphics layout: every offset is expressed in bits.
struct GfxLayout {
    int width = 8;
    int height = 8;
    int total = 0;
    int planes = 1;
    int char_increment = 0;  // bit distance between two consecutive elements
    // Rotates every decoded element 90 degrees clockwise (rot90 in convert_gfx).
    bool rotate_cw = false;
    std::vector<int> plane_offsets;
    std::vector<int> x_offsets;
    std::vector<int> y_offsets;
};

// Decoded set of characters/sprites: one byte per pixel holding the colour index.
class GfxSet {
public:
    void decode(const GfxLayout& layout, const std::vector<uint8_t>& rom);

    // Allocates room for `total` elements, to be filled with decode_elements().
    void create(int width, int height, int total);
    // Decodes layout.total elements starting at `first_element`, so a set can be
    // built out of several ROM regions with different plane offsets.
    void decode_elements(const GfxLayout& layout, const std::vector<uint8_t>& rom,
                         int first_element);

    int width() const { return width_; }
    int height() const { return height_; }
    int total() const { return total_; }
    const uint8_t* element(int index) const {
        return pixels_.data() + size_t(index % total_) * size_t(width_ * height_);
    }

private:
    int width_ = 0;
    int height_ = 0;
    int total_ = 0;
    std::vector<uint8_t> pixels_;
};

struct ResistorNet {
    std::vector<int> resistances;
    int pulldown = 0;
    int pullup = 0;
};

// Port of pal_engine.pas compute_resistor_weights(). All networks share the same
// scale factor when `scaler` is negative (autoscale), like in the original code.
std::vector<std::vector<double>> compute_resistor_weights(double min_value, double max_value,
                                                          double scaler,
                                                          const std::vector<ResistorNet>& nets);

int combine_weights(const std::vector<double>& weights, const std::vector<int>& bits);

}  // namespace dsp
