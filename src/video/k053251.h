#pragma once

#include <array>
#include <cstdint>

namespace dsp {

// K053251 priority encoder / palette base (Konami)
class K053251 {
public:
    static constexpr int CI0 = 0;
    static constexpr int CI1 = 1;
    static constexpr int CI2 = 2;
    static constexpr int CI3 = 3;
    static constexpr int CI4 = 4;

    K053251() { reset(); }

    void reset();
    void write(uint8_t offset, uint8_t value);

    uint8_t get_priority(int ci) const { return ram_[std::size_t(ci & 0xf)]; }
    uint8_t get_palette_index(int ci) const {
        return palette_index_[std::size_t(ci % 5)];
    }

    bool dirty_tmap(int ci) const { return dirty_tmap_[std::size_t(ci % 5)]; }
    void clear_dirty_tmap(int ci) { dirty_tmap_[std::size_t(ci % 5)] = false; }

    // Sort 3 layers by priority (ascending pri = drawn first / behind)
    static void sort_layers3(uint8_t layer[3], uint8_t pri[3]);

private:
    void reset_indexes();

    std::array<uint8_t, 16> ram_{};
    std::array<uint8_t, 5> palette_index_{};
    std::array<bool, 5> dirty_tmap_{};
};

}  // namespace dsp
