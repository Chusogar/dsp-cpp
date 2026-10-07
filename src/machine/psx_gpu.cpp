// PlayStation GPU.
// Ported/adapted from ProjectPSX (MIT) by Pedro Cortés:
//   https://github.com/BluestormDNA/ProjectPSX
#include "machine/psx_gpu.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace dsp {

const int PsxGpu::kResolutions[5] = {256, 320, 512, 640, 368};
const int PsxGpu::kDotDiv[5] = {10, 8, 5, 4, 7};
const uint8_t PsxGpu::kCommandSize[256] = {
1,1,3,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
4,4,4,4,7,7,7,7,5,5,5,5,9,9,9,9,6,6,6,6,9,9,9,9,8,8,8,8,12,12,12,12,
3,3,3,3,3,3,3,3,16,16,16,16,16,16,16,16,4,4,4,4,4,4,4,4,16,16,16,16,16,16,16,16,
3,3,3,1,4,4,4,4,2,1,2,1,3,3,3,3,2,1,2,1,3,3,3,3,2,1,2,2,3,3,3,3,
4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,
3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,
3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,
1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
};

PsxGpu::PsxGpu() {
    init_color_lut();
    reset();
}

void PsxGpu::init_color_lut() {
    for (int m = 0; m < 2; m++) {
        for (int r = 0; r < 32; r++) {
            for (int g = 0; g < 32; g++) {
                for (int b = 0; b < 32; b++) {
                    // ProjectPSX stores as M<<24 | R<<16 | G<<8 | B (with 5->8 expand)
                    color_lut_[size_t(m << 15 | b << 10 | g << 5 | r)] =
                        uint32_t(m << 24 | (r << 3) << 16 | (g << 3) << 8 | (b << 3));
                }
            }
        }
    }
}

void PsxGpu::reset() {
    vram8888_.fill(0);
    vram1555_.fill(0);
    mode_ = Mode::Command;
    pointer_ = 0;
    gpuread_ = 0;
    video_cycles_ = 0;
    scan_line_ = 0;
    gp1_reset();
}

void PsxGpu::set_pixel(int x, int y, int color) {
    const int i = (x & 0x3FF) + ((y & 0x1FF) * kVramW);
    vram8888_[size_t(i)] = uint32_t(color);
}

int PsxGpu::get_pixel888(int x, int y) const {
    return int(vram8888_[size_t((x & 0x3FF) + ((y & 0x1FF) * kVramW))]);
}

uint16_t PsxGpu::get_pixel555(int x, int y) const {
    return vram1555_[size_t((x & 0x3FF) + ((y & 0x1FF) * kVramW))];
}

int16_t PsxGpu::signed11(uint32_t n) {
    return int16_t((int32_t(n) << 21) >> 21);
}

uint8_t PsxGpu::clamp_ff(int v) { return v > 0xFF ? 0xFF : uint8_t(v); }
uint8_t PsxGpu::clamp_zero(int v) { return v < 0 ? 0 : uint8_t(v); }

int PsxGpu::orient2d(Point2D a, Point2D b, Point2D c) {
    return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
}

bool PsxGpu::is_top_left(Point2D a, Point2D b) {
    return (a.y == b.y && b.x > a.x) || b.y < a.y;
}

int PsxGpu::interpolate(int w0, int w1, int w2, int t0, int t1, int t2, int area) {
    return (t0 * w0 + t1 * w1 + t2 * w2) / area;
}

int PsxGpu::mask_texel(int axis, int pre, int post) const {
    return (axis & 0xFF & pre) | post;
}

int PsxGpu::get_rgb_color(uint32_t value) const {
    Color c; c.val = value;
    return int(uint32_t(c.m()) << 24 | uint32_t(c.r()) << 16 | uint32_t(c.g()) << 8 | c.b());
}

bool PsxGpu::tick(int cycles) {
    video_cycles_ += cycles * 11 / 7;
    if (video_cycles_ >= horizontal_timing_) {
        video_cycles_ -= horizontal_timing_;
        scan_line_++;
        if (!vres480_) odd_line_ = (scan_line_ & 1) != 0;
        if (scan_line_ >= vertical_timing_) {
            scan_line_ = 0;
            if (vertical_interlace_ && vres480_) {
                odd_line_ = !odd_line_;
                interlace_field_ = !odd_line_;
            }
            return true;
        }
    }
    return false;
}

PsxGpuSync PsxGpu::blanks_and_dot() const {
    PsxGpuSync s;
    s.dot_div = kDotDiv[hres2_ << 2 | hres1_];
    s.hblank = video_cycles_ < disp_x1_ || video_cycles_ > disp_x2_;
    s.vblank = scan_line_ < disp_y1_ || scan_line_ > disp_y2_;
    return s;
}

