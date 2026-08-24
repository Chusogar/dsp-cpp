#include "video/huc6270.h"

#include <algorithm>
#include <cstring>

namespace dsp {
namespace {

constexpr uint16_t ST_CR = 0x01;
constexpr uint16_t ST_OR = 0x02;
constexpr uint16_t ST_RR = 0x04;
constexpr uint16_t ST_DS = 0x08;
constexpr uint16_t ST_DV = 0x10;
constexpr uint16_t ST_VD = 0x20;
constexpr uint16_t ST_BSY = 0x40;

int bat_width(uint16_t mwr) {
    switch ((mwr >> 4) & 3) {
        case 0: return 32;
        case 1: return 64;
        default: return 128;
    }
}
int bat_height(uint16_t mwr) { return (mwr & 0x40) ? 64 : 32; }

}  // namespace

void HuC6270::reset() {
    vram_.fill(0);
    sat_.fill(0);
    reg_.fill(0);
    reg_[HSR] = 0x0202;
    reg_[HDR] = 0x041F;  // 32 tiles → 256px
    reg_[VPR] = 0x0F02;
    reg_[VDW] = 0x00EF;
    reg_[VCR] = 0x0003;
    addr_reg_ = 0;
    status_ = 0;
    vram_read_buf_ = 0;
    have_low_ = false;
    low_byte_ = 0;
    bg_y_ = 0;
    vblank_ = false;
    line_spr_count_ = 0;
    if (irq_) irq_(false);
}

int HuC6270::display_width() const {
    return std::min(kMaxWidth, ((reg_[HDR] & 0x7F) + 1) * 8);
}

int HuC6270::display_height() const {
    return std::min(kMaxHeight, int(reg_[VDW] & 0x1FF) + 1);
}

void HuC6270::select_reg(uint8_t index) {
    addr_reg_ = index & 0x1F;
    have_low_ = false;
}

void HuC6270::write_data(uint16_t value) {
    const int r = addr_reg_;
    if (r >= 20) return;
    reg_[r] = value;
    if (r == VWR) {
        vram_[reg_[MAWR] & (kVramWords - 1)] = value;
        static const int kInc[4] = {1, 32, 64, 128};
        reg_[MAWR] = uint16_t(reg_[MAWR] + kInc[(reg_[CR] >> 11) & 3]);
    } else if (r == MARR) {
        vram_read_buf_ = vram_[reg_[MARR] & (kVramWords - 1)];
        static const int kInc[4] = {1, 32, 64, 128};
        reg_[MARR] = uint16_t(reg_[MARR] + kInc[(reg_[CR] >> 11) & 3]);
    }
}

uint16_t HuC6270::read_data() {
    if (addr_reg_ == VRR || addr_reg_ == 2) {
        const uint16_t data = vram_read_buf_;
        vram_read_buf_ = vram_[reg_[MARR] & (kVramWords - 1)];
        static const int kInc[4] = {1, 32, 64, 128};
        reg_[MARR] = uint16_t(reg_[MARR] + kInc[(reg_[CR] >> 11) & 3]);
        return data;
    }
    return (addr_reg_ < 20) ? reg_[addr_reg_] : 0;
}

void HuC6270::write(uint8_t port, uint8_t value) {
    switch (port & 3) {
        case 0:
            select_reg(value);
            break;
        case 2:
            low_byte_ = value;
            have_low_ = true;
            break;
        case 3:
            if (have_low_) {
                write_data(uint16_t(low_byte_ | (uint16_t(value) << 8)));
                have_low_ = false;
            } else {
                write_data(uint16_t(value) << 8);
            }
            break;
        default:
            break;
    }
}

uint8_t HuC6270::read(uint8_t port) {
    switch (port & 3) {
        case 0: {
            const uint8_t s = uint8_t(status_ & 0x7F);
            status_ = uint16_t(status_ & ST_BSY);
            update_irq();
            return s;
        }
        case 2: {
            const uint16_t v = read_data();
            low_byte_ = uint8_t(v & 0xFF);
            return low_byte_;
        }
        case 3:
            return uint8_t((read_data() >> 8) & 0xFF);  // rare
        default:
            return 0;
    }
}

void HuC6270::update_irq() {
    if (!irq_) return;
    const bool want =
        ((status_ & ST_VD) && (reg_[CR] & 0x08)) ||
        ((status_ & ST_RR) && (reg_[CR] & 0x04)) ||
        ((status_ & ST_OR) && (reg_[CR] & 0x01)) ||
        ((status_ & ST_CR) && (reg_[CR] & 0x02));
    irq_(want);
}

void HuC6270::do_satb_dma() {
    const uint16_t src = reg_[DVSSR];
    for (int i = 0; i < kSatWords; ++i)
        sat_[i] = vram_[(src + i) & (kVramWords - 1)];
    status_ = uint16_t(status_ | ST_DS);
}

void HuC6270::fetch_sprites(int display_y) {
    line_spr_count_ = 0;
    if (!(reg_[CR] & 0x40)) return;

    int overflow = 0;
    for (int i = 0; i < 64; ++i) {
        const uint16_t sy = sat_[i * 4 + 0] & 0x3FF;
        const uint16_t sx = sat_[i * 4 + 1] & 0x3FF;
        const uint16_t pattern = sat_[i * 4 + 2];
        const uint16_t attr = sat_[i * 4 + 3];

        const int spr_y = int(sy) - 64;
        const int hbits = (attr >> 12) & 3;
        const int height = (hbits == 0) ? 16 : (hbits == 1) ? 32 : 64;
        if (display_y < spr_y || display_y >= spr_y + height) continue;

        if (line_spr_count_ >= kSpritesPerLine) {
            overflow = 1;
            if (sprite_limit_) break;
            continue;
        }

        LineSprite& ls = line_spr_[line_spr_count_++];
        ls.x = int(sx) - 32;
        ls.width = (attr & 0x100) ? 32 : 16;
        ls.height = height;
        ls.pattern = pattern & 0x7FF;
        // CGY: for tall sprites, pattern bits select which 16-row block
        ls.palette = attr & 0x0F;
        ls.priority = (attr & 0x80) != 0;
        ls.hflip = (attr & 0x800) != 0;
        int row = display_y - spr_y;
        if (attr & 0x8000) row = height - 1 - row;  // CGY vflip uses bit 15? PCE uses bit 15 of attr for y flip = 0x8000
        // Correct: vertical flip is bit 15 of attribute word... actually bit 15 is unused; V flip is bit 11 (0x800) is H, bit 15?
        // Standard: bit 11 = H-flip (0x800), bit 15 = V-flip (0x8000) — some docs say bit 14-15 for CGY
        // MAME: yflip = BIT(attr, 15)
        if (attr & 0x8000) {
            // already applied if we used 0x8000 above incorrectly with height
        }
        // Re-read: HuC6270 attr: bit11=Hflip, bit15=Vflip
        ls.row = (attr & 0x8000) ? (height - 1 - (display_y - spr_y)) : (display_y - spr_y);
        // Wide sprite pattern alignment
        if (ls.width == 32) ls.pattern &= ~1;
        if (height >= 32) ls.pattern &= ~2;
        if (height >= 64) ls.pattern &= ~4;
    }
    if (overflow) {
        status_ = uint16_t(status_ | ST_OR);
    }
}

uint16_t HuC6270::bg_pixel(int x, int y) const {
    if (!(reg_[CR] & 0x80)) return 0;

    const int bw = bat_width(reg_[MWR]);
    const int bh = bat_height(reg_[MWR]);
    const int scroll_x = (reg_[BXR] + x) & 0x3FF;
    const int scroll_y = (reg_[BYR] + y) & 0x3FF;
    const int tx = (scroll_x >> 3) & (bw - 1);
    const int ty = (scroll_y >> 3) & (bh - 1);
    const uint16_t bat = vram_[(ty * bw + tx) & (kVramWords - 1)];
    const int code = bat & 0x0FFF;
    const int pal = (bat >> 12) & 0x0F;
    const int cx = scroll_x & 7;
    const int cy = scroll_y & 7;
    // 4bpp: each tile = 16 words (8 rows × 2 words of bitplanes)
    const int addr = (code << 4) + cy;
    const uint16_t w0 = vram_[addr & (kVramWords - 1)];
    const uint16_t w1 = vram_[(addr + 8) & (kVramWords - 1)];
    const int bit = 7 - cx;
    const int c =
        ((w0 >> bit) & 1) |
        (((w0 >> (bit + 8)) & 1) << 1) |
        (((w1 >> bit) & 1) << 2) |
        (((w1 >> (bit + 8)) & 1) << 3);
    if (c == 0) return 0;
    return uint16_t((pal << 4) | c);
}

uint16_t HuC6270::sprite_pixel(int x, int /*line*/, int& out_pri) const {
    out_pri = 0;
    uint16_t color = 0;
    for (int i = 0; i < line_spr_count_; ++i) {
        const LineSprite& ls = line_spr_[i];
        const int dx = x - ls.x;
        if (dx < 0 || dx >= ls.width) continue;

        int col = ls.hflip ? (ls.width - 1 - dx) : dx;
        const int half = (col >= 16) ? 1 : 0;
        col &= 15;
        const int row = ls.row & 15;
        const int block = (ls.row >> 4);  // which 16-line block for 32/64

        // Sprite pattern: 64 words per 16×16 cell
        int code = ls.pattern;
        if (ls.width == 32) code += half;
        if (ls.height >= 32) code += block * 2;
        // 64-tall: block 0..3
        const int base = (code & 0x7FF) * 64;
        const int row_addr = base + row;
        const uint16_t p0 = vram_[(row_addr + 0) & (kVramWords - 1)];
        const uint16_t p1 = vram_[(row_addr + 16) & (kVramWords - 1)];
        const uint16_t p2 = vram_[(row_addr + 32) & (kVramWords - 1)];
        const uint16_t p3 = vram_[(row_addr + 48) & (kVramWords - 1)];
        const int bit = 15 - col;
        const int c =
            ((p0 >> bit) & 1) |
            (((p1 >> bit) & 1) << 1) |
            (((p2 >> bit) & 1) << 2) |
            (((p3 >> bit) & 1) << 3);
        if (c == 0) continue;
        color = uint16_t(0x100 | (ls.palette << 4) | c);
        out_pri = ls.priority ? 1 : 0;
        break;  // first (lowest index = highest priority among sprites)
    }
    return color;
}

void HuC6270::render_line(int display_y, uint16_t* out, int width) {
    fetch_sprites(display_y);
    for (int x = 0; x < width; ++x) {
        const uint16_t bg = bg_pixel(x, display_y);
        int spri = 0;
        const uint16_t sp = sprite_pixel(x, display_y, spri);
        // Priority: sprite over BG when spri=1 or BG transparent
        if (sp && (spri || bg == 0))
            out[x] = sp;
        else
            out[x] = bg;
    }
}

void HuC6270::run_line(int line, uint16_t* pixel_out, int width) {
    const int disp_h = display_height();
    const int active_start = 14;
    const int active_end = active_start + disp_h;

    status_ = uint16_t(status_ & ~ST_RR);

    if (line == active_start) {
        bg_y_ = 0;
        vblank_ = false;
        status_ = uint16_t(status_ & ~ST_VD);
    }

    if (line >= active_start && line < active_end) {
        if (pixel_out) {
            const int w = std::min(width, display_width());
            render_line(line - active_start, pixel_out, w);
            for (int x = w; x < width; ++x) pixel_out[x] = 0;
        }
        ++bg_y_;
    }

    // RCR: compare against line + $40 bias used by hardware
    if ((reg_[RCR] & 0x3FF) != 0 &&
        (reg_[RCR] & 0x3FF) == uint16_t(line + 0x40)) {
        status_ = uint16_t(status_ | ST_RR);
    }

    if (line == active_end) {
        vblank_ = true;
        status_ = uint16_t(status_ | ST_VD);
        do_satb_dma();
    }

    update_irq();
}

// ---- HuC6202 VPC (SuperGrafx) ----

void HuC6202::reset() {
    priority_[0] = priority_[1] = 0x11;
    window_[0] = window_[1] = 0;
    st_mode_ = 0;
}

void HuC6202::write(uint8_t port, uint8_t value) {
    switch (port & 7) {
        case 0: priority_[0] = value; break;
        case 1: priority_[1] = value; break;
        case 2: window_[0] = uint16_t((window_[0] & 0xFF00) | value); break;
        case 3: window_[0] = uint16_t((window_[0] & 0x00FF) | (value << 8)); break;
        case 4: window_[1] = uint16_t((window_[1] & 0xFF00) | value); break;
        case 5: window_[1] = uint16_t((window_[1] & 0x00FF) | (value << 8)); break;
        case 6: st_mode_ = value; break;
        default: break;
    }
}

uint8_t HuC6202::read(uint8_t port) {
    switch (port & 7) {
        case 0: return priority_[0];
        case 1: return priority_[1];
        case 2: return uint8_t(window_[0] & 0xFF);
        case 3: return uint8_t(window_[0] >> 8);
        case 4: return uint8_t(window_[1] & 0xFF);
        case 5: return uint8_t(window_[1] >> 8);
        case 6: return st_mode_;
        default: return 0xFF;
    }
}

uint16_t HuC6202::mix(uint16_t p0, uint16_t p1, int x) const {
    // Simplified VPC: choose VDC based on windows / priority settings.
    const int w0 = window_[0] & 0x3FF;
    const int w1 = window_[1] & 0x3FF;
    int region = 0;
    if (x >= w1) region = 2;
    else if (x >= w0) region = 1;
    const uint8_t pri = priority_[region > 1 ? 1 : 0];
    // Bits select source: lower nibble = BG/sprite source rules (simplified)
    const int mode = (pri >> (region == 1 ? 0 : 4)) & 0x0F;
    switch (mode & 3) {
        case 0: return p0 ? p0 : p1;
        case 1: return p1 ? p1 : p0;
        case 2: return p0;
        default: return p1;
    }
}

}  // namespace dsp
