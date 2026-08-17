#pragma once

#include <array>
#include <cstdint>
#include <functional>

#include "cpu/irq_line.h"

namespace dsp {

// MOS 6566/6569 VIC-II (PAL), ported from mos6566.pas.
class Mos6566 {
public:
    using IrqCallback = std::function<void(IrqLine)>;
    using VblankCallback = std::function<void()>;

    static constexpr int kLineWidth = 384;
    static constexpr int kCyclesPerLine = 63;
    static constexpr int kFirstDispLine = 0x10;
    static constexpr int kLastDispLine = 0x11c;
    static constexpr int kFirstDmaLine = 0x30;
    static constexpr int kLastDmaLine = 0xf7;
    static constexpr int kDisplayX = 0x195;

    explicit Mos6566(uint32_t clock);

    void set_irq_callback(IrqCallback cb) { irq_call_ = std::move(cb); }
    void set_vblank_callback(VblankCallback cb) { vblank_cb_ = std::move(cb); }
    void set_memory(uint8_t* ram, const uint8_t* chargen, const uint8_t* color_ram) {
        ram_ = ram;
        chargen_ = chargen;
        color_ram_ = color_ram;
    }

    void reset();
    uint8_t read(uint8_t address);
    void write(uint8_t address, uint8_t value);
    void changed_va(uint16_t new_va);

    // Runs one raster line. Returns leftover CPU cycles (63 minus bad-line/sprite DMA).
    uint8_t update(uint16_t line);

    const uint32_t* scanline() const { return line_.data(); }
    uint16_t raster_line() const { return linea_; }
    uint8_t vbase() const { return vbase_; }

    static uint32_t palette_color(int index);

private:
    uint8_t vic_read(uint16_t address) const;
    void raster_irq();
    void vblank();
    void draw_sprites();
    uint8_t update_mc(uint16_t line);
    void paint_line();

    uint16_t linea_ = 0;
    uint16_t irq_raster_ = 0;
    uint16_t rc_ = 7;
    uint16_t vc_ = 0;
    uint16_t vc_base_ = 0;
    uint8_t x_scroll_ = 0;
    uint8_t y_scroll_ = 0;
    uint16_t cia_vabase_ = 0;
    std::array<uint16_t, 8> mx_{};
    std::array<uint16_t, 8> mc_{};
    uint8_t mx8_ = 0;
    std::array<uint8_t, 8> my_{};
    std::array<uint8_t, 4> mc_color_lookup_{};
    uint8_t ctrl1_ = 0;
    uint8_t ctrl2_ = 0;
    uint8_t lpx_ = 0;
    uint8_t lpy_ = 0;
    uint8_t me_ = 0;
    uint8_t mxe_ = 0;
    uint8_t mye_ = 0;
    uint8_t mdp_ = 0;
    uint8_t mmc_ = 0;
    uint8_t sprite_on_ = 0;
    uint8_t irq_flag_ = 0;
    uint8_t irq_mask_ = 0;
    uint8_t clx_spr_ = 0;
    uint8_t clx_bgr_ = 0;
    uint8_t ec_ = 0;
    uint8_t mm0_ = 0;
    uint8_t mm1_ = 0;
    std::array<uint8_t, 8> sc_{};
    uint8_t display_idx_ = 0;
    bool display_state_ = false;
    bool border_on_ = false;
    bool border_40_col_ = false;
    bool bad_lines_enabled_ = false;
    bool lp_triggered_ = false;
    bool row25_ = false;
    uint8_t vbase_ = 0;
    uint16_t matrix_off_ = 0;
    uint16_t char_off_ = 0;
    uint16_t bitmap_off_ = 0;
    uint8_t mm0_color_ = 0;
    uint8_t mm1_color_ = 0;
    std::array<uint8_t, 8> spr_color_{};
    std::array<uint8_t, 0x180> spr_coll_buf_{};
    std::array<uint8_t, 0x30> fore_mask_buf_{};
    std::array<uint8_t, 40> matrix_line_{};
    std::array<uint8_t, 40> color_line_{};
    std::array<uint32_t, kLineWidth> line_{};
    std::array<uint32_t, 16> palette_{};
    std::array<uint16_t, 256> exp_table_{};
    std::array<uint16_t, 256> multi_exp_table_{};

    uint8_t* ram_ = nullptr;
    const uint8_t* chargen_ = nullptr;
    const uint8_t* color_ram_ = nullptr;
    IrqCallback irq_call_;
    VblankCallback vblank_cb_;
};

}  // namespace dsp
