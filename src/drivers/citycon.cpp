#include "drivers/citycon.h"

#include "core/rom_loader.h"

#include <algorithm>
#include <cstring>

namespace dsp {
namespace {

uint8_t pal4bit(uint8_t n) {
    n &= 0x0f;
    return uint8_t(n | (n << 4));
}

bool load_raw(RomLoader& loader, const std::vector<RomEntry>& entries, std::vector<uint8_t>& dest,
              std::string* error) {
    return loader.load(entries, dest, error);
}

// Char layout: 8x8, 2bpp, planes at bit 4 and 0, x spans two 256-char banks.
GfxLayout char_layout() {
    GfxLayout layout;
    layout.width = 8;
    layout.height = 8;
    layout.total = 256;
    layout.planes = 2;
    layout.char_increment = 8 * 8;
    layout.plane_offsets = {4, 0};
    layout.x_offsets = {0, 1, 2, 3, 256 * 8 * 8 + 0, 256 * 8 * 8 + 1, 256 * 8 * 8 + 2,
                        256 * 8 * 8 + 3};
    layout.y_offsets = {0 * 8, 1 * 8, 2 * 8, 3 * 8, 4 * 8, 5 * 8, 6 * 8, 7 * 8};
    return layout;
}

// One 256-tile bank of the BG tileset (4bpp).
GfxLayout tile_bank_layout(int bank) {
    const int b0 = 4 + (0x1000 * bank * 8);
    const int b1 = 0 + (0x1000 * bank * 8);
    const int b2 = (0xc000 + 0x1000 * bank) * 8 + 4;
    const int b3 = (0xc000 + 0x1000 * bank) * 8 + 0;
    GfxLayout layout;
    layout.width = 8;
    layout.height = 8;
    layout.total = 256;
    layout.planes = 4;
    layout.char_increment = 8 * 8;
    layout.plane_offsets = {b0, b1, b2, b3};
    layout.x_offsets = {0, 1, 2, 3, 256 * 8 * 8 + 0, 256 * 8 * 8 + 1, 256 * 8 * 8 + 2,
                        256 * 8 * 8 + 3};
    layout.y_offsets = {0 * 8, 1 * 8, 2 * 8, 3 * 8, 4 * 8, 5 * 8, 6 * 8, 7 * 8};
    return layout;
}

// Sprite 8x16, 4bpp, two banks of 128.
GfxLayout sprite_bank_layout(int bank) {
    const int base = 0x1000 * bank * 8;
    const int high = (0x2000 + 0x1000 * bank) * 8;
    GfxLayout layout;
    layout.width = 8;
    layout.height = 16;
    layout.total = 128;
    layout.planes = 4;
    layout.char_increment = 16 * 8;
    layout.plane_offsets = {base + 4, base + 0, high + 4, high + 0};
    layout.x_offsets = {0, 1, 2, 3, 128 * 16 * 8 + 0, 128 * 16 * 8 + 1, 128 * 16 * 8 + 2,
                        128 * 16 * 8 + 3};
    layout.y_offsets = {0 * 8, 1 * 8, 2 * 8,  3 * 8,  4 * 8,  5 * 8,  6 * 8,  7 * 8,
                        8 * 8, 9 * 8, 10 * 8, 11 * 8, 12 * 8, 13 * 8, 14 * 8, 15 * 8};
    return layout;
}

}  // namespace

CityCon::CityCon()
    : main_cpu_(kMainClock),
      sound_cpu_(kSoundClock),
      ym_(kYmClock),
      ay_(kAyClock),
      layer_bg_(1024 * 256, 0),
      layer_fg_(1024 * 256, 0),
      composite_(256 * 256, 0),
      framebuffer_(size_t(kScreenWidth * kScreenHeight), 0xff000000u) {
    main_cpu_.set_memory_handlers(
        [this](uint16_t a) { return main_read(a); },
        [this](uint16_t a, uint8_t v) { main_write(a, v); });
    sound_cpu_.set_memory_handlers(
        [this](uint16_t a) { return sound_read(a); },
        [this](uint16_t a, uint8_t v) { sound_write(a, v); });
    sound_cpu_.set_cycle_handler([this](int c) { on_sound_cycles(c); });

    ym_.set_port_handlers(
        [this]() { return soundlatch_; },
        [this]() { return soundlatch2_; },
        nullptr, nullptr);
}

bool CityCon::load_roms(const std::string& rom_path, std::string* error) {
    RomLoader loader;
    if (!loader.open(rom_path, error)) return false;

    // Main program: c10 @ $4000 (16K), c11 @ $8000 (32K)
    std::vector<uint8_t> rom(0x10000, 0);
    if (!load_raw(loader,
                  {{"c10", 0x4000, 0x4000, 0xae88b53c},
                   {"c11", 0x8000, 0x8000, 0x139eb1aa}},
                  rom, error))
        return false;
    std::copy(rom.begin(), rom.end(), memory_.begin());

    std::vector<uint8_t> snd(0x10000, 0);
    if (!load_raw(loader, {{"c1", 0x8000, 0x8000, 0x1fad7589}}, snd, error)) return false;
    std::copy(snd.begin(), snd.end(), mem_snd_.begin());

    gfx_char_.assign(0x2000, 0);
    if (!load_raw(loader, {{"c4", 0x2000, 0, 0xa6b32fc6}}, gfx_char_, error)) return false;

    gfx_sprites_.assign(0x4000, 0);
    if (!load_raw(loader,
                  {{"c12", 0x2000, 0, 0x08eaaccd},
                   {"c13", 0x2000, 0x2000, 0x1819aafb}},
                  gfx_sprites_, error))
        return false;

    gfx_tiles_.assign(0x18000, 0);
    if (!load_raw(loader,
                  {{"c9", 0x8000, 0, 0x8aeb47e6},
                   {"c8", 0x4000, 0x8000, 0x0d7a1eeb},
                   {"c6", 0x8000, 0xc000, 0x2246fe9d},
                   {"c7", 0x4000, 0x14000, 0xe8b97de9}},
                  gfx_tiles_, error))
        return false;

    std::vector<uint8_t> fondo(0xe000, 0);
    if (!load_raw(loader,
                  {{"c2", 0x8000, 0, 0xf2da4f23},
                   {"c3", 0x4000, 0x8000, 0x7ef3ac1b},
                   {"c5", 0x2000, 0xc000, 0xc03d8b1b}},
                  fondo, error))
        return false;
    std::copy(fondo.begin(), fondo.begin() + 0xe000, memoria_fondo_.begin());

    warnings_ = loader.warnings();
    decode_graphics();
    return true;
}

void CityCon::decode_graphics() {
    chars_.decode(char_layout(), gfx_char_);

    // 12 banks × 256 tiles
    tiles_.create(8, 8, 3072);
    for (int bank = 0; bank < 12; bank++) {
        GfxLayout layout = tile_bank_layout(bank);
        tiles_.decode_elements(layout, gfx_tiles_, bank * 256);
    }

    sprites_.create(8, 16, 256);
    for (int bank = 0; bank < 2; bank++) {
        GfxLayout layout = sprite_bank_layout(bank);
        sprites_.decode_elements(layout, gfx_sprites_, bank * 128);
    }
}

bool CityCon::init(const std::string& rom_path, std::string* error) {
    if (!load_roms(rom_path, error)) return false;
    reset();
    return true;
}

void CityCon::set_color(int index) {
    // Two consecutive palette bytes: R/G then B/x
    const int dir = index * 2;
    if (dir + 1 >= int(palette_ram_.size())) return;
    const uint8_t rg = palette_ram_[size_t(dir)];
    const uint8_t bx = palette_ram_[size_t(dir + 1)];
    const uint8_t r = pal4bit(rg >> 4);
    const uint8_t g = pal4bit(rg);
    const uint8_t b = pal4bit(bx >> 4);
    palette_[size_t(index & 0x3ff)] =
        0xff000000u | (uint32_t(r) << 16) | (uint32_t(g) << 8) | uint32_t(b);
    if (index >= 256 && index <= 511) bg_dirty_ = true;
}

void CityCon::reset() {
    main_cpu_.reset();
    sound_cpu_.reset();
    ym_.reset();
    ay_.reset();
    fondo_ = 0;
    soundlatch_ = 0;
    soundlatch2_ = 0;
    scroll_x_ = 0;
    bg_dirty_ = true;
    flip_screen_ = false;
    in0_ = 0xff;
    in1_ = 0x80;
    in2_ = 0xff;
    lines_color_.fill(0);
    palette_ram_.fill(0);
    palette_.fill(0xff000000u);
    audio_accum_ = 0;
    audio_.clear();
    main_cpu_.set_irq(IrqLine::Clear);
}

void CityCon::set_inputs(const MachineInputs& inputs) {
    in0_ = 0xff;
    in1_ = 0x80;
    in2_ = 0xff;
    auto clr0 = [&](uint8_t m) { in0_ = uint8_t(in0_ & ~m); };
    auto clr2 = [&](uint8_t m) { in2_ = uint8_t(in2_ & ~m); };
    if (inputs.player1.up) clr0(0x01);
    if (inputs.player1.down) clr0(0x02);
    if (inputs.player1.right) clr0(0x04);
    if (inputs.player1.left) clr0(0x08);
    if (inputs.player1.button1) clr0(0x10);
    if (inputs.player1.button2) clr0(0x20);
    if (inputs.player1.start) clr0(0x40);
    if (inputs.player2.start) clr0(0x80);
    if (inputs.player2.up) clr2(0x01);
    if (inputs.player2.down) clr2(0x02);
    if (inputs.player2.right) clr2(0x04);
    if (inputs.player2.left) clr2(0x08);
    if (inputs.player2.button1) clr2(0x10);
    if (inputs.player2.button2) clr2(0x20);
    if (inputs.coin1) in1_ = uint8_t(in1_ & ~0x80);  // active low coin in in1 bit7? Pascal: in1 starts $80
    // Pascal only clears coin via... actually coin may be in dsw path. Keep default.
}

void CityCon::set_dip_switch(int bank, uint8_t value) {
    if (bank == 0) dsw_a_ = value;
    else if (bank == 1) dsw_b_ = value;
}

uint8_t CityCon::main_read(uint16_t address) {
    if (address <= 0x1fff || (address >= 0x2800 && address <= 0x28ff) || address >= 0x4000)
        return memory_[address];
    if (address >= 0x2000 && address <= 0x20ff)
        return lines_color_[address & 0xff];
    if (address == 0x3000) return flip_screen_ ? in2_ : in0_;
    if (address == 0x3001) return uint8_t(dsw_a_ + in1_);
    if (address == 0x3002) return dsw_b_;
    if (address == 0x3007) {
        main_cpu_.set_irq(IrqLine::Clear);
        return 0;
    }
    if (address >= 0x3800 && address <= 0x3cff)
        return palette_ram_[address & 0x7ff];
    return 0xff;
}

void CityCon::main_write(uint16_t address, uint8_t value) {
    if (address <= 0x0fff || (address >= 0x2800 && address <= 0x28ff)) {
        memory_[address] = value;
        return;
    }
    if (address >= 0x1000 && address <= 0x1fff) {
        memory_[address] = value;
        return;
    }
    if (address >= 0x2000 && address <= 0x20ff) {
        lines_color_[address & 0xff] = value;
        return;
    }
    switch (address) {
        case 0x3000: {
            const uint8_t new_fondo = uint8_t(value >> 4);
            if (fondo_ != new_fondo) {
                fondo_ = new_fondo;
                bg_dirty_ = true;
            }
            flip_screen_ = (value & 1) != 0;
            return;
        }
        case 0x3001: soundlatch_ = value; return;
        case 0x3002: soundlatch2_ = value; return;
        case 0x3004:
            scroll_x_ = uint16_t((scroll_x_ & 0xff) | ((value & 3) << 8));
            return;
        case 0x3005:
            scroll_x_ = uint16_t((scroll_x_ & 0x300) | value);
            return;
        default: break;
    }
    if (address >= 0x3800 && address <= 0x3cff) {
        const int off = address & 0x7ff;
        if (palette_ram_[size_t(off)] != value) {
            palette_ram_[size_t(off)] = value;
            set_color(off & ~1);  // pair aligned
            set_color(off >> 1);  // pos = dir shr 1 in Pascal uses full dir
            // Pascal: cambiar_color(direccion and $7fe) then pos:=dir shr 1
            const int dir = off & ~1;
            set_color(dir >> 1);
        }
        return;
    }
}

uint8_t CityCon::sound_read(uint16_t address) {
    if (address <= 0x0fff || address >= 0x8000) return mem_snd_[address];
    if (address == 0x4000) return ay_.read();
    if (address == 0x6000) return ym_.status();
    if (address == 0x6001) return ym_.read();
    return 0xff;
}

void CityCon::sound_write(uint16_t address, uint8_t value) {
    if (address <= 0x0fff) {
        mem_snd_[address] = value;
        return;
    }
    switch (address) {
        case 0x4000: ay_.control(value); break;
        case 0x4001: ay_.write(value); break;
        case 0x6000: ym_.control(value); break;
        case 0x6001: ym_.write(value); break;
        default: break;
    }
}

void CityCon::on_sound_cycles(int cycles) {
    audio_accum_ += int64_t(cycles) * YM2203::kSampleRate;
    while (audio_accum_ >= int64_t(kSoundClock)) {
        audio_accum_ -= int64_t(kSoundClock);
        int32_t sample = ym_.update() + ay_.update();
        sample = std::clamp(sample, int32_t(-32768), int32_t(32767));
        audio_.push_back(int16_t(sample));
    }
}

void CityCon::draw_background() {
    if (!bg_dirty_) return;
    layer_bg_.assign(1024 * 256, 0xff000000u);
    for (int f = 0; f < 0x1000; f++) {
        const int y_tile = f >> 5;
        int x = ((f & 0x1f) + (y_tile & 0x60)) << 3;
        int y = (y_tile & 0x1f) << 3;
        const int nchar = int(memoria_fondo_[0x1000 * fondo_ + f]) + (fondo_ << 8);
        const int color = memoria_fondo_[0xc000 + (nchar & 0x1fff)] & 0x0f;
        const int color_base = (color << 4) + 256;
        const uint8_t* pixels = tiles_.element(nchar);
        for (int py = 0; py < 8; py++) {
            for (int px = 0; px < 8; px++) {
                const uint8_t pix = pixels[py * 8 + px];
                const int sx = (x + px) & 0x3ff;
                const int sy = y + py;
                if (sy >= 0 && sy < 256)
                    layer_bg_[size_t(sy * 1024 + sx)] = palette_[size_t(color_base + pix)];
            }
        }
    }
    bg_dirty_ = false;
}

void CityCon::draw_foreground() {
    layer_fg_.assign(1024 * 256, 0);  // 0 = transparent
    for (int f = 0; f < 0x1000; f++) {
        const int y_tile = f >> 5;
        int x = (f & 0x1f) + (y_tile & 0x60);
        int y = y_tile & 0x1f;
        const int nchar = memory_[0x1000 + f];
        const uint8_t* pixels = chars_.element(nchar);
        for (int y2 = 0; y2 < 8; y2++) {
            const int color_base = (int(lines_color_[y2 + (y << 3)]) << 2) + 512;
            for (int x2 = 0; x2 < 8; x2++) {
                const uint8_t pix = pixels[y2 * 8 + x2];
                if (pix == 0) continue;
                const int sx = (x << 3) + x2;
                const int sy = (y << 3) + y2;
                if (sx >= 0 && sx < 1024 && sy >= 0 && sy < 256)
                    layer_fg_[size_t(sy * 1024 + sx)] = palette_[size_t(color_base + pix)];
            }
        }
    }
}

void CityCon::draw_sprites() {
    for (int f = 0x3f; f >= 0; f--) {
        const int base = 0x2800 + f * 4;
        const int y = 239 - int(memory_[base + 0]);
        const int nchar = memory_[base + 1];
        const uint8_t atrib = memory_[base + 2];
        const int x = memory_[base + 3];
        const int color_base = (atrib & 0x0f) << 4;
        const bool flipx = (atrib & 0x10) == 0;
        const uint8_t* pixels = sprites_.element(nchar);
        for (int py = 0; py < 16; py++) {
            const int sy = y + py;
            if (sy < 0 || sy >= 256) continue;
            for (int px = 0; px < 8; px++) {
                const int src_x = flipx ? (7 - px) : px;
                const uint8_t pix = pixels[py * 8 + src_x];
                if (pix == 0) continue;
                const int sx = (x + px) & 0xff;
                composite_[size_t(sy * 256 + sx)] = palette_[size_t(color_base + pix)];
            }
        }
    }
}

void CityCon::update_video() {
    draw_background();
    draw_foreground();

    // composite 256x256
    std::fill(composite_.begin(), composite_.end(), 0xff000000u);

    // BG scrolled into 256-wide window
    for (int y = 0; y < 256; y++) {
        for (int x = 0; x < 256; x++) {
            const int sx = (x + int(scroll_x_)) & 0x3ff;
            composite_[size_t(y * 256 + x)] = layer_bg_[size_t(y * 1024 + sx)];
        }
    }

    // FG: top 48 lines no scroll, rest scrolled (scroll_x_cut from y=48)
    for (int y = 0; y < 48; y++) {
        for (int x = 0; x < 256; x++) {
            const uint32_t p = layer_fg_[size_t(y * 1024 + x)];
            if (p != 0) composite_[size_t(y * 256 + x)] = p;
        }
    }
    for (int y = 48; y < 256; y++) {
        for (int x = 0; x < 256; x++) {
            const int sx = (x + int(scroll_x_)) & 0x3ff;
            const uint32_t p = layer_fg_[size_t(y * 1024 + sx)];
            if (p != 0) composite_[size_t(y * 256 + x)] = p;
        }
    }

    draw_sprites();

    // Crop: actualiza_trozo_final(8, 16, 240, 224, 3)
    for (int y = 0; y < kScreenHeight; y++) {
        const uint32_t* src = &composite_[size_t((y + 16) * 256 + 8)];
        uint32_t* dst = &framebuffer_[size_t(y * kScreenWidth)];
        std::copy(src, src + kScreenWidth, dst);
    }
}

void CityCon::run_frame() {
    const int cycles_main =
        int(double(kMainClock) / (kFramesPerSecond * kScanlines) + 0.5);
    const int cycles_sound =
        int(double(kSoundClock) / (kFramesPerSecond * kScanlines) + 0.5);

    for (int line = 0; line < kScanlines; line++) {
        if (line == 255) {
            main_cpu_.set_irq(IrqLine::Assert);
            update_video();
        }
        main_cpu_.run(cycles_main);
        sound_cpu_.run(cycles_sound);
    }
}

void CityCon::drain_audio(std::vector<int16_t>& out) {
    out.insert(out.end(), audio_.begin(), audio_.end());
    audio_.clear();
}

}  // namespace dsp
