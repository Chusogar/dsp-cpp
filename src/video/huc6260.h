#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace dsp {

// Hudson HuC6260 VCE: the PC Engine's video colour encoder. It owns the 512
// entry colour table (9 bit GRB) and the dot clock, which decides how many
// pixels the HuC6270 draws on a scanline.
class HuC6260 {
public:
    static constexpr int kEntries = 512;

    void reset();

    // $0400-$0407 in the hardware page.
    uint8_t read(uint8_t offset);
    void write(uint8_t offset, uint8_t value);

    // Active pixels per scanline for the current dot clock: 256, 341 or 512.
    int active_width() const;

    // ARGB8888 colour of a palette entry. Index 0 of every background palette
    // is transparent and shows entry 0, exactly like the hardware.
    uint32_t colour(int index) const { return argb_[size_t(index & (kEntries - 1))]; }
    uint32_t backdrop() const { return argb_[0]; }

    uint16_t entry(int index) const { return table_[size_t(index & (kEntries - 1))]; }
    uint8_t control() const { return control_; }

private:
    void refresh(int index);

    std::array<uint16_t, kEntries> table_{};
    std::array<uint32_t, kEntries> argb_{};
    uint16_t address_ = 0;
    uint8_t control_ = 0;
    uint8_t latch_ = 0;
};

}  // namespace dsp
