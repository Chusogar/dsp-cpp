#pragma once

#include <array>
#include <cstdint>
#include <functional>

#include "cpu/irq_line.h"

namespace dsp {

// MOS 6569 VIC-II (PAL) — register model + text / multicolor text / Hires bitmap
// subset. Ported structurally from leniad/dsp-emulator mos6566.pas.
class Mos6566 {
public:
    static constexpr int kLines = 312;
    static constexpr int kCyclesPerLine = 63;
    static constexpr int kScreenWidth = 384;
    static constexpr int kScreenHeight = 270;
    static constexpr int kVisibleX = 48;   // left border start of active area
    static constexpr int kCharsX = 40;
    static constexpr int kCharsY = 25;

    using IrqHandler = std::function<void(IrqLine)>;
    using MemRead = std::function<uint8_t(uint16_t)>;  // 14-bit VIC bus

    explicit Mos6566(uint32_t clock = 985248);

    void set_irq_handler(IrqHandler h) { irq_ = std::move(h); }
    void set_mem_read(MemRead r) { mem_ = std::move(r); }
    void set_color_ram(const uint8_t* cr) { color_ram_ = cr; }

    void reset();
    uint8_t read(uint8_t reg);
    void write(uint8_t reg, uint8_t value);

    // CIA2 VA14/15 → bits 14-15 of VIC address.
    void changed_va(uint16_t va14_15);

    // Render one raster line (0..311). Returns cycles stolen for badline (0 or 40).
    int update_line(int line, uint32_t* fb_row /*kScreenWidth pixels or null*/);

    uint16_t raster_line() const { return linea_; }
    uint8_t border_color() const { return ec_; }

    static constexpr uint32_t kPalette[16] = {
        0xFF000000, 0xFFFFFFFF, 0xFF880000, 0xFFAAFFEE, 0xFFCC44CC, 0xFF00CC55,
        0xFF0000AA, 0xFFEEEE77, 0xFFDD8855, 0xFF664400, 0xFFFF7777, 0xFF333333,
        0xFF777777, 0xFFAAFF66, 0xFF0088FF, 0xFFBBBBBB,
    };

private:
    void raster_irq();
    uint8_t vic_read(uint16_t addr14) const;

    uint32_t clock_;
    IrqHandler irq_;
    MemRead mem_;
    const uint8_t* color_ram_ = nullptr;

    uint16_t linea_ = 0;
    uint16_t irq_raster_ = 0;
    uint8_t ctrl1_ = 0, ctrl2_ = 0;
    uint8_t irq_flag_ = 0, irq_mask_ = 0;
    uint8_t me_ = 0, mxe_ = 0, mye_ = 0, mdp_ = 0, mmc_ = 0;
    uint8_t ec_ = 0, b0c_ = 0, b1c_ = 0, b2c_ = 0, b3c_ = 0;
    uint8_t mm0_ = 0, mm1_ = 0;
    std::array<uint8_t, 8> mx_{}, my_{}, sc_{};
    uint8_t mx8_ = 0;
    uint8_t vbase_ = 0;
    uint16_t cia_va_ = 0;  // bits 14-15

    uint16_t vc_ = 0, vc_base_ = 0, rc_ = 0;
    bool bad_lines_ = false;
    uint8_t clx_spr_ = 0, clx_bgr_ = 0;
    std::array<uint8_t, 384> spr_coll_{};
    std::array<uint8_t, 384> fore_mask_{};
};

}  // namespace dsp
