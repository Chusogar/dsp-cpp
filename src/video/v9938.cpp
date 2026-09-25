#include "video/v9938.h"

#include <algorithm>
#include <cstring>

namespace dsp {
namespace {

constexpr uint8_t kStatF = 0x80;
constexpr uint8_t kStatFh = 0x01;
constexpr uint8_t kStatCe = 0x01;
constexpr uint8_t kStatTr = 0x80;
constexpr uint8_t kStatVr = 0x40;
constexpr uint8_t kStatHr = 0x20;

uint32_t rgb333(int r, int g, int b) {
    auto expand = [](int v) { return uint8_t((std::clamp(v, 0, 7) * 255) / 7); };
    return 0xff000000u | (uint32_t(expand(r)) << 16) | (uint32_t(expand(g)) << 8) | expand(b);
}

const int kDefaultPal[16][3] = {
    {0, 0, 0}, {0, 0, 0}, {1, 6, 1}, {3, 7, 3}, {1, 1, 7}, {2, 3, 7}, {5, 1, 1}, {2, 6, 7},
    {7, 1, 1}, {7, 3, 3}, {6, 6, 1}, {6, 6, 4}, {1, 4, 1}, {6, 2, 5}, {5, 5, 5}, {7, 7, 7},
};

}  // namespace

V9938::V9938(InterruptHandler on_interrupt) : on_interrupt_(std::move(on_interrupt)) { reset(); }

void V9938::set_default_palette() {
    for (int i = 0; i < 16; i++) {
        int r = kDefaultPal[i][0], g = kDefaultPal[i][1], b = kDefaultPal[i][2];
        palette_[size_t(i)] = uint16_t(((r & 7) << 8) | ((b & 7) << 4) | (g & 7));
    }
}

void V9938::reset() {
    vram_.fill(0);
    registers_.fill(0);
    status_.fill(0);
    framebuffer_.fill(0xff000000u);
    set_default_palette();
    address_ = 0;
    read_buffer_ = 0;
    latch_byte_ = 0;
    latch_pending_ = false;
    last_int_line_ = false;
    status_read_s0_ = true;
    palette_byte_ = 0;
    palette_high_ = false;
    scanline_ = 0;
    in_vblank_ = false;
    command_ce_ = false;
    command_tr_ = false;
    command_ = 0;
    cmd_bd_ = false;
    hr_phase_ = 0;
    registers_[1] = 0x10;  // TEXT1 like a TMS after power-on
}

uint8_t V9938::vram_get(uint32_t addr) const { return vram_[addr & (kVramSize - 1)]; }

void V9938::vram_set(uint32_t addr, uint8_t value) { vram_[addr & (kVramSize - 1)] = value; }

void V9938::increment_vram() {
    // The low 14 bits count; past $3FFF the V9938 modes carry into R#14,
    // while the TMS9918 modes wrap inside the 16 KiB bank.
    uint32_t low = (address_ + 1) & 0x3fff;
    uint32_t bank = address_ & 0x1c000;
    if (low == 0) {
        const Mode mode = current_mode();
        if (mode != kTxt1 && mode != kG1 && mode != kG2 && mode != kMc) {
            registers_[14] = uint8_t((registers_[14] + 1) & 7);
            bank = uint32_t(registers_[14] & 7) << 14;
        }
    }
    address_ = (bank | low) & (kVramSize - 1);
}

uint8_t V9938::vram_read() {
    uint8_t value = read_buffer_;
    read_buffer_ = vram_get(address_);
    increment_vram();
    latch_pending_ = false;
    return value;
}

void V9938::vram_write(uint8_t value) {
    vram_set(address_, value);
    increment_vram();
    latch_pending_ = false;
}

void V9938::write_register(int index, uint8_t value) {
    index &= 63;
    registers_[size_t(index)] = value;
    if (index == 15) status_read_s0_ = (value & 0x0f) == 0;
    // Selecting a palette entry restarts the two-byte palette write.
    if (index == 16) palette_high_ = false;
    if (index == 44 && command_ce_) {
        uint8_t kind = uint8_t(command_ >> 4);
        if (kind == 0x0f || kind == 0x0b) cpu_data_byte(value);
    }
    if (index == 46) start_command(value);
    update_interrupt_line();
}

void V9938::register_write(uint8_t value) {
    if (!latch_pending_) {
        latch_byte_ = value;
        latch_pending_ = true;
        return;
    }
    latch_pending_ = false;
    if ((value & 0x80) != 0) {
        write_register(value & 0x3f, latch_byte_);
        return;
    }
    uint32_t low = latch_byte_;
    uint32_t mid = uint32_t(value & 0x3f) << 8;
    uint32_t high = uint32_t(registers_[14] & 0x07) << 14;
    address_ = (high | mid | low) & (kVramSize - 1);
    if ((value & 0x40) == 0) {
        read_buffer_ = vram_get(address_);
        increment_vram();
    }
}

uint8_t V9938::status_read() {
    int which = registers_[15] & 0x0f;
    uint8_t value = 0;
    if (which == 0) {
        value = status_[0];
        status_[0] &= 0x1f;  // clear F, 5S, C
        update_interrupt_line();
    } else if (which == 1) {
        value = status_[1] & ~uint8_t(0x01);
        if (status_[1] & kStatFh) value |= kStatFh;
        status_[1] &= uint8_t(~kStatFh);
        update_interrupt_line();
        // ID = 0 → V9938
    } else if (which == 2) {
        value = 0x0c;  // bits 3-2 read as 1
        if (command_ce_) value |= kStatCe;
        if (command_tr_) value |= kStatTr;
        if (in_vblank_) value |= kStatVr;
        if (cmd_bd_) value |= 0x10;
        // No dot clock here: HR alternates so loops waiting for either
        // edge of the horizontal blank make progress.
        if (hr_phase_++ & 1) value |= kStatHr;
        status_[2] = value;
    } else if (which == 7) {
        value = status_[7];
        if (command_ce_ && (command_ >> 4) == 0x0a) lmcm_next();
    } else if (which < 10) {
        value = status_[size_t(which)];
    }
    latch_pending_ = false;
    return value;
}

void V9938::palette_write(uint8_t value) {
    if (!palette_high_) {
        palette_byte_ = value;
        palette_high_ = true;
        return;
    }
    palette_high_ = false;
    int index = registers_[16] & 0x0f;
        uint16_t packed = uint16_t((((palette_byte_ >> 4) & 7) << 8) | ((palette_byte_ & 7) << 4) |
                                   (value & 7));
    palette_[size_t(index)] = packed;
    registers_[16] = uint8_t((index + 1) & 0x0f);
}

void V9938::indirect_write(uint8_t value) {
    int index = registers_[17] & 0x3f;
    write_register(index, value);
    if ((registers_[17] & 0x80) == 0) registers_[17] = uint8_t((index + 1) & 0x3f);
}

void V9938::update_interrupt_line() {
    bool level = ((status_[0] & kStatF) != 0 && irq0_enabled()) ||
                 ((status_[1] & kStatFh) != 0 && irq1_enabled());
    if (level != last_int_line_) {
        last_int_line_ = level;
        if (on_interrupt_) on_interrupt_(level);
    }
}

V9938::Mode V9938::current_mode() const {
    int m5 = (registers_[0] >> 3) & 1;
    int m4 = (registers_[0] >> 2) & 1;
    int m3 = (registers_[0] >> 1) & 1;
    int m2 = (registers_[1] >> 3) & 1;
    int m1 = (registers_[1] >> 4) & 1;
    int code = (m5 << 4) | (m4 << 3) | (m3 << 2) | (m2 << 1) | m1;
    switch (code) {
        case 0x01: return kTxt1;
        case 0x09: return kTxt2;
        case 0x00: return kG1;
        case 0x04: return kG2;
        case 0x02: return kMc;
        case 0x08: return kG3;
        case 0x0c: return kG4;
        case 0x10: return kG5;
        case 0x14: return kG6;
        case 0x1c: return kG7;
        default: return kUnknown;
    }
}

uint32_t V9938::backdrop_argb() const {
    const uint8_t r7 = registers_[7];
    switch (current_mode()) {
        case kG7: {
            // SCREEN 8: the backdrop is an 8-bit GRB332 colour.
            int r = (r7 >> 2) & 7;
            int g = (r7 >> 5) & 7;
            int b = ((r7 & 3) << 1) | (r7 & 1);
            return rgb333(r, g, b);
        }
        case kG5:
            // SCREEN 6 has 4 colours: R#7 bits 1-0 pick the backdrop.
            return palette_argb(uint8_t(r7 & 0x03));
        default:
            return palette_argb(uint8_t(r7 & 0x0f));
    }
}

uint32_t V9938::palette_argb(uint8_t index) const {
    uint16_t p = palette_[index & 0x0f];
    int r = (p >> 8) & 7;
    int b = (p >> 4) & 7;
    int g = p & 7;
    return rgb333(r, g, b);
}

void V9938::plot(int x, int y, uint8_t color, int width) {
    if (y < 0 || y >= kScreenHeight) return;
    // Colour 0 is transparent (shows the backdrop) unless R#8 TP is set.
    uint32_t argb = (color == 0 && (registers_[8] & 0x20) == 0) ? backdrop_argb()
                                                                : palette_argb(color);
    if (width == 256) {
        int dx = x * 2;
        if (dx < 0 || dx + 1 >= kScreenWidth) return;
        framebuffer_[size_t(y * kScreenWidth + dx)] = argb;
        framebuffer_[size_t(y * kScreenWidth + dx + 1)] = argb;
    } else if (x >= 0 && x < kScreenWidth) {
        framebuffer_[size_t(y * kScreenWidth + x)] = argb;
    }
}

void V9938::render_text(int line, int columns) {
    const int y = scrolled_line(line);
    uint32_t nt = columns == 80 ? uint32_t(registers_[2] & 0x7c) << 10 : uint32_t(registers_[2] & 0x7f) << 10;
    uint32_t pt = uint32_t(registers_[4] & 0x3f) << 11;
    uint8_t fg = uint8_t(registers_[7] >> 4);
    uint8_t bg = backdrop();
    int row = y / 8;
    int y_in = y % 8;
    // 40 x 6 = 240 of 256 pixels, 80 x 6 = 480 of 512: centred.
    int px = columns == 80 ? 16 : 8;
    for (int col = 0; col < columns; col++) {
        uint8_t name = vram_get(nt + uint32_t(row * columns + col));
        uint8_t pattern = vram_get(pt + uint32_t(name) * 8 + uint32_t(y_in));
        const int bits = 6;  // TEXT1 and TEXT2 characters are 6 pixels wide
        for (int bit = 0; bit < bits; bit++) {
            bool set = ((pattern >> (7 - bit)) & 1) != 0;
            plot(px + col * bits + bit, line, set ? fg : bg, columns == 80 ? 512 : 256);
        }
    }
}

void V9938::render_g1(int line) {
    const int y = scrolled_line(line);
    int row = (y / 8) % 24;
    int y_in = y % 8;
    uint32_t nt = uint32_t(registers_[2] & 0x7f) << 10;
    uint32_t ct = (uint32_t(registers_[10] & 7) << 14) | (uint32_t(registers_[3]) << 6);
    uint32_t pt = uint32_t(registers_[4] & 0x3f) << 11;
    for (int col = 0; col < 32; col++) {
        uint8_t name = vram_get(nt + uint32_t(row * 32 + col));
        uint8_t pattern = vram_get(pt + uint32_t(name) * 8 + uint32_t(y_in));
        uint8_t colors = vram_get(ct + (name >> 3));
        uint8_t fg = uint8_t(colors >> 4);
        uint8_t bg = uint8_t(colors & 0x0f);
        for (int bit = 0; bit < 8; bit++) {
            bool set = ((pattern >> (7 - bit)) & 1) != 0;
            plot(col * 8 + bit, line, set ? fg : bg, 256);
        }
    }
}

void V9938::render_g2(int line, bool /*g3*/) {
    const int y = scrolled_line(line);
    int row = (y / 8) % 24;
    int y_in = y % 8;
    int third = row / 8;
    // Table bases and the index masks of the TMS9918 GRAPHIC 2 layout; the
    // V9938 adds address bits A14-A16 (R#10 for colours, R#4 bits 3-5).
    uint32_t nt = uint32_t(registers_[2] & 0x7f) << 10;
    uint32_t ct = (uint32_t(registers_[10] & 7) << 14) | (uint32_t(registers_[3] & 0x80) << 6);
    uint32_t cmask = (uint32_t(registers_[3] & 0x7f) << 3) | 7;
    uint32_t pt = uint32_t(registers_[4] & 0x3c) << 11;
    uint32_t pmask = (uint32_t(registers_[4] & 3) << 8) | 0xff;
    for (int col = 0; col < 32; col++) {
        uint8_t name = vram_get(nt + uint32_t(row * 32 + col));
        uint32_t index = uint32_t(third) * 256 + name;
        uint8_t pattern = vram_get(pt + (index & pmask) * 8 + uint32_t(y_in));
        uint8_t colors = vram_get(ct + (index & cmask) * 8 + uint32_t(y_in));
        uint8_t fg = uint8_t(colors >> 4);
        uint8_t bg = uint8_t(colors & 0x0f);
        for (int bit = 0; bit < 8; bit++) {
            bool set = ((pattern >> (7 - bit)) & 1) != 0;
            plot(col * 8 + bit, line, set ? fg : bg, 256);
        }
    }
}

void V9938::render_mc(int line) {
    const int y = scrolled_line(line);
    int row = (y / 8) % 24;
    uint32_t nt = uint32_t(registers_[2] & 0x7f) << 10;
    uint32_t pt = uint32_t(registers_[4] & 0x3f) << 11;
    for (int col = 0; col < 32; col++) {
        uint8_t name = vram_get(nt + uint32_t(row * 32 + col));
        uint8_t colors = vram_get(pt + uint32_t(name) * 8 + uint32_t((row & 3) * 2 + (y % 8) / 4));
        uint8_t left = uint8_t(colors >> 4);
        uint8_t right = uint8_t(colors & 0x0f);
        for (int px = 0; px < 4; px++) plot(col * 8 + px, line, left, 256);
        for (int px = 4; px < 8; px++) plot(col * 8 + px, line, right, 256);
    }
}

int V9938::screen_width_px() const {
    Mode mode = current_mode();
    if (mode == kG5 || mode == kG6 || mode == kTxt2) return 512;
    return 256;
}

int V9938::command_width() const {
    Mode mode = current_mode();
    return (mode == kG5 || mode == kG6) ? 512 : 256;
}

int V9938::bits_per_pixel() const {
    switch (current_mode()) {
        case kG5: return 2;
        case kG4:
        case kG6: return 4;
        case kG7: return 8;
        default: return 8;  // commands in character modes work on bytes
    }
}

int V9938::pixels_per_byte() const { return std::max(8 / std::max(bits_per_pixel(), 1), 1); }

uint32_t V9938::pixel_address(int x, int y) const {
    switch (current_mode()) {
        case kG4:  // 256 x 1024, 128 bytes per line
            return (uint32_t(y & 1023) << 7) | (uint32_t(x & 255) >> 1);
        case kG5:  // 512 x 1024, 128 bytes per line
            return (uint32_t(y & 1023) << 7) | (uint32_t(x & 511) >> 2);
        case kG6:  // 512 x 512, even/odd byte columns in the two 64 KiB banks
            return ((uint32_t(x) & 2) << 15) | (uint32_t(y & 511) << 7) | (uint32_t(x & 511) >> 2);
        case kG7:  // 256 x 512, same interleave by pixel
            return ((uint32_t(x) & 1) << 16) | (uint32_t(y & 511) << 7) | (uint32_t(x & 255) >> 1);
        default:
            return ((uint32_t(x) & 1) << 16) | (uint32_t(y & 511) << 7) | (uint32_t(x & 255) >> 1);
    }
}

uint8_t V9938::get_pixel(int x, int y) const {
    int bpp = bits_per_pixel();
    int ppb = pixels_per_byte();
    uint8_t data = vram_get(pixel_address(x, y));
    int shift = (ppb - 1 - (x % ppb)) * bpp;
    uint8_t mask = uint8_t((1 << bpp) - 1);
    return uint8_t((data >> shift) & mask);
}

void V9938::put_pixel(int x, int y, uint8_t color) {
    int bpp = bits_per_pixel();
    int ppb = pixels_per_byte();
    uint32_t addr = pixel_address(x, y);
    uint8_t data = vram_get(addr);
    int shift = (ppb - 1 - (x % ppb)) * bpp;
    uint8_t mask = uint8_t((1 << bpp) - 1);
    data = uint8_t((data & ~(mask << shift)) | ((color & mask) << shift));
    vram_set(addr, data);
}

void V9938::render_bitmap(int line) {
    const Mode mode = current_mode();
    const int width = screen_width_px();
    // R#2 selects the displayed page: 4 pages of 256 lines in SCREEN 5/6,
    // 2 pages in SCREEN 7/8.  R#23 scrolls vertically within the page.
    const int page = (mode == kG4 || mode == kG5) ? (registers_[2] >> 5) & 3 : (registers_[2] >> 5) & 1;
    const int y = page * 256 + scrolled_line(line);
    for (int x = 0; x < width; x++) {
        uint8_t color = get_pixel(x, y);
        if (mode == kG7) {
            // SCREEN 8: 8-bit GRB332
            int r = (color >> 2) & 7;
            int g = (color >> 5) & 7;
            int b = ((color & 3) << 1) | (color & 1);
            uint32_t argb = rgb333(r, g, b);
            int dx = x * 2;
            framebuffer_[size_t(line * kScreenWidth + dx)] = argb;
            framebuffer_[size_t(line * kScreenWidth + dx + 1)] = argb;
        } else {
            plot(x, line, color, width);
        }
    }
}

void V9938::render_sprites(int line) {
    if (registers_[8] & 0x02) return;  // sprites disabled (SPD)
    const Mode mode = current_mode();
    const bool mode2 = mode != kG1 && mode != kG2 && mode != kMc;
    const bool large = (registers_[1] & 0x02) != 0;
    const bool mag = (registers_[1] & 0x01) != 0;
    const int size = (large ? 16 : 8) * (mag ? 2 : 1);
    const uint32_t base = (uint32_t(registers_[11] & 0x03) << 15) | (uint32_t(registers_[5]) << 7);
    // Mode 2: colour table at the 1 KiB boundary, attributes 512 bytes on.
    const uint32_t sat = mode2 ? ((base & 0x1fc00) | 0x200) : (base & 0x1ff80);
    const uint32_t sct = base & 0x1fc00;
    const uint32_t spt = uint32_t(registers_[6] & 0x3f) << 11;
    const int max_per_line = mode2 ? 8 : 4;
    const int terminator = mode2 ? 216 : 208;
    // Sprites follow the vertical scroll on the V9938.
    const int y = scrolled_line(line);

    std::array<uint8_t, 256> colour{};
    std::array<bool, 256> used{};
    int shown = 0;
    bool group = false;  // a CC=0 sprite is on this line (for CC=1 sprites)
    for (int sp = 0; sp < 32; sp++) {
        const uint32_t attr = sat + uint32_t(sp) * 4;
        const uint8_t sy = vram_get(attr);
        if (sy == terminator) break;
        const int row = (y - (int(sy) + 1)) & 255;
        if (row >= size) continue;
        if (shown >= max_per_line) {
            if ((status_[0] & 0x40) == 0) status_[0] = uint8_t((status_[0] & 0xa0) | 0x40 | sp);
            break;
        }
        shown++;
        const int prow = mag ? row / 2 : row;
        uint8_t pattern = vram_get(attr + 2);
        uint8_t info;
        if (mode2) info = vram_get(sct + uint32_t(sp) * 16 + uint32_t(prow));
        else info = vram_get(attr + 3);
        const uint8_t c = info & 0x0f;
        const bool cc = mode2 && (info & 0x40) != 0;
        const bool ic = mode2 && (info & 0x20) != 0;
        if (cc && !group) continue;  // CC sprites need a CC=0 sprite before them
        if (!cc) group = true;
        int sx = vram_get(attr + 1);
        if (info & 0x80) sx -= 32;  // EC
        if (large) pattern &= 0xfc;
        uint16_t bits = uint16_t(vram_get(spt + uint32_t(pattern) * 8 + uint32_t(prow)) << 8);
        if (large) bits |= vram_get(spt + uint32_t(pattern) * 8 + 16 + uint32_t(prow));
        const int count = large ? 16 : 8;
        for (int px = 0; px < count; px++) {
            if (!((bits >> (15 - px)) & 1)) continue;
            for (int m = 0; m < (mag ? 2 : 1); m++) {
                const int dx = sx + (mag ? px * 2 + m : px);
                if (dx < 0 || dx > 255) continue;
                if (cc) {
                    colour[size_t(dx)] |= c;
                    used[size_t(dx)] = true;
                } else if (!used[size_t(dx)]) {
                    // Colour 0 is transparent and does not hide later sprites.
                    if (c != 0 || (mode2 && (registers_[8] & 0x20))) {
                        colour[size_t(dx)] = c;
                        used[size_t(dx)] = true;
                    }
                } else if (!ic) {
                    status_[0] |= 0x20;  // collision
                }
            }
        }
    }
    if (!shown) return;
    for (int x = 0; x < 256; x++) {
        if (!used[size_t(x)]) continue;
        const uint8_t c = colour[size_t(x)];
        uint32_t* out = &framebuffer_[size_t(line * kScreenWidth + x * 2)];
        if (mode == kG7) {
            // SCREEN 8 sprites use a fixed 16-colour GRB332 palette.
            static const uint8_t kG7Sprite[16] = {0x00, 0x02, 0x10, 0x12, 0x80, 0x82, 0x90, 0x92,
                                                  0x49, 0x0b, 0x59, 0x5b, 0xc9, 0xcb, 0xd9, 0xdb};
            const uint8_t v = kG7Sprite[c];
            out[0] = out[1] = rgb333((v >> 2) & 7, (v >> 5) & 7, ((v & 3) << 1) | (v & 1));
        } else if (mode == kG5) {
            // SCREEN 6: one sprite pixel is two 4-colour pixels (bits 3-2, 1-0).
            out[0] = ((c >> 2) & 3) == 0 && !(registers_[8] & 0x20) ? backdrop_argb() : palette_argb((c >> 2) & 3);
            out[1] = (c & 3) == 0 && !(registers_[8] & 0x20) ? backdrop_argb() : palette_argb(c & 3);
        } else {
            plot(x, line, c, 256);
        }
    }
}

void V9938::render_scanline(int line) {
    uint32_t bg = backdrop_argb();
    for (int x = 0; x < kScreenWidth; x++) framebuffer_[size_t(line * kScreenWidth + x)] = bg;
    if (!display_enabled()) return;
    Mode mode = current_mode();
    switch (mode) {
        case kTxt1: render_text(line, 40); break;
        case kTxt2: render_text(line, 80); break;
        case kG1: render_g1(line); break;
        case kG2: render_g2(line, false); break;
        case kG3: render_g2(line, true); break;
        case kMc: render_mc(line); break;
        case kG4:
        case kG5:
        case kG6:
        case kG7: render_bitmap(line); break;
        default: break;
    }
    if (mode != kTxt1 && mode != kTxt2 && mode != kUnknown) render_sprites(line);
}

void V9938::refresh_line(int line, int total_lines) {
    (void)total_lines;
    scanline_ = line;
    int height = visible_height();
    in_vblank_ = line >= height;
    if (line >= 0 && line < height && line < kScreenHeight) {
        render_scanline(line);
    } else if (line >= height && line < kScreenHeight) {
        // 192-line modes leave the bottom of the 212-line frame as border.
        const uint32_t bg = backdrop_argb();
        for (int x = 0; x < kScreenWidth; x++) framebuffer_[size_t(line * kScreenWidth + x)] = bg;
    }
    if (line == height) {
        status_[0] |= kStatF;
        update_interrupt_line();
    }
    uint8_t hit = uint8_t((int(registers_[19]) - int(registers_[23])) & 0xff);
    if (uint8_t(line) == hit) {
        status_[1] |= kStatFh;
        update_interrupt_line();
    }
}

// ---------------------------------------------------------------------------
// Command engine.  Commands complete immediately (CE drops at once), except
// the CPU transfers HMMC/LMMC/LMCM that proceed one byte or dot per access.

uint8_t V9938::logical_op(uint8_t dst, uint8_t src, uint8_t op) const {
    // T-variants (op bit 3) leave the destination alone where the source is 0.
    if ((op & 0x08) && src == 0) return dst;
    switch (op & 0x07) {
        case 0: return src;                       // IMP
        case 1: return uint8_t(src & dst);        // AND
        case 2: return uint8_t(src | dst);        // OR
        case 3: return uint8_t(src ^ dst);        // XOR
        case 4: return uint8_t(~src);             // NOT
        default: return dst;
    }
}

int V9938::clip_nx(int x, int nx, bool left) const {
    const int width = command_width();
    if (left) return std::min(nx, x + cmd_step_x_);
    return std::max(std::min(nx, width - x), 0);
}

void V9938::command_advance(int* x, int* y, int nx, int ny, int* count_x, int step) {
    int dix = (cmd_arg_ & 0x04) ? -step : step;
    int diy = (cmd_arg_ & 0x08) ? -1 : 1;
    (void)ny;
    *x += dix;
    (*count_x) += step;
    if (*count_x >= nx) {
        *count_x = 0;
        *x -= (dix > 0 ? nx : -nx);
        *y += diy;
    }
}

void V9938::start_command(uint8_t cmd) {
    command_ = cmd;
    cmd_sx_ = registers_[32] | (int(registers_[33] & 1) << 8);
    cmd_sy_ = registers_[34] | (int(registers_[35] & 3) << 8);
    cmd_dx_ = registers_[36] | (int(registers_[37] & 1) << 8);
    cmd_dy_ = registers_[38] | (int(registers_[39] & 3) << 8);
    cmd_nx_ = registers_[40] | (int(registers_[41] & 1) << 8);
    cmd_ny_ = registers_[42] | (int(registers_[43] & 3) << 8);
    cmd_clr_ = registers_[44];
    cmd_arg_ = registers_[45];
    const uint8_t kind = uint8_t(cmd >> 4);
    const int ppb = pixels_per_byte();
    const bool left = (cmd_arg_ & 0x04) != 0;
    const int width = command_width();
    cmd_sx_ %= width;
    cmd_dx_ %= width;
    if (kind != 0x07 && kind != 0x05 && kind != 0x04 && kind != 0x06) {
        if (cmd_nx_ == 0) cmd_nx_ = 512;
        if (cmd_ny_ == 0) cmd_ny_ = 1024;
    }
    if (kind >= 0x0c) {
        // Byte commands work on whole bytes (2, 4 or 1 pixels).
        cmd_dx_ &= ~(ppb - 1);
        cmd_sx_ &= ~(ppb - 1);
        cmd_nx_ &= ~(ppb - 1);
        if (cmd_nx_ == 0) cmd_nx_ = ppb;
        cmd_step_x_ = ppb;
    } else {
        cmd_step_x_ = 1;
    }
    // Blocks stop at the screen edge (in the X direction of travel).
    int nx = cmd_nx_;
    if (kind == 0x0c || kind == 0x0f || kind == 0x08 || kind == 0x0b) {
        nx = clip_nx(cmd_dx_, nx, left);
    } else if (kind == 0x0d || kind == 0x09) {
        nx = std::min(clip_nx(cmd_dx_, nx, left), clip_nx(cmd_sx_, nx, left));
    } else if (kind == 0x0a) {
        nx = clip_nx(cmd_sx_, nx, left);
    }
    if (kind >= 0x0c) nx = std::max(nx - (nx % ppb), 0);
    cmd_nx_ = std::max(nx, 0);
    cmd_x0_ = cmd_dx_;
    cmd_sx0_ = cmd_sx_;
    cmd_remaining_x_ = cmd_nx_;
    command_ce_ = true;
    command_tr_ = false;
    if (cmd_nx_ == 0 && kind >= 0x08) {
        finish_command();
        return;
    }
    const int diy = (cmd_arg_ & 0x08) ? -1 : 1;
    switch (kind) {
        case 0x0c: exec_hmmv(); break;
        case 0x0d: exec_hmmm(); break;
        case 0x0e: exec_ymmm(); break;
        case 0x08: exec_lmmv(); break;
        case 0x09: exec_lmmm(); break;
        case 0x07: exec_line(); break;
        case 0x06: exec_srch(); break;
        case 0x05: exec_pset(); break;
        case 0x0f:  // HMMC: first byte is already in R44
        case 0x0b:  // LMMC
            cpu_data_byte(cmd_clr_);
            return;
        case 0x0a:  // LMCM: dots go to the CPU through S#7
            status_[7] = get_pixel(cmd_sx_, cmd_sy_);
            command_tr_ = true;
            return;
        case 0x04:  // POINT
            status_[7] = get_pixel(cmd_sx_, cmd_sy_);
            finish_command();
            break;
        default:
            finish_command();
            break;
    }
    // Block commands leave SY/DY on the line after the block, as the chip
    // does; programs that reissue a command with only some registers
    // rewritten rely on it.
    if (kind == 0x0c || kind == 0x0d || kind == 0x0e || kind == 0x08 || kind == 0x09) {
        const int lines = registers_[42] | (int(registers_[43] & 3) << 8);
        const int n = lines ? lines : 1024;
        const int dy = (cmd_dy_ + diy * n) & 1023;
        registers_[38] = uint8_t(dy);
        registers_[39] = uint8_t(dy >> 8);
        if (kind == 0x0d || kind == 0x0e || kind == 0x09) {
            const int sy = (cmd_sy_ + diy * n) & 1023;
            registers_[34] = uint8_t(sy);
            registers_[35] = uint8_t(sy >> 8);
        }
    }
}

void V9938::finish_command() {
    command_ce_ = false;
    command_tr_ = false;
}

bool V9938::command_advance_dst() {
    int dix = (cmd_arg_ & 0x04) ? -cmd_step_x_ : cmd_step_x_;
    int diy = (cmd_arg_ & 0x08) ? -1 : 1;
    cmd_dx_ += dix;
    cmd_remaining_x_ -= cmd_step_x_;
    if (cmd_remaining_x_ <= 0) {
        cmd_remaining_x_ = cmd_nx_;
        cmd_dx_ = cmd_x0_;
        cmd_dy_ = (cmd_dy_ + diy) & 1023;
        cmd_ny_--;
        if (cmd_ny_ <= 0) {
            finish_command();
            return false;
        }
    }
    return true;
}

void V9938::cpu_data_byte(uint8_t value) {
    uint8_t kind = uint8_t(command_ >> 4);
    command_tr_ = false;
    if (kind == 0x0f) {
        vram_set(pixel_address(cmd_dx_, cmd_dy_), value);
    } else if (kind == 0x0b) {
        const uint8_t mask = uint8_t((1 << bits_per_pixel()) - 1);
        uint8_t dst = get_pixel(cmd_dx_, cmd_dy_);
        put_pixel(cmd_dx_, cmd_dy_, logical_op(dst, value & mask, command_ & 0x0f));
    } else {
        return;
    }
    if (command_advance_dst()) command_tr_ = true;
}

void V9938::lmcm_next() {
    int dix = (cmd_arg_ & 0x04) ? -1 : 1;
    int diy = (cmd_arg_ & 0x08) ? -1 : 1;
    cmd_sx_ += dix;
    if (--cmd_remaining_x_ <= 0) {
        cmd_remaining_x_ = cmd_nx_;
        cmd_sx_ = cmd_sx0_;
        cmd_sy_ = (cmd_sy_ + diy) & 1023;
        if (--cmd_ny_ <= 0) {
            finish_command();
            return;
        }
    }
    status_[7] = get_pixel(cmd_sx_, cmd_sy_);
    command_tr_ = true;
}

void V9938::exec_hmmv() {
    const int dix = (cmd_arg_ & 0x04) ? -1 : 1;
    const int diy = (cmd_arg_ & 0x08) ? -1 : 1;
    for (int row = 0; row < cmd_ny_; row++) {
        const int y = cmd_dy_ + row * diy;
        for (int i = 0; i < cmd_nx_; i += cmd_step_x_) vram_set(pixel_address(cmd_dx_ + dix * i, y), cmd_clr_);
    }
    finish_command();
}

void V9938::exec_hmmm() {
    const int dix = (cmd_arg_ & 0x04) ? -1 : 1;
    const int diy = (cmd_arg_ & 0x08) ? -1 : 1;
    for (int row = 0; row < cmd_ny_; row++) {
        const int sy = cmd_sy_ + row * diy, dy = cmd_dy_ + row * diy;
        for (int i = 0; i < cmd_nx_; i += cmd_step_x_) {
            vram_set(pixel_address(cmd_dx_ + dix * i, dy), vram_get(pixel_address(cmd_sx_ + dix * i, sy)));
        }
    }
    finish_command();
}

void V9938::exec_ymmm() {
    // Copies from DX to the screen edge (in the X direction), lines SY -> DY.
    const int step = cmd_step_x_;
    const int width = command_width();
    const int diy = (cmd_arg_ & 0x08) ? -1 : 1;
    const int dix = (cmd_arg_ & 0x04) ? -step : step;
    int sy = cmd_sy_, dy = cmd_dy_;
    for (int n = 0; n < cmd_ny_; n++) {
        for (int x = cmd_dx_; x >= 0 && x < width; x += dix) {
            vram_set(pixel_address(x, dy), vram_get(pixel_address(x, sy)));
        }
        sy += diy;
        dy += diy;
    }
    finish_command();
}

void V9938::exec_lmmv() {
    const int dix = (cmd_arg_ & 0x04) ? -1 : 1;
    const int diy = (cmd_arg_ & 0x08) ? -1 : 1;
    const uint8_t op = command_ & 0x0f;
    const uint8_t mask = uint8_t((1 << bits_per_pixel()) - 1);
    const uint8_t clr = cmd_clr_ & mask;
    for (int row = 0; row < cmd_ny_; row++) {
        const int y = cmd_dy_ + row * diy;
        for (int i = 0; i < cmd_nx_; i++) {
            const int x = cmd_dx_ + dix * i;
            put_pixel(x, y, logical_op(get_pixel(x, y), clr, op));
        }
    }
    finish_command();
}

void V9938::exec_lmmm() {
    const int dix = (cmd_arg_ & 0x04) ? -1 : 1;
    const int diy = (cmd_arg_ & 0x08) ? -1 : 1;
    const uint8_t op = command_ & 0x0f;
    for (int row = 0; row < cmd_ny_; row++) {
        const int sy = cmd_sy_ + row * diy, dy = cmd_dy_ + row * diy;
        for (int i = 0; i < cmd_nx_; i++) {
            const int sx = cmd_sx_ + dix * i, dx = cmd_dx_ + dix * i;
            put_pixel(dx, dy, logical_op(get_pixel(dx, dy), get_pixel(sx, sy), op));
        }
    }
    finish_command();
}

void V9938::exec_pset() {
    const uint8_t mask = uint8_t((1 << bits_per_pixel()) - 1);
    uint8_t dst = get_pixel(cmd_dx_, cmd_dy_);
    put_pixel(cmd_dx_, cmd_dy_, logical_op(dst, cmd_clr_ & mask, command_ & 0x0f));
    finish_command();
}

void V9938::exec_srch() {
    // Scan from (SX, SY) along X for the border colour in CLR.  EQ (ARG
    // bit 1) = 0 stops on that colour, EQ = 1 stops on any other colour.
    const int dix = (cmd_arg_ & 0x04) ? -1 : 1;
    const bool eq = (cmd_arg_ & 0x02) != 0;
    const int width = command_width();
    const uint8_t mask = uint8_t((1 << bits_per_pixel()) - 1);
    const uint8_t clr = cmd_clr_ & mask;
    cmd_bd_ = false;
    for (int x = cmd_sx_; x >= 0 && x < width; x += dix) {
        if ((get_pixel(x, cmd_sy_) == clr) != eq) {
            cmd_bd_ = true;
            status_[8] = uint8_t(x);
            status_[9] = uint8_t(0xfe | ((x >> 8) & 1));
            break;
        }
    }
    finish_command();
}

void V9938::exec_line() {
    int nx = cmd_nx_;
    int ny = cmd_ny_;
    int tx = (cmd_arg_ & 0x04) ? -1 : 1;
    int ty = (cmd_arg_ & 0x08) ? -1 : 1;
    int x = cmd_dx_;
    int y = cmd_dy_;
    int asx = (nx - 1) >> 1;
    int adx = 0;
    uint8_t op = command_ & 0x0f;
    const uint8_t mask = uint8_t((1 << bits_per_pixel()) - 1);
    const uint8_t clr = cmd_clr_ & mask;
    const int max_x = command_width();
    bool x_major = (cmd_arg_ & 0x01) == 0;
    for (int n = 0; n < 2048; n++) {
        uint8_t dst = get_pixel(x, y);
        put_pixel(x, y, logical_op(dst, clr, op));
        if (x_major) {
            x += tx;
            if (adx++ == nx || (x & max_x) != 0) break;
            if (asx < ny) {
                asx += nx;
                y += ty;
            }
            asx -= ny;
            asx &= 1023;
        } else {
            y += ty;
            if (asx < ny) {
                asx += nx;
                x += tx;
            }
            asx -= ny;
            asx &= 1023;
            if (adx++ == nx || (x & max_x) != 0) break;
        }
        y &= 1023;
    }
    finish_command();
}

}  // namespace dsp
