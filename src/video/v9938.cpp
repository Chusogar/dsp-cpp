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
    registers_[1] = 0x10;  // TEXT1 like a TMS after power-on
}

uint8_t V9938::vram_get(uint32_t addr) const { return vram_[addr & (kVramSize - 1)]; }

void V9938::vram_set(uint32_t addr, uint8_t value) { vram_[addr & (kVramSize - 1)] = value; }

void V9938::increment_vram() { address_ = (address_ + 1) & (kVramSize - 1); }

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
        value = 0;
        if (command_ce_) value |= kStatCe;
        if (command_tr_) value |= kStatTr;
        if (in_vblank_) value |= kStatVr;
        value |= kStatHr;  // pretend we are always in hblank between CPU polls
        status_[2] = value;
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

uint32_t V9938::palette_argb(uint8_t index) const {
    uint16_t p = palette_[index & 0x0f];
    int r = (p >> 8) & 7;
    int b = (p >> 4) & 7;
    int g = p & 7;
    return rgb333(r, g, b);
}

void V9938::plot(int x, int y, uint8_t color, int width) {
    if (y < 0 || y >= kScreenHeight) return;
    uint32_t argb = (color == 0 && (registers_[8] & 0x20) == 0) ? palette_argb(backdrop())
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
    int name_shift = columns == 80 ? 12 : 10;
    uint32_t nt = uint32_t(registers_[2] & (columns == 80 ? 0x7c : 0x0f)) << name_shift;
    uint32_t pt = uint32_t(registers_[4] & 0x07) << 11;
    uint8_t fg = uint8_t(registers_[7] >> 4);
    uint8_t bg = backdrop();
    int row = line / 8;
    int y_in = line % 8;
    int chars = columns;
    int px = columns == 80 ? 0 : 8;  // 40-col has 8 px border each side of 240
    for (int col = 0; col < chars; col++) {
        uint8_t name = vram_get(nt + uint32_t(row * chars + col));
        uint8_t pattern = vram_get(pt + uint32_t(name) * 8 + uint32_t(y_in));
        int bits = columns == 80 ? 6 : 8;
        for (int bit = 0; bit < bits; bit++) {
            bool set = ((pattern >> (7 - bit)) & 1) != 0;
            plot(px + col * bits + bit, line, set ? fg : bg, columns == 80 ? 512 : 256);
        }
    }
}

void V9938::render_g1(int line) {
    int row = line / 8;
    int y_in = line % 8;
    uint32_t nt = uint32_t(registers_[2] & 0x0f) << 10;
    uint32_t ct = uint32_t(registers_[3]) << 6;
    uint32_t pt = uint32_t(registers_[4] & 0x07) << 11;
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
    int row = line / 8;
    int y_in = line % 8;
    int third = row / 8;
    uint32_t nt = uint32_t(registers_[2] & 0x0f) << 10;
    uint32_t ct = uint32_t(registers_[3] & 0x80) << 6;
    uint32_t pt = uint32_t(registers_[4] & 0x04) << 11;
    for (int col = 0; col < 32; col++) {
        uint8_t name = vram_get(nt + uint32_t(row * 32 + col));
        uint32_t index = uint32_t(third) * 256 + name;
        uint8_t pattern = vram_get(pt + index * 8 + uint32_t(y_in));
        uint8_t colors = vram_get(ct + index * 8 + uint32_t(y_in));
        uint8_t fg = uint8_t(colors >> 4);
        uint8_t bg = uint8_t(colors & 0x0f);
        for (int bit = 0; bit < 8; bit++) {
            bool set = ((pattern >> (7 - bit)) & 1) != 0;
            plot(col * 8 + bit, line, set ? fg : bg, 256);
        }
    }
}