int PsxGpu::display_width() const {
    return kResolutions[hres2_ << 2 | hres1_];
}

int PsxGpu::display_height() const {
    return vres480_ ? 480 : 240;
}

void PsxGpu::blit_display(uint32_t* dst, int dst_w, int dst_h) const {
    const int w = std::min(dst_w, display_width());
    const int h = std::min(dst_h, display_height());
    const int src_x = disp_vram_x_;
    const int src_y = disp_vram_y_;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            const int color = get_pixel888(src_x + x, src_y + y);
            // VRAM stored as M|R|G|B in bytes of the int as ProjectPSX (R in high)
            const uint8_t r = uint8_t((color >> 16) & 0xFF);
            const uint8_t g = uint8_t((color >> 8) & 0xFF);
            const uint8_t b = uint8_t(color & 0xFF);
            dst[y * dst_w + x] = 0xFF000000u | (uint32_t(r) << 16) | (uint32_t(g) << 8) | b;
        }
    }
    // Clear unused area if any
    for (int y = 0; y < dst_h; y++) {
        for (int x = (y < h ? w : 0); x < dst_w; x++) {
            if (y >= h || x >= w) dst[y * dst_w + x] = 0xFF000000u;
        }
    }
}

uint32_t PsxGpu::load_gpustat() const {
    uint32_t s = 0;
    s |= draw_mode_bits_ & 0x7FFu;
    s |= uint32_t(mask_while_drawing_) << 11;
    s |= uint32_t(check_mask_ ? 1 : 0) << 12;
    s |= uint32_t(interlace_field_ ? 1 : 0) << 13;
    s |= uint32_t(reverse_flag_ ? 1 : 0) << 14;
    s |= uint32_t(texture_disabled_ ? 1 : 0) << 15;
    s |= uint32_t(hres2_) << 16;
    s |= uint32_t(hres1_) << 17;
    s |= uint32_t(vres480_ ? 1 : 0) << 19;
    s |= uint32_t(pal_ ? 1 : 0) << 20;
    s |= uint32_t(depth24_ ? 1 : 0) << 21;
    s |= uint32_t(vertical_interlace_ ? 1 : 0) << 22;
    s |= uint32_t(display_disabled_ ? 1 : 0) << 23;
    s |= uint32_t(interrupt_requested_ ? 1 : 0) << 24;
    s |= uint32_t(dma_request_ ? 1 : 0) << 25;
    s |= uint32_t(ready_command_ ? 1 : 0) << 26;
    s |= uint32_t(ready_vram_to_cpu_ ? 1 : 0) << 27;
    s |= uint32_t(ready_dma_ ? 1 : 0) << 28;
    s |= uint32_t(dma_direction_) << 29;
    s |= uint32_t(odd_line_ ? 1 : 0) << 31;
    return s;
}

uint32_t PsxGpu::load_gpuread() {
    if (xfer_.half_words > 0) return read_from_vram();
    return gpuread_;
}

void PsxGpu::write(uint32_t addr, uint32_t value) {
    if ((addr & 0xF) == 0) write_gp0(value);
    else if ((addr & 0xF) == 4) write_gp1(value);
}

void PsxGpu::write_gp0(uint32_t value) {
    if (mode_ == Mode::Command) decode_gp0(value);
    else write_to_vram(value);
}

void PsxGpu::process_dma(const uint32_t* data, int words) {
    if (mode_ == Mode::Command) decode_gp0_dma(data, words);
    else for (int i = 0; i < words; i++) write_to_vram(data[i]);
}

void PsxGpu::write_to_vram(uint32_t value) {
    uint16_t p0 = uint16_t(value & 0xFFFF);
    uint16_t p1 = uint16_t(value >> 16);
    p0 = uint16_t(p0 | (mask_while_drawing_ << 15));
    p1 = uint16_t(p1 | (mask_while_drawing_ << 15));
    draw_vram_pixel(p0);
    if (--xfer_.half_words == 0) { mode_ = Mode::Command; return; }
    draw_vram_pixel(p1);
    if (--xfer_.half_words == 0) mode_ = Mode::Command;
}

uint32_t PsxGpu::read_from_vram() {
    uint16_t p0 = get_pixel555(xfer_.x & 0x3FF, xfer_.y & 0x1FF);
    step_vram_transfer();
    uint16_t p1 = get_pixel555(xfer_.x & 0x3FF, xfer_.y & 0x1FF);
    step_vram_transfer();
    xfer_.half_words -= 2;
    if (xfer_.half_words == 0) {
        ready_vram_to_cpu_ = false;
        ready_dma_ = true;
    }
    return uint32_t(p1) << 16 | p0;
}

