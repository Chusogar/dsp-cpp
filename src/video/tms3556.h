#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace dsp {

// Texas Instruments TMS3556 Video Display Processor, used by the Exelvision
// EXL-100 and EXELTEL. Behaviour follows MAME's tms3556 (Raphael Nabet, 2004):
// register/VRAM ports, text 40×25, bitmap 320×250 and mixed modes, 8 colours.
class Tms3556 {
public:
    static constexpr int kTopBorder = 1;
    static constexpr int kBottomBorder = 1;
    static constexpr int kLeftBorder = 8;
    static constexpr int kRightBorder = 8;
    static constexpr int kActiveWidth = 320;
    static constexpr int kActiveHeight = 250;
    static constexpr int kTotalWidth = kActiveWidth + kLeftBorder + kRightBorder;
    static constexpr int kTotalHeight = kActiveHeight + kTopBorder + kBottomBorder;
    static constexpr int kScanlines = 313;

    Tms3556();

    void reset();

    uint8_t vram_r();
    void vram_w(uint8_t data);
    uint8_t reg_r();
    void reg_w(uint8_t data);
    uint8_t initptr_r();

    // Advance one PAL scanline: draw when inside the visible window.
    void interrupt();

    // ARGB8888 framebuffer, kTotalWidth * kTotalHeight.
    const uint32_t* framebuffer() const { return framebuffer_.data(); }
    int width() const { return kTotalWidth; }
    int height() const { return kTotalHeight; }

    uint8_t* vram() { return vram_.data(); }
    const uint8_t* vram() const { return vram_.data(); }
    uint8_t control_reg(int index) const {
        return (index >= 0 && index < 8) ? control_regs_[size_t(index)] : 0;
    }

    static uint32_t rgb3(uint8_t color);

private:
    enum class DmaMode : uint8_t { Read, Write };

    static constexpr uint8_t kModeOff = 0;
    static constexpr uint8_t kModeText = 1;
    static constexpr uint8_t kModeBitmap = 2;
    static constexpr uint8_t kModeMixed = 3;

    uint8_t read_byte(uint16_t address) const { return vram_[address]; }
    void write_byte(uint16_t address, uint8_t data) { vram_[address] = data; }

    void draw_line_empty(uint32_t* ln);
    void draw_line_text_common(uint32_t* ln);
    void draw_line_bitmap_common(uint32_t* ln);
    void draw_line_text(uint32_t* ln);
    void draw_line_bitmap(uint32_t* ln);
    void draw_line_mixed(uint32_t* ln);
    void draw_line(int line);
    void interrupt_start_vblank();

    std::array<uint8_t, 8> control_regs_{};
    std::array<uint16_t, 8> address_regs_{};
    std::vector<uint8_t> vram_;
    std::vector<uint32_t> framebuffer_;

    uint8_t reg_ = 0;
    uint8_t reg2_ = 0;
    int row_col_written_ = 0;
    int bamp_written_ = 0;
    uint16_t colrow_ = 0;
    DmaMode acmpxy_mode_ = DmaMode::Write;
    uint16_t acmpxy_ = 0;
    uint16_t acmp_ = 0;
    int init_read_ = 0;

    int scanline_ = 0;
    int blink_ = 0;
    int blink_count_ = 0;
    int bg_color_ = 0;
    int name_offset_ = 0;
    int cg_flag_ = 0;
    int char_line_counter_ = 0;
    int dbl_h_phase_[40] = {};
};

}  // namespace dsp
