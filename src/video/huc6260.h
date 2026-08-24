#pragma once

#include <array>
#include <cstdint>

namespace dsp {

class HuC6260 {
public:
    static constexpr int kPaletteSize = 512;

    void reset();
    void write(uint8_t port, uint8_t value);
    uint8_t read(uint8_t port);

    uint32_t color(uint16_t index) const;
    // 0=5.37MHz→256px, 1=7.16MHz→320px, 2/3=10.7MHz→512px
    int clock_mode() const { return clock_mode_; }
    int display_width_for_mode() const {
        switch (clock_mode_) {
            case 1: return 320;
            case 2:
            case 3: return 512;
            default: return 256;
        }
    }

private:
    uint16_t addr_ = 0;
    int clock_mode_ = 0;
    std::array<uint16_t, kPaletteSize> palette_{};
};

}  // namespace dsp