void PsxGpu::step_vram_transfer() {
    if (++xfer_.x == xfer_.origin_x + xfer_.w) {
        xfer_.x -= xfer_.w;
        xfer_.y++;
    }
}

void PsxGpu::draw_vram_pixel(uint16_t c1555) {
    if (!check_mask_ || (get_pixel888(xfer_.x, xfer_.y) >> 24) == 0) {
        set_pixel(xfer_.x & 0x3FF, xfer_.y & 0x1FF, int(color_lut_[c1555]));
        vram1555_[size_t((xfer_.x & 0x3FF) + ((xfer_.y & 0x1FF) * kVramW))] = c1555;
    }
    step_vram_transfer();
}

void PsxGpu::decode_gp0(uint32_t value) {
    if (pointer_ == 0) {
        command_ = value >> 24;
        command_size_ = kCommandSize[command_ & 0xFF];
    }
    command_buffer_[size_t(pointer_++)] = value;
    if (pointer_ == command_size_ ||
        (command_size_ == 16 && (value & 0xF000F000u) == 0x50005000u)) {
        pointer_ = 0;
        execute_gp0(command_, command_buffer_.data());
        pointer_ = 0;
    }
}

void PsxGpu::decode_gp0_dma(const uint32_t* data, int words) {
    int p = 0;
    while (p < words) {
        if (mode_ == Mode::Command) {
            command_ = data[p] >> 24;
            pointer_ = p;
            execute_gp0(command_, data);
            p = pointer_;
        } else {
            write_to_vram(data[p++]);
            pointer_ = p;
        }
    }
    pointer_ = 0;
}

void PsxGpu::execute_gp0(uint32_t opcode, const uint32_t* buffer) {
    if (opcode == 0x00 || opcode == 0x01) { pointer_++; return; }
    if (opcode == 0x02) { gp0_fill_rect(buffer); return; }
    if (opcode == 0x1F) { pointer_++; interrupt_requested_ = true; return; }
    if (opcode == 0xE1) { gp0_e1(buffer[pointer_++]); return; }
    if (opcode == 0xE2) { gp0_e2(buffer[pointer_++]); return; }
    if (opcode == 0xE3) { gp0_e3(buffer[pointer_++]); return; }
    if (opcode == 0xE4) { gp0_e4(buffer[pointer_++]); return; }
    if (opcode == 0xE5) { gp0_e5(buffer[pointer_++]); return; }
    if (opcode == 0xE6) { gp0_e6(buffer[pointer_++]); return; }
    if (opcode >= 0x20 && opcode <= 0x3F) { gp0_render_polygon(buffer); return; }
    if (opcode >= 0x40 && opcode <= 0x5F) { gp0_render_line(buffer); return; }
    if (opcode >= 0x60 && opcode <= 0x7F) { gp0_render_rectangle(buffer); return; }
    if (opcode >= 0x80 && opcode <= 0x9F) { gp0_copy_vram_vram(buffer); return; }
    if (opcode >= 0xA0 && opcode <= 0xBF) { gp0_copy_cpu_vram(buffer); return; }
    if (opcode >= 0xC0 && opcode <= 0xDF) { gp0_copy_vram_cpu(buffer); return; }
    pointer_++;  // NOP for unused
}

void PsxGpu::gp0_fill_rect(const uint32_t* buffer) {
    Color c; c.val = buffer[pointer_++];
    const uint32_t yx = buffer[pointer_++];
    const uint32_t hw = buffer[pointer_++];
    const uint16_t x = uint16_t(yx & 0x3F0);
    const uint16_t y = uint16_t((yx >> 16) & 0x1FF);
    const uint16_t w = uint16_t(((hw & 0x3FF) + 0xF) & ~0xF);
    const uint16_t h = uint16_t((hw >> 16) & 0x1FF);
    const int color = int(uint32_t(c.r()) << 16 | uint32_t(c.g()) << 8 | c.b());
    for (int yy = y; yy < h + y; yy++) {
        for (int xx = x; xx < w + x; xx++) {
            set_pixel(xx & 0x3FF, yy & 0x1FF, color);
        }
    }
}

void PsxGpu::gp0_e1(uint32_t val) {
    const uint32_t bits = val & 0xFFFFFFu;
    if (bits == draw_mode_bits_) return;
    draw_mode_bits_ = bits;
    texture_x_base_ = uint8_t(val & 0xF);
    texture_y_base_ = uint8_t((val >> 4) & 1);
    transparency_mode_ = uint8_t((val >> 5) & 3);
    texture_depth_ = uint8_t((val >> 7) & 3);
    dithered_ = ((val >> 9) & 1) != 0;
    draw_to_display_ = ((val >> 10) & 1) != 0;
    texture_disabled_ = texture_disable_allowed_ && ((val >> 11) & 1) != 0;
}

