#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <vector>

namespace dsp {

// Sega Model 3 tile generator ("2D" layers): four scrolling 8x8 tile layers
// (A, A', B, B') with per-line scroll, a per-32-pixel mask choosing between
// each primary/alternate pair, 4- or 8-bit tiles, a 32768 entry palette and
// colour offset registers. Each layer can be placed below or above the 3D
// picture, so the layers are drawn into two ARGB surfaces (alpha 0 is
// transparent) that the board composes with the Real3D output.
//
// VRAM (0xF1000000-0xF111FFFF on the PowerPC bus) is a little-endian device:
// the byte array holds the bytes in the order the big-endian CPU writes
// them, and the generator reads 32-bit words from it in host (LE) order.
class Model3TileGen {
public:
    static constexpr int kWidth = 496;
    static constexpr int kHeight = 384;
    static constexpr uint32_t kVramSize = 0x120000;
    static constexpr uint32_t kPaletteBase = 0x100000;

    Model3TileGen();

    void reset();

    uint8_t* vram() { return vram_.data(); }
    const uint8_t* vram() const { return vram_.data(); }

    // Byte access to VRAM from the bus (offsets 0..0x11FFFF), keeping the
    // decoded palette in step with palette RAM writes.
    uint8_t read8(uint32_t offset) const { return vram_[offset % kVramSize]; }
    void write8(uint32_t offset, uint8_t value);

    uint32_t read_register(uint32_t reg) const { return regs_[(reg & 0xff) / 4]; }
    // `irq_ack` receives the bits written to the IRQ acknowledge register.
    void write_register(uint32_t reg, uint32_t value, const std::function<void(uint8_t)>& irq_ack);

    // Clears both surfaces (start of a new frame).
    void begin_frame();
    void draw_line(int line);

    const uint32_t* bottom() const { return surface_[0].data(); }
    const uint32_t* top() const { return surface_[1].data(); }

    uint32_t palette_raw(int index) const { return word(kPaletteBase + uint32_t(index) * 4); }

private:
    uint32_t word(uint32_t offset) const {
        uint32_t v;
        __builtin_memcpy(&v, &vram_[offset], 4);
        return v;
    }
    uint32_t colour(int bank, uint32_t data) const;
    void recompute_palette(int bank);

    std::vector<uint8_t> vram_;
    std::array<uint32_t, 64> regs_{};
    std::array<std::array<int, 3>, 2> offset_{};  // r, g, b colour offsets per bank
    std::array<std::vector<uint32_t>, 2> pal_;    // decoded ARGB per bank (layers A/A', B/B')
    std::array<std::vector<uint32_t>, 2> surface_;
};

}  // namespace dsp
