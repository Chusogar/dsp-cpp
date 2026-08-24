#pragma once

#include <array>
#include <cstdint>
#include <functional>

namespace dsp {

// Hudson HuC6270 Video Display Controller.
// BG + sprites with priority, 16/line limit, RCR/VBlank IRQs, SATB DMA,
// display widths driven by HDR (256/320/512 via VCE dot clock).
class HuC6270 {
public:
    static constexpr int kVramWords = 0x8000;  // 64 KiB
    static constexpr int kSatWords = 256;
    static constexpr int kMaxWidth = 512;
    static constexpr int kMaxHeight = 256;
    static constexpr int kLinesPerFrame = 263;
    static constexpr int kSpritesPerLine = 16;

    using IrqHandler = std::function<void(bool assert)>;

    void set_irq_handler(IrqHandler h) { irq_ = std::move(h); }
    void set_sprite_limit(bool enabled) { sprite_limit_ = enabled; }

    void reset();

    void write(uint8_t port, uint8_t value);
    uint8_t read(uint8_t port);

    // Render one scanline into line_out[width]. Palette via vce_color(index).
    void run_line(int line, uint16_t* pixel_out, int width);

    bool vblank() const { return vblank_; }
    int display_width() const;
    int display_height() const;
    uint16_t status() const { return status_; }

    // SuperGrafx / debug
    uint16_t vram_read(uint16_t addr) const { return vram_[addr & (kVramWords - 1)]; }
    void vram_write(uint16_t addr, uint16_t value) { vram_[addr & (kVramWords - 1)] = value; }

private:
    enum R : int {
        MAWR = 0, MARR = 1, VWR = 2, VRR = 2,
        CR = 5, RCR = 6, BXR = 7, BYR = 8,
        MWR = 9, HSR = 10, HDR = 11, VPR = 12,
        VDW = 13, VCR = 14, DCR = 15, SOUR = 16,
        DESR = 17, LENR = 18, DVSSR = 19
    };

    void select_reg(uint8_t index);
    void write_data(uint16_t value);
    uint16_t read_data();
    void update_irq();
    void do_satb_dma();
    void fetch_sprites(int display_y);
    void render_line(int display_y, uint16_t* out, int width);

    uint16_t bg_pixel(int x, int y) const;
    uint16_t sprite_pixel(int x, int line_local, int& out_pri) const;

    std::array<uint16_t, kVramWords> vram_{};
    std::array<uint16_t, kSatWords> sat_{};
    std::array<uint16_t, 20> reg_{};

    uint8_t addr_reg_ = 0;
    uint16_t status_ = 0;
    uint16_t vram_read_buf_ = 0;
    bool have_low_ = false;
    uint8_t low_byte_ = 0;

    int bg_y_ = 0;
    bool vblank_ = false;
    bool sprite_limit_ = true;

    // Per-line sprite cache (up to 16)
    struct LineSprite {
        int x = 0;
        int width = 16;
        int height = 16;
        int pattern = 0;
        int palette = 0;
        int row = 0;  // row within sprite for this line
        bool priority = false;
        bool hflip = false;
    };
    std::array<LineSprite, kSpritesPerLine> line_spr_{};
    int line_spr_count_ = 0;

    IrqHandler irq_;
};

// SuperGrafx HuC6202 VPC — mixes two VDC pixel streams.
class HuC6202 {
public:
    void reset();
    void write(uint8_t port, uint8_t value);
    uint8_t read(uint8_t port);

    // Combine VDC1 + VDC2 9-bit palette indices into one index (0x200 = transparent).
    uint16_t mix(uint16_t p0, uint16_t p1, int x) const;

private:
    uint8_t priority_[2]{};
    uint16_t window_[2]{};
    uint8_t st_mode_ = 0;
};

}  // namespace dsp