void PsxGpu::gp0_e2(uint32_t val) {
    const uint32_t bits = val & 0xFFFFFFu;
    if (bits == texture_window_bits_) return;
    texture_window_bits_ = bits;
    const uint8_t mx = uint8_t(val & 0x1F);
    const uint8_t my = uint8_t((val >> 5) & 0x1F);
    const uint8_t ox = uint8_t((val >> 10) & 0x1F);
    const uint8_t oy = uint8_t((val >> 15) & 0x1F);
    pre_mask_x_ = ~(mx * 8);
    pre_mask_y_ = ~(my * 8);
    post_mask_x_ = (ox & mx) * 8;
    post_mask_y_ = (oy & my) * 8;
}

void PsxGpu::gp0_e3(uint32_t val) {
    draw_top_ = uint16_t((val >> 10) & 0x1FF);
    draw_left_ = uint16_t(val & 0x3FF);
}
void PsxGpu::gp0_e4(uint32_t val) {
    draw_bottom_ = uint16_t((val >> 10) & 0x1FF);
    draw_right_ = uint16_t(val & 0x3FF);
}
void PsxGpu::gp0_e5(uint32_t val) {
    draw_x_off_ = signed11(val & 0x7FF);
    draw_y_off_ = signed11((val >> 11) & 0x7FF);
}
void PsxGpu::gp0_e6(uint32_t val) {
    mask_while_drawing_ = int(val & 1);
    check_mask_ = (val & 2) != 0;
}

void PsxGpu::write_gp1(uint32_t value) {
    const uint32_t op = value >> 24;
    switch (op) {
        case 0x00: gp1_reset(); break;
        case 0x01: pointer_ = 0; break;
        case 0x02: interrupt_requested_ = false; break;
        case 0x03: display_disabled_ = (value & 1) != 0; break;
        case 0x04:
            dma_direction_ = uint8_t(value & 3);
            dma_request_ = (dma_direction_ == 1 || dma_direction_ == 2) ? ready_dma_
                         : (dma_direction_ == 3) ? ready_vram_to_cpu_ : false;
            break;
        case 0x05:
            disp_vram_x_ = uint16_t(value & 0x3FE);
            disp_vram_y_ = uint16_t((value >> 10) & 0x1FE);
            break;
        case 0x06: {
            const uint32_t bits = value & 0xFFFFFFu;
            if (bits == display_h_range_) break;
            display_h_range_ = bits;
            disp_x1_ = uint16_t(value & 0xFFF);
            disp_x2_ = uint16_t((value >> 12) & 0xFFF);
            break;
        }
        case 0x07: {
            const uint32_t bits = value & 0xFFFFFFu;
            if (bits == display_v_range_) break;
            display_v_range_ = bits;
            disp_y1_ = uint16_t(value & 0x3FF);
            disp_y2_ = uint16_t((value >> 10) & 0x3FF);
            break;
        }
        case 0x08: gp1_display_mode(value); break;
        case 0x09: texture_disable_allowed_ = (value & 1) != 0; break;
        default:
            if (op >= 0x10 && op <= 0x1F) gp1_info(value);
            break;
    }
}

void PsxGpu::gp1_reset() {
    pointer_ = 0;
    interrupt_requested_ = false;
    display_disabled_ = true;
    dma_direction_ = 0;
    dma_request_ = false;
    disp_vram_x_ = 0;
    disp_vram_y_ = 0;
    display_h_range_ = 0xFFFFFFFFu;
    display_v_range_ = 0xFFFFFFFFu;
    disp_x1_ = 0x200; disp_x2_ = 0xC00;
    disp_y1_ = 0x10; disp_y2_ = 0x100;
    gp1_display_mode(0);
    gp0_e1(0); gp0_e2(0); gp0_e3(0); gp0_e4(0); gp0_e5(0); gp0_e6(0);
}

void PsxGpu::gp1_display_mode(uint32_t value) {
    const uint32_t bits = value & 0xFFFFFFu;
    if (bits == display_mode_bits_) return;
    display_mode_bits_ = bits;
    hres1_ = uint8_t(value & 3);
    vres480_ = (value & 4) != 0;
    pal_ = (value & 8) != 0;
    depth24_ = (value & 0x10) != 0;
    vertical_interlace_ = (value & 0x20) != 0;
    hres2_ = uint8_t((value & 0x40) >> 6);
    reverse_flag_ = (value & 0x80) != 0;
    interlace_field_ = vertical_interlace_;
    horizontal_timing_ = pal_ ? 3406 : 3413;
    vertical_timing_ = pal_ ? 314 : 263;
}

