#include "drivers/arcade/ambush.h"

#include <algorithm>
#include <cstring>

#include "core/rom_loader.h"
#include "cpu/irq_line.h"

namespace dsp {
namespace {

const std::vector<RomEntry> kMain = {
    {"a1.i7", 0x2000, 0x0000, 0x31b85d9d},
    {"a2.g7", 0x2000, 0x2000, 0x8328d88a},
    {"a3.f7", 0x2000, 0x4000, 0x8db57ab5},
    {"a4.e7", 0x2000, 0x6000, 0x4a34d2a4},
};
const std::vector<RomEntry> kGfx = {
    {"fa1.m4", 0x2000, 0x0000, 0xad10969e},
    {"fa2.n4", 0x2000, 0x2000, 0xe7f134ba},
};
const std::vector<RomEntry> kProms = {
    {"a.bpr", 0x100, 0x000, 0x5f27f511},
    {"b.bpr", 0x100, 0x100, 0x1b03fd3b},
};

void blit8(uint32_t* dest, int dw, int dh, int dx, int dy, const uint8_t* src, int sw, int sh,
           const uint32_t* pal, int color_base, bool transparent, bool flipx, bool flipy) {
    for (int y = 0; y < sh; y++) {
        const int sy = flipy ? (sh - 1 - y) : y;
        const int py = dy + y;
        if (py < 0 || py >= dh) continue;
        for (int x = 0; x < sw; x++) {
            const int sx = flipx ? (sw - 1 - x) : x;
            const int px = dx + x;
            if (px < 0 || px >= dw) continue;
            const uint8_t pen = src[sy * sw + sx];
            if (transparent && pen == 0) continue;
            dest[py * dw + px] = pal[(color_base + pen) & 0xff];
        }
    }
}

}  // namespace

Ambush::Ambush()
    : cpu_(kCpuClock),
      ay0_(kAyClock),
      ay1_(kAyClock) {
    framebuffer_.assign(kScreenWidth * kScreenHeight, 0xff000000u);
    cpu_.set_memory_handlers([this](uint16_t a) { return main_read(a); },
                             [this](uint16_t a, uint8_t v) { main_write(a, v); });
    cpu_.set_io_handlers([this](uint16_t p) { return main_in(p); },
                         [this](uint16_t p, uint8_t v) { main_out(p, v); });
    ay0_.set_port_handlers([this]() { return in0_; }, nullptr, nullptr, nullptr);
    ay1_.set_port_handlers([this]() { return in1_; }, nullptr, nullptr, nullptr);
}

bool Ambush::init(const std::string& rom_path, std::string* error) {
    RomLoader loader;
    if (!loader.open(rom_path, error)) return false;

    std::vector<uint8_t> main_rom(0x8000, 0);
    if (!loader.load(kMain, main_rom, error)) return false;
    std::memcpy(memory_.data(), main_rom.data(), 0x8000);

    std::vector<uint8_t> gfx(0x4000, 0);
    if (!loader.load(kGfx, gfx, error)) return false;

    // Chars 8x8, 2bpp, $400 tiles — plane1 at $400*8*8, plane0 at 0
    {
        GfxLayout layout;
        layout.width = 8;
        layout.height = 8;
        layout.total = 0x400;
        layout.planes = 2;
        layout.char_increment = 8 * 8;
        layout.plane_offsets = {0x400 * 8 * 8, 0};
        layout.x_offsets = {0, 1, 2, 3, 4, 5, 6, 7};
        layout.y_offsets = {0 * 8, 1 * 8, 2 * 8, 3 * 8, 4 * 8, 5 * 8, 6 * 8, 7 * 8};
        chars_.decode(layout, gfx);
    }
    // Sprites 16x16, 2bpp, $100 — plane1 at $100*32*8, plane0 at 0
    {
        GfxLayout layout;
        layout.width = 16;
        layout.height = 16;
        layout.total = 0x100;
        layout.planes = 2;
        layout.char_increment = 32 * 8;
        layout.plane_offsets = {0x100 * 32 * 8, 0};
        layout.x_offsets = {0, 1, 2, 3, 4, 5, 6, 7, 8 * 8 + 0, 8 * 8 + 1, 8 * 8 + 2, 8 * 8 + 3,
                            8 * 8 + 4, 8 * 8 + 5, 8 * 8 + 6, 8 * 8 + 7};
        layout.y_offsets = {0 * 8,  1 * 8,  2 * 8,  3 * 8,  4 * 8,  5 * 8,  6 * 8,  7 * 8,
                            16 * 8, 17 * 8, 18 * 8, 19 * 8, 20 * 8, 21 * 8, 22 * 8, 23 * 8};
        sprites_.decode(layout, gfx);
    }

    std::vector<uint8_t> prom(0x200, 0);
    if (!loader.load(kProms, prom, error)) return false;
    for (int f = 0; f < 0x100; f++) {
        const uint8_t v = prom[f];
        const int r = 0x21 * ((v >> 0) & 1) + 0x47 * ((v >> 1) & 1) + 0x97 * ((v >> 2) & 1);
        const int g = 0x21 * ((v >> 3) & 1) + 0x47 * ((v >> 4) & 1) + 0x97 * ((v >> 5) & 1);
        const int b = 0x21 * 0 + 0x47 * ((v >> 6) & 1) + 0x97 * ((v >> 7) & 1);
        palette_[f] = 0xff000000u | (uint32_t(r) << 16) | (uint32_t(g) << 8) | uint32_t(b);
    }

    warnings_ = loader.warnings();
    reset();
    return true;
}

void Ambush::reset() {
    cpu_.reset();
    ay0_.reset();
    ay1_.reset();
    in0_ = 0xff;
    in1_ = 0xff;
    color_bank_ = 0;
    flip_screen_ = false;
    scroll_y_.fill(0);
    audio_accum_ = 0;
    audio_.clear();
    std::fill(framebuffer_.begin(), framebuffer_.end(), 0xff000000u);
}

uint8_t Ambush::main_read(uint16_t address) {
    if (address <= 0x87ff || (address >= 0xc000 && address <= 0xc7ff)) {
        return memory_[address];
    }
    if (address == 0xc800) return dsw_;
    return 0xff;
}

void Ambush::main_write(uint16_t address, uint8_t value) {
    if (address <= 0x7fff) return;  // ROM
    if ((address >= 0x8000 && address <= 0x87ff) || (address >= 0xc000 && address <= 0xc07f) ||
        (address >= 0xc0a0 && address <= 0xc0ff) || (address >= 0xc200 && address <= 0xc3ff)) {
        memory_[address] = value;
        return;
    }
    if (address == 0xa000) return;  // watchdog
    if (address >= 0xc080 && address <= 0xc09f) {
        scroll_y_[address & 0x1f] = uint16_t(value + 1);
        return;
    }
    if (address >= 0xc100 && address <= 0xc1ff) {
        memory_[address] = value;
        return;
    }
    if (address >= 0xc400 && address <= 0xc7ff) {
        memory_[address] = value;
        return;
    }
    if (address >= 0xcc00 && address <= 0xcc07) {
        switch (address & 7) {
            case 4:
                flip_screen_ = (value & 1) != 0;
                break;
            case 5:
                color_bank_ = value & 3;
                break;
            default:
                break;
        }
        return;
    }
}

uint8_t Ambush::main_in(uint16_t port) {
    switch (port & 0xff) {
        case 0x00:
            return ay0_.read();
        case 0x80:
            return ay1_.read();
        default:
            return 0xff;
    }
}

void Ambush::main_out(uint16_t port, uint8_t value) {
    switch (port & 0xff) {
        case 0x00:
            ay0_.control(value);
            break;
        case 0x01:
            ay0_.write(value);
            break;
        case 0x80:
            ay1_.control(value);
            break;
        case 0x81:
            ay1_.write(value);
            break;
        default:
            break;
    }
}

void Ambush::update_video() {
    // Pascal update_video_ambush
    std::fill(layer_bg_.begin(), layer_bg_.end(), 0);
    std::fill(layer_pri_.begin(), layer_pri_.end(), 0);
    std::fill(layer_spr_.begin(), layer_spr_.end(), 0);

    for (int f = 0; f < 0x400; f++) {
        const int x = f % 32;
        const int y = f / 32;
        const uint8_t atrib =
            memory_[0xc100 + (((f >> 2) & 0xe0) | (f & 0x1f))];
        const int nchar = ((atrib & 0x60) << 3) | memory_[0xc400 + f];
        const int color = ((color_bank_ << 4) | (atrib & 0x0f)) << 2;
        blit8(layer_bg_.data(), 256, 256, x * 8, y * 8, chars_.element(nchar & 0x3ff), 8, 8,
              palette_.data(), color, false, false, false);
        if (atrib & 0x10) {
            blit8(layer_pri_.data(), 256, 256, x * 8, y * 8, chars_.element(nchar & 0x3ff), 8, 8,
                  palette_.data(), color, true, false, false);
        }
    }

    // scroll__y_part2(1,3,8,@scroll_y) — per 8-pixel column Y scroll of BG → compose
    std::array<uint32_t, 256 * 256> compose{};
    compose.fill(0xff000000u);
    for (int col = 0; col < 32; col++) {
        const int sy = int(scroll_y_[col]) & 0xff;
        for (int y = 0; y < 256; y++) {
            const int syy = (y + sy) & 0xff;
            for (int x = 0; x < 8; x++) {
                compose[y * 256 + col * 8 + x] = layer_bg_[syy * 256 + col * 8 + x];
            }
        }
    }

    // Sprites (front to back: f = $7f downto 0)
    for (int f = 0x7f; f >= 0; f--) {
        const int x = memory_[0xc203 + f * 4];
        int y = memory_[0xc200 + f * 4];
        if (x == 0 && y == 0xff) continue;
        const uint8_t atrib = memory_[0xc202 + f * 4];
        const bool wrap = (atrib & 0x10) != 0;
        if ((x < 0x40 && wrap) || (x >= 0xc0 && !wrap)) continue;
        const int ngfx = atrib >> 7;
        int nchar = ((atrib & 0x60) << 1) | (memory_[0xc201 + f * 4] & 0x3f);
        if (ngfx == 0) {
            nchar <<= 2;
            y = 248 - y;
        } else {
            y = 240 - y;
        }
        const int color = ((color_bank_ << 4) | (atrib & 0x0f)) << 2;
        const bool flipx = (memory_[0xc201 + f * 4] & 0x40) != 0;
        const bool flipy = (memory_[0xc201 + f * 4] & 0x80) != 0;
        if (ngfx == 0) {
            // 8x8 from char set (4 consecutive for 16x16-ish grouping in nchar)
            blit8(compose.data(), 256, 256, x, y, chars_.element(nchar & 0x3ff), 8, 8,
                  palette_.data(), color, true, flipx, flipy);
        } else {
            blit8(compose.data(), 256, 256, x, y, sprites_.element(nchar & 0xff), 16, 16,
                  palette_.data(), color, true, flipx, flipy);
        }
    }

    // Priority tile layer with same column scroll
    for (int col = 0; col < 32; col++) {
        const int sy = int(scroll_y_[col]) & 0xff;
        for (int y = 0; y < 256; y++) {
            const int syy = (y + sy) & 0xff;
            for (int x = 0; x < 8; x++) {
                const uint32_t p = layer_pri_[syy * 256 + col * 8 + x];
                if (p) compose[y * 256 + col * 8 + x] = p;
            }
        }
    }

    // actualiza_trozo_final(0,16,256,224,3)
    for (int y = 0; y < kScreenHeight; y++) {
        for (int x = 0; x < kScreenWidth; x++) {
            uint32_t pix = compose[(y + 16) * 256 + x];
            if (flip_screen_) {
                // simple full-screen flip
                pix = compose[(255 - (y + 16)) * 256 + (255 - x)];
            }
            framebuffer_[size_t(y * kScreenWidth + x)] = pix ? pix : 0xff000000u;
        }
    }
}

void Ambush::on_cycles(int cycles) {
    audio_accum_ += int64_t(cycles) * AY8910::kSampleRate;
    while (audio_accum_ >= int64_t(kCpuClock)) {
        audio_accum_ -= int64_t(kCpuClock);
        const int32_t s = ay0_.update() + ay1_.update();
        audio_.push_back(int16_t(std::clamp(s, int32_t(-32768), int32_t(32767))));
    }
}

void Ambush::run_frame() {
    const int slice = std::max(1, int(kCpuClock / kFramesPerSecond) / kScanlines);
    for (int line = 0; line < kScanlines; line++) {
        if (line == 240) {
            update_video();
            cpu_.set_irq(IrqLine::Hold);
        }
        cpu_.run(slice);
        on_cycles(slice);
    }
}

void Ambush::set_inputs(const MachineInputs& inputs) {
    in0_ = 0xff;
    in1_ = 0xff;
    const auto& p1 = inputs.player1;
    const auto& p2 = inputs.player2;
    if (p1.button2) in0_ &= ~0x01;
    if (p1.button1) in0_ &= ~0x02;
    if (p2.button2) in0_ &= ~0x04;
    if (p2.button1) in0_ &= ~0x08;
    if (p2.start) in0_ &= ~0x10;
    if (p1.start) in0_ &= ~0x20;
    if (inputs.coin2) in0_ &= ~0x40;
    if (inputs.coin1) in0_ &= ~0x80;

    if (p1.up) in1_ &= ~0x01;
    if (p1.down) in1_ &= ~0x02;
    if (p1.left) in1_ &= ~0x04;
    if (p1.right) in1_ &= ~0x08;
    if (p2.up) in1_ &= ~0x10;
    if (p2.down) in1_ &= ~0x20;
    if (p2.left) in1_ &= ~0x40;
    if (p2.right) in1_ &= ~0x80;
}

void Ambush::set_dip_switch(int bank, uint8_t value) {
    if (bank == 0) dsw_ = value;
}

void Ambush::drain_audio(std::vector<int16_t>& out) {
    out.insert(out.end(), audio_.begin(), audio_.end());
    audio_.clear();
}

}  // namespace dsp
