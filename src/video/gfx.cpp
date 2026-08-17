#include "video/gfx.h"

#include <algorithm>
#include <cmath>

namespace dsp {
namespace {

inline uint8_t get_bit(const std::vector<uint8_t>& rom, int bit_index) {
    size_t byte_index = size_t(bit_index) >> 3;
    if (byte_index >= rom.size()) return 0;
    return uint8_t((rom[byte_index] >> (7 - (bit_index & 7))) & 1);
}

}  // namespace

void GfxSet::decode(const GfxLayout& layout, const std::vector<uint8_t>& rom) {
    create(layout.width, layout.height, layout.total);
    decode_elements(layout, rom, 0);
}

void GfxSet::create(int width, int height, int total) {
    width_ = width;
    height_ = height;
    total_ = total;
    pixels_.assign(size_t(total_) * size_t(width_ * height_), 0);
}

void GfxSet::decode_elements(const GfxLayout& layout, const std::vector<uint8_t>& rom,
                             int first_element) {
    size_t index = size_t(first_element) * size_t(width_ * height_);
    for (int n = 0; n < layout.total; n++) {
        int element = first_element + n;
        int base = n * layout.char_increment;
        for (int y = 0; y < layout.height; y++) {
            for (int x = 0; x < layout.width; x++) {
                uint8_t value = 0;
                for (int plane = 0; plane < layout.planes; plane++) {
                    uint8_t bit = get_bit(rom, layout.plane_offsets[size_t(plane)] +
                                                   layout.y_offsets[size_t(y)] +
                                                   layout.x_offsets[size_t(x)] + base);
                    value = uint8_t(value | (bit << (layout.planes - 1 - plane)));
                }
                pixels_[index++] = value;
            }
        }
        if (layout.rotate_cw) {
            uint8_t* target = pixels_.data() + size_t(element) * size_t(width_ * height_);
            std::vector<uint8_t> source(target, target + size_t(width_ * height_));
            for (int row = 0; row < height_; row++) {
                for (int column = 0; column < width_; column++) {
                    target[row * width_ + column] =
                        source[size_t((height_ - 1 - column) * width_ + row)];
                }
            }
        }
        if (layout.rotate_ccw) {
            // Matches gfx_engine.pas Rotatel (rol90): dest[r][c] = src[c][w-1-r].
            uint8_t* target = pixels_.data() + size_t(element) * size_t(width_ * height_);
            std::vector<uint8_t> source(target, target + size_t(width_ * height_));
            for (int row = 0; row < height_; row++) {
                for (int column = 0; column < width_; column++) {
                    target[row * width_ + column] =
                        source[size_t(column * width_ + (width_ - 1 - row))];
                }
            }
        }
    }
}

std::vector<std::vector<double>> compute_resistor_weights(double min_value, double max_value,
                                                          double scaler,
                                                          const std::vector<ResistorNet>& nets) {
    std::vector<std::vector<double>> weights(nets.size());
    std::vector<double> max_out(nets.size(), 0.0);

    for (size_t net = 0; net < nets.size(); net++) {
        const ResistorNet& current = nets[net];
        size_t count = current.resistances.size();
        weights[net].assign(count, 0.0);
        for (size_t n = 0; n < count; n++) {
            double r0 = (current.pulldown == 0) ? 1.0 / 1e12 : 1.0 / current.pulldown;
            double r1 = (current.pullup == 0) ? 1.0 / 1e12 : 1.0 / current.pullup;
            for (size_t j = 0; j < count; j++) {
                double resistance = current.resistances[j];
                if (resistance == 0.0) continue;
                if (j == n) {
                    r1 += 1.0 / resistance;
                } else {
                    r0 += 1.0 / resistance;
                }
            }
            r0 = 1.0 / r0;
            r1 = 1.0 / r1;
            double vout = (max_value - min_value) * r0 / (r1 + r0) + min_value;
            weights[net][n] = std::clamp(vout, min_value, max_value);
            max_out[net] += weights[net][n];
        }
    }

    double scale = scaler;
    if (scaler < 0.0) {
        double max = 0.0;
        for (double value : max_out) max = std::max(max, value);
        scale = (max != 0.0) ? max_value / max : 0.0;
    }
    for (auto& net_weights : weights) {
        for (double& weight : net_weights) weight *= scale;
    }
    return weights;
}

int combine_weights(const std::vector<double>& weights, const std::vector<int>& bits) {
    double result = 0.5;
    for (size_t i = 0; i < bits.size() && i < weights.size(); i++) {
        result += weights[i] * bits[i];
    }
    return int(std::min(result, 255.0));
}

}  // namespace dsp