void PsxGpu::gp1_info(uint32_t value) {
    switch (value & 0xF) {
        case 0x2: gpuread_ = texture_window_bits_; break;
        case 0x3: gpuread_ = uint32_t(draw_top_) << 10 | draw_left_; break;
        case 0x4: gpuread_ = uint32_t(draw_bottom_) << 10 | draw_right_; break;
        case 0x5: gpuread_ = uint32_t(uint16_t(draw_y_off_)) << 11 | uint16_t(draw_x_off_); break;
        case 0x7: gpuread_ = 2; break;
        case 0x8: gpuread_ = 0; break;
        default: break;
    }
}

int PsxGpu::get_texel(int x, int y, Point2D clut, Point2D base, int depth) const {
    if (depth == 0) {
        const uint16_t index = get_pixel555(x / 4 + base.x, y + base.y);
        const int p = (index >> ((x & 3) * 4)) & 0xF;
        return get_pixel888(clut.x + p, clut.y);
    }
    if (depth == 1) {
        const uint16_t index = get_pixel555(x / 2 + base.x, y + base.y);
        const int p = (index >> ((x & 1) * 8)) & 0xFF;
        return get_pixel888(clut.x + p, clut.y);
    }
    return get_pixel888(x + base.x, y + base.y);
}

int PsxGpu::handle_semi(int x, int y, int color, int mode) const {
    Color back; back.val = uint32_t(get_pixel888(x, y));
    Color front; front.val = uint32_t(color);
    switch (mode) {
        case 0:
            front.set_r(uint8_t((back.r() + front.r()) >> 1));
            front.set_g(uint8_t((back.g() + front.g()) >> 1));
            front.set_b(uint8_t((back.b() + front.b()) >> 1));
            break;
        case 1:
            front.set_r(clamp_ff(back.r() + front.r()));
            front.set_g(clamp_ff(back.g() + front.g()));
            front.set_b(clamp_ff(back.b() + front.b()));
            break;
        case 2:
            front.set_r(clamp_zero(back.r() - front.r()));
            front.set_g(clamp_zero(back.g() - front.g()));
            front.set_b(clamp_zero(back.b() - front.b()));
            break;
        case 3:
            front.set_r(clamp_ff(back.r() + (front.r() >> 2)));
            front.set_g(clamp_ff(back.g() + (front.g() >> 2)));
            front.set_b(clamp_ff(back.b() + (front.b() >> 2)));
            break;
    }
    return int(front.val);
}

void PsxGpu::gp0_render_polygon(const uint32_t* buffer) {
    const uint32_t command = buffer[pointer_];
    const bool is_quad = (command & (1u << 27)) != 0;
    const bool shaded = (command & (1u << 28)) != 0;
    const bool textured = (command & (1u << 26)) != 0;
    const bool semi = (command & (1u << 25)) != 0;
    const bool raw = (command & (1u << 24)) != 0;
    Primitive prim;
    prim.shaded = shaded;
    prim.textured = textured;
    prim.semi_transparent = semi;
    prim.raw_textured = raw;
    prim.semi_mode = transparency_mode_;
    const int n = is_quad ? 4 : 3;
    uint32_t c[4] = {};
    Point2D v[4] = {};
    TextureData t[4] = {};
    if (!shaded) {
        const uint32_t color = buffer[pointer_++];
        const uint32_t rgb = uint32_t(get_rgb_color(color));
        c[0] = c[1] = rgb;
    }
    for (int i = 0; i < n; i++) {
        if (shaded) c[i] = buffer[pointer_++];
        const uint32_t xy = buffer[pointer_++];
        v[i].x = int16_t(signed11(xy & 0xFFFF) + draw_x_off_);
        v[i].y = int16_t(signed11(xy >> 16) + draw_y_off_);
        if (textured) {
            const uint32_t td = buffer[pointer_++];
            t[i].val = uint16_t(td);
            if (i == 0) {
                const uint32_t pal = td >> 16;
                prim.clut.x = int16_t((pal & 0x3F) << 4);
                prim.clut.y = int16_t((pal >> 6) & 0x1FF);
            } else if (i == 1) {
                gp0_e1(td >> 16);
                prim.depth = texture_depth_;
                prim.texture_base.x = int16_t(texture_x_base_ << 6);
                prim.texture_base.y = int16_t(texture_y_base_ << 8);
                prim.semi_mode = transparency_mode_;
            }
        }
    }
    rasterize_tri(v[0], v[1], v[2], t[0], t[1], t[2], c[0], c[1], c[2], prim);
    if (is_quad) rasterize_tri(v[1], v[2], v[3], t[1], t[2], t[3], c[1], c[2], c[3], prim);
}

