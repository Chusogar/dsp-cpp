#include "drivers/scv.h"

#include <algorithm>
#include <cstring>
#include <fstream>

namespace dsp {
namespace {

bool read_file(const std::string& path, std::vector<uint8_t>* out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    f.seekg(0, std::ios::end);
    const auto n = f.tellg();
    if (n <= 0) return false;
    f.seekg(0, std::ios::beg);
    out->resize(size_t(n));
    f.read(reinterpret_cast<char*>(out->data()), n);
    return bool(f);
}

}  // namespace

Scv::Scv()
    : cpu_(kCpuClock),
      sound_(6000000, 10.0f),
      framebuffer_(size_t(kScreenWidth * kScreenHeight), 0) {
    cpu_.set_memory_handlers(
        [this](uint16_t a) { return read_mem(a); },
        [this](uint16_t a, uint8_t v) { write_mem(a, v); });
    cpu_.set_port_in(
        [this](uint8_t m) { return port_b_in(m); },
        [this](uint8_t m) { return port_c_in(m); });
    cpu_.set_port_out(
        [this](uint8_t v) { port_a_out(v); },
        [this](uint8_t v) { port_c_out(v); });
    cpu_.set_cycle_handler([this](int c) { on_cycles(c); });
    sound_.set_ack_handler([this](bool asserted) {
        cpu_.set_input_line(Upd7801::kIntf1,
                            asserted ? IrqLine::Assert : IrqLine::Clear);
    });
}

bool Scv::init(const std::string& rom_path, std::string* error) {
	load_bios("upd7801g.s01", "epochtv.chr", error);
    return load_media(rom_path, error);
}

bool Scv::load_bios(const std::string& bios_path, const std::string& chr_path,
                    std::string* error) {
    std::vector<uint8_t> bios, chr;
    if (!read_file(bios_path, &bios) || bios.size() < 0x1000) {
        if (error) *error = "cannot load SCV BIOS";
        return false;
    }
    if (!read_file(chr_path, &chr) || chr.size() < 0x400) {
        if (error) *error = "cannot load SCV character ROM";
        return false;
    }
    std::memcpy(mem_.data(), bios.data(), 0x1000);
    std::memcpy(chars_.data(), chr.data(), 0x400);
    return true;
}

bool Scv::load_media(const std::string& path, std::string* error) {
    std::vector<uint8_t> data;
    if (!read_file(path, &data)) {
        if (error) *error = "cannot open ROM: " + path;
        return false;
    }
    for (auto& b : rom_) b.fill(0xFF);
    rom_bank_type_ = 0;
    rom_window_ = 0;

    // Size-based banking (same heuristics as Pascal abrir_scv).
    if (data.size() <= 0x8000) {
        std::memcpy(rom_[0].data(), data.data(), data.size());
        rom_bank_type_ = 0;
    } else if (data.size() <= 0x10000) {
        std::memcpy(rom_[0].data(), data.data(), 0x8000);
        std::memcpy(rom_[1].data(), data.data() + 0x8000,
                    std::min(size_t(0x8000), data.size() - 0x8000));
        rom_bank_type_ = 1;
    } else {
        const size_t banks = std::min(size_t(4), (data.size() + 0x7FFF) / 0x8000);
        for (size_t i = 0; i < banks; i++) {
            const size_t off = i * 0x8000;
            const size_t n = std::min(size_t(0x8000), data.size() - off);
            std::memcpy(rom_[i].data(), data.data() + off, n);
        }
        rom_bank_type_ = 2;
    }
    reset();
    return true;
}

void Scv::reset() {
    cpu_.reset();
    sound_.reset();
    // Keep BIOS/chars; clear VRAM region
    for (int a = 0x2000; a <= 0x3403; a++) mem_[a] = 0;
    porta_val_ = 0xFF;
    portc_val_ = 0xFF;
    keys_.fill(0xFF);
    ram_bank_ = ram_bank2_ = false;
    rom_window_ = 0;
    plane_.fill(kPalette[1]);  // black
    std::fill(framebuffer_.begin(), framebuffer_.end(), kPalette[1]);
    audio_.clear();
}

void Scv::set_inputs(const MachineInputs& inputs) {
    keys_.fill(0xFF);
    auto clr = [](uint8_t& v, uint8_t mask) { v = uint8_t(v & ~mask); };

    // keys[0]: left0, up0, but0_0, left1, up1, but0_1
    if (inputs.player1.left) clr(keys_[0], 0x01);
    if (inputs.player1.up) clr(keys_[0], 0x02);
    if (inputs.player1.button1) clr(keys_[0], 0x04);
    if (inputs.player2.left) clr(keys_[0], 0x08);
    if (inputs.player2.up) clr(keys_[0], 0x10);
    if (inputs.player2.button1) clr(keys_[0], 0x20);

    // keys[1]: down0, right0, but1_0, down1, right1, but1_1
    if (inputs.player1.down) clr(keys_[1], 0x01);
    if (inputs.player1.right) clr(keys_[1], 0x02);
    if (inputs.player1.button2) clr(keys_[1], 0x04);
    if (inputs.player2.down) clr(keys_[1], 0x08);
    if (inputs.player2.right) clr(keys_[1], 0x10);
    if (inputs.player2.button2) clr(keys_[1], 0x20);

    // Numeric / keyboard rows left as all-released (0xFF).
    if (inputs.player1.start || inputs.coin1) clr(keys_[8], 0x01);
}

void Scv::set_dip_switch(int /*bank*/, uint8_t /*value*/) {}

void Scv::drain_audio(std::vector<int16_t>& out) {
    out.insert(out.end(), audio_.begin(), audio_.end());
    audio_.clear();
}

uint8_t Scv::read_mem(uint16_t addr) {
    if (addr <= 0x0FFF || (addr >= 0x2000 && addr <= 0x3403)) return mem_[addr];
    if (addr >= 0x6000 && addr <= 0x7FFF)
        return ram_bank_ ? mem_[addr] : 0xFF;
    if (addr >= 0x8000 && addr <= 0xDFFF)
        return rom_[rom_window_ & 3][addr & 0x7FFF];
    if (addr >= 0xE000 && addr <= 0xFF7F) {
        if (ram_bank2_) return mem_[addr];
        return rom_[rom_window_ & 3][addr & 0x7FFF];
    }
    if (addr >= 0xFF80) return cpu_.iram[addr & 0x7F];
    return 0xFF;
}

void Scv::write_mem(uint16_t addr, uint8_t value) {
    if (addr <= 0x0FFF || (addr >= 0x8000 && addr <= 0xDFFF)) return;
    if (addr >= 0x2000 && addr <= 0x3403) {
        mem_[addr] = value;
        return;
    }
    if (addr == 0x3600) {
        sound_.write(value);
        return;
    }
    if (addr >= 0x6000 && addr <= 0x7FFF) {
        if (ram_bank_) mem_[addr] = value;
        return;
    }
    if (addr >= 0xE000 && addr <= 0xFF7F) {
        if (ram_bank2_) mem_[addr] = value;
        return;
    }
    if (addr >= 0xFF80) cpu_.iram[addr & 0x7F] = value;
}

uint8_t Scv::port_b_in(uint8_t /*mask*/) {
    uint8_t data = 0xFF;
    for (int f = 0; f < 8; f++) {
        if ((porta_val_ & (1 << f)) == 0) data = uint8_t(data & keys_[f]);
    }
    return data;
}

uint8_t Scv::port_c_in(uint8_t /*mask*/) {
    return uint8_t((portc_val_ & 0xFE) | (keys_[8] & 1));
}

void Scv::port_a_out(uint8_t value) { porta_val_ = value; }

void Scv::port_c_out(uint8_t value) {
    portc_val_ = value;
    sound_.pcm_write(value & 0x08);
    switch (rom_bank_type_) {
        case 1:
            rom_window_ = uint8_t((value & 0x20) >> 5);
            break;
        case 2:
            rom_window_ = uint8_t((value >> 5) & 3);
            break;
        default:
            break;
    }
}

void Scv::on_cycles(int cycles) {
    sound_.run_cycles(cycles, kCpuClock);
}

void Scv::put_pixel(int x, int y, uint32_t color) {
    if (x < 0 || x >= 256 || y < 0 || y >= 256) return;
    plane_[size_t(y) * 256 + x] = color;
}

void Scv::draw_text(int x, int y, uint16_t char_data, uint8_t fg, uint8_t bg) {
    for (int f = 0; f < 8; f++) {
        uint8_t d = chars_[(char_data + f) & 0x3FF];
        for (int h = 0; h < 8; h++) {
            put_pixel(x + h, (y + f) & 0xFF, kPalette[(d & 0x80) ? fg : bg]);
            d = uint8_t(d << 1);
        }
    }
    for (int f = 8; f < 16; f++)
        for (int h = 0; h < 8; h++)
            put_pixel(x + h, (y + f) & 0xFF, kPalette[bg]);
}

void Scv::draw_semi_graph(int x, int y, uint8_t data, uint8_t fg) {
    if (data == 0) return;
    for (int f = 0; f < 4; f++)
        for (int h = 0; h < 4; h++)
            put_pixel(x + h, (y + f) & 0xFF, kPalette[fg]);
}

void Scv::draw_block_graph(int x, int y, uint8_t col) {
    for (int f = 0; f < 8; f++)
        for (int h = 0; h < 8; h++)
            put_pixel(x + h, (y + f) & 0xFF, kPalette[col]);
}

void Scv::plot_sprite_part(int x, int y, uint8_t pat, uint8_t col, int start_line) {
    if (x < 4 || (y + 2) < start_line) return;
    x -= 4;
    const uint32_t c = kPalette[col & 15];
    if (pat & 8) put_pixel(x, y + 2, c);
    if ((pat & 4) && x < 255) put_pixel(x + 1, y + 2, c);
    if ((pat & 2) && x < 254) put_pixel(x + 2, y + 2, c);
    if ((pat & 1) && x < 253) put_pixel(x + 3, y + 2, c);
}

void Scv::draw_sprite(int x, int y, uint8_t tile_idx, uint8_t col, bool left,
                      bool right, bool top, bool bottom, uint8_t clip_y,
                      int start_line) {
    y += int(clip_y) * 2;
    for (int f = clip_y; f < 8; f++) {
        const int base = 0x2000 + tile_idx * 32 + f * 4;
        const uint8_t pat0 = mem_[base];
        const uint8_t pat1 = mem_[base + 1];
        const uint8_t pat2 = mem_[base + 2];
        const uint8_t pat3 = mem_[base + 3];
        if ((top && (f * 4) < 16) || (bottom && (f * 4) >= 16)) {
            if (left) {
                plot_sprite_part(x, y, pat0 >> 4, col, start_line);
                plot_sprite_part(x + 4, y, pat1 >> 4, col, start_line);
            }
            if (right) {
                plot_sprite_part(x + 8, y, pat2 >> 4, col, start_line);
                plot_sprite_part(x + 12, y, pat3 >> 4, col, start_line);
            }
            if (left) {
                plot_sprite_part(x, y + 1, pat0 & 0xF, col, start_line);
                plot_sprite_part(x + 4, y + 1, pat1 & 0xF, col, start_line);
            }
            if (right) {
                plot_sprite_part(x + 8, y + 1, pat2 & 0xF, col, start_line);
                plot_sprite_part(x + 12, y + 1, pat3 & 0xF, col, start_line);
            }
        }
        y += 2;
    }
}

void Scv::update_video() {
    plane_.fill(kPalette[1]);
    const uint8_t ctrl = mem_[0x3400];
    const uint8_t fg = mem_[0x3401] & 0x0F;
    const uint8_t bg = (mem_[0x3401] >> 4) & 0x0F;
    const uint8_t gr_fg = mem_[0x3402] & 0x0F;
    const uint8_t gr_bg = (mem_[0x3402] >> 4) & 0x0F;
    const int mode = ctrl & 0x03;

    for (int y = 0; y < 16; y++) {
        for (int x = 0; x < 32; x++) {
            const uint8_t d = mem_[0x3000 + y * 32 + x];
            const bool text_x = x >= 2 && x < 30;
            const bool text_y = y < 14;
            if (text_x && text_y) {
                // Text mode always draws in the inner region for modes that use it
                if (mode == 0 || (ctrl & 0x40) == 0) {
                    draw_text(x * 8, y * 16, uint16_t(d * 8), fg, bg);
                } else {
                    switch (mode) {
                        case 1: {
                            draw_semi_graph(x * 8, y * 16, d >> 4, gr_fg);
                            draw_semi_graph(x * 8 + 4, y * 16, d & 0x0F, gr_fg);
                            draw_semi_graph(x * 8, y * 16 + 4, d >> 4, gr_fg);
                            draw_semi_graph(x * 8 + 4, y * 16 + 4, d & 0x0F, gr_fg);
                            break;
                        }
                        case 3:
                            draw_block_graph(x * 8, y * 16, d >> 4);
                            draw_block_graph(x * 8, y * 16 + 8, d & 0x0F);
                            break;
                        default:
                            draw_text(x * 8, y * 16, uint16_t(d * 8), fg, bg);
                            break;
                    }
                }
            } else {
                switch (mode) {
                    case 1:
                        draw_semi_graph(x * 8, y * 16, d >> 4, gr_fg);
                        draw_semi_graph(x * 8 + 4, y * 16, d & 0x0F, gr_fg);
                        break;
                    case 3:
                        draw_block_graph(x * 8, y * 16, d >> 4);
                        draw_block_graph(x * 8, y * 16 + 8, d & 0x0F);
                        break;
                    default:
                        break;
                }
            }
        }
    }

    if (ctrl & 0x10) {
        int screen_start_sprite_line = 0;
        if ((ctrl & 0xF7) == 0x17 && (mem_[0x3402] & 0xEF) == 0x4F)
            screen_start_sprite_line = 21 + 32;

        for (int f = 0; f < 128; f++) {
            int spr_y = mem_[0x3200 + f * 4] & 0xFE;
            bool y_32 = (mem_[0x3200 + f * 4] & 1) != 0;
            uint8_t clip = uint8_t(mem_[0x3201 + f * 4] >> 4);
            uint8_t col = mem_[0x3201 + f * 4] & 0x0F;
            int spr_x = mem_[0x3202 + f * 4] & 0xFE;
            bool x_32 = (mem_[0x3202 + f * 4] & 1) != 0;
            uint8_t tile_idx = mem_[0x3203 + f * 4] & 0x7F;
            bool half = (mem_[0x3203 + f * 4] & 0x80) != 0;
            bool left = true, right = true, top = true, bottom = true;
            if (col == 0 || spr_y == 0) continue;

            if (half) {
                if (tile_idx & 0x40) {
                    if (y_32) {
                        spr_y -= 8;
                        top = false;
                        bottom = true;
                        y_32 = false;
                    } else {
                        top = true;
                        bottom = false;
                    }
                }
                if (x_32) {
                    spr_x -= 8;
                    left = false;
                    right = true;
                    x_32 = false;
                } else {
                    left = true;
                    right = false;
                }
            }

            if ((ctrl & 0x20) && (f & 0x20)) {
                draw_sprite(spr_x, spr_y, tile_idx, col, left, right, top, bottom,
                            clip, screen_start_sprite_line);
                if (x_32 || y_32) {
                    const uint8_t spr_col =
                        (f & 0x40) ? kSpr2ColLut1[col] : kSpr2ColLut0[col];
                    draw_sprite(spr_x, spr_y,
                                uint8_t(tile_idx ^ (8 * int(x_32) + int(y_32))),
                                spr_col, left, right, top, bottom, clip,
                                screen_start_sprite_line);
                }
            } else {
                draw_sprite(spr_x, spr_y, tile_idx, col, left, right, top, bottom,
                            clip, screen_start_sprite_line);
                if (x_32)
                    draw_sprite(spr_x + 16, spr_y, uint8_t(tile_idx | 8), col, true,
                                true, top, bottom, clip, screen_start_sprite_line);
                if (y_32) {
                    if (clip & 8) clip = uint8_t(clip & 7);
                    else clip = 0;
                    draw_sprite(spr_x, spr_y + 16, uint8_t(tile_idx | 1), col, left,
                                right, true, true, clip, screen_start_sprite_line);
                    if (x_32)
                        draw_sprite(spr_x + 16, spr_y + 16, uint8_t(tile_idx | 9),
                                    col, true, true, true, true, clip,
                                    screen_start_sprite_line);
                }
            }
        }
    }

    // Crop (24,23) → 192×222
    for (int y = 0; y < kScreenHeight; y++) {
        const uint32_t* src = plane_.data() + size_t(y + 23) * 256 + 24;
        uint32_t* dst = framebuffer_.data() + size_t(y) * kScreenWidth;
        std::memcpy(dst, src, size_t(kScreenWidth) * sizeof(uint32_t));
    }
}

void Scv::run_frame() {
    for (int line = 0; line < kScanlines; line++) {
        if (line == 7)
            cpu_.set_input_line(Upd7801::kIntf2, IrqLine::Clear);
        if (line == 240) {
            update_video();
            cpu_.set_input_line(Upd7801::kIntf2, IrqLine::Assert);
        }
        cpu_.run(kCyclesPerLine);
    }
    sound_.take_samples(audio_);
}

}  // namespace dsp
