#include "video/sega_315_5313.h"

#include <algorithm>
#include <cstring>

namespace dsp {
namespace {

constexpr uint8_t kExpand3to8[8] = {0, 36, 73, 109, 146, 182, 219, 255};

uint32_t cram_to_rgb(uint16_t color) {
    const int r = (color >> 1) & 7;
    const int g = (color >> 5) & 7;
    const int b = (color >> 9) & 7;
    return 0xff000000u | (uint32_t(kExpand3to8[r]) << 16) | (uint32_t(kExpand3to8[g]) << 8) |
           kExpand3to8[b];
}

uint32_t shade_rgb(uint32_t rgb, bool shadow, bool highlight) {
    auto channel = [&](int shift) {
        int v = int((rgb >> shift) & 0xff);
        if (shadow && !highlight) v /= 2;
        if (highlight && !shadow) v = std::min(255, v + (255 - v) / 2);
        return uint32_t(v);
    };
    return 0xff000000u | (channel(16) << 16) | (channel(8) << 8) | channel(0);
}

}  // namespace

Sega3155313::Sega3155313(bool pal) : psg_(kPsgClock), pal_(pal) { reset(); }

void Sega3155313::reset() {
    regs_.fill(0);
    vram_.fill(0);
    cram_.fill(0);
    vsram_.fill(0);
    palette_.fill(0xff000000u);
    line_buf_.fill(0xff000000u);
    vblank_flag_ = false;
    sprite_collision_ = false;
    sprite_overflow_ = false;
    command_pending_ = false;
    irq4_pending_ = false;
    irq6_pending_ = false;
    vram_fill_pending_ = false;
    dma_active_ = false;
    imode_ = 0;
    imode_odd_frame_ = 0;
    vdp_code_ = 0;
    command_part1_ = 0;
    command_part2_ = 0;
    vdp_address_ = 0;
    vram_fill_length_ = 0;
    data_read_buffer_ = 0;
    total_scanlines_ = pal_ ? kLinesPal : kLinesNtsc;
    visible_scanlines_ = 224;
    irq6_scanline_ = 224;
    z80irq_scanline_ = 226;
    hint_counter_ = 0;
    hpos_cycles_ = 0;
    current_line_ = 0;
    h40_ = true;
    psg_.reset();
    raise_hint(false);
    raise_vint(false);
    raise_z80_irq(false);
}

void Sega3155313::raise_hint(bool state) {
    if (hint_) hint_(state);
}

void Sega3155313::raise_vint(bool state) {
    if (vint_) vint_(state);
}

void Sega3155313::raise_z80_irq(bool state) {
    if (z80_irq_) z80_irq_(state);
}

void Sega3155313::poke_vram_word(uint16_t addr, uint16_t value) {
    vram_[addr] = uint8_t(value >> 8);
    vram_[uint16_t(addr + 1)] = uint8_t(value);
}

void Sega3155313::poke_cram(int index, uint16_t value) {
    cram_[size_t(index) & 0x3f] = value & 0x0eee;
    palette_[size_t(index) & 0x3f] = cram_to_rgb(cram_[size_t(index) & 0x3f]);
}

void Sega3155313::poke_reg(int index, uint8_t value) { set_register(index, value); }

uint16_t Sega3155313::read_vram_word(uint16_t addr) const {
    addr &= 0xfffe;
    return uint16_t((vram_[addr] << 8) | vram_[uint16_t(addr + 1)]);
}

void Sega3155313::increment_address() {
    vdp_address_ = uint16_t(vdp_address_ + (regs_[0x0f] & 0xff));
}

void Sega3155313::write_vram_word(uint16_t value) {
    const uint16_t addr = vdp_address_ & 0xfffe;
    if ((vdp_address_ & 1) != 0) {
        vram_[addr] = uint8_t(value);
        vram_[uint16_t(addr + 1)] = uint8_t(value >> 8);
    } else {
        vram_[addr] = uint8_t(value >> 8);
        vram_[uint16_t(addr + 1)] = uint8_t(value);
    }
    increment_address();
}

void Sega3155313::write_cram_word(uint16_t value) {
    const int index = (vdp_address_ >> 1) & 0x3f;
    cram_[size_t(index)] = value & 0x0eee;
    palette_[size_t(index)] = cram_to_rgb(cram_[size_t(index)]);
    increment_address();
}

void Sega3155313::write_vsram_word(uint16_t value) {
    const int index = (vdp_address_ >> 1) % kVsramSize;
    vsram_[size_t(index)] = value & 0x07ff;
    increment_address();
}

void Sega3155313::update_code_and_address() {
    vdp_code_ = uint8_t(((command_part1_ & 0xc000) >> 14) | ((command_part2_ & 0x00f0) >> 2));
    vdp_address_ = uint16_t((command_part1_ & 0x3fff) | ((command_part2_ & 0x0003) << 14));
}

void Sega3155313::set_register(int regnum, uint8_t value) {
    regs_[size_t(regnum & 0x1f)] = value;
    if (regnum == 0x00) {
        if (irq4_pending_) raise_hint((regs_[0x00] & 0x10) != 0);
    }
    if (regnum == 0x01) {
        if (irq6_pending_) raise_vint((regs_[0x01] & 0x20) != 0);
    }
}

uint16_t Sega3155313::control_port_read() {
    command_pending_ = false;
    bool vblank = vblank_flag_;
    if ((regs_[0x01] & 0x40) == 0) vblank = true;
    uint8_t hblank = 0;
    if (hpos_cycles_ > 400) hblank = 1;
    if (hpos_cycles_ > 460) hblank = 0;
    const uint8_t odd = uint8_t((imode_ & 1) != 0 ? (imode_odd_frame_ ^ 1) : 0);
    // Status layout matches sega_315_5313.pas / the 315-5313 datasheet.
    const uint16_t ret = uint16_t(
        (1 << 13) | (1 << 12) | (1 << 10) | (1 << 9) |  // always-1 bits + FIFO empty
        (uint16_t(irq6_pending_) << 7) | (uint16_t(sprite_overflow_) << 6) |
        (uint16_t(sprite_collision_) << 5) | (uint16_t(odd) << 4) | (uint16_t(vblank) << 3) |
        (uint16_t(hblank) << 2) | (uint16_t(dma_active_) << 1) | uint16_t(pal_));
    // Reading status acknowledges the pending vertical interrupt flag.
    irq6_pending_ = false;
    sprite_collision_ = false;
    sprite_overflow_ = false;
    return ret;
}

uint16_t Sega3155313::data_port_read() {
    command_pending_ = false;
    uint16_t value = 0;
    switch (vdp_code_ & 0x0f) {
        case 0x00:  // VRAM read
            value = read_vram_word(vdp_address_);
            break;
        case 0x04:  // VSRAM read
            value = vsram_[(vdp_address_ >> 1) % kVsramSize];
            break;
        case 0x08:  // CRAM read
            value = cram_[(vdp_address_ >> 1) & 0x3f];
            break;
        default:
            value = data_read_buffer_;
            break;
    }
    data_read_buffer_ = value;
    increment_address();
    return value;
}

void Sega3155313::dma_fill(uint16_t value) {
    write_vram_word(value);
    uint32_t length = uint32_t(regs_[0x13] | (uint32_t(regs_[0x14]) << 8));
    if (length == 0) length = 0x10000;
    for (uint32_t i = 0; i < length; i++) {
        vram_[vdp_address_ ^ 1] = uint8_t(value >> 8);
        increment_address();
    }
    regs_[0x13] = 0;
    regs_[0x14] = 0;
    vram_fill_pending_ = false;
}

void Sega3155313::dma_68k_copy() {
    if (!dma_read_) return;
    uint32_t source = (uint32_t(regs_[0x15]) | (uint32_t(regs_[0x16]) << 8) |
                       (uint32_t(regs_[0x17] & 0x7f) << 16))
                      << 1;
    uint32_t length = uint32_t(regs_[0x13] | (uint32_t(regs_[0x14]) << 8));
    if (length == 0) length = 0x10000;
    const int cd = vdp_code_ & 0x0f;
    dma_active_ = true;
    for (uint32_t i = 0; i < length; i++) {
        const uint16_t data = dma_read_(source);
        if (cd == 0x01) write_vram_word(data);
        else if (cd == 0x03) write_cram_word(data);
        else if (cd == 0x05) write_vsram_word(data);
        source = (source & 0xfe0000) | ((source + 2) & 0x1ffff);
    }
    dma_active_ = false;
    regs_[0x13] = 0;
    regs_[0x14] = 0;
    // DMA source registers increment on hardware; keep them updated.
    source >>= 1;
    regs_[0x15] = uint16_t(source & 0xff);
    regs_[0x16] = uint16_t((source >> 8) & 0xff);
}

void Sega3155313::dma_vram_copy() {
    uint16_t source = uint16_t(regs_[0x15] | (uint16_t(regs_[0x16]) << 8));
    uint32_t length = uint32_t(regs_[0x13] | (uint32_t(regs_[0x14]) << 8));
    if (length == 0) length = 0x10000;
    dma_active_ = true;
    for (uint32_t i = 0; i < length; i++) {
        vram_[vdp_address_ & 0xffff] = vram_[source];
        source = uint16_t(source + 1);
        increment_address();
    }
    dma_active_ = false;
    regs_[0x13] = 0;
    regs_[0x14] = 0;
}

void Sega3155313::handle_dma() {
    if ((regs_[0x01] & 0x10) == 0) return;
    const int mode = (regs_[0x17] & 0xc0) >> 6;
    if (mode == 0 || mode == 1) {
        dma_68k_copy();
    } else if (mode == 2) {
        if ((vdp_code_ & 0x0f) == 0x01 || (vdp_code_ & 0x0f) == 0x03 ||
            (vdp_code_ & 0x0f) == 0x05) {
            vram_fill_pending_ = true;
            vram_fill_length_ = uint16_t(regs_[0x13] | (uint16_t(regs_[0x14]) << 8));
        }
    } else if (mode == 3) {
        dma_vram_copy();
    }
}

void Sega3155313::data_port_write(uint16_t value) {
    command_pending_ = false;
    if (vram_fill_pending_) {
        dma_fill(value);
        return;
    }
    switch (vdp_code_ & 0x0f) {
        case 0x01:
            write_vram_word(value);
            break;
        case 0x03:
            write_cram_word(value);
            break;
        case 0x05:
            write_vsram_word(value);
            break;
        default:
            break;
    }
}

void Sega3155313::control_port_write(uint16_t value) {
    vram_fill_pending_ = false;
    if (command_pending_) {
        command_pending_ = false;
        command_part2_ = value;
        update_code_and_address();
        if ((vdp_code_ & 0x20) != 0) handle_dma();
        return;
    }
    if ((value & 0xc000) == 0x8000) {
        set_register((value >> 8) & 0x1f, uint8_t(value));
        vdp_code_ = 0;
        vdp_address_ = 0;
        return;
    }
    command_pending_ = true;
    command_part1_ = value;
    update_code_and_address();
}

uint16_t Sega3155313::read(uint8_t address) {
    switch (address & 0x1f) {
        case 0x00:
        case 0x02:
            return data_port_read();
        case 0x04:
        case 0x06:
            return control_port_read();
        case 0x08:
        case 0x0a:
        case 0x0c:
        case 0x0e: {
            const uint8_t vc = v_counter(current_line_);
            // H counter is 9 bits internally, returned in the low byte as bits 8-1.
            const uint8_t hc = uint8_t((hpos_cycles_ * 2) & 0xff);
            return uint16_t((uint16_t(vc) << 8) | hc);
        }
        case 0x10:
        case 0x12:
        case 0x14:
        case 0x16:
            return 0;
        default:
            return 0xffff;
    }
}

void Sega3155313::write(uint8_t address, uint16_t value) {
    switch (address & 0x1f) {
        case 0x00:
        case 0x02:
            data_port_write(value);
            break;
        case 0x04:
        case 0x06:
            control_port_write(value);
            break;
        case 0x10:
        case 0x12:
        case 0x14:
        case 0x16:
            psg_.write(uint8_t(value));
            break;
        default:
            break;
    }
}

void Sega3155313::write_byte(uint8_t address, uint8_t value) {
    const uint8_t port = address & 0x1f;
    if (port >= 0x10 && port <= 0x17) {
        psg_.write(value);
        return;
    }
    // Byte writes to the 16-bit ports put the byte in both halves.
    write(port, uint16_t((uint16_t(value) << 8) | value));
}

uint8_t Sega3155313::v_counter(int line) const {
    if (!pal_) {
        if (line > 0xea) return uint8_t(line - 6);
        return uint8_t(line);
    }
    if (line >= 0x103) return uint8_t(line - 0x103 + 0xca);
    return uint8_t(line);
}

int Sega3155313::plane_width() const {
    switch (regs_[0x10] & 0x03) {
        case 0:
            return 32;
        case 1:
            return 64;
        default:
            return 128;
    }
}

int Sega3155313::plane_height() const {
    switch ((regs_[0x10] >> 4) & 0x03) {
        case 0:
            return 32;
        case 1:
            return 64;
        default:
            return 128;
    }
}

uint32_t Sega3155313::tile_row_bits(uint16_t tile, int row) const {
    const uint32_t addr = (uint32_t(tile & 0x7ff) << 5) + uint32_t(row << 2);
    return (uint32_t(vram_[addr & 0xffff]) << 24) | (uint32_t(vram_[(addr + 1) & 0xffff]) << 16) |
           (uint32_t(vram_[(addr + 2) & 0xffff]) << 8) | vram_[(addr + 3) & 0xffff];
}

void Sega3155313::blit_tile_row(int x, uint32_t row, int palette, bool hflip, bool priority,
                                uint8_t* color, uint8_t* pri, int width) const {
    for (int px = 0; px < 8; px++) {
        const int dest = x + px;
        if (dest < 0 || dest >= width) continue;
        const int src = hflip ? px : (7 - px);
        const int shift = src * 4;
        const uint8_t pix = uint8_t((row >> shift) & 0x0f);
        if (pix == 0) continue;
        color[dest] = uint8_t((palette << 4) | pix);
        pri[dest] = priority ? 1 : 0;
    }
}

void Sega3155313::draw_plane(int line, bool plane_a, uint8_t* color, uint8_t* pri) {
    const int width = h40_ ? 320 : 256;
    const int pw = plane_width();
    const int ph = plane_height();
    const uint32_t nt = plane_a ? (uint32_t(regs_[0x02] & 0x38) << 10)
                                : (uint32_t(regs_[0x04] & 0x07) << 13);
    const int hscroll_mode = regs_[0x0b] & 0x03;
    const uint32_t hbase = uint32_t(regs_[0x0d] & 0x3f) << 10;
    int hindex = 0;
    if (hscroll_mode == 2) hindex = line & ~7;
    else if (hscroll_mode == 3 || hscroll_mode == 1) hindex = line;
    const uint32_t hoff = hbase + uint32_t(hindex * 4) + (plane_a ? 0u : 2u);
    const uint16_t hscroll = read_vram_word(uint16_t(hoff));

    const bool column_scroll = (regs_[0x0b] & 0x04) != 0;
    const int vs_index = plane_a ? 0 : 1;

    for (int x = 0; x < width;) {
        int vscroll = vsram_[size_t(vs_index)] & 0x3ff;
        if (column_scroll) {
            const int col = ((x - int(hscroll & 15) + 16) / 16) % 20;
            vscroll = vsram_[size_t(((col * 2) + vs_index) % kVsramSize)] & 0x3ff;
        }
        const int plane_y = (line + vscroll) & ((ph * 8) - 1);
        const int tile_y = plane_y >> 3;
        const int row_in_tile = plane_y & 7;
        const int plane_x = (x - int(hscroll)) & ((pw * 8) - 1);
        const int tile_x = plane_x >> 3;
        const int fine_x = plane_x & 7;
        const uint32_t entry_addr = nt + uint32_t(((tile_y * pw) + tile_x) * 2);
        const uint16_t attr = read_vram_word(uint16_t(entry_addr));
        const uint16_t tile = attr & 0x7ff;
        const bool hflip = (attr & 0x0800) != 0;
        const bool vflip = (attr & 0x1000) != 0;
        const int palette = (attr >> 13) & 3;
        const bool priority = (attr & 0x8000) != 0;
        const int row = vflip ? (7 - row_in_tile) : row_in_tile;
        const uint32_t bits = tile_row_bits(tile, row);
        for (int px = fine_x; px < 8 && x < width; px++, x++) {
            const int src = hflip ? px : (7 - px);
            const uint8_t pix = uint8_t((bits >> (src * 4)) & 0x0f);
            if (pix == 0) continue;
            color[x] = uint8_t((palette << 4) | pix);
            pri[x] = priority ? 1 : 0;
        }
    }
}

void Sega3155313::draw_window(int line, uint8_t* color, uint8_t* pri) {
    const int width = h40_ ? 320 : 256;
    const uint8_t whp = uint8_t(regs_[0x11]);
    const uint8_t wvp = uint8_t(regs_[0x12]);
    const int window_h = (whp & 0x1f) * 8;
    const int window_v = (wvp & 0x1f) * 8;
    const bool window_right = (whp & 0x80) != 0;
    const bool window_down = (wvp & 0x80) != 0;

    bool line_in_window = false;
    if (window_down) line_in_window = line >= window_v;
    else line_in_window = window_v != 0 && line < window_v;

    const uint32_t nt = uint32_t(regs_[0x03] & (h40_ ? 0x3c : 0x3e)) << 10;
    const int cells = h40_ ? 64 : 32;

    for (int x = 0; x < width; x += 8) {
        bool in_window = line_in_window;
        if (!in_window) {
            if (window_right) in_window = x >= window_h;
            else in_window = window_h != 0 && x < window_h;
        }
        if (!in_window) continue;
        const int tile_x = x >> 3;
        const int tile_y = line >> 3;
        const uint32_t entry_addr = nt + uint32_t(((tile_y * cells) + tile_x) * 2);
        const uint16_t attr = read_vram_word(uint16_t(entry_addr));
        const bool hflip = (attr & 0x0800) != 0;
        const bool vflip = (attr & 0x1000) != 0;
        const int row = vflip ? (7 - (line & 7)) : (line & 7);
        blit_tile_row(x, tile_row_bits(attr & 0x7ff, row), (attr >> 13) & 3, hflip,
                      (attr & 0x8000) != 0, color, pri, width);
    }
}

void Sega3155313::draw_sprites(int line, uint8_t* color, uint8_t* pri) {
    const int width = h40_ ? 320 : 256;
    const int max_sprites = h40_ ? 80 : 64;
    const int max_per_line = h40_ ? 20 : 16;
    const int max_pixels = width;
    const uint32_t sat = uint32_t(regs_[0x05] & (h40_ ? 0x7e : 0x7f)) << 9;
    const int ybase = ((imode_ & 3) == 3) ? 256 : 128;

    int link = 0;
    int drawn = 0;
    int pixels = 0;
    bool masked = false;
    std::array<uint8_t, kMaxWidth> occupancy{};
    occupancy.fill(0);

    for (int n = 0; n < max_sprites; n++) {
        const uint32_t base = sat + uint32_t(link * 8);
        const uint16_t yword = read_vram_word(uint16_t(base));
        const uint16_t size_link = read_vram_word(uint16_t(base + 2));
        const uint16_t attr = read_vram_word(uint16_t(base + 4));
        const uint16_t xword = read_vram_word(uint16_t(base + 6));
        const int ypos = yword & 0x3ff;
        const int height = ((size_link >> 8) & 3) + 1;
        const int spr_width = ((size_link >> 10) & 3) + 1;
        const int y = line + ybase - ypos;
        const int next = size_link & 0x7f;
        if (y < 0 || y >= height * 8) {
            if (next == 0 || next == link) break;
            link = next;
            continue;
        }
        if (drawn >= max_per_line) {
            sprite_overflow_ = true;
            break;
        }
        const int xpos = xword & 0x1ff;
        if (xpos == 0) masked = true;
        if (!masked) {
            const bool hflip = (attr & 0x0800) != 0;
            const bool vflip = (attr & 0x1000) != 0;
            const int palette = (attr >> 13) & 3;
            const bool priority = (attr & 0x8000) != 0;
            const int y_in = vflip ? (height * 8 - 1 - y) : y;
            const int tile_row = y_in >> 3;
            const int row = y_in & 7;
            const uint16_t tile_base = attr & 0x7ff;
            for (int col = 0; col < spr_width; col++) {
                const int tile_col = hflip ? (spr_width - 1 - col) : col;
                const uint16_t tile = uint16_t(tile_base + tile_row + tile_col * height);
                const int x = xpos - 128 + col * 8;
                const uint32_t bits = tile_row_bits(tile, row);
                for (int px = 0; px < 8; px++) {
                    const int dest = x + px;
                    if (dest < 0 || dest >= width) continue;
                    const int src = hflip ? px : (7 - px);
                    const uint8_t pix = uint8_t((bits >> (src * 4)) & 0x0f);
                    if (pix == 0) continue;
                    if (occupancy[size_t(dest)] != 0) sprite_collision_ = true;
                    else {
                        occupancy[size_t(dest)] = 1;
                        color[dest] = uint8_t((palette << 4) | pix);
                        pri[dest] = priority ? 1 : 0;
                    }
                }
            }
        }
        drawn++;
        pixels += spr_width * 8;
        if (pixels >= max_pixels) break;
        if (next == 0 || next == link) break;
        link = next;
    }
}

uint32_t Sega3155313::palette_rgb(uint8_t index, bool shadow, bool highlight) const {
    const uint32_t rgb = palette_[size_t(index & 0x3f)];
    if (!shadow && !highlight) return rgb;
    return shade_rgb(rgb, shadow, highlight);
}

void Sega3155313::render_line(int line) {
    const int width = h40_ ? 320 : 256;
    const uint8_t backdrop = uint8_t(regs_[0x07] & 0x3f);
    const bool sh = (regs_[0x0c] & 0x08) != 0;
    std::array<uint8_t, kMaxWidth> a_col{}, a_pri{}, b_col{}, b_pri{}, s_col{}, s_pri{};
    a_col.fill(0);
    a_pri.fill(0);
    b_col.fill(0);
    b_pri.fill(0);
    s_col.fill(0);
    s_pri.fill(0);

    if ((regs_[0x01] & 0x40) != 0) {
        draw_plane(line, false, b_col.data(), b_pri.data());
        draw_plane(line, true, a_col.data(), a_pri.data());
        draw_window(line, a_col.data(), a_pri.data());
        draw_sprites(line, s_col.data(), s_pri.data());
    }

    const int left = h40_ ? 0 : 32;
    line_buf_.fill(palette_[size_t(backdrop)]);
    for (int x = 0; x < width; x++) {
        uint8_t color = backdrop;
        uint8_t layer_pri = 0;
        bool is_sprite = false;
        auto plot = [&](uint8_t col, uint8_t pri, bool sprite) {
            if (col == 0) return;
            color = col;
            layer_pri = pri;
            is_sprite = sprite;
        };
        plot(b_col[size_t(x)], b_pri[size_t(x)], false);
        if (a_pri[size_t(x)] == 0) plot(a_col[size_t(x)], a_pri[size_t(x)], false);
        if (s_pri[size_t(x)] == 0) plot(s_col[size_t(x)], s_pri[size_t(x)], true);
        if (b_pri[size_t(x)] != 0) plot(b_col[size_t(x)], b_pri[size_t(x)], false);
        if (a_pri[size_t(x)] != 0) plot(a_col[size_t(x)], a_pri[size_t(x)], false);
        if (s_pri[size_t(x)] != 0) plot(s_col[size_t(x)], s_pri[size_t(x)], true);

        bool shadow = false;
        bool highlight = false;
        if (sh) {
            shadow = layer_pri == 0;
            if (is_sprite && (s_col[size_t(x)] & 0x3f) == 0x3e) {
                highlight = true;
                shadow = false;
                color = backdrop;
                // Highlight the pixel underneath: recompute without this sprite.
                color = backdrop;
                layer_pri = 0;
                if (b_col[size_t(x)]) {
                    color = b_col[size_t(x)];
                    layer_pri = b_pri[size_t(x)];
                }
                if (a_col[size_t(x)] && (a_pri[size_t(x)] >= layer_pri || color == backdrop)) {
                    color = a_col[size_t(x)];
                }
            } else if (is_sprite && (s_col[size_t(x)] & 0x3f) == 0x3f) {
                shadow = true;
                highlight = false;
                color = backdrop;
                if (b_col[size_t(x)]) color = b_col[size_t(x)];
                if (a_col[size_t(x)]) color = a_col[size_t(x)];
            }
        }
        line_buf_[size_t(left + x)] = palette_rgb(color, shadow, highlight);
    }
    if (!h40_) {
        const uint32_t border = palette_[size_t(backdrop)];
        for (int x = 0; x < 32; x++) line_buf_[size_t(x)] = border;
        for (int x = 288; x < 320; x++) line_buf_[size_t(x)] = border;
    }
}

void Sega3155313::handle_scanline(int line) {
    current_line_ = line;
    h40_ = h40();
    raise_z80_irq(false);

    if (line == 0) hint_counter_ = int(regs_[0x0a]);

    if (line < visible_scanlines_) {
        render_line(line);
        if (hint_counter_ == 0) {
            irq4_pending_ = true;
            if ((regs_[0x00] & 0x10) != 0) raise_hint(true);
            hint_counter_ = int(regs_[0x0a]);
        } else {
            hint_counter_--;
        }
    } else {
        std::fill(line_buf_.begin(), line_buf_.end(), palette_[size_t(regs_[0x07] & 0x3f)]);
    }

    if (line == irq6_scanline_) {
        vblank_flag_ = true;
        irq6_pending_ = true;
        if ((regs_[0x01] & 0x20) != 0) raise_vint(true);
    }
    if (line == z80irq_scanline_) raise_z80_irq(true);
}

void Sega3155313::handle_eof() {
    vblank_flag_ = false;
    sprite_collision_ = false;
    sprite_overflow_ = false;
    imode_ = uint8_t((regs_[0x0c] & 0x06) >> 1);
    imode_odd_frame_ ^= 1;
    raise_z80_irq(false);
    if ((regs_[0x01] & 0x08) != 0) {
        // V30 (240-line) mode. Hardware only does this correctly on PAL.
        total_scanlines_ = pal_ ? kLinesPal : kLinesNtsc;
        visible_scanlines_ = 240;
        irq6_scanline_ = 240;
        z80irq_scanline_ = 240;
    } else {
        total_scanlines_ = pal_ ? kLinesPal : kLinesNtsc;
        visible_scanlines_ = 224;
        irq6_scanline_ = 224;
        z80irq_scanline_ = pal_ ? 224 : 226;
    }
}

}  // namespace dsp
