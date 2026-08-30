#include "video/k053251.h"

namespace dsp {

void K053251::reset_indexes() {
    palette_index_[0] = uint8_t(32 * ((ram_[9] >> 0) & 3));
    palette_index_[1] = uint8_t(32 * ((ram_[9] >> 2) & 3));
    palette_index_[2] = uint8_t(32 * ((ram_[9] >> 4) & 3));
    palette_index_[3] = uint8_t(16 * ((ram_[10] >> 0) & 7));
    palette_index_[4] = uint8_t(16 * ((ram_[10] >> 3) & 7));
}

void K053251::reset() {
    ram_.fill(0);
    dirty_tmap_.fill(false);
    reset_indexes();
}

void K053251::write(uint8_t offset, uint8_t value) {
    offset &= 0x0f;
    value &= 0x3f;
    if (ram_[offset] == value) return;
    ram_[offset] = value;
    if (offset == 9) {
        for (int i = 0; i < 3; i++) {
            const uint8_t newind = uint8_t(32 * ((value >> (2 * i)) & 3));
            if (palette_index_[std::size_t(i)] != newind) {
                palette_index_[std::size_t(i)] = newind;
                dirty_tmap_[std::size_t(i)] = true;
            }
        }
    } else if (offset == 10) {
        for (int i = 0; i < 2; i++) {
            const uint8_t newind = uint8_t(16 * ((value >> (3 * i)) & 7));
            if (palette_index_[std::size_t(3 + i)] != newind) {
                palette_index_[std::size_t(3 + i)] = newind;
                dirty_tmap_[std::size_t(3 + i)] = true;
            }
        }
    }
}

void K053251::sort_layers3(uint8_t layer[3], uint8_t pri[3]) {
    auto swap = [&](int a, int b) {
        if (pri[a] < pri[b]) {
            const uint8_t tp = pri[a];
            pri[a] = pri[b];
            pri[b] = tp;
            const uint8_t tl = layer[a];
            layer[a] = layer[b];
            layer[b] = tl;
        }
    };
    swap(0, 1);
    swap(0, 2);
    swap(1, 2);
}

}  // namespace dsp
