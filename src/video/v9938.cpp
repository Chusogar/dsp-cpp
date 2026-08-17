#include "video/v9938.h"

#include <cstddef>

namespace dsp {
namespace {

const uint8_t kDefaultPal[16][3] = {  // R,G,B (3-bit each)
    {0, 0, 0}, {0, 0, 0}, {1, 6, 1}, {3, 7, 3}, {1, 1, 7}, {2, 3, 7}, {5, 1, 1}, {2, 6, 7},
    {7, 1, 1}, {7, 3, 3}, {6, 6, 1}, {6, 7, 4}, {1, 4, 1}, {6, 2, 5}, {5, 5, 5}, {7, 7, 7},
};

uint32_t rgb3_to_argb(uint8_t r3, uint8_t g3, uint8_t b3) {
    const uint8_t r = uint8_t((r3 << 5) | (r3 << 2) | (r3 >> 1));
    const uint8_t g = uint8_t((g3 << 5) | (g3 << 2) | (g3 >> 1));
    const uint8_t b = uint8_t((b3 << 5) | (b3 << 2) | (b3 >> 1));
    return 0xFF000000u | (uint32_t(r) << 16) | (uint32_t(g) << 8) | b;
}

// G7 (Screen 8) 3-3-2 RGB. Blue uses 0/2/4/7, matching MSXEC.
uint32_t g7_color(uint8_t c) {
    const uint8_t r3 = (c >> 5) & 7;
    const uint8_t g3 = (c >> 2) & 7;
    const uint8_t b2 = c & 3;
    const uint8_t kblu[4] = {0, 2, 4, 7};
    const uint8_t r = uint8_t((r3 << 5) | (r3 << 2) | (r3 >> 1));
    const uint8_t g = uint8_t((g3 << 5) | (g3 << 2) | (g3 >> 1));
    const uint8_t b3 = kblu[b2];
    const uint8_t b = uint8_t((b3 << 5) | (b3 << 2) | (b3 >> 1));
    return 0xFF000000u | (uint32_t(r) << 16) | (uint32_t(g) << 8) | b;
}

void put256(uint32_t* buf, int x, uint32_t color) {
    if (static_cast<unsigned>(x) < 256u) {
        buf[x * 2] = color;
        buf[x * 2 + 1] = color;
    }
}

unsigned mgetii(const uint8_t* p) {
    return unsigned(p[0]) | (unsigned(p[1]) << 8);
}

}  // namespace

int V9938::screen_mode() const {
    const int m1 = (regs_[1] >> 4) & 1;
    const int m2 = (regs_[1] >> 3) & 1;
    const int m3 = (regs_[0] >> 1) & 1;
    const int m4 = (regs_[0] >> 2) & 1;
    const int m5 = (regs_[0] >> 3) & 1;
    const int bits = (m5 << 4) | (m4 << 3) | (m3 << 2) | (m2 << 1) | m1;
    switch (bits) {
    case 0x01: return 0;   // T1
    case 0x09: return 10;  // T2
    case 0x00: return 1;   // G1
    case 0x04: return 2;   // G2
    case 0x02: return 3;   // MC
    case 0x06: return 4;   // G3
    case 0x08: return 5;   // G4
    case 0x0C: return 6;   // G5
    case 0x10: return 7;   // G6
    case 0x18: return 8;   // G7
    default:   return 1;
    }
}

int V9938::active_lines() const {
    return (regs_[9] & 0x80) ? 212 : 192;
}

// 0 = 16K linear (MSX1 modes), 1 = 128K linear, 2 = 128K planar (G6/G7).
int V9938::memtype() const {
    switch (screen_mode()) {
    case 7:
    case 8:
        return 2;
    case 4:
    case 5:
    case 6:
        return 1;
    default:
        return 0;
    }
}

uint8_t V9938::vram_rd(uint32_t addr) const {
    return vram_[addr & (kVramSize - 1)];
}

void V9938::vram_wr(uint32_t addr, uint8_t value) {
    vram_[addr & (kVramSize - 1)] = value;
}

uint32_t V9938::cpu_linear() const {
    return (uint32_t(regs_[14] & 7) << 14) | uint32_t(vram_where_ & 0x3FFF);
}

uint32_t V9938::cpu_phys() const {
    uint32_t i = cpu_linear();
    if (memtype() > 1) i = ((i >> 1) + (i << 16)) & 0x1FFFF;
    return i;
}

void V9938::next_where() {
    vram_where_ = (vram_where_ + 1) & 0x3FFF;
    if (vram_where_ == 0 && memtype() != 0) regs_[14] = uint8_t((regs_[14] + 1) & 7);
}

uint8_t V9938::ram_recv() {
    const uint8_t value = vram_rd(cpu_phys());
    next_where();
    return value;
}

void V9938::ram_send(uint8_t value) {
    vram_wr(cpu_phys(), value);
    next_where();
}

int V9938::bit_bmp() const { return (regs_[2] & 31) * 8 + 7; }
int V9938::map_bm4() const { return (regs_[2] & 96) << 10; }
int V9938::map_bm8() const { return (regs_[2] & 32) << 10; }

uint32_t V9938::ink(uint8_t index) const {
    const int mode = screen_mode();
    if (mode == 6) index &= 3;
    else index &= 0x0F;
    if (index == 0 && !(regs_[8] & 0x20)) {
        const uint8_t bd = uint8_t(regs_[7] & (mode == 6 ? 3 : 0x0F));
        return palette_[bd];
    }
    return palette_[index];
}

uint32_t V9938::backdrop() const {
    if (screen_mode() == 8) return g7_color(regs_[7]);
    return ink(uint8_t(regs_[7] & 0x0F));
}

void V9938::render_t1(int y, uint32_t* buf) {
    const uint32_t nt = uint32_t(regs_[2] & 0x7F) << 10;
    const uint32_t pg = uint32_t(regs_[4] & 0x3F) << 11;
    const uint32_t fg = ink(uint8_t((regs_[7] >> 4) & 0x0F));
    const uint32_t bg = ink(uint8_t(regs_[7] & 0x0F));
    const int row = (y >> 3) & 31;
    const int ymod = y & 7;
    for (int x = 0; x < 256; x++) put256(buf, x, bg);
    for (int col = 0; col < 40; col++) {
        const uint8_t ch = vram_rd(nt + uint32_t(row * 40 + col));
        const uint8_t pat = vram_rd(pg + uint32_t(ch) * 8 + uint32_t(ymod));
        const int px = 8 + col * 6;
        for (int bit = 0; bit < 6; bit++)
            put256(buf, px + bit, (pat & (0x80 >> bit)) ? fg : bg);
    }
}

void V9938::render_t2(int y, uint32_t* buf) {
    const uint32_t nt = uint32_t(regs_[2] & 0x7C) << 10;
    const uint32_t pg = uint32_t(regs_[4] & 0x3F) << 11;
    const uint32_t bg = ink(uint8_t(regs_[7] & 0x0F));
    const uint32_t fg = ink(uint8_t((regs_[7] >> 4) & 0x0F));
    const int row = (y >> 3) & 31;
    const int ymod = y & 7;
    for (int x = 0; x < kPaperWidth; x++) buf[x] = bg;
    const int pad = (kPaperWidth - 480) / 2;
    for (int col = 0; col < 80; col++) {
        const uint8_t ch = vram_rd(nt + uint32_t(row * 80 + col));
        const uint8_t pat = vram_rd(pg + uint32_t(ch) * 8 + uint32_t(ymod));
        const int px = pad + col * 6;
        for (int bit = 0; bit < 6; bit++) {
            const int x = px + bit;
            if (x >= 0 && x < kPaperWidth) buf[x] = (pat & (0x80 >> bit)) ? fg : bg;
        }
    }
}

void V9938::render_g1(int y, uint32_t* buf) {
    const uint32_t nt = uint32_t(regs_[2] & 0x7F) << 10;
    const uint32_t ct = uint32_t(regs_[3]) << 6;
    const uint32_t pg = uint32_t(regs_[4] & 0x3F) << 11;
    const int row = (y >> 3) & 31;
    const int ymod = y & 7;
    for (int col = 0; col < 32; col++) {
        const uint8_t ch = vram_rd(nt + uint32_t(row * 32 + col));
        const uint8_t pat = vram_rd(pg + uint32_t(ch) * 8 + uint32_t(ymod));
        const uint8_t clr = vram_rd(ct + (ch >> 3));
        const uint32_t fg = ink(uint8_t((clr >> 4) & 0x0F));
        const uint32_t bg = ink(uint8_t(clr & 0x0F));
        const int px = col * 8;
        for (int bit = 0; bit < 8; bit++)
            put256(buf, px + bit, (pat & (0x80 >> bit)) ? fg : bg);
    }
}

void V9938::render_g2(int y, uint32_t* buf) {
    const uint32_t nt = uint32_t(regs_[2] & 0x7F) << 10;
    const uint32_t ct_base = uint32_t(regs_[3] & 0x80) << 6;
    const uint32_t pg_base = uint32_t(regs_[4] & 0x04) << 11;
    const uint16_t ct_mask = uint16_t((uint16_t(regs_[3] & 0x7F) << 3) | 0x07);
    const uint16_t pg_mask = uint16_t((uint16_t(regs_[4] & 0x03) << 8) | 0xFF);
    const int row = (y >> 3) & 31;
    const int ymod = y & 7;
    const int third = (y & 0xC0) << 2;
    for (int col = 0; col < 32; col++) {
        const uint8_t ch = vram_rd(nt + uint32_t(row * 32 + col));
        const uint16_t idx = uint16_t((ch + third) * 8 + ymod);
        const uint8_t pat = vram_rd(pg_base + (idx & ((pg_mask << 3) | 7)));
        const uint8_t clr = vram_rd(ct_base + (idx & ((ct_mask << 3) | 7)));
        const uint32_t fg = ink(uint8_t((clr >> 4) & 0x0F));
        const uint32_t bg = ink(uint8_t(clr & 0x0F));
        const int px = col * 8;
        for (int bit = 0; bit < 8; bit++)
            put256(buf, px + bit, (pat & (0x80 >> bit)) ? fg : bg);
    }
}

void V9938::render_mc(int y, uint32_t* buf) {
    const uint32_t nt = uint32_t(regs_[2] & 0x7F) << 10;
    const uint32_t pg = uint32_t(regs_[4] & 0x3F) << 11;
    const int row = (y >> 3) & 31;
    const int ymod = (y >> 2) & 7;
    for (int col = 0; col < 32; col++) {
        const uint8_t ch = vram_rd(nt + uint32_t(row * 32 + col));
        const uint8_t clr = vram_rd(pg + uint32_t(ch) * 8 + uint32_t(ymod));
        const uint32_t c1 = ink(uint8_t((clr >> 4) & 0x0F));
        const uint32_t c2 = ink(uint8_t(clr & 0x0F));
        const int px = col * 8;
        for (int x = 0; x < 4; x++) put256(buf, px + x, c1);
        for (int x = 4; x < 8; x++) put256(buf, px + x, c2);
    }
}

void V9938::render_g4(int y, uint32_t* buf) {
    const int i = (y & bit_bmp()) << 7;
    const int j = map_bm4();
    for (int x = 0; x < 128; x++) {
        const uint8_t b = vram_rd(uint32_t(j + i + x));
        put256(buf, x * 2, ink(uint8_t(b >> 4)));
        put256(buf, x * 2 + 1, ink(uint8_t(b & 0x0F)));
    }
}

void V9938::render_g5(int y, uint32_t* buf) {
    const int i = (y & bit_bmp()) << 7;
    const int j = map_bm4();
    for (int x = 0; x < 128; x++) {
        const uint8_t b = vram_rd(uint32_t(j + i + x));
        buf[x * 4] = ink(uint8_t(b >> 6));
        buf[x * 4 + 1] = ink(uint8_t((b >> 4) & 3));
        buf[x * 4 + 2] = ink(uint8_t((b >> 2) & 3));
        buf[x * 4 + 3] = ink(uint8_t(b & 3));
    }
}

void V9938::render_g6(int y, uint32_t* buf) {
    const int i = (y & bit_bmp()) << 7;
    const int j = map_bm8();
    for (int x = 0; x < 256; x++) {
        const uint8_t b = vram_rd(uint32_t(j + i + (x >> 1) + (x & 1) * 65536));
        buf[x * 2] = ink(uint8_t(b >> 4));
        buf[x * 2 + 1] = ink(uint8_t(b & 0x0F));
    }
}

void V9938::render_g7(int y, uint32_t* buf) {
    const int i = (y & bit_bmp()) << 7;
    const int j = map_bm8();
    for (int x = 0; x < 256; x++) {
        const uint8_t b = vram_rd(uint32_t(j + i + (x >> 1) + (x & 1) * 65536));
        put256(buf, x, g7_color(b));
    }
}

void V9938::render_sprites_m1(int y, uint32_t* buf) {
    const uint32_t sat = uint32_t(regs_[5] & 0x7F) << 7;
    const uint32_t spg = uint32_t(regs_[6] & 0x07) << 11;
    const int size = (regs_[1] & 0x02) ? 16 : 8;
    const int mag = (regs_[1] & 0x01) ? 2 : 1;
    int drawn = 0;
    for (int i = 0; i < 32 && drawn < 4; i++) {
        int sy0 = vram_rd(sat + uint32_t(i * 4));
        if (sy0 == 208) break;
        sy0 = (sy0 + 1) & 0xFF;
        if (y < sy0 || y >= sy0 + size * mag) continue;
        int x = vram_rd(sat + uint32_t(i * 4 + 1));
        int pat = vram_rd(sat + uint32_t(i * 4 + 2));
        const int attr = vram_rd(sat + uint32_t(i * 4 + 3));
        if (attr & 0x80) x -= 32;
        const int clr = attr & 0x0F;
        if (clr == 0) {
            drawn++;
            continue;
        }
        const uint32_t color = ink(uint8_t(clr));
        if (size == 16) pat &= 0xFC;
        const int sy = (y - sy0) / mag;
        for (int sx = 0; sx < size; sx++) {
            int bx = sx;
            const int by = sy;
            uint8_t bits;
            if (size == 16) {
                const int quad = (bx >= 8 ? 1 : 0) + (by >= 8 ? 2 : 0);
                bits = vram_rd(spg + uint32_t(pat + quad) * 8 + uint32_t(by & 7));
                bx &= 7;
            } else {
                bits = vram_rd(spg + uint32_t(pat) * 8 + uint32_t(by));
            }
            if (bits & (0x80 >> bx)) {
                for (int m = 0; m < mag; m++) {
                    const int px = x + sx * mag + m;
                    if (px >= 0 && px < 256) put256(buf, px, color);
                }
            }
        }
        drawn++;
    }
}

void V9938::render_sprites_m2(int y, uint32_t* buf) {
    const uint32_t sat_base =
        (uint32_t(regs_[11] & 0x03) << 15) | (uint32_t(regs_[5] & 0xFC) << 7);
    const uint32_t ct = sat_base - 0x200;
    const uint32_t spg = uint32_t(regs_[6] & 0x3F) << 11;
    const int size = (regs_[1] & 0x02) ? 16 : 8;
    const int mag = (regs_[1] & 0x01) ? 2 : 1;
    const bool tp = (regs_[8] & 0x20) != 0;
    int drawn = 0;
    for (int i = 0; i < 32 && drawn < 8; i++) {
        int sy0 = vram_rd(sat_base + uint32_t(i * 4));
        if (sy0 == 216) break;
        sy0 = (sy0 + 1) & 0xFF;
        if (y < sy0 || y >= sy0 + size * mag) continue;
        int x = vram_rd(sat_base + uint32_t(i * 4 + 1));
        int pat = vram_rd(sat_base + uint32_t(i * 4 + 2));
        const int sy = (y - sy0) / mag;
        const uint8_t cattr = vram_rd(ct + uint32_t(i * 16 + sy));
        if (cattr & 0x40) x -= 32;
        const int clr = cattr & 0x0F;
        if (clr == 0 && !(cattr & 0x20) && !tp) {
            drawn++;
            continue;
        }
        const uint32_t color = palette_[std::size_t(clr)];
        if (size == 16) pat &= 0xFC;
        for (int sx = 0; sx < size; sx++) {
            int bx = sx;
            const int by = sy;
            uint8_t bits;
            if (size == 16) {
                const int quad = (bx >= 8 ? 1 : 0) + (by >= 8 ? 2 : 0);
                bits = vram_rd(spg + uint32_t(pat + quad) * 8 + uint32_t(by & 7));
                bx &= 7;
            } else {
                bits = vram_rd(spg + uint32_t(pat) * 8 + uint32_t(by));
            }
            if (bits & (0x80 >> bx)) {
                for (int m = 0; m < mag; m++) {
                    const int px = x + sx * mag + m;
                    if (px >= 0 && px < 256) put256(buf, px, color);
                }
            }
        }
        drawn++;
    }
}

void V9938::render_line(int line) {
    const int active = active_lines();
    const int top_blank = (active == 212) ? 0 : 10;
    const int disp_line = line - (kBorderV + top_blank);
    const uint32_t border = backdrop();
    uint32_t* row = framebuffer_.data() + (line * kScreenWidth);

    if (disp_line < 0 || disp_line >= active || !(regs_[1] & 0x40)) {
        for (int x = 0; x < kScreenWidth; x++) row[x] = border;
        return;
    }

    for (int x = 0; x < kBorderH; x++) row[x] = border;
    for (int x = kBorderH + kPaperWidth; x < kScreenWidth; x++) row[x] = border;

    uint32_t* paper = row + kBorderH;
    // R#23 is an 8-bit wrap (256), not modulo the visible height.
    const int vy = int(uint8_t(disp_line + regs_[23]));
    const int mode = screen_mode();

    switch (mode) {
    case 0: render_t1(vy, paper); break;
    case 1: render_g1(vy, paper); break;
    case 2:
    case 4: render_g2(vy, paper); break;
    case 3: render_mc(vy, paper); break;
    case 5: render_g4(vy, paper); break;
    case 6: render_g5(vy, paper); break;
    case 7: render_g6(vy, paper); break;
    case 8: render_g7(vy, paper); break;
    case 10: render_t2(vy, paper); break;
    default:
        for (int x = 0; x < kPaperWidth; x++) paper[x] = border;
        break;
    }

    if (mode >= 1 && mode <= 3)
        render_sprites_m1(vy, paper);
    else if (mode >= 4 && mode <= 8)
        render_sprites_m2(vy, paper);
}

int V9938::blit_update() {
    blit_ay_ = (regs_[45] & 8) ? -1 : 1;
    blit_ax_ = (regs_[45] & 4) ? -1 : 1;
    switch (screen_mode()) {
    case 5:  // G4: 256×4-bit linear
        blit_xl_ = 255;
        blit_yl_ = 1023;
        blit_mask_ = 15;
        blit_step_ = 2;
        blit_addx_ = int8_t(blit_ax_ * blit_step_);
        blit_xh_ = ~blit_xl_;
        blit_yh_ = ~blit_yl_;
        return blit_case_ = 0;
    case 6:  // G5: 512×2-bit linear
        blit_xl_ = 511;
        blit_yl_ = 1023;
        blit_mask_ = 3;
        blit_step_ = 4;
        blit_addx_ = int8_t(blit_ax_ * blit_step_);
        blit_xh_ = ~blit_xl_;
        blit_yh_ = ~blit_yl_;
        return blit_case_ = 1;
    case 7:  // G6: 512×4-bit planar
        blit_xl_ = 511;
        blit_yl_ = 511;
        blit_mask_ = 15;
        blit_step_ = 2;
        blit_addx_ = int8_t(blit_ax_ * blit_step_);
        blit_xh_ = ~blit_xl_;
        blit_yh_ = ~blit_yl_;
        return blit_case_ = 2;
    case 8:  // G7: 256×8-bit planar
        blit_bits_ = 0;
        blit_xl_ = 255;
        blit_yl_ = 511;
        blit_mask_ = 255;
        blit_step_ = 1;
        blit_addx_ = int8_t(blit_ax_ * blit_step_);
        blit_xh_ = ~blit_xl_;
        blit_yh_ = ~blit_yl_;
        return blit_case_ = 3;
    default:
        blit_case_ = -1;
        return -1;
    }
}

uint8_t* V9938::blit_offs(int x, int y) {
    uint32_t addr = 0;
    switch (blit_case_) {
    case 0:
        blit_bits_ = uint8_t((1 & ~x) << 2);
        addr = uint32_t(y << 7) + uint32_t(x >> 1);
        break;
    case 1:
        blit_bits_ = uint8_t((3 & ~x) << 1);
        addr = uint32_t(y << 7) + uint32_t(x >> 2);
        break;
    case 2:
        blit_bits_ = uint8_t((1 & ~x) << 2);
        addr = uint32_t(y << 7) + uint32_t(x >> 2) + uint32_t((x & 2) << 15);
        break;
    default:
        blit_bits_ = 0;
        addr = uint32_t(y << 7) + uint32_t(x >> 1) + uint32_t((x & 1) << 16);
        break;
    }
    return &vram_[addr & (kVramSize - 1)];
}

uint8_t V9938::blit_test(int x, int y) {
    return uint8_t(*blit_offs(x, y) >> blit_bits_);
}

void V9938::blit_logo(int x, int y, uint8_t color) {
    color &= blit_mask_;
    if (color == 0 && (regs_[46] & 8)) return;
    uint8_t* o = blit_offs(x, y);
    const uint8_t m = uint8_t(blit_mask_ << blit_bits_);
    switch (regs_[46] & 7) {
    case 1:
        *o &= uint8_t((~m) + (color << blit_bits_));
        break;
    case 2:
        *o |= uint8_t(color << blit_bits_);
        break;
    case 3:
        *o ^= uint8_t(color << blit_bits_);
        break;
    case 4:
        color = uint8_t(blit_mask_ & ~color);
        *o = uint8_t((*o & ~m) + (color << blit_bits_));
        break;
    default:
        *o = uint8_t((*o & ~m) + (color << blit_bits_));
        break;
    }
}

unsigned V9938::blit_get_sx() const {
    return (unsigned(regs_[33]) << 8 | regs_[32]) & unsigned(blit_xl_);
}
unsigned V9938::blit_get_sy() const {
    return (unsigned(regs_[35]) << 8 | regs_[34]) & unsigned(blit_yl_);
}
unsigned V9938::blit_get_dx() const {
    return (unsigned(regs_[37]) << 8 | regs_[36]) & unsigned(blit_xl_);
}
unsigned V9938::blit_get_dy() const {
    return (unsigned(regs_[39]) << 8 | regs_[38]) & unsigned(blit_yl_);
}
unsigned V9938::blit_get_nx() const {
    return (unsigned(regs_[41]) << 8 | regs_[40]) & unsigned(blit_xl_);
}
unsigned V9938::blit_get_ny(int add) const {
    return (mgetii(&regs_[42]) + unsigned(add)) & unsigned(blit_yl_);
}
void V9938::blit_set_sy(unsigned y) {
    regs_[35] = uint8_t((y >> 8) & 3);
    regs_[34] = uint8_t(y);
}
void V9938::blit_set_dy(unsigned y) {
    regs_[39] = uint8_t((y >> 8) & 3);
    regs_[38] = uint8_t(y);
}
void V9938::blit_set_ny(unsigned y) {
    regs_[43] = uint8_t((y >> 8) & 3);
    regs_[42] = uint8_t(y);
}

void V9938::blit_launch() {
    cmd_op_ = regs_[46] >> 4;
    if (cmd_op_ == 0) {
        cmd_busy_ = false;
        status_[2] &= ~0x81;
        return;
    }
    if (blit_update() < 0) {
        regs_[46] &= 15;
        cmd_op_ = 0;
        cmd_busy_ = false;
        status_[2] &= ~0x81;
        return;
    }
    status_[2] = uint8_t((status_[2] & 0x6E) | 0x0D);  // CE, drop TR/BD
    cmd_busy_ = true;
    blit_sx_ = blit_get_sx();
    blit_dx_ = blit_get_dx();
    if (regs_[46] < 128) {
        blit_nx_ = mgetii(&regs_[40]) & 1023;
        blit_nz_ = int((blit_nx_ - 1) >> 1);
    } else {
        blit_nx_ = blit_get_nx();
        blit_nz_ = 0;
    }
    blit_run();
}

void V9938::blit_run() {
    int guard = 0;
    while (cmd_busy_ && (status_[2] & 1) && guard++ < 1024 * 1024) {
        const int cmd = regs_[46] >> 4;
        unsigned s = 0, d = 0, n = 0;
        switch (cmd) {
        case 15: {  // HMMC
            if (blit_nz_) return;
            d = blit_get_dy();
            *blit_offs(int(blit_dx_), int(d)) = regs_[44];
            blit_dx_ += unsigned(blit_addx_);
            if ((blit_nx_ -= blit_step_) < blit_step_ || (int(blit_dx_) & blit_xh_)) {
                blit_dx_ = blit_get_dx();
                blit_nx_ = blit_get_nx();
                d += unsigned(blit_ay_);
                blit_set_dy(d);
                n = blit_get_ny(-1);
                blit_set_ny(n);
                if (!n || (int(d) & blit_yh_)) {
                    status_[2] &= ~0x01;
                    cmd_busy_ = false;
                    break;
                }
            }
            blit_nz_ = 1;
            status_[2] |= 0x80;
            return;
        }
        case 14: {  // YMMM
            s = blit_get_sy();
            d = blit_get_dy();
            *blit_offs(int(blit_dx_), int(d)) = *blit_offs(int(blit_dx_), int(s));
            blit_dx_ += unsigned(blit_addx_);
            if (int(blit_dx_) & blit_xh_) {
                blit_dx_ = blit_get_dx();
                s += unsigned(blit_ay_);
                blit_set_sy(s);
                d += unsigned(blit_ay_);
                blit_set_dy(d);
                n = blit_get_ny(-1);
                blit_set_ny(n);
                if (!n || ((int(s) | int(d)) & blit_yh_)) {
                    status_[2] &= ~0x01;
                    cmd_busy_ = false;
                }
            }
            break;
        }
        case 13: {  // HMMM
            s = blit_get_sy();
            d = blit_get_dy();
            *blit_offs(int(blit_dx_), int(d)) = *blit_offs(int(blit_sx_), int(s));
            blit_sx_ += unsigned(blit_addx_);
            blit_dx_ += unsigned(blit_addx_);
            if ((blit_nx_ -= blit_step_) < blit_step_ ||
                ((int(blit_sx_) | int(blit_dx_)) & blit_xh_)) {
                blit_sx_ = blit_get_sx();
                blit_nx_ = blit_get_nx();
                blit_dx_ = blit_get_dx();
                s += unsigned(blit_ay_);
                blit_set_sy(s);
                d += unsigned(blit_ay_);
                blit_set_dy(d);
                n = blit_get_ny(-1);
                blit_set_ny(n);
                if (!n || ((int(s) | int(d)) & blit_yh_)) {
                    status_[2] &= ~0x01;
                    cmd_busy_ = false;
                }
            }
            break;
        }
        case 12: {  // HMMV
            d = blit_get_dy();
            *blit_offs(int(blit_dx_), int(d)) = regs_[44];
            blit_dx_ += unsigned(blit_addx_);
            if ((blit_nx_ -= blit_step_) < blit_step_ || (int(blit_dx_) & blit_xh_)) {
                blit_dx_ = blit_get_dx();
                blit_nx_ = blit_get_nx();
                d += unsigned(blit_ay_);
                blit_set_dy(d);
                n = blit_get_ny(-1);
                blit_set_ny(n);
                if (!n || (int(d) & blit_yh_)) {
                    status_[2] &= ~0x01;
                    cmd_busy_ = false;
                }
            }
            break;
        }
        case 11: {  // LMMC
            if (blit_nz_) return;
            d = blit_get_dy();
            blit_logo(int(blit_dx_), int(d), regs_[44]);
            blit_dx_ += unsigned(blit_ax_);
            if (!--blit_nx_ || (int(blit_dx_) & blit_xh_)) {
                blit_dx_ = blit_get_dx();
                blit_nx_ = blit_get_nx();
                d += unsigned(blit_ay_);
                blit_set_dy(d);
                n = blit_get_ny(-1);
                blit_set_ny(n);
                if (!n || (int(d) & blit_yh_)) {
                    status_[2] &= ~0x01;
                    cmd_busy_ = false;
                    break;
                }
            }
            blit_nz_ = 1;
            status_[2] |= 0x80;
            return;
        }
        case 10:  // LMCM
            status_[2] |= 0x80;
            status_[7] = uint8_t(blit_test(int(blit_sx_), int(blit_get_sy())) & blit_mask_);
            return;
        case 9: {  // LMMM
            s = blit_get_sy();
            d = blit_get_dy();
            blit_logo(int(blit_dx_), int(d), blit_test(int(blit_sx_), int(s)));
            blit_sx_ += unsigned(blit_ax_);
            blit_dx_ += unsigned(blit_ax_);
            if (!--blit_nx_ || ((int(blit_sx_) | int(blit_dx_)) & blit_xh_)) {
                blit_sx_ = blit_get_sx();
                blit_nx_ = blit_get_nx();
                blit_dx_ = blit_get_dx();
                s += unsigned(blit_ay_);
                blit_set_sy(s);
                d += unsigned(blit_ay_);
                blit_set_dy(d);
                n = blit_get_ny(-1);
                blit_set_ny(n);
                if (!n || ((int(s) | int(d)) & blit_yh_)) {
                    status_[2] &= ~0x01;
                    cmd_busy_ = false;
                }
            }
            break;
        }
        case 8: {  // LMMV
            d = blit_get_dy();
            blit_logo(int(blit_dx_), int(d), regs_[44]);
            blit_dx_ += unsigned(blit_ax_);
            if (!--blit_nx_ || (int(blit_dx_) & blit_xh_)) {
                blit_dx_ = blit_get_dx();
                blit_nx_ = blit_get_nx();
                d += unsigned(blit_ay_);
                blit_set_dy(d);
                n = blit_get_ny(-1);
                blit_set_ny(n);
                if (!n || (int(d) & blit_yh_)) {
                    status_[2] &= ~0x01;
                    cmd_busy_ = false;
                }
            }
            break;
        }
        case 7: {  // LINE — major length is NX+1; NY=0 stays axis-aligned
            d = blit_get_dy();
            blit_logo(int(blit_dx_), int(d), regs_[44]);
            if (!blit_nx_) {
                status_[2] &= ~0x01;
                cmd_busy_ = false;
                break;
            }
            if (regs_[45] & 1)
                d += unsigned(blit_ay_);
            else
                blit_dx_ += unsigned(blit_ax_);
            if ((blit_nz_ -= int(mgetii(&regs_[42]) & 511)) < 0) {
                blit_nz_ += int(mgetii(&regs_[40]) & 1023);
                if (regs_[45] & 1)
                    blit_dx_ += unsigned(blit_ax_);
                else
                    d += unsigned(blit_ay_);
            }
            blit_set_dy(d);
            if ((int(blit_dx_) & blit_xh_) || (int(d) & blit_yh_)) {
                status_[2] &= ~0x01;
                cmd_busy_ = false;
            } else {
                --blit_nx_;
            }
            break;
        }
        case 6: {  // SRCH
            const uint8_t pix =
                uint8_t((blit_test(int(blit_sx_), int(blit_get_sy())) ^ regs_[44]) & blit_mask_);
            const bool match = (regs_[45] & 2) ? pix != 0 : pix == 0;
            if (match) {
                status_[8] = uint8_t(blit_sx_);
                status_[9] = uint8_t((blit_sx_ >> 8) | 0xFE);
                status_[2] = uint8_t((status_[2] & 0x6E) | 0x10);
                cmd_busy_ = false;
            } else {
                blit_sx_ += unsigned(blit_ax_);
                if (int(blit_sx_) & blit_xh_) {
                    status_[8] = uint8_t(blit_sx_);
                    status_[9] = uint8_t((blit_sx_ >> 8) | 0xFE);
                    status_[2] &= 0x6E;
                    cmd_busy_ = false;
                }
            }
            break;
        }
        case 5:  // PSET
            blit_logo(int(blit_dx_), int(blit_get_dy()), regs_[44]);
            status_[2] &= ~0x01;
            cmd_busy_ = false;
            break;
        case 4:  // POINT
            status_[7] = uint8_t(blit_test(int(blit_sx_), int(blit_get_sy())) & blit_mask_);
            status_[2] &= ~0x01;
            cmd_busy_ = false;
            break;
        default:
            status_[2] &= ~0x01;
            cmd_busy_ = false;
            break;
        }
    }
    if (!cmd_busy_) status_[2] &= ~0x01;
}

void V9938::blit_lmcm() {
    const unsigned s0 = blit_get_sy();
    status_[2] &= ~0x80;
    status_[7] = uint8_t(blit_test(int(blit_sx_), int(s0)) & blit_mask_);
    blit_sx_ += unsigned(blit_ax_);
    unsigned s = s0;
    if (!--blit_nx_ || (int(blit_sx_) & blit_xh_)) {
        blit_sx_ = blit_get_sx();
        blit_nx_ = blit_get_nx();
        s += unsigned(blit_ay_);
        blit_set_sy(s);
        const unsigned n = blit_get_ny(-1);
        blit_set_ny(n);
        if (!n || (int(s) & blit_yh_)) {
            status_[2] &= ~0x01;
            cmd_busy_ = false;
        }
    }
}

void V9938::command_write_byte(uint8_t data) {
    regs_[44] = data;
    if (!cmd_busy_) return;
    const int cmd = regs_[46] >> 4;
    if (cmd == 0x0B || cmd == 0x0F) {
        blit_nz_ = 0;
        blit_run();
    }
}

void V9938::write_register(int index, uint8_t value) {
    if (index == 17) {
        regs_[17] = value;
        return;
    }
    if (index < 0 || index >= kNumRegs) return;
    regs_[std::size_t(index)] = value;
    if (index == 44 && cmd_busy_ && (status_[2] & 0x80)) command_write_byte(value);
    if (index == 46) blit_launch();
}

uint8_t V9938::port_read(int port) {
    switch (port & 3) {
    case 0: {
        const uint8_t val = read_buf_;
        read_buf_ = ram_recv();
        latch_flag_ = false;
        return val;
    }
    case 1: {
        uint8_t sr = regs_[15] & 0x0F;
        if (sr >= kNumStatus) sr = 0;
        uint8_t val = status_[sr];
        if (sr == 0) {
            status_[0] &= 0x1F;
            irq_vblank_ = false;
        }
        if (sr == 1) {
            status_[1] &= ~0x01;
            irq_hblank_ = false;
        }
        if (sr == 2) {
            if (!(status_[2] & 1)) status_[2] &= ~0x80;
            val = status_[2];
        }
        if (sr == 7 && cmd_busy_ && (regs_[46] >> 4) == 0x0A) blit_lmcm();
        latch_flag_ = false;
        return val;
    }
    default:
        return 0xFF;
    }
}

void V9938::port_write(int port, uint8_t val) {
    switch (port & 3) {
    case 0:
        if (cmd_busy_ && ((regs_[46] >> 4) == 0x0B || (regs_[46] >> 4) == 0x0F)) {
            command_write_byte(val);
        } else {
            read_buf_ = val;
            ram_send(val);
        }
        latch_flag_ = false;
        break;

    case 1:
        if (!latch_flag_) {
            latch_ = val;
            latch_flag_ = true;
        } else {
            latch_flag_ = false;
            if (val & 0x80) {
                write_register(val & 0x3F, latch_);
            } else {
                vram_where_ = int(((uint32_t(val & 0x3F) << 8) | latch_) & 0x3FFF);
                if (!(val & 0x40)) read_buf_ = ram_recv();
            }
        }
        break;

    case 2:
        if (!pal_flag_) {
            pal_latch_ = val;
            pal_flag_ = true;
        } else {
            pal_flag_ = false;
            const uint8_t entry = regs_[16] & 0x0F;
            const uint8_t r = (pal_latch_ >> 4) & 0x07;
            const uint8_t b = pal_latch_ & 0x07;
            const uint8_t g = val & 0x07;
            pal_rgb_[entry][0] = r;
            pal_rgb_[entry][1] = g;
            pal_rgb_[entry][2] = b;
            palette_[entry] = rgb3_to_argb(r, g, b);
            regs_[16] = (regs_[16] + 1) & 0x0F;
        }
        break;

    case 3: {
        const uint8_t reg = regs_[17] & 0x3F;
        if (reg != 17) write_register(reg, val);
        if (!(regs_[17] & 0x80))
            regs_[17] = uint8_t((regs_[17] & 0x80) | ((reg + 1) & 0x3F));
        break;
    }
    }
}

V9938::V9938() { reset(); }

void V9938::reset() {
    vram_.fill(0);
    regs_.fill(0);
    status_.fill(0);
    palette_.fill(0);
    framebuffer_.fill(0);
    for (int i = 0; i < 16; i++) {
        pal_rgb_[i][0] = kDefaultPal[i][0];
        pal_rgb_[i][1] = kDefaultPal[i][1];
        pal_rgb_[i][2] = kDefaultPal[i][2];
        palette_[i] = rgb3_to_argb(kDefaultPal[i][0], kDefaultPal[i][1], kDefaultPal[i][2]);
    }
    latch_ = 0;
    latch_flag_ = false;
    read_buf_ = 0;
    vram_where_ = 0;
    pal_latch_ = 0;
    pal_flag_ = false;
    frame_counter_ = 0;
    irq_vblank_ = false;
    irq_hblank_ = false;
    cmd_op_ = 0;
    cmd_busy_ = false;
    blit_nx_ = blit_sx_ = blit_dx_ = 0;
    blit_nz_ = 0;
    blit_case_ = -1;
    status_[2] = 0x0C;
    status_[4] = 0xFE;
    status_[6] = 0xFC;
    status_[9] = 0xFE;
    regs_[9] = 0x02;
}

void V9938::begin_frame() {
    status_[0] |= 0x80;
    status_[2] |= 0x40;
    if (regs_[1] & 0x20) irq_vblank_ = true;
    frame_counter_++;
}

void V9938::check_line_irq(int line) {
    if (line == kBorderV) status_[2] &= ~0x40;
    if (line == (regs_[19] + kBorderV) && (regs_[0] & 0x10)) {
        status_[1] |= 0x01;
        irq_hblank_ = true;
    }
}

}  // namespace dsp
