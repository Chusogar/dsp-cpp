#include "video/mos6566.h"

#include <cstring>

namespace dsp {
namespace {

const int kPaletteRgb[16] = {
    0x000000, 0xFDFEFC, 0xBE1A24, 0x30E6C6, 0xB41AE2, 0x1FD21E, 0x211BAE, 0xDFF60A,
    0xB84104, 0x6A3304, 0xFE4A57, 0x424540, 0x70746F, 0x59FE59, 0x5F53FE, 0xA4A7A2,
};

}  // namespace

uint32_t Mos6566::palette_color(int index) {
    const int rgb = kPaletteRgb[index & 15];
    return 0xff000000u | uint32_t(rgb);
}

Mos6566::Mos6566(uint32_t clock) {
    (void)clock;
    for (int i = 0; i < 16; i++) palette_[size_t(i)] = palette_color(i);
    for (int v = 0; v < 256; v++) {
        uint16_t exp = 0;
        uint16_t multi = 0;
        for (int b = 0; b < 8; b++) {
            if (v & (1 << b)) exp = uint16_t(exp | (3u << (b * 2)));
        }
        for (int p = 0; p < 4; p++) {
            const uint8_t pix = uint8_t((v >> (p * 2)) & 3);
            multi = uint16_t(multi | (uint16_t(pix | (pix << 2)) << (p * 4)));
        }
        exp_table_[size_t(v)] = exp;
        multi_exp_table_[size_t(v)] = multi;
    }
    reset();
}

void Mos6566::reset() {
    linea_ = 0;
    irq_raster_ = 0;
    rc_ = 7;
    vc_ = 0;
    vc_base_ = 0;
    x_scroll_ = 0;
    y_scroll_ = 0;
    cia_vabase_ = 0;
    for (int i = 0; i < 8; i++) {
        mx_[size_t(i)] = 0;
        my_[size_t(i)] = 0;
        sc_[size_t(i)] = 0;
        spr_color_[size_t(i)] = 0;
        mc_[size_t(i)] = 63;
    }
    mc_color_lookup_.fill(0);
    fore_mask_buf_.fill(0);
    ctrl1_ = 0;
    ctrl2_ = 0;
    lpx_ = 0;
    lpy_ = 0;
    sprite_on_ = 0;
    me_ = 0;
    mxe_ = 0;
    mye_ = 0;
    mdp_ = 0;
    mmc_ = 0;
    vbase_ = 0;
    irq_flag_ = 0;
    irq_mask_ = 0;
    clx_spr_ = 0;
    clx_bgr_ = 0;
    ec_ = 0;
    mm0_ = 0;
    mm1_ = 0;
    display_idx_ = 0;
    display_state_ = false;
    border_on_ = false;
    border_40_col_ = false;
    bad_lines_enabled_ = false;
    lp_triggered_ = false;
    row25_ = false;
    matrix_off_ = 0;
    char_off_ = 0;
    bitmap_off_ = 0;
    mm0_color_ = 0;
    mm1_color_ = 0;
    matrix_line_.fill(0);
    color_line_.fill(0);
    line_.fill(palette_[0]);
}

void Mos6566::changed_va(uint16_t new_va) {
    cia_vabase_ = uint16_t(new_va << 14);
    write(0x18, vbase_);
}

uint8_t Mos6566::vic_read(uint16_t address) const {
    const uint16_t va = uint16_t(address | cia_vabase_);
    if (((va & 0xf000) == 0x9000) || ((va & 0xf000) == 0x1000)) {
        return chargen_ ? chargen_[va & 0xfff] : uint8_t(0);
    }
    return ram_ ? ram_[va] : uint8_t(0);
}

uint8_t Mos6566::read(uint8_t address) {
    uint8_t ret = 0xff;
    switch (address) {
        case 0x00:
        case 0x02:
        case 0x04:
        case 0x06:
        case 0x08:
        case 0x0a:
        case 0x0c:
        case 0x0e:
            ret = uint8_t(mx_[address >> 1]);
            break;
        case 0x01:
        case 0x03:
        case 0x05:
        case 0x07:
        case 0x09:
        case 0x0b:
        case 0x0d:
        case 0x0f:
            ret = my_[address >> 1];
            break;
        case 0x10:
            ret = mx8_;
            break;
        case 0x11:
            ret = uint8_t((ctrl1_ & 0x7f) | ((linea_ & 0x100) >> 1));
            break;
        case 0x12:
            ret = uint8_t(linea_);
            break;
        case 0x13:
            ret = lpx_;
            break;
        case 0x14:
            ret = lpy_;
            break;
        case 0x15:
            ret = me_;
            break;
        case 0x16:
            ret = uint8_t(ctrl2_ | 0xc0);
            break;
        case 0x17:
            ret = mye_;
            break;
        case 0x18:
            ret = uint8_t(vbase_ | 0x01);
            break;
        case 0x19:
            ret = uint8_t(irq_flag_ | 0x70);
            break;
        case 0x1a:
            ret = uint8_t(irq_mask_ | 0xf0);
            break;
        case 0x1b:
            ret = mdp_;
            break;
        case 0x1c:
            ret = mmc_;
            break;
        case 0x1d:
            ret = mxe_;
            break;
        case 0x1e:
            ret = clx_spr_;
            clx_spr_ = 0;
            break;
        case 0x1f:
            ret = clx_bgr_;
            clx_bgr_ = 0;
            break;
        case 0x20:
            ret = uint8_t(ec_ | 0xf0);
            break;
        case 0x21:
            ret = uint8_t(mc_color_lookup_[0] | 0xf0);
            break;
        case 0x22:
            ret = uint8_t(mc_color_lookup_[1] | 0xf0);
            break;
        case 0x23:
            ret = uint8_t(mc_color_lookup_[2] | 0xf0);
            break;
        case 0x24:
            ret = uint8_t(mc_color_lookup_[3] | 0xf0);
            break;
        case 0x25:
            ret = uint8_t(mm0_ | 0xf0);
            break;
        case 0x26:
            ret = uint8_t(mm1_ | 0xf0);
            break;
        case 0x27:
        case 0x28:
        case 0x29:
        case 0x2a:
        case 0x2b:
        case 0x2c:
        case 0x2d:
        case 0x2e:
            ret = uint8_t(sc_[address - 0x27] | 0xf0);
            break;
        default:
            break;
    }
    return ret;
}

void Mos6566::raster_irq() {
    irq_flag_ = uint8_t(irq_flag_ | 0x01);
    if (irq_mask_ & 0x01) {
        irq_flag_ = uint8_t(irq_flag_ | 0x80);
        if (irq_call_) irq_call_(IrqLine::Assert);
    }
}

void Mos6566::write(uint8_t address, uint8_t value) {
    switch (address) {
        case 0x00:
        case 0x02:
        case 0x04:
        case 0x06:
        case 0x08:
        case 0x0a:
        case 0x0c:
        case 0x0e:
            mx_[address >> 1] = uint16_t((mx_[address >> 1] & 0xff00) | value);
            break;
        case 0x01:
        case 0x03:
        case 0x05:
        case 0x07:
        case 0x09:
        case 0x0b:
        case 0x0d:
        case 0x0f:
            my_[address >> 1] = value;
            break;
        case 0x10: {
            mx8_ = value;
            uint16_t bit = 1;
            for (int i = 0; i < 8; i++) {
                if (mx8_ & bit) {
                    mx_[size_t(i)] = uint16_t(mx_[size_t(i)] | 0x100);
                } else {
                    mx_[size_t(i)] = uint16_t(mx_[size_t(i)] & 0xff);
                }
                bit = uint16_t(bit << 1);
            }
            break;
        }
        case 0x11: {
            ctrl1_ = value;
            y_scroll_ = uint8_t(value & 7);
            const uint16_t new_irq_raster = uint16_t((irq_raster_ & 0xff) | ((value & 0x80) << 1));
            if ((irq_raster_ != new_irq_raster) && (linea_ == new_irq_raster)) raster_irq();
            irq_raster_ = new_irq_raster;
            row25_ = (value & 8) != 0;
            display_idx_ = uint8_t(((ctrl1_ & 0x60) | (ctrl2_ & 0x10)) >> 4);
            break;
        }
        case 0x12: {
            const uint16_t new_irq_raster = uint16_t((irq_raster_ & 0xff00) | value);
            if ((irq_raster_ != new_irq_raster) && (linea_ == new_irq_raster)) raster_irq();
            irq_raster_ = new_irq_raster;
            break;
        }
        case 0x15:
            me_ = value;
            break;
        case 0x16:
            ctrl2_ = value;
            x_scroll_ = uint8_t(value & 7);
            border_40_col_ = (value & 8) != 0;
            display_idx_ = uint8_t(((ctrl1_ & 0x60) | (ctrl2_ & 0x10)) >> 4);
            break;
        case 0x17:
            mye_ = value;
            break;
        case 0x18:
            vbase_ = value;
            matrix_off_ = uint16_t((value & 0xf0) << 6);
            char_off_ = uint16_t((value & 0x0e) << 10);
            bitmap_off_ = uint16_t((value & 0x08) << 10);
            break;
        case 0x19:
            irq_flag_ = uint8_t(irq_flag_ & (~value & 0x0f));
            if (irq_call_) irq_call_(IrqLine::Clear);
            if (irq_flag_ & irq_mask_) irq_flag_ = uint8_t(irq_flag_ | 0x80);
            break;
        case 0x1a:
            irq_mask_ = uint8_t(value & 0x0f);
            if (irq_flag_ & irq_mask_) {
                irq_flag_ = uint8_t(irq_flag_ | 0x80);
                if (irq_call_) irq_call_(IrqLine::Assert);
            } else {
                irq_flag_ = uint8_t(irq_flag_ & 0x7f);
                if (irq_call_) irq_call_(IrqLine::Clear);
            }
            break;
        case 0x1b:
            mdp_ = value;
            break;
        case 0x1c:
            mmc_ = value;
            break;
        case 0x1d:
            mxe_ = value;
            break;
        case 0x20:
            ec_ = uint8_t(value & 0x0f);
            break;
        case 0x21:
            mc_color_lookup_[0] = uint8_t(value & 0x0f);
            break;
        case 0x22:
            mc_color_lookup_[1] = uint8_t(value & 0x0f);
            break;
        case 0x23:
            mc_color_lookup_[2] = uint8_t(value & 0x0f);
            break;
        case 0x24:
            mc_color_lookup_[3] = uint8_t(value & 0x0f);
            break;
        case 0x25:
            mm0_ = value;
            mm0_color_ = uint8_t(value & 0x0f);
            break;
        case 0x26:
            mm1_ = value;
            mm1_color_ = uint8_t(value & 0x0f);
            break;
        case 0x27:
        case 0x28:
        case 0x29:
        case 0x2a:
        case 0x2b:
        case 0x2c:
        case 0x2d:
        case 0x2e:
            sc_[address - 0x27] = value;
            spr_color_[address - 0x27] = uint8_t(value & 0x0f);
            break;
        default:
            break;
    }
}

void Mos6566::vblank() {
    vc_base_ = 0;
    lp_triggered_ = false;
    if (vblank_cb_) vblank_cb_();
}

void Mos6566::draw_sprites() {
    uint8_t spr_coll = 0;
    uint8_t gfx_coll = 0;
    uint8_t sbit = 1;
    for (int snum = 0; snum < 8; snum++) {
        if ((sprite_on_ & sbit) && (mx_[size_t(snum)] < (kDisplayX - 32))) {
            int ptemp = int(mx_[size_t(snum)] + 8);
            const uint8_t coll_pos = uint8_t(mx_[size_t(snum)] + 8);
            const uint8_t ptr = vic_read(uint16_t(matrix_off_ + 0x3f8 + snum));
            const uint16_t data_addr = uint16_t((uint16_t(ptr) << 6) | mc_[size_t(snum)]);
            uint32_t sdata = (uint32_t(vic_read(data_addr)) << 24) |
                             (uint32_t(vic_read(uint16_t(data_addr + 1))) << 16) |
                             (uint32_t(vic_read(uint16_t(data_addr + 2))) << 8);
            const uint8_t color = uint8_t(spr_color_[size_t(snum)] & 0x0f);
            const uint8_t spr_mask_pos = uint8_t(coll_pos - x_scroll_);
            const uint8_t ptempb = uint8_t(spr_mask_pos / 8);
            const uint8_t sshift = uint8_t(spr_mask_pos & 7);
            auto mask_at = [&](int index) -> uint8_t {
                if (index < 0 || index >= int(fore_mask_buf_.size())) return 0;
                return fore_mask_buf_[size_t(index)];
            };
            uint32_t fore_mask = (uint32_t(mask_at(ptempb)) << 24) | (uint32_t(mask_at(ptempb + 1)) << 16) |
                                 (uint32_t(mask_at(ptempb + 2)) << 8) | (uint32_t(mask_at(ptempb + 3)) << sshift) |
                                 (uint32_t(mask_at(ptempb + 4)) >> (8 - sshift));
            auto coll_hit = [&](int f) -> uint8_t {
                const int idx = int(coll_pos) + f;
                if (idx < 0 || idx >= int(spr_coll_buf_.size())) return 0;
                return spr_coll_buf_[size_t(idx)];
            };
            auto coll_set = [&](int f, uint8_t bit) {
                const int idx = int(coll_pos) + f;
                if (idx >= 0 && idx < int(spr_coll_buf_.size())) spr_coll_buf_[size_t(idx)] = bit;
            };

            if (mxe_ & sbit) {
                if (mx_[size_t(snum)] >= (kDisplayX - 56)) {
                    sbit = uint8_t(sbit << 1);
                    continue;
                }
                const uint32_t fore_mask_r =
                    (uint32_t(mask_at(ptempb + 4)) << 24) | (uint32_t(mask_at(ptempb + 5)) << 16) |
                    (uint32_t(mask_at(ptempb + 6)) << 8) | (uint32_t(mask_at(ptempb + 7)) << sshift) |
                    (uint32_t(mask_at(ptempb + 8)) >> (8 - sshift));
                if (mmc_ & sbit) {
                    uint32_t sdata_l = (uint32_t(multi_exp_table_[(sdata >> 24) & 0xff]) << 16) |
                                       uint32_t(multi_exp_table_[(sdata >> 16) & 0xff]);
                    uint32_t sdata_r = uint32_t(multi_exp_table_[(sdata >> 8) & 0xff]) << 16;
                    uint32_t plane0_l = (sdata_l & 0x55555555u) | ((sdata_l & 0x55555555u) << 1);
                    uint32_t plane1_l = (sdata_l & 0xaaaaaaaau) | ((sdata_l & 0xaaaaaaaau) >> 1);
                    uint32_t plane0_r = (sdata_r & 0x55555555u) | ((sdata_r & 0x55555555u) << 1);
                    uint32_t plane1_r = (sdata_r & 0xaaaaaaaau) | ((sdata_r & 0xaaaaaaaau) >> 1);
                    if ((fore_mask & (plane0_l | plane1_l)) || (fore_mask_r & (plane0_r | plane1_r))) {
                        gfx_coll = uint8_t(gfx_coll | sbit);
                        if (mdp_ & sbit) {
                            plane0_l &= ~fore_mask;
                            plane1_l &= ~fore_mask;
                            plane0_r &= ~fore_mask_r;
                            plane1_r &= ~fore_mask_r;
                        }
                    }
                    for (int f = 0; f < 32; f++) {
                        uint8_t col = 0;
                        bool draw = true;
                        if (plane1_l & 0x80000000u) {
                            col = (plane0_l & 0x80000000u) ? mm1_ : color;
                        } else if (plane0_l & 0x80000000u) {
                            col = mm0_;
                        } else {
                            draw = false;
                        }
                        if (draw) {
                            const int x = ptemp;
                            if (x >= 0 && x < kLineWidth) {
                                if (coll_hit(f) != 0) {
                                    spr_coll = uint8_t(spr_coll | coll_hit(f) | sbit);
                                } else {
                                    line_[size_t(x)] = palette_[col & 15];
                                    coll_set(f, sbit);
                                }
                            }
                        }
                        ptemp++;
                        plane0_l <<= 1;
                        plane1_l <<= 1;
                    }
                    for (int f = 32; f < 48; f++) {
                        uint8_t col = 0;
                        bool draw = true;
                        if (plane1_r & 0x80000000u) {
                            col = (plane0_r & 0x80000000u) ? mm1_ : color;
                        } else if (plane0_r & 0x80000000u) {
                            col = mm0_;
                        } else {
                            draw = false;
                        }
                        if (draw) {
                            const int x = ptemp;
                            if (x >= 0 && x < kLineWidth) {
                                if (coll_hit(f) != 0) {
                                    spr_coll = uint8_t(spr_coll | coll_hit(f) | sbit);
                                } else {
                                    line_[size_t(x)] = palette_[col & 15];
                                    coll_set(f, sbit);
                                }
                            }
                        }
                        ptemp++;
                        plane0_r <<= 1;
                        plane1_r <<= 1;
                    }
                } else {
                    uint32_t sdata_l = (uint32_t(exp_table_[(sdata >> 24) & 0xff]) << 16) |
                                       uint32_t(exp_table_[(sdata >> 16) & 0xff]);
                    uint32_t sdata_r = uint32_t(exp_table_[(sdata >> 8) & 0xff]) << 16;
                    if ((fore_mask & sdata_l) || (fore_mask_r & sdata_r)) {
                        gfx_coll = uint8_t(gfx_coll | sbit);
                        if (mdp_ & sbit) {
                            sdata_l &= ~fore_mask;
                            sdata_r &= ~fore_mask_r;
                        }
                    }
                    for (int f = 0; f < 32; f++) {
                        if (sdata_l & 0x80000000u) {
                            const int x = ptemp;
                            if (x >= 0 && x < kLineWidth) {
                                if (coll_hit(f) != 0) {
                                    spr_coll = uint8_t(spr_coll | coll_hit(f) | sbit);
                                } else {
                                    line_[size_t(x)] = palette_[color];
                                    coll_set(f, sbit);
                                }
                            }
                        }
                        ptemp++;
                        sdata_l <<= 1;
                    }
                    for (int f = 32; f < 48; f++) {
                        if (sdata_r & 0x80000000u) {
                            const int x = ptemp;
                            if (x >= 0 && x < kLineWidth) {
                                if (coll_hit(f) != 0) {
                                    spr_coll = uint8_t(spr_coll | coll_hit(f) | sbit);
                                } else {
                                    line_[size_t(x)] = palette_[color];
                                    coll_set(f, sbit);
                                }
                            }
                        }
                        ptemp++;
                        sdata_r <<= 1;
                    }
                }
            } else {
                if (mmc_ & sbit) {
                    uint32_t plane0_l = (sdata & 0x55555555u) | ((sdata & 0x55555555u) << 1);
                    uint32_t plane1_l = (sdata & 0xaaaaaaaau) | ((sdata & 0xaaaaaaaau) >> 1);
                    if (fore_mask & (plane0_l | plane1_l)) {
                        gfx_coll = uint8_t(gfx_coll | sbit);
                        if (mdp_ & sbit) {
                            plane0_l &= ~fore_mask;
                            plane1_l &= ~fore_mask;
                        }
                    }
                    for (int f = 0; f < 24; f++) {
                        uint8_t col = 0;
                        bool draw = true;
                        if (plane1_l & 0x80000000u) {
                            col = (plane0_l & 0x80000000u) ? mm1_ : color;
                        } else if (plane0_l & 0x80000000u) {
                            col = mm0_;
                        } else {
                            draw = false;
                        }
                        if (draw) {
                            const int x = ptemp;
                            if (x >= 0 && x < kLineWidth) {
                                if (coll_hit(f) != 0) {
                                    spr_coll = uint8_t(coll_hit(f) | sbit);
                                } else {
                                    line_[size_t(x)] = palette_[col & 15];
                                    coll_set(f, sbit);
                                }
                            }
                        }
                        plane0_l <<= 1;
                        plane1_l <<= 1;
                        ptemp++;
                    }
                } else {
                    for (int f = 0; f < 24; f++) {
                        if (sdata & 0x80000000u) {
                            const int x = ptemp;
                            if (x >= 0 && x < kLineWidth) {
                                if (coll_hit(f) != 0) {
                                    spr_coll = uint8_t(spr_coll | coll_hit(f) | sbit);
                                } else {
                                    line_[size_t(x)] = palette_[color];
                                    coll_set(f, sbit);
                                }
                            }
                        }
                        ptemp++;
                        sdata <<= 1;
                    }
                }
            }
        }
        if (clx_spr_ != 0) {
            clx_spr_ = uint8_t(clx_spr_ | spr_coll);
        } else {
            clx_spr_ = uint8_t(clx_spr_ | spr_coll);
            irq_flag_ = uint8_t(irq_flag_ | 0x04);
            if (irq_mask_ & 0x04) {
                irq_flag_ = uint8_t(irq_flag_ | 0x80);
                if (irq_call_) irq_call_(IrqLine::Assert);
            }
        }
        if (clx_bgr_ != 0) {
            clx_bgr_ = uint8_t(clx_bgr_ | gfx_coll);
        } else {
            clx_bgr_ = uint8_t(clx_bgr_ | gfx_coll);
            irq_flag_ = uint8_t(irq_flag_ | 0x02);
            if (irq_mask_ & 0x02) {
                irq_flag_ = uint8_t(irq_flag_ | 0x80);
                if (irq_call_) irq_call_(IrqLine::Assert);
            }
        }
        sbit = uint8_t(sbit << 1);
    }
}

uint8_t Mos6566::update_mc(uint16_t line) {
    uint8_t spron = sprite_on_;
    const uint8_t spren = me_;
    const uint8_t sprye = mye_;
    uint8_t cycles_used = 0;
    uint8_t j = 1;
    for (int i = 0; i < 8; i++) {
        if (spren & j) {
            if (my_[size_t(i)] == (line & 0xff)) {
                mc_[size_t(i)] = 0;
                spron = uint8_t(spron | j);
            } else if (mc_[size_t(i)] != 63) {
                if (sprye & j) {
                    if (((my_[size_t(i)] ^ (line & 0xff)) & 1) == 0) {
                        mc_[size_t(i)] = uint16_t(mc_[size_t(i)] + 3);
                        cycles_used = uint8_t(cycles_used + 2);
                        if (mc_[size_t(i)] == 63) spron = uint8_t(spron & uint8_t(~j));
                    }
                } else {
                    mc_[size_t(i)] = uint16_t(mc_[size_t(i)] + 3);
                    cycles_used = uint8_t(cycles_used + 2);
                    if (mc_[size_t(i)] == 63) spron = uint8_t(spron & uint8_t(~j));
                }
            }
        }
        j = uint8_t(j << 1);
    }
    sprite_on_ = spron;
    return cycles_used;
}

void Mos6566::paint_line() {
    int p = 32;
    auto put = [&](uint32_t color) {
        if (p >= 0 && p < kLineWidth) line_[size_t(p)] = color;
        p++;
    };
    if (display_state_) {
        for (int i = 0; i < x_scroll_; i++) put(palette_[mc_color_lookup_[0]]);
        switch (display_idx_) {
            case 0:
                for (int f = 0; f < 40; f++) {
                    const uint8_t color = uint8_t(color_line_[size_t(f)] & 0x0f);
                    uint8_t data = vic_read(uint16_t(char_off_ + (uint16_t(matrix_line_[size_t(f)]) << 3) + rc_));
                    fore_mask_buf_[size_t(f + 4)] = data;
                    for (int h = 0; h < 8; h++) {
                        put((data & 0x80) ? palette_[color] : palette_[mc_color_lookup_[0]]);
                        data = uint8_t(data << 1);
                    }
                }
                break;
            case 1:
                for (int f = 0; f < 40; f++) {
                    uint8_t data =
                        vic_read(uint16_t(char_off_ + rc_ + (uint16_t(matrix_line_[size_t(f)]) << 3)));
                    if (color_line_[size_t(f)] & 8) {
                        mc_color_lookup_[3] = uint8_t(color_line_[size_t(f)] & 7);
                        fore_mask_buf_[size_t(f + 4)] = uint8_t((data & 0xaa) | ((data & 0xaa) >> 1));
                        for (int s = 6; s >= 0; s -= 2) {
                            const uint8_t color = mc_color_lookup_[(data >> s) & 3];
                            put(palette_[color]);
                            put(palette_[color]);
                        }
                    } else {
                        const uint8_t color = uint8_t(color_line_[size_t(f)] & 0x0f);
                        fore_mask_buf_[size_t(f + 4)] = data;
                        for (int h = 0; h < 8; h++) {
                            put((data & 0x80) ? palette_[color] : palette_[mc_color_lookup_[0]]);
                            data = uint8_t(data << 1);
                        }
                    }
                }
                break;
            case 2: {
                uint16_t addr = uint16_t(bitmap_off_ + (vc_ << 3) + rc_);
                for (int f = 0; f < 40; f++) {
                    uint8_t data = vic_read(addr);
                    addr = uint16_t(addr + 8);
                    fore_mask_buf_[size_t(f + 4)] = data;
                    const uint8_t color = uint8_t(matrix_line_[size_t(f)] >> 4);
                    const uint8_t bcolor = uint8_t(matrix_line_[size_t(f)] & 0x0f);
                    for (int h = 0; h < 8; h++) {
                        put((data & 0x80) ? palette_[color] : palette_[bcolor]);
                        data = uint8_t(data << 1);
                    }
                }
                break;
            }
            case 3: {
                uint8_t lookup[4] = {mc_color_lookup_[0], 0, 0, 0};
                uint16_t addr = uint16_t(bitmap_off_ + (vc_ << 3) + rc_);
                for (int f = 0; f < 40; f++) {
                    lookup[1] = uint8_t(matrix_line_[size_t(f)] >> 4);
                    lookup[2] = uint8_t(matrix_line_[size_t(f)] & 0x0f);
                    lookup[3] = uint8_t(color_line_[size_t(f)] & 0x0f);
                    uint8_t data = vic_read(addr);
                    addr = uint16_t(addr + 8);
                    fore_mask_buf_[size_t(f + 4)] = uint8_t((data & 0xaa) | ((data & 0xaa) >> 1));
                    for (int s = 6; s >= 0; s -= 2) {
                        const uint8_t color = lookup[(data >> s) & 3];
                        put(palette_[color]);
                        put(palette_[color]);
                    }
                }
                break;
            }
            case 4:
                for (int f = 0; f < 40; f++) {
                    uint8_t data = matrix_line_[size_t(f)];
                    fore_mask_buf_[size_t(f + 4)] = data;
                    const uint8_t color = color_line_[size_t(f)];
                    const uint8_t bcolor = mc_color_lookup_[(data >> 6) & 3];
                    data = vic_read(uint16_t(char_off_ + rc_ + (uint16_t(data & 0x3f) << 3)));
                    for (int h = 0; h < 8; h++) {
                        put((data & 0x80) ? palette_[color & 15] : palette_[bcolor]);
                        data = uint8_t(data << 1);
                    }
                }
                break;
            default:
                for (int i = 0; i < 320; i++) put(palette_[0]);
                break;
        }
        vc_ = uint16_t(vc_ + 40);
    } else {
        switch (display_idx_) {
            case 0:
            case 1:
            case 4: {
                uint8_t data = vic_read(uint16_t((ctrl1_ & 0x40) ? 0x39ff : 0x3fff));
                for (int f = 0; f < 40; f++) {
                    fore_mask_buf_[size_t(f + 4)] = data;
                    uint8_t bits = data;
                    for (int h = 0; h < 8; h++) {
                        put((bits & 0x80) ? palette_[0] : palette_[mc_color_lookup_[0]]);
                        bits = uint8_t(bits << 1);
                    }
                }
                break;
            }
            case 3: {
                const uint8_t data = vic_read(0x3fff);
                const uint8_t lookup[4] = {mc_color_lookup_[0], 0, 0, 0};
                const uint8_t color = lookup[(data >> 6) & 3];
                const uint8_t color2 = lookup[(data >> 4) & 3];
                const uint8_t bcolor = lookup[(data >> 2) & 3];
                const uint8_t bcolor2 = lookup[(data >> 0) & 3];
                for (int f = 0; f < 40; f++) {
                    put(palette_[color]);
                    put(palette_[color]);
                    put(palette_[color2]);
                    put(palette_[color2]);
                    put(palette_[bcolor]);
                    put(palette_[bcolor]);
                    put(palette_[bcolor2]);
                    put(palette_[bcolor2]);
                    fore_mask_buf_[size_t(f + 4)] = data;
                }
                break;
            }
            default:
                for (int i = 0; i < 320; i++) put(palette_[0]);
                break;
        }
    }
    if (sprite_on_ != 0) {
        spr_coll_buf_.fill(0);
        draw_sprites();
    }
    if (border_40_col_) {
        for (int i = 0; i < 32; i++) line_[size_t(i)] = palette_[ec_];
        for (int i = 352; i < kLineWidth; i++) line_[size_t(i)] = palette_[ec_];
    } else {
        for (int i = 0; i < 32; i++) line_[size_t(i)] = palette_[ec_];
        for (int i = 336; i < kLineWidth; i++) line_[size_t(i)] = palette_[ec_];
    }
}

uint8_t Mos6566::update(uint16_t line) {
    linea_ = line;
    if (line == irq_raster_) raster_irq();
    if (line <= 15 || line >= 301) return kCyclesPerLine;
    if (line == 300) {
        vblank();
        return kCyclesPerLine;
    }
    uint8_t cycles_left = kCyclesPerLine;
    if (line == 0x30) bad_lines_enabled_ = (ctrl1_ & 0x10) != 0;
    if (line >= kFirstDispLine && line <= kLastDispLine) {
        vc_ = vc_base_;
        if ((line >= kFirstDmaLine) && (line <= kLastDmaLine) && ((line & 7) == y_scroll_) &&
            bad_lines_enabled_) {
            display_state_ = true;
            cycles_left = 23;
            rc_ = 0;
            for (int i = 0; i < 40; i++) {
                matrix_line_[size_t(i)] = vic_read(uint16_t(matrix_off_ + vc_ + i));
                color_line_[size_t(i)] =
                    color_ram_ ? uint8_t(color_ram_[(vc_ + uint16_t(i)) & 0x3ff] & 0x0f) : uint8_t(0);
            }
        }
        if ((line >= 16 && line <= 50) || (line >= 251 && line <= 299)) {
            line_.fill(palette_[ec_]);
        } else if ((line >= 51 && line <= 54) || (line >= 247 && line <= 250)) {
            if (row25_) {
                paint_line();
            } else {
                line_.fill(palette_[ec_]);
            }
        } else if (line >= 55 && line <= 246) {
            paint_line();
        }
        if (rc_ == 7) {
            display_state_ = false;
            vc_base_ = vc_;
        } else {
            rc_ = uint16_t(rc_ + 1);
        }
        if ((line >= kFirstDmaLine - 1) && (line <= kLastDmaLine - 1) &&
            ((((line + 1) & 7) == y_scroll_) && bad_lines_enabled_)) {
            rc_ = 0;
        }
    }
    if (me_ || sprite_on_) {
        const uint8_t used = update_mc(line);
        if (cycles_left > used) {
            cycles_left = uint8_t(cycles_left - used);
        } else {
            cycles_left = 0;
        }
    }
    return cycles_left;
}

}  // namespace dsp
