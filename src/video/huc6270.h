#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <utility>

namespace dsp {

// Hudson HuC6270 VDC: the PC Engine's video display controller. It owns the
// 64 KiB (32K word) VRAM, one scrolling tilemap, 64 sprites and the raster
// compare / vblank / sprite interrupts. Rendering is per scanline into a
// buffer of HuC6260 palette indices, so the VCE owns the colours.
class HuC6270 {
public:
    static constexpr int kVramWords = 0x8000;
    static constexpr int kSprites = 64;
    static constexpr int kSpritesPerLine = 16;
    static constexpr int kMaxWidth = 512;

    // Status register bits, as read from port 0.
    static constexpr uint8_t kStatusCollision = 0x01;
    static constexpr uint8_t kStatusOverflow = 0x02;
    static constexpr uint8_t kStatusRaster = 0x04;
    static constexpr uint8_t kStatusSatbDone = 0x08;
    static constexpr uint8_t kStatusDmaDone = 0x10;
    static constexpr uint8_t kStatusVblank = 0x20;

    void reset();

    // IRQ1 towards the HuC6280.
    void set_irq_handler(std::function<void(bool)> handler) { irq_handler_ = std::move(handler); }

    // $0000-$0003 in the hardware page.
    uint8_t read(uint8_t offset);
    void write(uint8_t offset, uint8_t value);

    // Frame loop. `line` counts from the start of the frame; the driver calls
    // this once per scanline and blits `out` (palette indices) when it returns
    // true, meaning the line belongs to the active display.
    bool scanline(int line, uint16_t* out, int width);
    void end_frame();

    int display_start() const { return (vpr_ & 0x1f) + 1 + ((vpr_ >> 8) & 0xff) + 2; }
    int display_height() const { return (vdw_ & 0x1ff) + 1; }
    // Visible pixels per line, from the HDW field of HDR.
    int display_width() const { return ((hdr_ & 0x7f) + 1) * 8; }
    // Line of the active display drawn by the last scanline() that returned true.
    int display_line() const { return display_line_; }

    uint16_t vram(int address) const { return vram_[size_t(address) & (kVramWords - 1)]; }
    void set_vram(int address, uint16_t value) {
        vram_[size_t(address) & (kVramWords - 1)] = value;
    }
    uint16_t sat(int index) const { return sat_[size_t(index) & 0xff]; }
    uint8_t status() const { return status_; }
    uint16_t reg(int index) const { return regs_[size_t(index) & 0x1f]; }

private:
    void register_w(int index, uint16_t value, bool high_byte);
    void raise_irq(uint8_t flag);
    void update_irq();
    void vram_dma();
    void satb_dma();
    void render_background(uint16_t* out, int width);
    void render_sprites(int display_line, uint16_t* out, int width);
    uint16_t increment() const;

    std::array<uint16_t, kVramWords> vram_{};
    std::array<uint16_t, 0x100> sat_{};  // 64 sprites x 4 words
    std::array<uint16_t, 0x20> regs_{};

    uint8_t reg_index_ = 0;
    uint8_t write_latch_ = 0;
    uint8_t status_ = 0;
    bool irq_asserted_ = false;

    uint16_t mawr_ = 0, marr_ = 0, cr_ = 0, rcr_ = 0, bxr_ = 0, byr_ = 0, mwr_ = 0;
    uint16_t vpr_ = 0, vdw_ = 0, vcr_ = 0, dcr_ = 0;
    uint16_t hsr_ = 0, hdr_ = 0;
    uint16_t sour_ = 0, desr_ = 0, lenr_ = 0, dvssr_ = 0;
    uint16_t bg_y_ = 0;
    int display_line_ = 0;
    bool satb_pending_ = false;

    std::array<uint16_t, kMaxWidth> sprite_line_{};
    std::array<uint8_t, kMaxWidth> sprite_front_{};

    std::function<void(bool)> irq_handler_;
};

}  // namespace dsp