void V9938::render_mc(int line) {
    int row = line / 8;
    int block = (line / 4) % 2;
    uint32_t nt = uint32_t(registers_[2] & 0x0f) << 10;
    uint32_t pt = uint32_t(registers_[4] & 0x07) << 11;
    for (int col = 0; col < 32; col++) {
        uint8_t name = vram_get(nt + uint32_t(row * 32 + col));
        uint8_t colors = vram_get(pt + uint32_t(name) * 8 + uint32_t(line / 4 % 8));
        (void)block;
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

int V9938::bits_per_pixel() const {
    switch (current_mode()) {
        case kG5: return 2;
        case kG4:
        case kG6: return 4;
        case kG7: return 8;
        default: return 4;
    }
}

int V9938::pixels_per_byte() const { return std::max(8 / std::max(bits_per_pixel(), 1), 1); }

uint32_t V9938::pixel_address(int x, int y) const {
    Mode mode = current_mode();
    y &= 1023;
    x &= 511;
    if (mode == kG6) {
        // SCREEN 7: even/odd VRAM planes in the two 64 KiB banks.
        return ((uint32_t(x) & 2) << 15) + (uint32_t(y & 511) << 7) + (uint32_t(x) >> 2);
    }
    if (mode == kG7) {
        return ((uint32_t(x) & 1) << 16) + (uint32_t(y & 511) << 7) + ((uint32_t(x) >> 1) & 127);
    }
    int ppb = pixels_per_byte();
    int width = screen_width_px();
    if (width <= 0) width = 256;
    int bytes_per_line = width / ppb;
    uint32_t page = 0;
    if (mode == kG4 || mode == kG5) page = uint32_t((registers_[2] >> 5) & 3) * 0x8000u;
    return page + uint32_t(y & 255) * uint32_t(bytes_per_line) + uint32_t(x / ppb);
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
    int width = screen_width_px();
    int y = (line + registers_[23]) & 255;
    uint8_t bg = backdrop();
    for (int x = 0; x < width; x++) {
        uint8_t color = get_pixel(x, y);
        if (current_mode() == kG7) {
            // SCREEN 8: 8-bit GRB332
            int r = (color >> 2) & 7;
            int g = (color >> 5) & 7;
            int b = ((color & 3) << 1) | (color & 1);
            uint32_t argb = rgb333(r, g, b);
            int dx = x * 2;
            if (dx + 1 < kScreenWidth) {
                framebuffer_[size_t(line * kScreenWidth + dx)] = argb;
                framebuffer_[size_t(line * kScreenWidth + dx + 1)] = argb;
            }
        } else {
            plot(x, line, color == 0 ? bg : color, width);
        }
    }
}

void V9938::render_sprites(int line, int width) {
    if (registers_[8] & 0x02) return;  // sprites disabled
    bool large = (registers_[1] & 0x02) != 0;
    bool mag = (registers_[1] & 0x01) != 0;
    uint32_t sat = uint32_t(registers_[5] & 0x7f) << 7;
    sat |= uint32_t(registers_[11] & 0x03) << 15;
    uint32_t spt = uint32_t(registers_[6] & 0x3f) << 11;
    int size = large ? 16 : 8;
    if (mag) size *= 2;
    int shown = 0;
    const int max_per_line = (current_mode() >= kG3) ? 8 : 4;
    for (int sp = 0; sp < 32; sp++) {
        uint8_t y = vram_get(sat + uint32_t(sp) * 4);
        if (y == 208 && current_mode() < kG3) break;
        if (y == 216 && current_mode() >= kG3) break;
        int sy = int(y);
        if (sy > 216) sy -= 256;
        sy += 1;
        if (line < sy || line >= sy + size) continue;
        if (shown >= max_per_line) {
            status_[0] |= 0x40;
            status_[0] = uint8_t((status_[0] & 0xe0) | (sp & 0x1f));
            break;
        }
        uint8_t x = vram_get(sat + uint32_t(sp) * 4 + 1);
        uint8_t pattern = vram_get(sat + uint32_t(sp) * 4 + 2);
        uint8_t attr = vram_get(sat + uint32_t(sp) * 4 + 3);
        int sx = int(x);
        if (attr & 0x80) sx -= 32;
        uint8_t color = attr & 0x0f;
        int row = line - sy;
        if (mag) row /= 2;
        if (large) pattern &= 0xfc;
        uint16_t bits = vram_get(spt + uint32_t(pattern) * 8 + uint32_t(row));
        if (large) bits = uint16_t((bits << 8) | vram_get(spt + uint32_t(pattern) * 8 + 16 + uint32_t(row)));
        int px_count = large ? 16 : 8;
        for (int px = 0; px < px_count; px++) {
            bool on = large ? ((bits >> (15 - px)) & 1) != 0 : ((bits >> (7 - px)) & 1) != 0;
            if (!on || color == 0) continue;
            int dx = sx + (mag ? px * 2 : px);
            plot(dx, line, color, width);
            if (mag) plot(dx + 1, line, color, width);
        }
        shown++;
    }
}

void V9938::render_scanline(int line) {
    uint32_t bg = palette_argb(backdrop());
    for (int x = 0; x < kScreenWidth; x++) framebuffer_[size_t(line * kScreenWidth + x)] = bg;
    if (!display_enabled()) return;
    Mode mode = current_mode();
    int width = 256;
    switch (mode) {
        case kTxt1: render_text(line, 40); width = 256; break;
        case kTxt2: render_text(line, 80); width = 512; break;
        case kG1: render_g1(line); break;
        case kG2: render_g2(line, false); break;
        case kG3: render_g2(line, true); break;
        case kMc: render_mc(line); break;
        case kG4:
        case kG5:
        case kG6:
        case kG7: render_bitmap(line); width = screen_width_px(); break;
        default: break;
    }
    if (mode != kTxt1 && mode != kTxt2) render_sprites(line, width);
}

void V9938::refresh_line(int line, int total_lines) {
    (void)total_lines;
    scanline_ = line;
    int height = visible_height();
    in_vblank_ = line >= height;
    if (line >= 0 && line < height && line < kScreenHeight) render_scanline(line);
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

uint8_t V9938::logical_op(uint8_t dst, uint8_t src, uint8_t op) const {
    switch (op & 0x0f) {
        case 0: return src;                       // IMP
        case 1: return uint8_t(src & dst);        // AND
        case 2: return uint8_t(src | dst);        // OR
        case 3: return uint8_t(src ^ dst);        // XOR
        case 4: return uint8_t(~src);             // NOT
        default: return src;
    }
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
    cmd_nx_ = registers_[40] | (int(registers_[41] & 3) << 8);
    cmd_ny_ = registers_[42] | (int(registers_[43] & 3) << 8);
    cmd_clr_ = registers_[44];
    cmd_arg_ = registers_[45];
    uint8_t kind = uint8_t(cmd >> 4);
    int ppb = pixels_per_byte();
    if (kind != 0x07 && kind != 0x05 && kind != 0x04) {
        if (cmd_nx_ == 0) cmd_nx_ = screen_width_px();
        if (cmd_ny_ == 0) cmd_ny_ = 1024;
    }
    if (kind >= 0x0c) {
        cmd_dx_ &= ~(ppb - 1);
        cmd_sx_ &= ~(ppb - 1);
        cmd_nx_ &= ~(ppb - 1);
        if (cmd_nx_ == 0) cmd_nx_ = screen_width_px();
        cmd_step_x_ = ppb;
    } else {
        cmd_step_x_ = 1;
    }
    cmd_x0_ = cmd_dx_;
    cmd_sx0_ = cmd_sx_;
    cmd_remaining_x_ = cmd_nx_;
    command_ce_ = true;
    command_tr_ = false;
    switch (kind) {
        case 0x0c: exec_hmmv(); break;
        case 0x0d: exec_hmmm(); break;
        case 0x0e: exec_ymmm(); break;
        case 0x08: exec_lmmv(); break;
        case 0x09: exec_lmmm(); break;
        case 0x07: exec_line(); break;
        case 0x05: exec_pset(); break;
        case 0x0f:  // HMMC: first byte is already in R44
        case 0x0b:  // LMMC
            cpu_data_byte(cmd_clr_);
            return;
        case 0x04:  // POINT
            status_[7] = get_pixel(cmd_sx_, cmd_sy_);
            finish_command();
            break;
        default:
            finish_command();
            break;
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
        cmd_dy_ += diy;
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
        uint8_t dst = get_pixel(cmd_dx_, cmd_dy_);
        put_pixel(cmd_dx_, cmd_dy_, logical_op(dst, value, command_ & 0x0f));
    } else {
        return;
    }
    if (command_advance_dst()) command_tr_ = true;
}

void V9938::exec_hmmv() {
    int x = cmd_dx_, y = cmd_dy_, cx = 0;
    int step = cmd_step_x_;
    const int total = std::max(cmd_nx_ / std::max(step, 1), 1) * cmd_ny_;
    for (int n = 0; n < total; n++) {
        vram_set(pixel_address(x, y), cmd_clr_);
        command_advance(&x, &y, cmd_nx_, cmd_ny_, &cx, step);
    }
    finish_command();
}

void V9938::exec_hmmm() {
    int sx = cmd_sx_, sy = cmd_sy_, dx = cmd_dx_, dy = cmd_dy_, cx = 0, cs = 0;
    int step = cmd_step_x_;
    const int total = std::max(cmd_nx_ / std::max(step, 1), 1) * cmd_ny_;
    for (int n = 0; n < total; n++) {
        vram_set(pixel_address(dx, dy), vram_get(pixel_address(sx, sy)));
        command_advance(&dx, &dy, cmd_nx_, cmd_ny_, &cx, step);
        command_advance(&sx, &sy, cmd_nx_, cmd_ny_, &cs, step);
    }
    finish_command();
}

void V9938::exec_ymmm() {
    int sy = cmd_sy_, dx = cmd_dx_, dy = cmd_dy_;
    int step = cmd_step_x_;
    int width = screen_width_px();
    int diy = (cmd_arg_ & 0x08) ? -1 : 1;
    int dix = (cmd_arg_ & 0x04) ? -step : step;
    for (int n = 0; n < cmd_ny_; n++) {
        int x = dx;
        for (;;) {
            vram_set(pixel_address(x, dy), vram_get(pixel_address(x, sy)));
            x += dix;
            if (x < 0 || x >= width) break;
        }
        sy += diy;
        dy += diy;
    }
    finish_command();
}

void V9938::exec_lmmv() {
    int x = cmd_dx_, y = cmd_dy_, cx = 0;
    uint8_t op = command_ & 0x0f;
    for (int n = 0; n < cmd_nx_ * cmd_ny_; n++) {
        uint8_t dst = get_pixel(x, y);
        put_pixel(x, y, logical_op(dst, cmd_clr_, op));
        command_advance(&x, &y, cmd_nx_, cmd_ny_, &cx, 1);
    }
    finish_command();
}

void V9938::exec_lmmm() {
    int sx = cmd_sx_, sy = cmd_sy_, dx = cmd_dx_, dy = cmd_dy_, cx = 0, cs = 0;
    uint8_t op = command_ & 0x0f;
    for (int n = 0; n < cmd_nx_ * cmd_ny_; n++) {
        uint8_t dst = get_pixel(dx, dy);
        put_pixel(dx, dy, logical_op(dst, get_pixel(sx, sy), op));
        command_advance(&dx, &dy, cmd_nx_, cmd_ny_, &cx, 1);
        command_advance(&sx, &sy, cmd_nx_, cmd_ny_, &cs, 1);
    }
    finish_command();
}

void V9938::exec_pset() {
    uint8_t dst = get_pixel(cmd_dx_, cmd_dy_);
    put_pixel(cmd_dx_, cmd_dy_, logical_op(dst, cmd_clr_, command_ & 0x0f));
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
    const int max_x = screen_width_px();
    bool x_major = (cmd_arg_ & 0x01) == 0;
    for (int n = 0; n < 2048; n++) {
        uint8_t dst = get_pixel(x, y);
        put_pixel(x, y, logical_op(dst, cmd_clr_, op));
        if (x_major) {
            x += tx;
            if (adx++ == nx || (x & max_x) != 0) break;
            if (asx < ny) {
                asx += nx;
                y += ty;
                if (ty < 0 && y < 0) break;
            }
            asx -= ny;
            asx &= 1023;
        } else {
            y += ty;
            if (ty < 0 && y < 0) break;
            if (asx < ny) {
                asx += nx;
                x += tx;
            }
            asx -= ny;
            asx &= 1023;
            if (adx++ == nx || (x & max_x) != 0) break;
        }
    }
    finish_command();
}

}  // namespace dsp
