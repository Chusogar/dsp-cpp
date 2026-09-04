#include "drivers/arcade/wwfsstar.h"

#include <algorithm>
#include <cstring>

#include "core/rom_loader.h"
#include "cpu/irq_line.h"

namespace dsp {
namespace {

// Program ROMs (16-bit interleaved: even = high byte, odd = low byte).
const std::vector<RomEntry> kProgEven = {{"24ac-0_j-1.34", 0x20000, 0, 0xec8fd2c9}};
const std::vector<RomEntry> kProgOdd = {{"24ad-0_j-1.35", 0x20000, 0, 0x54e614e4}};
// Alternate naming used by some dumps / Pascal driver.
const std::vector<RomEntry> kProgEvenAlt = {{"24ac-0.34", 0x20000, 0, 0}};
const std::vector<RomEntry> kProgOddAlt = {{"24ad-0.35", 0x20000, 0, 0}};

const std::vector<RomEntry> kSoundRom = {{"24ab-0.12", 0x8000, 0, 0x1e44f8aa}};

const std::vector<RomEntry> kOkiRoms = {
    {"24a9-0.46", 0x20000, 0, 0x703ff08f},
    {"24j8-0.45", 0x20000, 0x20000, 0x61138487},
};

const std::vector<RomEntry> kCharRom = {
    {"24aa-0.58", 0x20000, 0, 0xcb12ba40},
};
const std::vector<RomEntry> kCharRomAlt = {
    {"24aa-0_j.58", 0x20000, 0, 0xb9201b36},
};

const std::vector<RomEntry> kBgRoms = {
    {"24j7-0.113", 0x40000, 0, 0xe0a1909e},
    {"24j6-0.112", 0x40000, 0x40000, 0x77932ef8},
};

const std::vector<RomEntry> kSpriteRoms = {
    {"c951.114", 0x80000, 0, 0xfa76d1f0},
    {"24j4-0.115", 0x40000, 0x80000, 0xc4a589a3},
    {"24j5-0.116", 0x40000, 0xc0000, 0xd6bca436},
    {"c950.117", 0x80000, 0x100000, 0xcca5703d},
    {"24j2-0.118", 0x40000, 0x180000, 0xdc1b7600},
    {"24j3-0.119", 0x40000, 0x1c0000, 0x3ba12d43},
};

inline uint8_t pal4bit(uint8_t value) { return uint8_t((value & 0x0f) * 0x11); }

// Char x offsets from Pascal: (1,0, 8*8+1,8*8+0, 16*8+1,16*8+0, 24*8+1,24*8+0)
GfxLayout char_layout() {
    GfxLayout layout;
    layout.width = 8;
    layout.height = 8;
    layout.total = 0x1000;
    layout.planes = 4;
    layout.char_increment = 32 * 8;
    layout.plane_offsets = {0, 2, 4, 6};
    layout.x_offsets = {1, 0, 8 * 8 + 1, 8 * 8 + 0, 16 * 8 + 1, 16 * 8 + 0, 24 * 8 + 1, 24 * 8 + 0};
    layout.y_offsets = {0 * 8, 1 * 8, 2 * 8, 3 * 8, 4 * 8, 5 * 8, 6 * 8, 7 * 8};
    return layout;
}

// 16x16 tiles/sprites: planes at region_bytes*8+0 / +4 and 0 / 4
GfxLayout tile16_layout(int total, int plane_bytes) {
    GfxLayout layout;
    layout.width = 16;
    layout.height = 16;
    layout.total = total;
    layout.planes = 4;
    layout.char_increment = 64 * 8;
    layout.plane_offsets = {plane_bytes * 8 + 0, plane_bytes * 8 + 4, 0, 4};
    layout.x_offsets = {3, 2, 1, 0, 16 * 8 + 3, 16 * 8 + 2, 16 * 8 + 1, 16 * 8 + 0,
                        32 * 8 + 3, 32 * 8 + 2, 32 * 8 + 1, 32 * 8 + 0, 48 * 8 + 3, 48 * 8 + 2,
                        48 * 8 + 1, 48 * 8 + 0};
    layout.y_offsets = {0 * 8,  1 * 8,  2 * 8,  3 * 8,  4 * 8,  5 * 8,  6 * 8,  7 * 8,
                        8 * 8,  9 * 8,  10 * 8, 11 * 8, 12 * 8, 13 * 8, 14 * 8, 15 * 8};
    return layout;
}

bool load_optional(RomLoader& loader, const std::vector<RomEntry>& primary,
                   const std::vector<RomEntry>& alt, std::vector<uint8_t>& dest,
                   std::string* error) {
    if (loader.load(primary, dest, error)) return true;
    std::string ignored;
    return loader.load(alt, dest, &ignored);
}

}  // namespace

Wwfsstar::Wwfsstar() {
    framebuffer_.assign(size_t(kScreenWidth) * kScreenHeight, 0xff000000u);
    palette_rgb_.fill(0xff000000u);

    main_cpu_.set_memory_handlers(
        [this](uint32_t a) { return main_read(a); },
        [this](uint32_t a, uint16_t v) { main_write(a, v); });

    sound_cpu_.set_memory_handlers(
        [this](uint16_t a) { return sound_read(a); },
        [this](uint16_t a, uint8_t v) { sound_write(a, v); });
    sound_cpu_.set_io_handlers([](uint16_t) { return uint8_t(0xff); },
                               [](uint16_t, uint8_t) {});

    ym_.set_irq_handler([this](bool state) {
        sound_cpu_.set_irq(state ? IrqLine::Assert : IrqLine::Clear);
    });
}

bool Wwfsstar::init(const std::string& rom_path, std::string* error) {
    if (!load_roms(rom_path, error)) return false;
    reset();
    return true;
}

bool Wwfsstar::load_roms(const std::string& rom_path, std::string* error) {
    RomLoader loader;
    if (!loader.open(rom_path, error)) return false;

    // --- program (16-bit interleaved) ---
    std::vector<uint8_t> even(0x20000, 0), odd(0x20000, 0);
    if (!load_optional(loader, kProgEven, kProgEvenAlt, even, error)) return false;
    if (!load_optional(loader, kProgOdd, kProgOddAlt, odd, error)) return false;
    rom_.resize(0x20000);
    for (size_t i = 0; i < rom_.size(); i++) {
        rom_[i] = uint16_t((uint16_t(even[i]) << 8) | odd[i]);
    }

    // --- sound CPU ---
    std::vector<uint8_t> sound(0x8000, 0);
    if (!loader.load(kSoundRom, sound, error)) return false;
    std::copy(sound.begin(), sound.end(), sound_mem_.begin());

    // --- OKI samples ---
    std::vector<uint8_t> oki(0x40000, 0);
    if (!loader.load(kOkiRoms, oki, error)) return false;
    oki_.set_rom(std::move(oki));

    // --- graphics ---
    std::vector<uint8_t> chars(0x20000, 0);
    if (!load_optional(loader, kCharRom, kCharRomAlt, chars, error)) return false;

    std::vector<uint8_t> bg(0x80000, 0);
    if (!loader.load(kBgRoms, bg, error)) return false;

    std::vector<uint8_t> spr(0x200000, 0);
    if (!loader.load(kSpriteRoms, spr, error)) return false;

    decode_graphics(chars, bg, spr);
    warnings_ = loader.warnings();
    return true;
}

void Wwfsstar::decode_graphics(const std::vector<uint8_t>& chars, const std::vector<uint8_t>& bg,
                               const std::vector<uint8_t>& sprites) {
    chars_.decode(char_layout(), chars);
    // BG: two 0x40000 halves → plane offset 0x40000
    bg_tiles_.decode(tile16_layout(0x1000, 0x40000), bg);
    // Sprites: two 0x100000 halves → plane offset 0x100000
    sprites_.decode(tile16_layout(0x4000, 0x100000), sprites);
}

void Wwfsstar::reset() {
    main_cpu_.reset();
    sound_cpu_.reset();
    ym_.reset();
    oki_.reset();
    ram_.fill(0);
    fg_ram_.fill(0);
    bg_ram_.fill(0);
    sprite_ram_.fill(0);
    palette_ram_.fill(0);
    palette_rgb_.fill(0xff000000u);
    scroll_x_ = 0;
    scroll_y_ = 0;
    sound_latch_ = 0;
    flip_screen_ = false;
    vblank_ = false;
    // Clear sound RAM (ROM stays in low 32K).
    std::fill(sound_mem_.begin() + 0x8000, sound_mem_.end(), 0);
    std::fill(framebuffer_.begin(), framebuffer_.end(), 0xff000000u);
    audio_buf_.clear();
    audio_acc_ = 0;
}


void Wwfsstar::set_inputs(const MachineInputs& inputs) { inputs_ = inputs; }

void Wwfsstar::set_dip_switch(int bank, uint8_t value) {
    if (bank == 0) dsw_a_ = value;
    else if (bank == 1) dsw_b_ = value;
}

uint8_t Wwfsstar::p1_inputs() const {
    uint8_t v = 0xff;
    const auto& p = inputs_.player1;
    if (p.right) v &= ~0x01;
    if (p.left) v &= ~0x02;
    if (p.up) v &= ~0x04;
    if (p.down) v &= ~0x08;
    if (p.button1) v &= ~0x10;
    if (p.button2) v &= ~0x20;
    if (p.button3) v &= ~0x40;
    if (p.start) v &= ~0x80;
    return v;
}

uint8_t Wwfsstar::p2_inputs() const {
    uint8_t v = 0xff;
    const auto& p = inputs_.player2;
    if (p.right) v &= ~0x01;
    if (p.left) v &= ~0x02;
    if (p.up) v &= ~0x04;
    if (p.down) v &= ~0x08;
    if (p.button1) v &= ~0x10;
    if (p.button2) v &= ~0x20;
    if (p.button3) v &= ~0x40;
    if (p.start) v &= ~0x80;
    return v;
}

uint8_t Wwfsstar::system_inputs() const {
    // Bit 0 = vblank (active high in hardware read path used by game).
    // Coins clear bits 1 and 2 (active low).
    uint8_t v = 0xfe;
    if (vblank_) v |= 0x01;
    if (inputs_.coin1) v &= ~0x02;
    if (inputs_.coin2) v &= ~0x04;
    return v;
}

uint16_t Wwfsstar::main_read(uint32_t address) {
    address &= 0xffffff;
    if (address < 0x40000) {
        return rom_[(address >> 1) & 0x1ffff];
    }
    if (address >= 0x80000 && address < 0x81000) {
        return fg_ram_[(address & 0xfff) >> 1];
    }
    if (address >= 0xc0000 && address < 0xc1000) {
        return bg_ram_[(address & 0xfff) >> 1];
    }
    if (address >= 0x100000 && address < 0x100400) {
        return sprite_ram_[(address & 0x3ff) >> 1];
    }
    if (address >= 0x140000 && address < 0x141000) {
        return palette_ram_[(address & 0xfff) >> 1];
    }
    switch (address & 0xfffffe) {
        case 0x180000: return dsw_a_;
        case 0x180002: return dsw_b_;
        case 0x180004: return p1_inputs();
        case 0x180006: return p2_inputs();
        case 0x180008: return system_inputs();
        default: break;
    }
    if (address >= 0x1c0000 && address < 0x1c4000) {
        return ram_[(address & 0x3fff) >> 1];
    }
    return 0xffff;
}

void Wwfsstar::main_write(uint32_t address, uint16_t value) {
    address &= 0xffffff;
    if (address < 0x40000) return;
    if (address >= 0x80000 && address < 0x81000) {
        fg_ram_[(address & 0xfff) >> 1] = value;
        return;
    }
    if (address >= 0xc0000 && address < 0xc1000) {
        bg_ram_[(address & 0xfff) >> 1] = value;
        return;
    }
    if (address >= 0x100000 && address < 0x100400) {
        sprite_ram_[(address & 0x3ff) >> 1] = value;
        return;
    }
    if (address >= 0x140000 && address < 0x141000) {
        const int index = (address & 0xfff) >> 1;
        palette_ram_[size_t(index)] = value;
        write_palette(index, value);
        return;
    }
    switch (address & 0xfffffe) {
        case 0x180000:
            main_cpu_.set_irq(6, IrqLine::Clear);
            return;
        case 0x180002:
            main_cpu_.set_irq(5, IrqLine::Clear);
            return;
        case 0x180004:
            scroll_x_ = value & 0x1ff;
            return;
        case 0x180006:
            scroll_y_ = value & 0x1ff;
            return;
        case 0x180008:
            sound_latch_ = uint8_t(value & 0xff);
            sound_cpu_.set_nmi(IrqLine::Pulse);
            return;
        case 0x18000a:
            flip_screen_ = (value & 1) != 0;
            return;
        default:
            break;
    }
    if (address >= 0x1c0000 && address < 0x1c4000) {
        ram_[(address & 0x3fff) >> 1] = value;
    }
}

uint8_t Wwfsstar::sound_read(uint16_t address) {
    if (address < 0x8800) return sound_mem_[address];
    if (address == 0x8801) return ym_.status();
    if (address == 0x9800) return oki_.read();
    if (address == 0xa000) return sound_latch_;
    return 0xff;
}

void Wwfsstar::sound_write(uint16_t address, uint8_t value) {
    if (address >= 0x8000 && address < 0x8800) {
        sound_mem_[address] = value;
        return;
    }
    if (address == 0x8800) {
        ym_.select_register(value);
        return;
    }
    if (address == 0x8801) {
        ym_.write(value);
        return;
    }
    if (address == 0x9800) {
        oki_.write(value);
        return;
    }
}

void Wwfsstar::write_palette(int index, uint16_t data) {
    // Pascal: B = bits 11-8, G = 7-4, R = 3-0 (4-bit each).
    const uint8_t r = pal4bit(uint8_t(data));
    const uint8_t g = pal4bit(uint8_t(data >> 4));
    const uint8_t b = pal4bit(uint8_t(data >> 8));
    palette_rgb_[size_t(index & 0x1ff)] =
        0xff000000u | (uint32_t(r) << 16) | (uint32_t(g) << 8) | uint32_t(b);
}

void Wwfsstar::blit_tile(const GfxSet& set, int code, int color_base, int dx, int dy, bool flipx,
                         bool flipy, bool transparent) {
    const int w = set.width();
    const int h = set.height();
    const uint8_t* src = set.element(code);
    for (int y = 0; y < h; y++) {
        const int sy = flipy ? (h - 1 - y) : y;
        const int py = dy + y;
        if (py < 0 || py >= kScreenHeight) continue;
        uint32_t* line = framebuffer_.data() + size_t(py) * kScreenWidth;
        for (int x = 0; x < w; x++) {
            const int sx = flipx ? (w - 1 - x) : x;
            const int px = dx + x;
            if (px < 0 || px >= kScreenWidth) continue;
            const uint8_t pen = src[sy * w + sx];
            if (transparent && pen == 0) continue;
            line[px] = palette_rgb_[size_t((color_base + pen) & 0x1ff)];
        }
    }
}

void Wwfsstar::draw_bg() {
    // 32x32 map of 16x16 tiles with Technos “swizzled” index (Pascal pos calc).
    for (int f = 0; f < 0x400; f++) {
        const int x = f & 0x1f;
        const int y = f >> 5;
        const int pos = (x & 0x0f) + ((y & 0x0f) << 4) + ((x & 0x10) << 4) + ((y & 0x10) << 5);
        const uint16_t attr = bg_ram_[size_t(pos * 2)];
        const uint16_t code_lo = bg_ram_[size_t(pos * 2 + 1)];
        const int color = (attr >> 4) & 7;
        const int code = (code_lo & 0xff) | ((attr & 0x0f) << 8);
        const bool flipx = (attr & 0x08) != 0;
        // World coords then apply scroll.
        const int wx = x * 16 - int(scroll_x_);
        const int wy = y * 16 - int(scroll_y_) - 8;  // crop 8 lines top
        blit_tile(bg_tiles_, code, 256 + (color << 4), wx, wy, flipx, false, false);
        // Wrap once for horizontal/vertical scroll.
        blit_tile(bg_tiles_, code, 256 + (color << 4), wx + 512, wy, flipx, false, false);
        blit_tile(bg_tiles_, code, 256 + (color << 4), wx, wy + 512, flipx, false, false);
        blit_tile(bg_tiles_, code, 256 + (color << 4), wx + 512, wy + 512, flipx, false, false);
    }
}

void Wwfsstar::draw_fg() {
    // 32x32 map of 8x8 characters, transparent pen 0.
    for (int f = 0; f < 0x400; f++) {
        const int x = f & 0x1f;
        const int y = f >> 5;
        const uint16_t attr = fg_ram_[size_t(f * 2)];
        const uint16_t code_lo = fg_ram_[size_t(f * 2 + 1)];
        const int color = (attr >> 4) & 0x0f;
        const int code = (code_lo & 0xff) | ((attr & 0x0f) << 8);
        blit_tile(chars_, code, color << 4, x * 8, y * 8 - 8, false, false, true);
    }
}

void Wwfsstar::draw_sprites() {
    // 0x66 sprite slots × 5 words (Pascal buffer_sprites_w).
    for (int f = 0; f <= 0x65; f++) {
        const uint16_t attr = sprite_ram_[size_t(f * 5 + 1)];
        if ((attr & 1) == 0) continue;

        int y = (sprite_ram_[size_t(f * 5)] & 0xff) | ((attr & 4) << 6);
        y = ((256 - y) & 0x1ff) - 32;
        int x = (sprite_ram_[size_t(f * 5 + 4)] & 0xff) | ((attr & 8) << 5);
        x = ((256 - x) & 0x1ff) - 16;

        const uint16_t attr2 = sprite_ram_[size_t(f * 5 + 2)];
        const bool flipx = (attr2 & 0x80) != 0;
        const bool flipy = (attr2 & 0x40) != 0;
        int code = (sprite_ram_[size_t(f * 5 + 3)] & 0xff) | ((attr2 & 0x3f) << 8);
        const int color = attr & 0xf0;

        if (attr & 2) {
            // 16x32: two stacked 16x16 tiles.
            code &= 0x3ffe;
            const int a = flipy ? 16 : 0;
            blit_tile(sprites_, code, 128 + color, x, y + a - 8, flipx, flipy, true);
            blit_tile(sprites_, code + 1, 128 + color, x, y + (a ^ 16) - 8, flipx, flipy, true);
        } else {
            // 16x16, drawn with +16 Y offset as in Pascal.
            blit_tile(sprites_, code, 128 + color, x, y + 16 - 8, flipx, flipy, true);
        }
    }
}

void Wwfsstar::update_video() {
    std::fill(framebuffer_.begin(), framebuffer_.end(), palette_rgb_[0]);
    draw_bg();
    draw_sprites();
    draw_fg();
}

void Wwfsstar::run_frame() {
    const int main_per_line =
        std::max(1, int(double(kMainClock) / kFramesPerSecond / kScanlines));
    const int sound_per_line =
        std::max(1, int(double(kSoundClock) / kFramesPerSecond / kScanlines));

    for (int line = 0; line < kScanlines; line++) {
        // VBlank flag: clear at line 0, set at line 240 (Pascal).
        if (line == 0) vblank_ = false;
        if (line == kVBlankLine) {
            vblank_ = true;
            main_cpu_.set_irq(5, IrqLine::Assert);
            main_cpu_.set_irq(6, IrqLine::Assert);
            update_video();
        } else if (line > 0 && (line % 16) == 0 && line <= 256) {
            // Periodic IRQ5 every 16 lines (Pascal list includes 16..256).
            main_cpu_.set_irq(5, IrqLine::Assert);
        }

        main_cpu_.run(main_per_line);
        sound_cpu_.run(sound_per_line);
        ym_.run_timers(sound_per_line);
        // Mix audio at host sample rate from sound-CPU clock.
        audio_acc_ += int64_t(sound_per_line) * YM2151::kSampleRate;
        while (audio_acc_ >= kSoundClock) {
            audio_acc_ -= kSoundClock;
            const int32_t sample = ym_.update() + oki_.update() / 2;
            int32_t clipped = sample;
            if (clipped > 32767) clipped = 32767;
            if (clipped < -32768) clipped = -32768;
            audio_buf_.push_back(int16_t(clipped));
        }
    }
}

void Wwfsstar::drain_audio(std::vector<int16_t>& out) {
    out.insert(out.end(), audio_buf_.begin(), audio_buf_.end());
    audio_buf_.clear();
}

}  // namespace dsp
