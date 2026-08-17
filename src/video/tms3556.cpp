#include "video/tms3556.h"

#include <algorithm>
#include <cstring>

namespace dsp {

uint32_t Tms3556::rgb3(uint8_t color) {
    const uint32_t r = (color & 1) ? 0xffu : 0;
    const uint32_t g = (color & 2) ? 0xffu : 0;
    const uint32_t b = (color & 4) ? 0xffu : 0;
    return 0xff000000u | (r << 16) | (g << 8) | b;
}

Tms3556::Tms3556()
    : vram_(0x10000, 0), framebuffer_(size_t(kTotalWidth * kTotalHeight), 0) {
    address_regs_.fill(0xffff);
}

void Tms3556::reset() {
    control_regs_.fill(0);
    address_regs_.fill(0xffff);
    std::fill(vram_.begin(), vram_.end(), 0);
    std::fill(framebuffer_.begin(), framebuffer_.end(), rgb3(0));
    reg_ = reg2_ = 0;
    row_col_written_ = 0;
    bamp_written_ = 0;
    colrow_ = 0;
    acmpxy_mode_ = DmaMode::Write;
    acmpxy_ = acmp_ = 0;
    init_read_ = 0;
    scanline_ = 0;
    blink_ = 0;
    blink_count_ = 0;
    bg_color_ = 0;
    name_offset_ = 0;
    cg_flag_ = 0;
    char_line_counter_ = 0;
    std::memset(dbl_h_phase_, 0, sizeof(dbl_h_phase_));
}

uint8_t Tms3556::vram_r() {
    if (bamp_written_) {
        bamp_written_ = false;
        acmpxy_mode_ = DmaMode::Write;
        acmp_ = init_read_ ? address_regs_[1] : uint16_t(address_regs_[1] - 1);
    }
    if (row_col_written_) {
        row_col_written_ = 0;
        acmpxy_mode_ = DmaMode::Read;
        acmpxy_ = init_read_ ? colrow_ : uint16_t(colrow_ - 1);
    }
    init_read_ = false;
    reg_ = 0;

    uint8_t ret;
    if (acmpxy_mode_ == DmaMode::Read) {
        ret = read_byte(acmpxy_);
        acmpxy_++;
        if (acmpxy_ == address_regs_[7]) acmpxy_ = address_regs_[1];
    } else {
        ret = read_byte(acmp_);
        acmp_++;
        if (acmp_ == address_regs_[7]) acmp_ = address_regs_[1];
    }
    return ret;
}

void Tms3556::vram_w(uint8_t data) {
    if (bamp_written_) {
        bamp_written_ = false;
        acmpxy_mode_ = DmaMode::Read;
        acmp_ = address_regs_[1];
    }
    if (row_col_written_) {
        row_col_written_ = 0;
        acmpxy_mode_ = DmaMode::Write;
        acmpxy_ = colrow_;
    }
    if (acmpxy_mode_ == DmaMode::Write) {
        write_byte(acmpxy_, data);
        acmpxy_++;
        if (acmpxy_ == address_regs_[7]) acmpxy_ = address_regs_[1];
    } else {
        write_byte(acmp_, data);
        acmp_++;
        if (acmp_ == address_regs_[7]) acmp_ = address_regs_[1];
    }
    reg_ = 0;
}

uint8_t Tms3556::reg_r() {
    const uint8_t reply = 0;
    reg_ = reg2_;
    return reply;
}

void Tms3556::reg_w(uint8_t data) {
    if (reg_ == 0) {
        reg_ = data & 0x0f;
        reg2_ = uint8_t(data >> 4);
        return;
    }
    if (reg_ < 8) {
        control_regs_[reg_] = data;
        if (reg_ == 2 || reg_ == 1) {
            colrow_ = uint16_t((control_regs_[2] << 8) | control_regs_[1]);
            row_col_written_ = true;
        }
        reg_ = reg2_;
        return;
    }
    address_regs_[reg_ - 8] = uint16_t((control_regs_[2] << 8) | control_regs_[1]);
    if (reg_ >= 0x0b && reg_ <= 0x0e) {
        address_regs_[reg_ - 8] = uint16_t(address_regs_[reg_ - 8] + 2);
    } else {
        address_regs_[reg_ - 8] = uint16_t(address_regs_[reg_ - 8] + 1);
    }
    if (reg_ == 9) {
        row_col_written_ = false;
        bamp_written_ = true;
    } else {
        row_col_written_ = 0;
        bamp_written_ = false;
    }
    reg_ = reg2_;
}

uint8_t Tms3556::initptr_r() {
    init_read_ = true;
    reg_ = 0;
    return 0xff;
}

void Tms3556::draw_line_empty(uint32_t* ln) {
    const uint32_t color = rgb3(uint8_t(bg_color_));
    for (int i = 0; i < kTotalWidth; i++) ln[i] = color;
}

void Tms3556::draw_line_text_common(uint32_t* ln) {
    const uint16_t nametbl_base = address_regs_[2];
    uint16_t patterntbl_base[4];
    for (int i = 0; i < 4; i++) patterntbl_base[i] = address_regs_[i + 3];

    const uint32_t border = rgb3(uint8_t(bg_color_));
    int xx = 0;
    for (; xx < kLeftBorder; xx++) *ln++ = border;

    int name_offset = name_offset_;
    int dbl_w_phase = 0;
    for (int x = 0; x < 40; x++) {
        const int name_hi = read_byte(uint16_t(nametbl_base + name_offset));
        const int name_lo = read_byte(uint16_t(nametbl_base + name_offset + 1));
        const int pattern_ix = ((name_hi >> 2) & 2) | ((name_hi >> 4) & 1);
        const bool alphanumeric = (pattern_ix < 2) || ((pattern_ix == 3) && !(control_regs_[7] & 0x08));
        uint8_t fg = uint8_t((name_hi >> 5) & 0x7);
        uint8_t bg;
        int dbl_w = 0;
        int dbl_h = 0;
        if (alphanumeric) {
            if (name_hi & 4) {
                bg = fg;
                fg = uint8_t(bg_color_);
            } else {
                bg = uint8_t(bg_color_);
            }
            dbl_w = name_hi & 0x2;
            dbl_h = name_hi & 0x1;
        } else {
            bg = uint8_t(name_hi & 0x7);
        }
        if ((name_lo & 0x80) && blink_) fg = bg;

        uint8_t pattern;
        if (!dbl_h) {
            pattern = read_byte(uint16_t(patterntbl_base[pattern_ix] + (name_lo & 0x7f) +
                                         128 * char_line_counter_));
            if (char_line_counter_ == 0) dbl_h_phase_[x] = 0;
        } else {
            if (!dbl_h_phase_[x])
                pattern = read_byte(uint16_t(patterntbl_base[pattern_ix] + (name_lo & 0x7f) +
                                             128 * (5 + (char_line_counter_ >> 1))));
            else
                pattern = read_byte(uint16_t(patterntbl_base[pattern_ix] + (name_lo & 0x7f) +
                                             128 * (char_line_counter_ >> 1)));
            if (char_line_counter_ == 0) dbl_h_phase_[x] = !dbl_h_phase_[x];
        }

        if (!dbl_w) {
            for (int bit = 0; bit < 8; bit++) {
                *ln++ = rgb3((pattern & 0x80) ? fg : bg);
                pattern = uint8_t(pattern << 1);
            }
            dbl_w_phase = 0;
        } else {
            if (dbl_w_phase) pattern = uint8_t(pattern << 4);
            for (int bit = 0; bit < 4; bit++) {
                const uint32_t color = rgb3((pattern & 0x80) ? fg : bg);
                *ln++ = color;
                *ln++ = color;
                pattern = uint8_t(pattern << 1);
            }
            dbl_w_phase = !dbl_w_phase;
        }
        name_offset += 2;
    }

    for (xx = 0; xx < kRightBorder; xx++) *ln++ = border;
    if (char_line_counter_ == 0) name_offset_ = name_offset;
}

void Tms3556::draw_line_bitmap_common(uint32_t* ln) {
    const uint16_t nametbl_base = address_regs_[2];
    const uint32_t border = rgb3(uint8_t(bg_color_));
    for (int xx = 0; xx < kLeftBorder; xx++) *ln++ = border;
    for (int x = 0; x < 40; x++) {
        uint8_t name_b = read_byte(uint16_t(nametbl_base + name_offset_));
        uint8_t name_g = read_byte(uint16_t(nametbl_base + name_offset_ + 1));
        uint8_t name_r = read_byte(uint16_t(nametbl_base + name_offset_ + 2));
        for (int xx = 0; xx < 8; xx++) {
            const uint8_t color = uint8_t(((name_b >> 5) & 0x4) | ((name_g >> 6) & 0x2) |
                                          ((name_r >> 7) & 0x1));
            *ln++ = rgb3(color);
            name_b = uint8_t(name_b << 1);
            name_g = uint8_t(name_g << 1);
            name_r = uint8_t(name_r << 1);
        }
        name_offset_ += 3;
    }
    for (int xx = 0; xx < kRightBorder; xx++) *ln++ = border;
}

void Tms3556::draw_line_text(uint32_t* ln) {
    if (char_line_counter_ == 0) char_line_counter_ = 10;
    char_line_counter_--;
    draw_line_text_common(ln);
}

void Tms3556::draw_line_bitmap(uint32_t* ln) {
    draw_line_bitmap_common(ln);
    bg_color_ = (read_byte(uint16_t(address_regs_[2] + name_offset_)) >> 5) & 0x7;
    name_offset_ += 2;
}

void Tms3556::draw_line_mixed(uint32_t* ln) {
    if (cg_flag_) {
        draw_line_bitmap_common(ln);
        const uint8_t extra = read_byte(uint16_t(address_regs_[2] + name_offset_));
        bg_color_ = (extra >> 5) & 0x7;
        cg_flag_ = (extra >> 4) & 0x1;
        name_offset_ += 2;
    } else {
        if (char_line_counter_ == 0) char_line_counter_ = 10;
        char_line_counter_--;
        draw_line_text_common(ln);
        if (char_line_counter_ == 0) {
            const uint8_t extra = read_byte(uint16_t(address_regs_[2] + name_offset_));
            bg_color_ = (extra >> 5) & 0x7;
            cg_flag_ = (extra >> 4) & 0x1;
            name_offset_ += 2;
        }
    }
}

void Tms3556::draw_line(int line) {
    uint32_t* ln = framebuffer_.data() + size_t(line) * kTotalWidth;
    if (line < kTopBorder || line >= (kTopBorder + kActiveHeight)) {
        draw_line_empty(ln);
        cg_flag_ = 0;
        return;
    }
    switch (control_regs_[6] >> 6) {
        case kModeOff:
            draw_line_empty(ln);
            break;
        case kModeText:
            draw_line_text(ln);
            break;
        case kModeBitmap:
            draw_line_bitmap(ln);
            break;
        case kModeMixed:
            draw_line_mixed(ln);
            break;
        default:
            draw_line_empty(ln);
            break;
    }
}

void Tms3556::interrupt_start_vblank() {
    if (blink_count_) blink_count_--;
    if (!blink_count_) {
        blink_ = !blink_;
        blink_count_ = 60;
    }
    bg_color_ = (control_regs_[7] >> 5) & 0x7;
    name_offset_ = 0;
    char_line_counter_ = 0;
    cg_flag_ = 0;
    std::memset(dbl_h_phase_, 0, sizeof(dbl_h_phase_));
}

void Tms3556::interrupt() {
    if (scanline_ == 310) interrupt_start_vblank();
    if (scanline_ >= 0 && scanline_ < kTotalHeight) draw_line(scanline_);
    if (++scanline_ == kScanlines) scanline_ = 0;
}

}  // namespace dsp
