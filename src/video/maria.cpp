#include "video/maria.h"

#include <algorithm>

namespace dsp {

void Maria::reset() {
    palette_.fill(0);
    line_ram_.fill(0);
    dpp_ = dll_ = dl_ = charbase_ = 0;
    offset_ = holey_ = 0;
    dli_ = dmaon_ = wsync_ = false;
    write_mode_ = cwidth_ = kangaroo_ = color_kill_ = false;
    rm_ = 0;
    vblank_ = 0x80;
}

uint8_t Maria::read(uint16_t offset) {
    // MSTAT is the only readable register; bit 7 reports vertical blank.
    if ((offset & 0x1f) == 0x08) return vblank_;
    return 0;
}

void Maria::write(uint16_t offset, uint8_t value) {
    const uint8_t reg = offset & 0x1f;
    switch (reg) {
        case 0x00:                                        // BACKGRND
            // Every palette slot with zero in its low two bits is the
            // background colour, so writing it updates all eight.
            for (int i = 0; i < 8; i++) palette_[size_t(i) * 4] = value;
            return;
        case 0x04: wsync_ = true; return;                 // WSYNC: halt to end of line
        case 0x0c: dpp_ = uint16_t((dpp_ & 0x00ff) | (value << 8)); return;  // DPPH
        case 0x10: dpp_ = uint16_t((dpp_ & 0xff00) | value); return;         // DPPL
        case 0x14: charbase_ = uint16_t(value << 8); return;                 // CHARBASE
        case 0x1c:                                                           // CTRL
            color_kill_ = (value & 0x80) != 0;
            // Bits 6-5 select the DMA mode; only %10 turns DMA on, %11 off,
            // and the two low combinations are factory test modes.
            dmaon_ = ((value >> 5) & 3) == 2;
            cwidth_ = (value & 0x10) != 0;
            kangaroo_ = (value & 0x04) != 0;
            rm_ = value & 0x03;
            return;
        default: break;
    }
    // The remaining registers are the eight three-colour palettes; the
    // register number is already the index MARIA uses internally.
    if ((reg & 3) != 0) palette_[reg] = value;
}

bool Maria::is_holey(uint16_t addr) const {
    if ((holey_ & 2) && ((addr & 0x9000) == 0x9000)) return true;
    if ((holey_ & 1) && ((addr & 0x8800) == 0x8800)) return true;
    return false;
}

int Maria::write_line_ram(uint16_t addr, uint8_t offset, uint8_t pal) {
    const uint8_t data = rd(addr);
    pal = uint8_t(pal << 2);
    if (write_mode_) {
        // 320B/320D style: two cells per byte, colour bits interleaved.
        uint8_t c = uint8_t((pal & 0x10) | (data & 0x0c) | (data >> 6));
        if (((c & 3) || kangaroo_) && offset < kLineRam) line_ram_[offset] = c;
        offset++;
        c = uint8_t((pal & 0x10) | ((data & 0x03) << 2) | ((data & 0x30) >> 4));
        if (((c & 3) || kangaroo_) && offset < kLineRam) line_ram_[offset] = c;
        return 2;
    }
    for (int i = 0; i < 4; i++, offset++) {
        const uint8_t c = uint8_t(pal | ((data >> (6 - 2 * i)) & 0x03));
        if (((c & 3) || kangaroo_) && offset < kLineRam) line_ram_[offset] = c;
    }
    return 4;
}

void Maria::draw_scanline() {
    line_ram_.fill(0);
    if (!dmaon_) return;

    // MARIA gets a fixed budget of cycles per line; anything it hasn't
    // fetched by then simply doesn't appear, exactly as on hardware.
    int cycles = 16;
    uint16_t dl = dl_;
    while (((rd(uint16_t(dl + 1)) & 0x5f) != 0) && cycles < 426) {
        uint16_t graph_adr;
        int width;
        uint8_t hpos, pal;
        bool ind;
        if ((rd(uint16_t(dl + 1)) & 0x1f) == 0) {
            // Five-byte header: adds write mode and indirect (character) mode.
            graph_adr = uint16_t((rd(uint16_t(dl + 2)) << 8) | rd(dl));
            width = ((rd(uint16_t(dl + 3)) ^ 0xff) & 0x1f) + 1;
            hpos = rd(uint16_t(dl + 4));
            pal = uint8_t(rd(uint16_t(dl + 3)) >> 5);
            write_mode_ = (rd(uint16_t(dl + 1)) & 0x80) != 0;
            ind = (rd(uint16_t(dl + 1)) & 0x20) != 0;
            dl += 5;
            cycles += 10;
        } else {
            graph_adr = uint16_t((rd(uint16_t(dl + 2)) << 8) | rd(dl));
            width = ((rd(uint16_t(dl + 1)) ^ 0xff) & 0x1f) + 1;
            hpos = rd(uint16_t(dl + 3));
            pal = uint8_t(rd(uint16_t(dl + 1)) >> 5);
            ind = false;
            dl += 4;
            cycles += 8;
        }
        for (int x = 0; x < width && cycles < 426; x++) {
            uint16_t data_addr;
            if (ind) {
                const uint8_t c = rd(uint16_t(graph_adr + x));
                data_addr = uint16_t((charbase_ | c) + (offset_ << 8));
                if (is_holey(data_addr)) continue;
                cycles += 3;
                hpos = uint8_t(hpos + write_line_ram(data_addr, hpos, pal));
                cycles += 3;
                if (cwidth_) {  // two data bytes per map byte
                    hpos = uint8_t(hpos + write_line_ram(uint16_t(data_addr + 1), hpos, pal));
                    cycles += 3;
                }
            } else {
                data_addr = uint16_t(graph_adr + x + (offset_ << 8));
                if (is_holey(data_addr)) continue;
                hpos = uint8_t(hpos + write_line_ram(data_addr, hpos, pal));
                cycles += 3;
            }
        }
    }
}

void Maria::scanline(int frame_scanline, int lines) {
    if (frame_scanline == 16) vblank_ = 0x00;
    if (frame_scanline == lines - 5) vblank_ = 0x80;

    if (frame_scanline == 16 && dmaon_) {
        // Leaving vblank: latch the display list list and read its first zone.
        dll_ = dpp_;
        dl_ = uint16_t((rd(uint16_t(dll_ + 1)) << 8) | rd(uint16_t(dll_ + 2)));
        const uint8_t header = rd(dll_);
        offset_ = header & 0x0f;
        holey_ = uint8_t((header & 0x60) >> 5);
        dli_ = (header & 0x80) != 0;
    }

    if (frame_scanline > 15 && frame_scanline < lines - 5) draw_scanline();

    if (frame_scanline > 16 && frame_scanline < lines - 5 && dmaon_) {
        if (offset_ == 0) {
            // Zone finished: step to the next DLL entry.
            dll_ += 3;
            dl_ = uint16_t((rd(uint16_t(dll_ + 1)) << 8) | rd(uint16_t(dll_ + 2)));
            const uint8_t header = rd(dll_);
            offset_ = header & 0x0f;
            holey_ = uint8_t((header & 0x60) >> 5);
            dli_ = (header & 0x80) != 0;
        } else {
            offset_--;
        }
    }

    if (dli_) {
        if (dli_cb_) dli_cb_();
        dli_ = false;
    }
}

void Maria::emit(uint8_t* dest) const {
    for (int i = 0; i < kLineRam; i++) {
        const uint8_t cell = line_ram_[size_t(i)];
        uint8_t left, right;
        switch (rm_) {
            case 0x02: {
                // 320B/320D: the cell holds two pixels, their colour bits
                // interleaved rather than adjacent.
                const uint8_t dl = uint8_t((cell & 0x10) | (cell & 0x02) | ((cell >> 3) & 1));
                const uint8_t dr = uint8_t((cell & 0x10) | ((cell << 1) & 0x02) | ((cell >> 2) & 1));
                left = palette_[dl];
                right = palette_[dr];
                break;
            }
            case 0x03: {
                // 320A/320C: one bit per pixel against the cell's palette.
                const uint8_t dl = uint8_t((cell & 0x1c) | (cell & 0x02));
                const uint8_t dr = uint8_t((cell & 0x1c) | ((cell << 1) & 0x02));
                left = palette_[dl];
                right = palette_[dr];
                break;
            }
            default:
                // 160A/160B: one colour across both pixels of the cell.
                left = right = palette_[cell];
                break;
        }
        if (color_kill_) { left &= 0x0f; right &= 0x0f; }
        dest[i * 2] = left;
        dest[i * 2 + 1] = right;
    }
}

}  // namespace dsp