void PsxGpu::rasterize_tri(Point2D v0, Point2D v1, Point2D v2,
                           TextureData t0, TextureData t1, TextureData t2,
                           uint32_t c0, uint32_t c1, uint32_t c2, const Primitive& prim) {
    int area = orient2d(v0, v1, v2);
    if (area == 0) return;
    if (area < 0) {
        std::swap(v1, v2); std::swap(t1, t2); std::swap(c1, c2);
        area = -area;
    }
    int minX = std::min({v0.x, v1.x, v2.x});
    int minY = std::min({v0.y, v1.y, v2.y});
    int maxX = std::max({v0.x, v1.x, v2.x});
    int maxY = std::max({v0.y, v1.y, v2.y});
    if ((maxX - minX) > 1024 || (maxY - minY) > 512) return;
    min_.x = int16_t(std::max(minX, int(draw_left_)));
    min_.y = int16_t(std::max(minY, int(draw_top_)));
    max_.x = int16_t(std::min(maxX, int(draw_right_) + 1));
    max_.y = int16_t(std::min(maxY, int(draw_bottom_) + 1));
    const int A01 = v0.y - v1.y, B01 = v1.x - v0.x;
    const int A12 = v1.y - v2.y, B12 = v2.x - v1.x;
    const int A20 = v2.y - v0.y, B20 = v0.x - v2.x;
    const int bias0 = is_top_left(v1, v2) ? 0 : -1;
    const int bias1 = is_top_left(v2, v0) ? 0 : -1;
    const int bias2 = is_top_left(v0, v1) ? 0 : -1;
    int w0_row = orient2d(v1, v2, min_) + bias0;
    int w1_row = orient2d(v2, v0, min_) + bias1;
    int w2_row = orient2d(v0, v1, min_) + bias2;
    for (int y = min_.y; y < max_.y; y++) {
        int w0 = w0_row, w1 = w1_row, w2 = w2_row;
        for (int x = min_.x; x < max_.x; x++) {
            if ((w0 | w1 | w2) >= 0) {
                if (check_mask_) {
                    color0_.val = uint32_t(get_pixel888(x, y));
                    if (color0_.m() != 0) {
                        w0 += A12; w1 += A20; w2 += A01; continue;
                    }
                }
                int color = int(c0);
                if (prim.shaded) {
                    color0_.val = c0; color1_.val = c1; color2_.val = c2;
                    const int r = interpolate(w0 - bias0, w1 - bias1, w2 - bias2, color0_.r(), color1_.r(), color2_.r(), area);
                    const int g = interpolate(w0 - bias0, w1 - bias1, w2 - bias2, color0_.g(), color1_.g(), color2_.g(), area);
                    const int b = interpolate(w0 - bias0, w1 - bias1, w2 - bias2, color0_.b(), color1_.b(), color2_.b(), area);
                    color = r << 16 | g << 8 | b;
                }
                if (prim.textured) {
                    const int tx = interpolate(w0 - bias0, w1 - bias1, w2 - bias2, t0.x(), t1.x(), t2.x(), area);
                    const int ty = interpolate(w0 - bias0, w1 - bias1, w2 - bias2, t0.y(), t1.y(), t2.y(), area);
                    int texel = get_texel(mask_texel(tx, pre_mask_x_, post_mask_x_),
                                          mask_texel(ty, pre_mask_y_, post_mask_y_),
                                          prim.clut, prim.texture_base, prim.depth);
                    if (texel == 0) { w0 += A12; w1 += A20; w2 += A01; continue; }
                    if (!prim.raw_textured) {
                        color0_.val = uint32_t(color);
                        color1_.val = uint32_t(texel);
                        color1_.set_r(clamp_ff(color0_.r() * color1_.r() >> 7));
                        color1_.set_g(clamp_ff(color0_.g() * color1_.g() >> 7));
                        color1_.set_b(clamp_ff(color0_.b() * color1_.b() >> 7));
                        texel = int(color1_.val);
                    }
                    color = texel;
                }
                if (prim.semi_transparent && (!prim.textured || (color & 0xFF000000) != 0)) {
                    color = handle_semi(x, y, color, prim.semi_mode);
                }
                color |= mask_while_drawing_ << 24;
                set_pixel(x, y, color);
            }
            w0 += A12; w1 += A20; w2 += A01;
        }
        w0_row += B12; w1_row += B20; w2_row += B01;
    }
}

void PsxGpu::gp0_render_line(const uint32_t* buffer) {
    const uint32_t command = buffer[pointer_++];
    uint32_t color1 = command & 0xFFFFFF;
    uint32_t color2 = color1;
    const bool is_poly = (command & (1u << 27)) != 0;
    const bool shaded = (command & (1u << 28)) != 0;
    const bool transparent = (command & (1u << 25)) != 0;
    uint32_t v1 = buffer[pointer_++];
    if (shaded) color2 = buffer[pointer_++];
    uint32_t v2 = buffer[pointer_++];
    rasterize_line(v1, v2, color1, color2, transparent);
    if (!is_poly) return;
    while ((buffer[pointer_] & 0xF000F000u) != 0x50005000u) {
        color1 = color2;
        if (shaded) color2 = buffer[pointer_++];
        v1 = v2;
        v2 = buffer[pointer_++];
        rasterize_line(v1, v2, color1, color2, transparent);
    }
    pointer_++;
}

