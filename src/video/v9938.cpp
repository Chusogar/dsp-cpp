#include "video/v9938.h"

#include <cstring>

namespace dsp {
namespace {

const uint8_t v9938_default_pal[16][3] = { // R,G,B (3-bit each)
    {0,0,0},{0,0,0},{1,6,1},{3,7,3},{1,1,7},{2,3,7},{5,1,1},{2,6,7},
    {7,1,1},{7,3,3},{6,6,1},{6,7,4},{1,4,1},{6,2,5},{5,5,5},{7,7,7}
};

uint32_t rgb3_to_argb(uint8_t r3, uint8_t g3, uint8_t b3) {
    uint8_t r = (r3 << 5) | (r3 << 2) | (r3 >> 1);
    uint8_t g = (g3 << 5) | (g3 << 2) | (g3 >> 1);
    uint8_t b = (b3 << 5) | (b3 << 2) | (b3 >> 1);
    return 0xFF000000 | (r << 16) | (g << 8) | b;
}

// G7 (Screen 8) fixed 256-color palette: 3-3-2 bit RGB
uint32_t g7_color(uint8_t c) {
    uint8_t r = (c >> 5) & 7;
    uint8_t g = (c >> 2) & 7;
    uint8_t b = c & 3;
    r = (r << 5) | (r << 2) | (r >> 1);
    g = (g << 5) | (g << 2) | (g >> 1);
    b = (b << 6) | (b << 4) | (b << 2) | b;
    return 0xFF000000 | (r << 16) | (g << 8) | b;
}

}  // namespace

// =============================================================================
// V9938 VDP - Modo de pantalla
// =============================================================================

// Screen mode from mode bits M1-M5
int V9938::screen_mode() const {
    int m1 = (regs_[1] >> 4) & 1;
    int m2 = (regs_[1] >> 3) & 1;
    int m3 = (regs_[0] >> 1) & 1;
    int m4 = (regs_[0] >> 2) & 1;
    int m5 = (regs_[0] >> 3) & 1;
    int bits = (m5 << 4) | (m4 << 3) | (m3 << 2) | (m2 << 1) | m1;
    switch (bits) {
    case 0x01: return 0;   // T1  (Screen 0, 40 col)
    case 0x09: return 10;  // T2  (Screen 0, 80 col)
    case 0x00: return 1;   // G1  (Screen 1)
    case 0x04: return 2;   // G2  (Screen 2)
    case 0x02: return 3;   // MC  (Screen 3)
    case 0x06: return 4;   // G3  (Screen 4)
    case 0x08: return 5;   // G4  (Screen 5)
    case 0x0C: return 6;   // G5  (Screen 6)
    case 0x10: return 7;   // G6  (Screen 7)
    case 0x18: return 8;   // G7  (Screen 8)
    default:   return 1;
    }
}

// Active lines: 192 or 212
int V9938::active_lines() const {
    return (regs_[9] & 0x80) ? 212 : 192;
}

// =============================================================================
// V9938 VDP - VRAM access helpers
// =============================================================================

uint8_t V9938::vram_rd(uint32_t addr) const {
    return vram_[addr & (kVramSize - 1)];
}

void V9938::vram_wr(uint32_t addr, uint8_t val) {
    vram_[addr & (kVramSize - 1)] = val;
}

// =============================================================================
// V9938 VDP - Scanline rendering
// =============================================================================

// Render T1 (Screen 0, 40-column text)
void V9938::render_t1(int line, uint32_t* buf) {
    uint32_t nt = (uint32_t)(regs_[2] & 0x7F) << 10; // name table
    uint32_t pg = (uint32_t)(regs_[4] & 0x3F) << 11; // pattern gen
    uint8_t  fg_idx = (regs_[7] >> 4) & 0x0F;
    uint8_t  bg_idx = regs_[7] & 0x0F;
    uint32_t fg = palette_[fg_idx ? fg_idx : 0];
    uint32_t bg = palette_[bg_idx ? bg_idx : 0];
    int row = line / 8;
    int ymod = line & 7;
    // 8-pixel left border, 240 pixels text (40 chars × 6), 8-pixel right border
    for (int x = 0; x < kPaperWidth; x++) buf[x] = bg;
    for (int col = 0; col < 40; col++) {
        uint8_t ch = vram_rd(nt + row * 40 + col);
        uint8_t pat = vram_rd(pg + ch * 8 + ymod);
        int px = 8 + col * 6;
        for (int bit = 0; bit < 6; bit++) {
            if (px + bit < kPaperWidth)
                buf[px + bit] = (pat & (0x80 >> bit)) ? fg : bg;
        }
    }
}

// Render G1 (Screen 1, 32×24 tiles)
void V9938::render_g1(int line, uint32_t* buf) {
    uint32_t nt = (uint32_t)(regs_[2] & 0x7F) << 10;
    uint32_t ct = (uint32_t)(regs_[3]) << 6;
    uint32_t pg = (uint32_t)(regs_[4] & 0x3F) << 11;
    int row = line / 8;
    int ymod = line & 7;
    for (int col = 0; col < 32; col++) {
        uint8_t ch = vram_rd(nt + row * 32 + col);
        uint8_t pat = vram_rd(pg + ch * 8 + ymod);
        uint8_t clr = vram_rd(ct + (ch >> 3));
        uint8_t fg_i = (clr >> 4) & 0x0F;
        uint8_t bg_i = clr & 0x0F;
        uint32_t fg = palette_[fg_i ? fg_i : 0];
        uint32_t bg = palette_[bg_i ? bg_i : 0];
        int px = col * 8;
        for (int bit = 0; bit < 8; bit++)
            buf[px + bit] = (pat & (0x80 >> bit)) ? fg : bg;
    }
}

// Render G2/G3 (Screen 2/4, high-res tiles)
void V9938::render_g2(int line, uint32_t* buf) {
    uint32_t nt = (uint32_t)(regs_[2] & 0x7F) << 10;
    uint32_t ct_base = (uint32_t)(regs_[3] & 0x80) << 6;
    uint32_t pg_base = (uint32_t)(regs_[4] & 0x04) << 11;
    uint16_t ct_mask = ((uint16_t)(regs_[3] & 0x7F) << 3) | 0x07;
    uint16_t pg_mask = ((uint16_t)(regs_[4] & 0x03) << 8) | 0xFF;
    // Never mask off bits in practice for most Spectrum-like games: both are 0x1FFF
    int row = line / 8;
    int ymod = line & 7;
    int third = (line / 64) * 256; // 0, 256, or 512
    for (int col = 0; col < 32; col++) {
        uint8_t ch = vram_rd(nt + row * 32 + col);
        uint16_t idx = ((ch + third) * 8 + ymod);
        uint8_t pat = vram_rd(pg_base + (idx & (pg_mask * 8 + 7)));
        uint8_t clr = vram_rd(ct_base + (idx & (ct_mask * 8 + 7)));
        // Correct masking for G2: pattern index & mask
        pat = vram_rd(pg_base + (idx & ((pg_mask << 3) | 7)));
        clr = vram_rd(ct_base + (idx & ((ct_mask << 3) | 7)));
        uint8_t fg_i = (clr >> 4) & 0x0F;
        uint8_t bg_i = clr & 0x0F;
        uint32_t fg = palette_[fg_i ? fg_i : 0];
        uint32_t bg = palette_[bg_i ? bg_i : 0];
        int px = col * 8;
        for (int bit = 0; bit < 8; bit++)
            buf[px + bit] = (pat & (0x80 >> bit)) ? fg : bg;
    }
}

// Render MC (Screen 3, multicolor)
void V9938::render_mc(int line, uint32_t* buf) {
    uint32_t nt = (uint32_t)(regs_[2] & 0x7F) << 10;
    uint32_t pg = (uint32_t)(regs_[4] & 0x3F) << 11;
    int row = line / 8;
    int sub = (line / 4) & 1;
    for (int col = 0; col < 32; col++) {
        uint8_t ch = vram_rd(nt + row * 32 + col);
        uint8_t clr = vram_rd(pg + ch * 8 + sub * 2 + ((row & 3) >= 2 ? 1 : 0));
        // Hmm, MC addressing: pattern data at pg + ch*8 + (line/4)%2 * ... 
        // Actually: each char gives 2 colors per 4-pixel-high block
        clr = vram_rd(pg + ch * 8 + ((line >> 2) & 7));
        uint8_t hi = (clr >> 4) & 0x0F;
        uint8_t lo = clr & 0x0F;
        uint32_t c1 = palette_[hi ? hi : 0];
        uint32_t c2 = palette_[lo ? lo : 0];
        int px = col * 8;
        for (int x = 0; x < 4; x++) buf[px + x] = c1;
        for (int x = 4; x < 8; x++) buf[px + x] = c2;
    }
}

// Render G4 (Screen 5, 256×212 bitmap, 4bpp)
void V9938::render_g4(int line, uint32_t* buf) {
    uint32_t base = (uint32_t)(regs_[2] & 0x60) << 10;
    uint32_t addr = base + (uint32_t)line * 128;
    for (int x = 0; x < 256; x += 2) {
        uint8_t byte = vram_rd(addr++);
        buf[x]     = palette_[(byte >> 4) & 0x0F];
        buf[x + 1] = palette_[byte & 0x0F];
    }
}

// Render G5 (Screen 6, 512×212 bitmap, 2bpp → display as 256 wide)
void V9938::render_g5(int line, uint32_t* buf) {
    uint32_t base = (uint32_t)(regs_[2] & 0x60) << 10;
    uint32_t addr = base + (uint32_t)line * 128;
    for (int x = 0; x < 256; x += 2) {
        uint8_t byte = vram_rd(addr++);
        // 4 pixels per byte, 2 bits each; take pixels 0 and 2 for 2:1 downscale
        buf[x]     = palette_[(byte >> 6) & 3];
        buf[x + 1] = palette_[(byte >> 2) & 3];
    }
}

// Render G6 (Screen 7, 512×212 bitmap, 4bpp → display as 256 wide)
void V9938::render_g6(int line, uint32_t* buf) {
    uint32_t base = (uint32_t)(regs_[2] & 0x20) << 11;
    uint32_t addr = base + (uint32_t)line * 256;
    for (int x = 0; x < 256; x++) {
        uint8_t byte = vram_rd(addr);
        // Two 4bpp pixels at 512 width → we take the left one
        buf[x] = palette_[(byte >> 4) & 0x0F];
        addr++;
    }
}

// Render G7 (Screen 8, 256×212 bitmap, 8bpp, fixed 3-3-2 palette)
void V9938::render_g7(int line, uint32_t* buf) {
    uint32_t base = (uint32_t)(regs_[2] & 0x60) << 10;
    uint32_t addr = base + (uint32_t)line * 256;
    for (int x = 0; x < 256; x++)
        buf[x] = g7_color(vram_rd(addr++));
}

// Render sprites mode 1 (Screen 1-3: 8/16 px, 4 per line, 1 color)
void V9938::render_sprites_m1(int line, uint32_t* buf) {
    uint32_t sat = (uint32_t)(regs_[5] & 0x7F) << 7;
    uint32_t spg = (uint32_t)(regs_[6] & 0x07) << 11;
    int size = (regs_[1] & 0x02) ? 16 : 8;
    int mag  = (regs_[1] & 0x01) ? 2 : 1;
    int drawn = 0;
    for (int i = 0; i < 32 && drawn < 4; i++) {
        int y = vram_rd(sat + i * 4);
        if (y == 208) break;
        y = (y + 1) & 0xFF;
        if (line < y || line >= y + size * mag) continue;
        int x    = vram_rd(sat + i * 4 + 1);
        int pat  = vram_rd(sat + i * 4 + 2);
        int attr = vram_rd(sat + i * 4 + 3);
        if (attr & 0x80) x -= 32;
        int clr = attr & 0x0F;
        if (clr == 0) { drawn++; continue; }
        uint32_t color = palette_[clr];
        if (size == 16) pat &= 0xFC;
        int sy = (line - y) / mag;
        for (int sx = 0; sx < size; sx++) {
            int bx = sx;
            int by = sy;
            uint8_t bits;
            if (size == 16) {
                int quad = (bx >= 8 ? 1 : 0) + (by >= 8 ? 2 : 0);
                bits = vram_rd(spg + (pat + quad) * 8 + (by & 7));
                bx &= 7;
            } else {
                bits = vram_rd(spg + pat * 8 + by);
            }
            if (bits & (0x80 >> bx)) {
                for (int m = 0; m < mag; m++) {
                    int px = x + sx * mag + m;
                    if (px >= 0 && px < 256) buf[px] = color;
                }
            }
        }
        drawn++;
    }
}

// Render sprites mode 2 (Screen 4+: 8/16 px, 8 per line, color per line)
void V9938::render_sprites_m2(int line, uint32_t* buf) {
    uint32_t sat_base = ((uint32_t)(regs_[11] & 0x03) << 15) |
                        ((uint32_t)(regs_[5] & 0xFC) << 7);
    uint32_t ct = sat_base - 0x200;  // color table is 512 bytes before SAT
    uint32_t spg = (uint32_t)(regs_[6] & 0x3F) << 11;
    int size = (regs_[1] & 0x02) ? 16 : 8;
    int mag  = (regs_[1] & 0x01) ? 2 : 1;
    int drawn = 0;
    for (int i = 0; i < 32 && drawn < 8; i++) {
        int y = vram_rd(sat_base + i * 4);
        if (y == 216) break;
        y = (y + 1) & 0xFF;
        if (line < y || line >= y + size * mag) continue;
        int x    = vram_rd(sat_base + i * 4 + 1);
        int pat  = vram_rd(sat_base + i * 4 + 2);
        int sy   = (line - y) / mag;
        uint8_t cattr = vram_rd(ct + i * 16 + sy);
        if (cattr & 0x40) x -= 32; // EC bit
        int clr  = cattr & 0x0F;
        bool cc  = (cattr & 0x20) != 0; // OR mode
        (void)cc;
        if (clr == 0 && !(cattr & 0x20)) { drawn++; continue; }
        uint32_t color = palette_[clr];
        if (size == 16) pat &= 0xFC;
        for (int sx = 0; sx < size; sx++) {
            int bx = sx, by = sy;
            uint8_t bits;
            if (size == 16) {
                int quad = (bx >= 8 ? 1 : 0) + (by >= 8 ? 2 : 0);
                bits = vram_rd(spg + (pat + quad) * 8 + (by & 7));
                bx &= 7;
            } else {
                bits = vram_rd(spg + pat * 8 + by);
            }
            if (bits & (0x80 >> bx)) {
                for (int m = 0; m < mag; m++) {
                    int px = x + sx * mag + m;
                    if (px >= 0 && px < 256) buf[px] = color;
                }
            }
        }
        drawn++;
    }
}

// Render one scanline
void V9938::render_line(int line) {
    int active = active_lines();
    int vscroll = regs_[23];
    int top_blank = (active == 212) ? 0 : 10; // adjust for 192/212 modes
    int disp_line = line - (kBorderV + top_blank);

    uint32_t border = palette_[regs_[7] & 0x0F];
    uint32_t* row = framebuffer_.data() + (line * kScreenWidth);

    // Border or blank line?
    if (disp_line < 0 || disp_line >= active || !(regs_[1] & 0x40)) {
        for (int x = 0; x < kScreenWidth; x++) row[x] = border;
        return;
    }

    // Left border
    for (int x = 0; x < kBorderH; x++) row[x] = border;
    // Right border
    for (int x = kBorderH + kPaperWidth; x < kScreenWidth; x++) row[x] = border;

    uint32_t* paper = row + kBorderH;
    int render_line = (disp_line + vscroll) % active;
    int mode = screen_mode();

    switch (mode) {
    case 0:  render_t1(render_line, paper); break;
    case 1:  render_g1(render_line, paper); break;
    case 2:  // G2 (Screen 2)
    case 4:  // G3 (Screen 4)
        render_g2(render_line, paper); break;
    case 3:  render_mc(render_line, paper); break;
    case 5:  render_g4(render_line, paper); break;
    case 6:  render_g5(render_line, paper); break;
    case 7:  render_g6(render_line, paper); break;
    case 8:  render_g7(render_line, paper); break;
    case 10: // T2 (80 col) - render as T1 simplified
        render_t1(render_line, paper); break;
    default:
        for (int x = 0; x < kPaperWidth; x++) paper[x] = border;
        break;
    }

    // Sprites
    if (mode >= 1 && mode <= 3)
        render_sprites_m1(render_line, paper);
    else if (mode >= 4 && mode <= 8)
        render_sprites_m2(render_line, paper);
}

// =============================================================================
// V9938 VDP - Comandos
// =============================================================================

// Logical operation
uint8_t V9938::log_op(int op, uint8_t src, uint8_t dst) const {
    switch (op & 0x0F) {
    case 0x0: return src;              // IMP
    case 0x1: return src & dst;        // AND
    case 0x2: return src | dst;        // OR
    case 0x3: return src ^ dst;        // XOR
    case 0x4: return ~src & dst;       // NOT
    case 0x8: return src ? src : dst;  // TIMP
    case 0x9: return src ? (src & dst) : dst;
    case 0xA: return src ? (src | dst) : dst;
    case 0xB: return src ? (src ^ dst) : dst;
    case 0xC: return src ? (~src & dst) : dst;
    default:  return src;
    }
}

// Get pixel from VRAM (bitmap modes)
uint8_t V9938::get_pixel(int x, int y) const {
    int mode = screen_mode();
    uint32_t base = (uint32_t)(regs_[2] & 0x60) << 10;
    switch (mode) {
    case 5: { // G4: 4bpp, 256 wide
        uint32_t addr = base + (uint32_t)y * 128 + x / 2;
        uint8_t byte = vram_rd(addr);
        return (x & 1) ? (byte & 0x0F) : ((byte >> 4) & 0x0F);
    }
    case 6: { // G5: 2bpp, 512 wide
        uint32_t addr = base + (uint32_t)y * 128 + x / 4;
        uint8_t byte = vram_rd(addr);
        int shift = (3 - (x & 3)) * 2;
        return (byte >> shift) & 0x03;
    }
    case 7: { // G6: 4bpp, 512 wide
        base = (uint32_t)(regs_[2] & 0x20) << 11;
        uint32_t addr = base + (uint32_t)y * 256 + x / 2;
        uint8_t byte = vram_rd(addr);
        return (x & 1) ? (byte & 0x0F) : ((byte >> 4) & 0x0F);
    }
    case 8: { // G7: 8bpp, 256 wide
        uint32_t addr = base + (uint32_t)y * 256 + x;
        return vram_rd(addr);
    }
    default: return 0;
    }
}

// Set pixel in VRAM (bitmap modes)
void V9938::set_pixel(int x, int y, uint8_t clr) {
    int mode = screen_mode();
    uint32_t base = (uint32_t)(regs_[2] & 0x60) << 10;
    switch (mode) {
    case 5: { // G4
        uint32_t addr = base + (uint32_t)y * 128 + x / 2;
        uint8_t byte = vram_rd(addr);
        if (x & 1) byte = (byte & 0xF0) | (clr & 0x0F);
        else       byte = (byte & 0x0F) | ((clr & 0x0F) << 4);
        vram_wr(addr, byte);
        break;
    }
    case 6: { // G5
        uint32_t addr = base + (uint32_t)y * 128 + x / 4;
        uint8_t byte = vram_rd(addr);
        int shift = (3 - (x & 3)) * 2;
        uint8_t mask = 0x03 << shift;
        byte = (byte & ~mask) | ((clr & 0x03) << shift);
        vram_wr(addr, byte);
        break;
    }
    case 7: { // G6
        base = (uint32_t)(regs_[2] & 0x20) << 11;
        uint32_t addr = base + (uint32_t)y * 256 + x / 2;
        uint8_t byte = vram_rd(addr);
        if (x & 1) byte = (byte & 0xF0) | (clr & 0x0F);
        else       byte = (byte & 0x0F) | ((clr & 0x0F) << 4);
        vram_wr(addr, byte);
        break;
    }
    case 8: { // G7
        uint32_t addr = base + (uint32_t)y * 256 + x;
        vram_wr(addr, clr);
        break;
    }
    default: break;
    }
}

int V9938::line_x_mask() const {
    const int mode = screen_mode();
    return (mode == 6 || mode == 7) ? 512 : 256;
}

void V9938::start_cpu_transfer(int cmd, int dx, int dy, int nx, int ny, int arg, uint8_t first) {
    cmd_op_ = cmd;
    cmd_dx_ = dx;
    cmd_dy_ = dy;
    cmd_nx_ = nx;
    cmd_ny_ = ny;
    cmd_px_ = 0;
    cmd_py_ = 0;
    cmd_arg_ = arg;
    cmd_busy_ = true;
    status_[2] |= 0x81;  // CE + TR
    command_write_byte(first);
}

void V9938::write_register(int index, uint8_t value) {
    if (index == 17) {
        regs_[17] = value;
        return;
    }
    if (cmd_busy_ && index == 44 && (cmd_op_ == 0x0B || cmd_op_ == 0x0F)) {
        regs_[44] = value;
        command_write_byte(value);
        return;
    }
    if (index < 0 || index >= kNumRegs) return;
    regs_[std::size_t(index)] = value;
    if (index == 46 && value >= 0x40) exec_command();
}

// Execute VDP command instantly
void V9938::exec_command() {
    int cmd = regs_[46] >> 4;
    int sx = regs_[32] | ((regs_[33] & 0x01) << 8);
    int sy = regs_[34] | ((regs_[35] & 0x03) << 8);
    int dx = regs_[36] | ((regs_[37] & 0x01) << 8);
    int dy = regs_[38] | ((regs_[39] & 0x03) << 8);
    int nx = regs_[40] | ((regs_[41] & 0x01) << 8);
    int ny = regs_[42] | ((regs_[43] & 0x03) << 8);
    int clr = regs_[44];
    int arg = regs_[45];
    int dix = (arg & 0x04) ? -1 : 1;
    int diy = (arg & 0x08) ? -1 : 1;
    int log = regs_[46] & 0x0F;

    // Area-move commands treat NX/NY=0 as the maximum size. LINE uses the raw
    // deltas: NY=0 is a straight horizontal/vertical stroke, which is how the
    // MSX2 BIOS draws the boot logo.
    if (cmd != 0x7) {
        if (nx == 0) nx = 512;
        if (ny == 0) ny = 1024;
    } else {
        nx &= 1023;
        ny &= 1023;
    }

    cmd_busy_ = false;
    status_[2] &= ~0x01; // clear TR
    status_[2] &= ~0x80; // clear CE

    switch (cmd) {
    case 0x0: break; // STOP
    case 0x5: // PSET
        set_pixel(dx, dy, clr);
        break;
    case 0x4: // POINT
        status_[7] = get_pixel(sx, sy);
        break;
    case 0x8: // LMMV (logical fill)
        for (int y = 0; y < ny; y++)
            for (int x = 0; x < nx; x++) {
                int px = dx + x * dix, py = dy + y * diy;
                uint8_t dst = get_pixel(px & 0x1FF, py & 0x3FF);
                set_pixel(px & 0x1FF, py & 0x3FF, log_op(log, clr, dst));
            }
        break;
    case 0x9: // LMMM (logical move)
        for (int y = 0; y < ny; y++)
            for (int x = 0; x < nx; x++) {
                int spx = sx + x * dix, spy = sy + y * diy;
                int dpx = dx + x * dix, dpy = dy + y * diy;
                uint8_t src = get_pixel(spx & 0x1FF, spy & 0x3FF);
                uint8_t dst = get_pixel(dpx & 0x1FF, dpy & 0x3FF);
                set_pixel(dpx & 0x1FF, dpy & 0x3FF, log_op(log, src, dst));
            }
        break;
    case 0xB: // LMMC (logical move CPU→VRAM); first pixel is already in R#44
        start_cpu_transfer(cmd, dx, dy, nx, ny, arg, uint8_t(clr));
        return;
    case 0xC: // HMMV (high-speed fill)
        for (int y = 0; y < ny; y++) {
            uint32_t base = (uint32_t)(regs_[2] & 0x60) << 10;
            int py = (dy + y * diy) & 0x3FF;
            for (int x = 0; x < nx; x++) {
                int px = (dx + x * dix) & 0x1FF;
                uint32_t addr = base + (uint32_t)py * 128 + px / 2;
                if (screen_mode() == 8) // G7: byte per pixel
                    addr = base + (uint32_t)py * 256 + px;
                vram_wr(addr, clr);
            }
        }
        break;
    case 0xD: // HMMM (high-speed move)
        for (int y = 0; y < ny; y++) {
            uint32_t base = (uint32_t)(regs_[2] & 0x60) << 10;
            int spy = (sy + y * diy) & 0x3FF;
            int dpy = (dy + y * diy) & 0x3FF;
            int bpl = (screen_mode() == 8) ? 256 : 128;
            for (int x = 0; x < nx; x++) {
                int spx = (sx + x * dix) & 0x1FF;
                int dpx = (dx + x * dix) & 0x1FF;
                uint32_t sa, da;
                if (bpl == 256) { sa = base + spy*256 + spx; da = base + dpy*256 + dpx; }
                else { sa = base + spy*128 + spx/2; da = base + dpy*128 + dpx/2; }
                vram_wr(da, vram_rd(sa));
            }
        }
        break;
    case 0xE: // YMMM (high-speed move Y-only)
        for (int y = 0; y < ny; y++) {
            int spy = (sy + y * diy) & 0x3FF;
            int dpy = (dy + y * diy) & 0x3FF;
            int bpl = (screen_mode() == 8) ? 256 : 128;
            uint32_t base = (uint32_t)(regs_[2] & 0x60) << 10;
            for (int x = 0; x < bpl; x++)
                vram_wr(base + dpy * bpl + x, vram_rd(base + spy * bpl + x));
        }
        break;
    case 0xF: // HMMC (high-speed move CPU→VRAM); first byte is already in R#44
        start_cpu_transfer(cmd, dx, dy, nx, ny, arg, uint8_t(clr));
        return;
    case 0x7: // LINE — MAME/openMSX Bresenham (ASX starts at (NX-1)/2)
    {
        int asx = (nx - 1) >> 1;
        int adx = 0;
        int px = dx;
        int py = dy;
        const bool ymaj = (arg & 0x01) != 0;
        const int xmask = line_x_mask();
        for (;;) {
            uint8_t dst = get_pixel(px & 0x1FF, py & 0x3FF);
            set_pixel(px & 0x1FF, py & 0x3FF, log_op(log, uint8_t(clr), dst));
            if (!ymaj) {
                px += dix;
                if ((asx -= ny) < 0) {
                    asx += nx;
                    py += diy;
                }
                asx &= 1023;
                if (adx++ == nx || (px & xmask)) break;
            } else {
                py += diy;
                if ((asx -= ny) < 0) {
                    asx += nx;
                    px += dix;
                }
                asx &= 1023;
                if (adx++ == nx || (px & xmask)) break;
            }
        }
        break;
    }
    case 0x6: // SRCH
    {
        int px = sx;
        bool eq = (arg & 0x02) == 0;
        for (int x = 0; x < 512; x++) {
            uint8_t p = get_pixel(px & 0x1FF, sy & 0x3FF);
            if ((eq && p == (clr & 0x0F)) || (!eq && p != (clr & 0x0F))) {
                status_[2] |= 0x10; // BD
                status_[8] = px & 0xFF;
                status_[9] = (px >> 8) & 0x01;
                break;
            }
            px += dix;
        }
        break;
    }
    case 0xA: // LMCM (logical move VRAM→CPU)
        cmd_op_ = cmd;
        cmd_sx_ = sx; cmd_sy_ = sy;
        cmd_nx_ = nx; cmd_ny_ = ny;
        cmd_px_ = 0;  cmd_py_ = 0;
        cmd_arg_ = arg;
        cmd_busy_ = true;
        status_[2] |= 0x81;
        // Pre-read first byte
        status_[7] = get_pixel(sx, sy);
        return;
    default:
        break;
    }
}

// LMMC/HMMC byte transfer
void V9938::command_write_byte(uint8_t data) {
    if (!cmd_busy_) return;
    int dix = (cmd_arg_ & 0x04) ? -1 : 1;
    int diy = (cmd_arg_ & 0x08) ? -1 : 1;

    if (cmd_op_ == 0x0B) { // LMMC
        int px = (cmd_dx_ + cmd_px_ * dix) & 0x1FF;
        int py = (cmd_dy_ + cmd_py_ * diy) & 0x3FF;
        int log = regs_[46] & 0x0F;
        uint8_t dst = get_pixel(px, py);
        set_pixel(px, py, log_op(log, data, dst));
    } else if (cmd_op_ == 0x0F) { // HMMC
        int px = (cmd_dx_ + cmd_px_ * dix) & 0x1FF;
        int py = (cmd_dy_ + cmd_py_ * diy) & 0x3FF;
        uint32_t base = (uint32_t)(regs_[2] & 0x60) << 10;
        int bpl = (screen_mode() == 8) ? 256 : 128;
        vram_wr(base + py * bpl + (bpl == 256 ? px : px / 2), data);
    }

    cmd_px_++;
    if (cmd_px_ >= cmd_nx_) {
        cmd_px_ = 0;
        cmd_py_++;
        if (cmd_py_ >= cmd_ny_) {
            cmd_busy_ = false;
            status_[2] &= ~0x81;
        }
    }
}

// LMCM byte read
uint8_t V9938::command_read_byte() {
    if (!cmd_busy_ || cmd_op_ != 0x0A) return 0xFF;
    int dix = (cmd_arg_ & 0x04) ? -1 : 1;
    int diy = (cmd_arg_ & 0x08) ? -1 : 1;
    int px = (cmd_sx_ + cmd_px_ * dix) & 0x1FF;
    int py = (cmd_sy_ + cmd_py_ * diy) & 0x3FF;
    uint8_t val = get_pixel(px, py);
    cmd_px_++;
    if (cmd_px_ >= cmd_nx_) {
        cmd_px_ = 0;
        cmd_py_++;
        if (cmd_py_ >= cmd_ny_) {
            cmd_busy_ = false;
            status_[2] &= ~0x81;
        }
    }
    // Pre-read next byte
    if (cmd_busy_) {
        int npx = (cmd_sx_ + cmd_px_ * dix) & 0x1FF;
        int npy = (cmd_sy_ + cmd_py_ * diy) & 0x3FF;
        status_[7] = get_pixel(npx, npy);
    }
    return val;
}

// =============================================================================
// V9938 VDP - Port I/O
// =============================================================================

uint8_t V9938::port_read(int port) {
    switch (port & 3) {
    case 0: { // Port 0x98: VRAM data read
        uint8_t val = read_buf_;
        read_buf_ = vram_rd(vram_addr_);
        vram_addr_ = (vram_addr_ + 1) & (kVramSize - 1);
        latch_flag_ = false;
        return val;
    }
    case 1: { // Port 0x99: Status register read
        uint8_t sr = regs_[15] & 0x0F;
        if (sr >= kNumStatus) sr = 0;
        uint8_t val = status_[sr];
        if (sr == 0) {
            status_[0] &= 0x1F; // clear VBLANK int, overflow, collision flags
            irq_vblank_ = false;
        }
        if (sr == 1) {
            status_[1] &= ~0x01; // clear HBLANK flag
            irq_hblank_ = false;
        }
        if (sr == 2 && cmd_busy_ && cmd_op_ == 0x0A) {
            val = status_[2];
            status_[7] = command_read_byte();
        }
        latch_flag_ = false;
        return val;
    }
    default:
        return 0xFF;
    }
}

void V9938::port_write(int port, uint8_t val) {
    switch (port & 3) {
    case 0: // Port 0x98: VRAM data write (or HMMC/LMMC payload)
        if (cmd_busy_ && (cmd_op_ == 0x0B || cmd_op_ == 0x0F)) {
            command_write_byte(val);
        } else {
            read_buf_ = val;
            vram_wr(vram_addr_, val);
            vram_addr_ = (vram_addr_ + 1) & (kVramSize - 1);
        }
        latch_flag_ = false;
        break;

    case 1: // Port 0x99: Register/address setup
        if (!latch_flag_) {
            latch_ = val;
            latch_flag_ = true;
        } else {
            latch_flag_ = false;
            if (val & 0x80) {
                write_register(val & 0x3F, latch_);
            } else {
                // VRAM address set
                vram_addr_ = ((uint32_t)(regs_[14] & 0x07) << 14) |
                               ((uint32_t)(val & 0x3F) << 8) | latch_;
                vram_write_ = (val & 0x40) != 0;
                if (!vram_write_) {
                    read_buf_ = vram_rd(vram_addr_);
                    vram_addr_ = (vram_addr_ + 1) & (kVramSize - 1);
                }
            }
        }
        break;

    case 2: // Port 0x9A: Palette write
        if (!pal_flag_) {
            pal_latch_ = val;
            pal_flag_ = true;
        } else {
            pal_flag_ = false;
            uint8_t entry = regs_[16] & 0x0F;
            uint8_t r = (pal_latch_ >> 4) & 0x07;
            uint8_t b = pal_latch_ & 0x07;
            uint8_t g = val & 0x07;
            pal_rgb_[entry][0] = r;
            pal_rgb_[entry][1] = g;
            pal_rgb_[entry][2] = b;
            palette_[entry] = rgb3_to_argb(r, g, b);
            regs_[16] = (regs_[16] + 1) & 0x0F;
        }
        break;

    case 3: // Port 0x9B: Indirect register access
    {
        uint8_t reg = regs_[17] & 0x3F;
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
        pal_rgb_[i][0] = v9938_default_pal[i][0];
        pal_rgb_[i][1] = v9938_default_pal[i][1];
        pal_rgb_[i][2] = v9938_default_pal[i][2];
        palette_[i] = rgb3_to_argb(v9938_default_pal[i][0], v9938_default_pal[i][1],
                                   v9938_default_pal[i][2]);
    }
    latch_ = 0;
    latch_flag_ = false;
    read_buf_ = 0;
    vram_addr_ = 0;
    vram_write_ = false;
    pal_latch_ = 0;
    pal_flag_ = false;
    scanline_ = 0;
    frame_counter_ = 0;
    irq_vblank_ = false;
    irq_hblank_ = false;
    cmd_sx_ = cmd_sy_ = cmd_dx_ = cmd_dy_ = 0;
    cmd_nx_ = cmd_ny_ = cmd_clr_ = cmd_arg_ = cmd_op_ = 0;
    cmd_px_ = cmd_py_ = 0;
    cmd_busy_ = false;
    regs_[9] = 0x02;
}

void V9938::begin_frame() {
    status_[0] |= 0x80;  // VBLANK flag is set every frame; IE0 only gates IRQ
    status_[2] |= 0x40;  // VR
    if (regs_[1] & 0x20) irq_vblank_ = true;
    frame_counter_++;
}

void V9938::check_line_irq(int line) {
    if (line == kBorderV) status_[2] &= ~0x40;  // end of vertical retrace
    if (line == (regs_[19] + kBorderV) && (regs_[0] & 0x10)) {
        status_[1] |= 0x01;
        irq_hblank_ = true;
    }
}


}  // namespace dsp
