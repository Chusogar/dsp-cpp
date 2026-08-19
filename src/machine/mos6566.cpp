#include "machine/mos6566.h"

namespace dsp {

Mos6566::Mos6566(uint32_t /*clock*/) {}

void Mos6566::reset() {
    linea_ = 0;
    irq_raster_ = 0;
    ctrl1_ = ctrl2_ = 0;
    irq_flag_ = irq_mask_ = 0;
    me_ = mxe_ = mye_ = mdp_ = mmc_ = 0;
    ec_ = b0c_ = b1c_ = b2c_ = b3c_ = 0;
    mm0_ = mm1_ = 0;
    mx_.fill(0);
    my_.fill(0);
    sc_.fill(0);
    mx8_ = 0;
    vbase_ = 0;
    cia_va_ = 0;
    vc_ = vc_base_ = rc_ = 0;
    bad_lines_ = false;
    clx_spr_ = clx_bgr_ = 0;
    spr_coll_.fill(0);
    fore_mask_.fill(0);
}

void Mos6566::changed_va(uint16_t va14_15) {
    cia_va_ = uint16_t((va14_15 & 3) << 14);
}

uint8_t Mos6566::vic_read(uint16_t addr14) const {
    if (!mem_) return 0xFF;
    return mem_(uint16_t((addr14 & 0x3FFF) | cia_va_));
}

void Mos6566::raster_irq() {
    irq_flag_ = uint8_t(irq_flag_ | 0x01);
    if (irq_mask_ & 0x01) {
        irq_flag_ = uint8_t(irq_flag_ | 0x80);
        if (irq_) irq_(IrqLine::Assert);
    }
}

uint8_t Mos6566::read(uint8_t reg) {
    switch (reg & 0x3F) {
        case 0x00: case 0x02: case 0x04: case 0x06:
        case 0x08: case 0x0A: case 0x0C: case 0x0E:
            return mx_[(reg & 0x0E) >> 1];
        case 0x01: case 0x03: case 0x05: case 0x07:
        case 0x09: case 0x0B: case 0x0D: case 0x0F:
            return my_[reg >> 1];
        case 0x10:
            return mx8_;
        case 0x11:
            return uint8_t((ctrl1_ & 0x7F) | ((linea_ & 0x100) >> 1));
        case 0x12:
            return uint8_t(linea_ & 0xFF);
        case 0x15:
            return me_;
        case 0x16:
            return uint8_t(ctrl2_ | 0xC0);
        case 0x17:
            return mye_;
        case 0x18:
            return uint8_t(vbase_ | 1);
        case 0x19: {
            const uint8_t v = uint8_t(irq_flag_ | 0x70);
            return v;
        }
        case 0x1A:
            return uint8_t(irq_mask_ | 0xF0);
        case 0x1B:
            return mdp_;
        case 0x1C:
            return mmc_;
        case 0x1D:
            return mxe_;
        case 0x1E: {
            const uint8_t v = clx_spr_;
            clx_spr_ = 0;
            return v;
        }
        case 0x1F: {
            const uint8_t v = clx_bgr_;
            clx_bgr_ = 0;
            return v;
        }
        case 0x20:
            return uint8_t(ec_ | 0xF0);
        case 0x21:
            return uint8_t(b0c_ | 0xF0);
        case 0x22:
            return uint8_t(b1c_ | 0xF0);
        case 0x23:
            return uint8_t(b2c_ | 0xF0);
        case 0x24:
            return uint8_t(b3c_ | 0xF0);
        case 0x25:
            return uint8_t(mm0_ | 0xF0);
        case 0x26:
            return uint8_t(mm1_ | 0xF0);
        case 0x27: case 0x28: case 0x29: case 0x2A:
        case 0x2B: case 0x2C: case 0x2D: case 0x2E:
            return uint8_t(sc_[reg - 0x27] | 0xF0);
        default:
            return 0xFF;
    }
}

void Mos6566::write(uint8_t reg, uint8_t value) {
    switch (reg & 0x3F) {
        case 0x00: case 0x02: case 0x04: case 0x06:
        case 0x08: case 0x0A: case 0x0C: case 0x0E:
            mx_[(reg & 0x0E) >> 1] = value;
            break;
        case 0x01: case 0x03: case 0x05: case 0x07:
        case 0x09: case 0x0B: case 0x0D: case 0x0F:
            my_[reg >> 1] = value;
            break;
        case 0x10:
            mx8_ = value;
            break;
        case 0x11: {
            ctrl1_ = value;
            const uint16_t new_r =
                uint16_t((irq_raster_ & 0xFF) | ((value & 0x80) << 1));
            if (new_r != irq_raster_ && linea_ == new_r) raster_irq();
            irq_raster_ = new_r;
            break;
        }
        case 0x12: {
            const uint16_t new_r = uint16_t((irq_raster_ & 0x100) | value);
            if (new_r != irq_raster_ && linea_ == new_r) raster_irq();
            irq_raster_ = new_r;
            break;
        }
        case 0x15:
            me_ = value;
            break;
        case 0x16:
            ctrl2_ = value;
            break;
        case 0x17:
            mye_ = value;
            break;
        case 0x18:
            vbase_ = value;
            break;
        case 0x19:
            irq_flag_ = uint8_t(irq_flag_ & ~(value & 0x0F));
            if (irq_) irq_(IrqLine::Clear);
            if (irq_flag_ & irq_mask_ & 0x0F) {
                irq_flag_ = uint8_t(irq_flag_ | 0x80);
                if (irq_) irq_(IrqLine::Assert);
            }
            break;
        case 0x1A:
            irq_mask_ = value & 0x0F;
            if (irq_flag_ & irq_mask_) {
                irq_flag_ = uint8_t(irq_flag_ | 0x80);
                if (irq_) irq_(IrqLine::Assert);
            }
            break;
        case 0x1B:
            mdp_ = value;
            break;
        case 0x1C:
            mmc_ = value;
            break;
        case 0x1D:
            mxe_ = value;
            break;
        case 0x20:
            ec_ = value & 0x0F;
            break;
        case 0x21:
            b0c_ = value & 0x0F;
            break;
        case 0x22:
            b1c_ = value & 0x0F;
            break;
        case 0x23:
            b2c_ = value & 0x0F;
            break;
        case 0x24:
            b3c_ = value & 0x0F;
            break;
        case 0x25:
            mm0_ = value & 0x0F;
            break;
        case 0x26:
            mm1_ = value & 0x0F;
            break;
        case 0x27: case 0x28: case 0x29: case 0x2A:
        case 0x2B: case 0x2C: case 0x2D: case 0x2E:
            sc_[reg - 0x27] = value & 0x0F;
            break;
        default:
            break;
    }
}

int Mos6566::update_line(int line, uint32_t* fb_row) {
    linea_ = uint16_t(line & 0x1FF);
    if (linea_ == irq_raster_) raster_irq();

    // Visible window roughly lines 16..284 → 270 rows for framebuffer.
    const int vis_y = line - 16;
    const bool den = (ctrl1_ & 0x10) != 0;
    const int yscroll = ctrl1_ & 7;
    const int xscroll = ctrl2_ & 7;
    const bool mcm = (ctrl2_ & 0x10) != 0;
    const bool bmm = (ctrl1_ & 0x20) != 0;
    const bool ecm = (ctrl1_ & 0x40) != 0;

    // Badline: lines 0x30..0xF7 when (line & 7) == yscroll and DEN.
    int stolen = 0;
    if (den && line >= 0x30 && line <= 0xF7 && ((line & 7) == yscroll)) {
        if (line == 0x30) vc_base_ = 0;
        vc_ = vc_base_;
        rc_ = 0;
        bad_lines_ = true;
        stolen = 40;
    }

    if (!fb_row || vis_y < 0 || vis_y >= kScreenHeight) {
        if (line >= 0x30 && line <= 0xF7 && (line & 7) == 7 && bad_lines_) {
            vc_base_ = vc_;
            bad_lines_ = false;
        }
        return stolen;
    }

    const uint32_t border = kPalette[ec_ & 15];
    for (int x = 0; x < kScreenWidth; x++) fb_row[x] = border;

    if (!den) return stolen;

    // Character row inside active area (Y=51..250 ≈).
    const int cy = line - (0x30 + yscroll);
    if (cy < 0 || cy >= 200) return stolen;

    const int char_row = cy >> 3;
    const int pixel_row = cy & 7;
    if (char_row >= 25) return stolen;

    const uint16_t video_base = uint16_t(((vbase_ & 0xF0) >> 4) << 10);
    const uint16_t char_base = uint16_t(((vbase_ & 0x0E) >> 1) << 11);
    const uint16_t bitmap_base = uint16_t(((vbase_ & 0x08) >> 3) << 13);

    for (int col = 0; col < 40; col++) {
        const int vc = (vc_base_ + char_row * 40 + col) & 0x3FF;
        const uint8_t ch = vic_read(uint16_t(video_base + vc));
        const uint8_t colc = color_ram_ ? (color_ram_[vc] & 0x0F) : 1;

        uint8_t pixels = 0;
        if (bmm) {
            // Hires bitmap: 8 bytes per cell
            const uint16_t addr =
                uint16_t(bitmap_base + (vc << 3) + pixel_row);
            pixels = vic_read(addr);
        } else {
            uint16_t ca = uint16_t(char_base + ((ch & (ecm ? 0x3F : 0xFF)) << 3) +
                                   pixel_row);
            pixels = vic_read(ca);
        }

        for (int px = 0; px < 8; px++) {
            const int sx = kVisibleX + xscroll + col * 8 + px;
            if (sx < 0 || sx >= kScreenWidth) continue;
            uint8_t color = b0c_;
            if (bmm) {
                // Standard hires: color from video matrix nybbles
                if (pixels & (0x80 >> px))
                    color = uint8_t(ch >> 4);
                else
                    color = uint8_t(ch & 0x0F);
            } else if (mcm && (colc & 8)) {
                // Multicolor text
                const int pair = (pixels >> (6 - (px & ~1))) & 3;
                switch (pair) {
                    case 0:
                        color = b0c_;
                        break;
                    case 1:
                        color = b1c_;
                        break;
                    case 2:
                        color = b2c_;
                        break;
                    case 3:
                        color = uint8_t(colc & 7);
                        break;
                }
                // double-width: skip odd px already covered
                if (px & 1) {
                    fb_row[sx] = kPalette[color & 15];
                    continue;
                }
            } else {
                if (pixels & (0x80 >> px)) color = colc;
            }
            fb_row[sx] = kPalette[color & 15];
        }
    }

    // Sprites with collision tracking (sprite-sprite + sprite-background).
    spr_coll_.fill(0);
    // Build foreground mask from non-border pixels that differ from background.
    for (int x = 0; x < kScreenWidth; x++) {
        fore_mask_[x] = (fb_row[x] != border && fb_row[x] != kPalette[b0c_ & 15]) ? 1 : 0;
    }

    for (int s = 7; s >= 0; s--) {  // draw low priority first
        if ((me_ & (1 << s)) == 0) continue;
        const int sy = my_[s];
        const int yexp = (mye_ & (1 << s)) ? 2 : 1;
        const int sh = 21 * yexp;
        if (line < sy || line >= sy + sh) continue;
        const int row = (line - sy) / yexp;
        if (row < 0 || row > 20) continue;

        // Sprite DMA timing: roughly cycles 0-15 of the line fetch data;
        // we still render on the visible line for correctness of image.
        const uint16_t sp_ptr_base =
            uint16_t((((vbase_ & 0xF0) >> 4) << 10) + 0x3F8);
        const uint8_t block = vic_read(uint16_t(sp_ptr_base + s));
        const uint16_t data = uint16_t((block << 6) + row * 3);
        const int mx = int(mx_[s]) | ((mx8_ & (1 << s)) ? 0x100 : 0);
        const int xexp = (mxe_ & (1 << s)) ? 2 : 1;
        const bool multi = (mmc_ & (1 << s)) != 0;
        const bool behind = (mdp_ & (1 << s)) != 0;

        for (int byte = 0; byte < 3; byte++) {
            const uint8_t bits = vic_read(uint16_t(data + byte));
            for (int b = 0; b < 8; ) {
                int color = -1;
                int width = 1;
                if (multi) {
                    const int pair = (bits >> (6 - (b & ~1))) & 3;
                    width = 2;
                    if (pair == 1) color = mm0_ & 15;
                    else if (pair == 2) color = sc_[s] & 15;
                    else if (pair == 3) color = mm1_ & 15;
                    b += 2;
                } else {
                    if (bits & (0x80 >> b)) color = sc_[s] & 15;
                    b += 1;
                }
                if (color < 0) continue;
                for (int px = 0; px < width * xexp; px++) {
                    const int bit_i = multi ? (b - 2) : (b - 1);
                    const int sx = mx + (byte * 8 + bit_i) * xexp + px + kVisibleX - 24;
                    if (sx < 0 || sx >= kScreenWidth) continue;

                    // Sprite-sprite collision
                    if (spr_coll_[sx]) {
                        clx_spr_ = uint8_t(clx_spr_ | spr_coll_[sx] | (1 << s));
                        if (irq_mask_ & 0x04) {
                            irq_flag_ = uint8_t(irq_flag_ | 0x04 | 0x80);
                            if (irq_) irq_(IrqLine::Assert);
                        }
                    }
                    spr_coll_[sx] = uint8_t(spr_coll_[sx] | (1 << s));

                    // Sprite-data collision
                    if (fore_mask_[sx]) {
                        clx_bgr_ = uint8_t(clx_bgr_ | (1 << s));
                        if (irq_mask_ & 0x02) {
                            irq_flag_ = uint8_t(irq_flag_ | 0x02 | 0x80);
                            if (irq_) irq_(IrqLine::Assert);
                        }
                    }

                    if (behind && fore_mask_[sx]) continue;  // data priority
                    fb_row[sx] = kPalette[color & 15];
                }
            }
        }
    }

    if (line >= 0x30 && line <= 0xF7 && (line & 7) == 7 && bad_lines_) {
        vc_base_ = uint16_t((vc_base_ + 40) & 0x3FF);
        bad_lines_ = false;
    }
    return stolen;
}

}  // namespace dsp