void PsxGpu::rasterize_line(uint32_t vv1, uint32_t vv2, uint32_t color1, uint32_t color2, bool transparent) {
    int16_t x = signed11(vv1 & 0xFFFF);
    int16_t y = signed11(vv1 >> 16);
    int16_t x2 = signed11(vv2 & 0xFFFF);
    int16_t y2 = signed11(vv2 >> 16);
    if (std::abs(x - x2) > 0x3FF || std::abs(y - y2) > 0x1FF) return;
    x = int16_t(x + draw_x_off_); y = int16_t(y + draw_y_off_);
    x2 = int16_t(x2 + draw_x_off_); y2 = int16_t(y2 + draw_y_off_);
    int w = x2 - x, h = y2 - y;
    int dx1 = 0, dy1 = 0, dx2 = 0, dy2 = 0;
    if (w < 0) dx1 = -1; else if (w > 0) dx1 = 1;
    if (h < 0) dy1 = -1; else if (h > 0) dy1 = 1;
    if (w < 0) dx2 = -1; else if (w > 0) dx2 = 1;
    int longest = std::abs(w), shortest = std::abs(h);
    if (!(longest > shortest)) {
        longest = std::abs(h); shortest = std::abs(w);
        if (h < 0) dy2 = -1; else if (h > 0) dy2 = 1;
        dx2 = 0;
    }
    int numerator = longest >> 1;
    Color ca; ca.val = color1; Color cb; cb.val = color2;
    for (int i = 0; i <= longest; i++) {
        const float ratio = longest ? float(i) / float(longest) : 0.f;
        const int r = int(cb.r() * ratio + ca.r() * (1 - ratio));
        const int g = int(cb.g() * ratio + ca.g() * (1 - ratio));
        const int b = int(cb.b() * ratio + ca.b() * (1 - ratio));
        int color = r << 16 | g << 8 | b;
        if (x >= draw_left_ && x < draw_right_ && y >= draw_top_ && y < draw_bottom_) {
            if (transparent) color = handle_semi(x, y, color, transparency_mode_);
            color |= mask_while_drawing_ << 24;
            set_pixel(x, y, color);
        }
        numerator += shortest;
        if (!(numerator < longest)) {
            numerator -= longest;
            x = int16_t(x + dx1); y = int16_t(y + dy1);
        } else {
            x = int16_t(x + dx2); y = int16_t(y + dy2);
        }
    }
}

void PsxGpu::gp0_render_rectangle(const uint32_t* buffer) {
    const uint32_t command = buffer[pointer_++];
    const uint32_t color = command & 0xFFFFFF;
    const uint32_t opcode = command >> 24;
    Primitive prim;
    prim.textured = (command & (1u << 26)) != 0;
    prim.semi_transparent = (command & (1u << 25)) != 0;
    prim.raw_textured = (command & (1u << 24)) != 0;
    const uint32_t vertex = buffer[pointer_++];
    const int16_t xo = int16_t(vertex & 0xFFFF);
    const int16_t yo = int16_t(vertex >> 16);
    if (prim.textured) {
        const uint32_t texture = buffer[pointer_++];
        texture_data_.val = uint16_t(texture);
        const uint16_t pal = uint16_t(texture >> 16);
        prim.clut.x = int16_t((pal & 0x3F) << 4);
        prim.clut.y = int16_t((pal >> 6) & 0x1FF);
    }
    prim.depth = texture_depth_;
    prim.texture_base.x = int16_t(texture_x_base_ << 6);
    prim.texture_base.y = int16_t(texture_y_base_ << 8);
    prim.semi_mode = transparency_mode_;
    int16_t width = 0, height = 0;
    const uint32_t type = (opcode & 0x18) >> 3;
    if (type == 0) {
        const uint32_t hw = buffer[pointer_++];
        width = int16_t(hw & 0xFFFF);
        height = int16_t(hw >> 16);
    } else if (type == 1) { width = 1; height = 1; }
    else if (type == 2) { width = 8; height = 8; }
    else { width = 16; height = 16; }
    Point2D origin{signed11(uint32_t(xo + draw_x_off_)), signed11(uint32_t(yo + draw_y_off_))};
    Point2D size{int16_t(origin.x + width), int16_t(origin.y + height)};
    rasterize_rect(origin, size, texture_data_, color, prim);
}

void PsxGpu::rasterize_rect(Point2D origin, Point2D size, TextureData texture, uint32_t bgr, const Primitive& prim) {
    const int xOrigin = std::max(int(origin.x), int(draw_left_));
    const int yOrigin = std::max(int(origin.y), int(draw_top_));
    const int width = std::min(int(size.x), int(draw_right_) + 1);
    const int height = std::min(int(size.y), int(draw_bottom_) + 1);
    const int uOrigin = texture.x() + (xOrigin - origin.x);
    const int vOrigin = texture.y() + (yOrigin - origin.y);
    const int baseColor = get_rgb_color(bgr);
    for (int y = yOrigin, vv = vOrigin; y < height; y++, vv++) {
        for (int x = xOrigin, uu = uOrigin; x < width; x++, uu++) {
            if (check_mask_) {
                color0_.val = uint32_t(get_pixel888(x & 0x3FF, y & 0x1FF));
                if (color0_.m() != 0) continue;
            }
            int color = baseColor;
            if (prim.textured) {
                int texel = get_texel(mask_texel(uu, pre_mask_x_, post_mask_x_),
                                      mask_texel(vv, pre_mask_y_, post_mask_y_),
                                      prim.clut, prim.texture_base, prim.depth);
                if (texel == 0) continue;
                if (!prim.raw_textured) {
                    color0_.val = uint32_t(color);
                    color1_.val = uint32_t(texel);
                    color1_.set_r(clamp_ff(color0_.r() * color1_.r() >> 7));
                    color1_.set_g(clamp_ff(color0_.g() * color1_.g() >> 7));
                    color1_.set_b(clamp_ff(color0_.b() * color1_.b() >> 7));
                    texel = int(color1_.val);
                }
                color = texel;
            }
            if (prim.semi_transparent && (!prim.textured || (color & 0xFF000000) != 0)) {
                color = handle_semi(x, y, color, prim.semi_mode);
            }
            color |= mask_while_drawing_ << 24;
            set_pixel(x, y, color);
        }
    }
}

void PsxGpu::gp0_copy_vram_vram(const uint32_t* buffer) {
    pointer_++;
    const uint32_t src = buffer[pointer_++];
    const uint32_t dst = buffer[pointer_++];
    const uint32_t wh = buffer[pointer_++];
    const uint16_t sx = uint16_t(src & 0x3FF), sy = uint16_t((src >> 16) & 0x1FF);
    const uint16_t dx = uint16_t(dst & 0x3FF), dy = uint16_t((dst >> 16) & 0x1FF);
    const uint16_t w = uint16_t((((wh & 0xFFFF) - 1) & 0x3FF) + 1);
    const uint16_t h = uint16_t((((wh >> 16) - 1) & 0x1FF) + 1);
    for (int yy = 0; yy < h; yy++) {
        for (int xx = 0; xx < w; xx++) {
            int color = get_pixel888((sx + xx) & 0x3FF, (sy + yy) & 0x1FF);
            if (check_mask_) {
                color0_.val = uint32_t(get_pixel888((dx + xx) & 0x3FF, (dy + yy) & 0x1FF));
                if (color0_.m() != 0) continue;
            }
            color |= mask_while_drawing_ << 24;
            set_pixel((dx + xx) & 0x3FF, (dy + yy) & 0x1FF, color);
        }
    }
}

void PsxGpu::gp0_copy_cpu_vram(const uint32_t* buffer) {
    pointer_++;
    const uint32_t yx = buffer[pointer_++];
    const uint32_t wh = buffer[pointer_++];
    xfer_.x = int(yx & 0x3FF);
    xfer_.y = int((yx >> 16) & 0x1FF);
    xfer_.w = uint16_t((((wh & 0xFFFF) - 1) & 0x3FF) + 1);
    xfer_.h = uint16_t((((wh >> 16) - 1) & 0x1FF) + 1);
    xfer_.origin_x = xfer_.x;
    xfer_.origin_y = xfer_.y;
    xfer_.half_words = xfer_.w * xfer_.h;
    mode_ = Mode::Vram;
}

void PsxGpu::gp0_copy_vram_cpu(const uint32_t* buffer) {
    pointer_++;
    const uint32_t yx = buffer[pointer_++];
    const uint32_t wh = buffer[pointer_++];
    xfer_.x = int(yx & 0x3FF);
    xfer_.y = int((yx >> 16) & 0x1FF);
    xfer_.w = uint16_t((((wh & 0xFFFF) - 1) & 0x3FF) + 1);
    xfer_.h = uint16_t((((wh >> 16) - 1) & 0x1FF) + 1);
    xfer_.origin_x = xfer_.x;
    xfer_.origin_y = xfer_.y;
    xfer_.half_words = xfer_.w * xfer_.h;
    ready_vram_to_cpu_ = true;
    ready_dma_ = false;
}

}  // namespace dsp
