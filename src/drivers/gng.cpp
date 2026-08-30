#include "drivers/gng.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "core/rom_loader.h"

namespace dsp {
namespace {

inline uint8_t pal4bit(uint8_t n) {
    n &= 0x0f;
    return uint8_t((n << 4) | n);
}

// Sprite / char X offsets (16 wide; chars use first 8)
const int kPsX[16] = {0, 1, 2, 3, 8 + 0, 8 + 1, 8 + 2, 8 + 3,
                      32 * 8 + 0, 32 * 8 + 1, 32 * 8 + 2, 32 * 8 + 3,
                      33 * 8 + 0, 33 * 8 + 1, 33 * 8 + 2, 33 * 8 + 3};
const int kPsY[16] = {0 * 16,  1 * 16,  2 * 16,  3 * 16,  4 * 16,  5 * 16,  6 * 16,  7 * 16,
                      8 * 16,  9 * 16, 10 * 16, 11 * 16, 12 * 16, 13 * 16, 14 * 16, 15 * 16};

// BG tile X/Y offsets
const int kPtX[16] = {0, 1, 2, 3, 4, 5, 6, 7,
                      16 * 8 + 0, 16 * 8 + 1, 16 * 8 + 2, 16 * 8 + 3,
                      16 * 8 + 4, 16 * 8 + 5, 16 * 8 + 6, 16 * 8 + 7};
const int kPtY[16] = {0 * 8, 1 * 8, 2 * 8, 3 * 8, 4 * 8, 5 * 8, 6 * 8, 7 * 8,
                      8 * 8, 9 * 8, 10 * 8, 11 * 8, 12 * 8, 13 * 8, 14 * 8, 15 * 8};

}  // namespace

Gng::Gng()
    : layer_bg_(512u * 512u, 0),
      layer_fg_(512u * 512u, 0),
      layer_char_(256u * 256u, 0),
      composite_(256u * 256u, 0),
      framebuffer_(size_t(kScreenWidth) * size_t(kScreenHeight), 0xff000000u) {
    main_cpu_.set_memory_handlers([this](uint16_t a) { return main_read(a); },
                                  [this](uint16_t a, uint8_t v) { main_write(a, v); });
    sound_cpu_.set_memory_handlers([this](uint16_t a) { return sound_read(a); },
                                   [this](uint16_t a, uint8_t v) { sound_write(a, v); });
    sound_cpu_.set_cycle_handler([this](int c) { on_sound_cycles(c); });
}

bool Gng::init(const std::string& rom_path, std::string* error) {
    if (!load_roms(rom_path, error)) return false;
    decode_graphics();
    reset();
    return true;
}

void Gng::reset() {
    main_cpu_.reset();
    sound_cpu_.reset();
    ym0_.reset();
    ym1_.reset();
    bank_ = 0;
    soundlatch_ = 0;
    scroll_x_ = 0;
    scroll_y_ = 0;
    flip_screen_ = false;
    frame_main_ = 0;
    frame_snd_ = 0;
    sound_irq_accum_ = 0;
    dsw_a_ = 0xdf;  // Pascal init_dips default
    dsw_b_ = 0x7b;
    bg_dirty_.fill(true);
    fg_dirty_.fill(true);
    color_dirty_.fill(true);
    sprite_buffer_.fill(0);
    // Clear work RAM + VRAM; keep ROM regions (0x6000+)
    std::memset(memory_.data(), 0, 0x6000);
    std::fill(layer_bg_.begin(), layer_bg_.end(), 0u);
    std::fill(layer_fg_.begin(), layer_fg_.end(), 0u);
    std::fill(layer_char_.begin(), layer_char_.end(), 0u);
    palette_.fill(0xff000000u);
    palette_ram_.fill(0);
}

void Gng::set_inputs(const MachineInputs& inputs) {
    auto bit = [](bool pressed, uint8_t& v, uint8_t mask) {
        if (pressed) v = uint8_t(v & ~mask);
        else v = uint8_t(v | mask);
    };
    in0_ = in1_ = in2_ = 0xff;
    const auto& p1 = inputs.player1;
    const auto& p2 = inputs.player2;
    bit(p1.right, in1_, 0x01);
    bit(p1.left, in1_, 0x02);
    bit(p1.down, in1_, 0x04);
    bit(p1.up, in1_, 0x08);
    bit(p1.button1, in1_, 0x10);
    bit(p1.button2, in1_, 0x20);
    bit(p2.right, in2_, 0x01);
    bit(p2.left, in2_, 0x02);
    bit(p2.down, in2_, 0x04);
    bit(p2.up, in2_, 0x08);
    bit(p2.button1, in2_, 0x10);
    bit(p2.button2, in2_, 0x20);
    bit(p1.start, in0_, 0x01);
    bit(p2.start, in0_, 0x02);
    bit(inputs.coin1, in0_, 0x40);
    bit(inputs.coin2, in0_, 0x80);
}

void Gng::set_dip_switch(int bank, uint8_t value) {
    if (bank == 0) dsw_a_ = value;
    if (bank == 1) dsw_b_ = value;
}

void Gng::on_sound_cycles(int cycles) {
    if (cycles <= 0) return;
    const int period = int(double(kSoundClock) / (4.0 * kFramesPerSecond) + 0.5);
    sound_irq_accum_ += cycles;
    while (sound_irq_accum_ >= period) {
        sound_irq_accum_ -= period;
        sound_cpu_.set_irq(IrqLine::Hold);
    }
}

void Gng::run_frame() {
    const double main_c = double(kMainClock) / kFramesPerSecond / double(kScanlines);
    const double snd_c = double(kSoundClock) / kFramesPerSecond / double(kScanlines);

    for (int line = 0; line < kScanlines; line++) {
        if (line == 246) {
            update_video();
            main_cpu_.set_irq(IrqLine::Hold);
        }
        main_cpu_.run(int(main_c + frame_main_));
        {
            static int once; static int once2;
            uint16_t pc = main_cpu_.pc();
            static bool seen6160 = false;
            if (pc == 0x6160 && !seen6160) {
                seen6160 = true;
                //fprintf(stderr, "ENTER 6160 X=%04x A=%02x Y=%04x\n", main_cpu_.x, main_cpu_.a, main_cpu_.y);
            }
            if (once < 5 && pc == 0x617d) {
                //fprintf(stderr, "JSR ,X  X=%04x A=%02x bank=%d\n", main_cpu_.x, main_cpu_.a, int(bank_));
                once++;
            }
            if (once2 < 3 && pc >= 0x6181 && pc <= 0x6186) {
                //fprintf(stderr, "spin PC=%04x X=%04x mem=%02x%02x\n",
                //        pc, main_cpu_.x, memory_[main_cpu_.x], memory_[main_cpu_.x+1]);
                once2++;
            }
        }
        frame_main_ = (main_c + frame_main_) - int(main_c + frame_main_);
        sound_cpu_.run(int(snd_c + frame_snd_));
        frame_snd_ = (snd_c + frame_snd_) - int(snd_c + frame_snd_);
    }

    static int frame_n = 0;
    frame_n++;
    if (frame_n <= 5 || frame_n % 60 == 0) {
        int vram_nz = 0;
        for (int i = 0x2000; i < 0x3000; i++) if (memory_[size_t(i)]) vram_nz++;
        int16_t peak = 0;
        for (int16_t s : audio_buffer_) {
            int16_t a = s < 0 ? int16_t(-s) : s;
            if (a > peak) peak = a;
        }
        //fprintf(stderr,
        //        "f%4d mainPC=%04x X=%04x A=%02x I=%d sndPC=%04x bank=%d latch=%02x vram=%d peak=%d\n",
        //        frame_n, main_cpu_.pc(), main_cpu_.x, main_cpu_.a, int(main_cpu_.cc.i),
        //        sound_cpu_.pc(), int(bank_), int(soundlatch_),
        //        vram_nz, int(peak));
    }

    const int samples = int(double(YM2203::kSampleRate) / kFramesPerSecond + 0.5);
    audio_buffer_.resize(size_t(samples));
    for (int i = 0; i < samples; i++) {
        int32_t s = ym0_.update() + ym1_.update();
        s *= 8;
        audio_buffer_[size_t(i)] = int16_t(std::clamp(s, -32768, 32767));
    }
}

void Gng::drain_audio(std::vector<int16_t>& out) {
    out.insert(out.end(), audio_buffer_.begin(), audio_buffer_.end());
    audio_buffer_.clear();
}

void Gng::set_color(int index) {
    if (index < 0 || index >= 0x100) return;
    const uint8_t lo = palette_ram_[size_t(index)];
    const uint8_t hi = palette_ram_[size_t(0x100 + index)];
    const uint8_t r = pal4bit(uint8_t(lo >> 4));
    const uint8_t g = pal4bit(lo);
    const uint8_t b = pal4bit(uint8_t(hi >> 4));
    palette_[size_t(index)] = 0xff000000u | (uint32_t(r) << 16) | (uint32_t(g) << 8) | b;
    if (index < 0x40) color_dirty_[size_t(0x10 + (index >> 3))] = true;
    if (index >= 0x80) color_dirty_[size_t((index >> 2) & 0x0f)] = true;
}

uint8_t Gng::main_read(uint16_t address) {
    if (address <= 0x2fff || address >= 0x6000) return memory_[address];
    switch (address) {
        case 0x3000: return in0_;
        case 0x3001: return in1_;
        case 0x3002: return in2_;
        case 0x3003: return dsw_a_;
        case 0x3004: return dsw_b_;
        default: break;
    }
    if (address >= 0x3800 && address <= 0x39ff) return palette_ram_[address & 0x1ff];
    if (address >= 0x4000 && address <= 0x5fff)
        return bank_rom_[bank_ % 5][address & 0x1fff];
    return 0;
}

void Gng::main_write(uint16_t address, uint8_t value) {
    if (address <= 0x1fff) {
        memory_[address] = value;
        return;
    }
    if (address >= 0x2000 && address <= 0x27ff) {
        if (memory_[address] != value) {
            memory_[address] = value;
            fg_dirty_[address & 0x3ff] = true;
        }
        return;
    }
    if (address >= 0x2800 && address <= 0x2fff) {
        if (memory_[address] != value) {
            memory_[address] = value;
            bg_dirty_[address & 0x3ff] = true;
        }
        return;
    }
    if (address >= 0x3800 && address <= 0x39ff) {
        if (palette_ram_[address & 0x1ff] != value) {
            palette_ram_[address & 0x1ff] = value;
            set_color(int(address & 0xff));
        }
        return;
    }
    if (address == 0x3a00) {
        static int lw;
        if (lw < 20) {
            //fprintf(stderr, "soundlatch write %02x (mainPC after)\n", value);
            lw++;
        }
        soundlatch_ = value;
        return;
    }
    if (address == 0x3b08) {
        scroll_x_ = uint16_t((scroll_x_ & 0x100) | value);
        return;
    }
    if (address == 0x3b09) {
        scroll_x_ = uint16_t((scroll_x_ & 0xff) | ((value & 1) << 8));
        return;
    }
    if (address == 0x3b0a) {
        scroll_y_ = uint16_t((scroll_y_ & 0x100) | value);
        return;
    }
    if (address == 0x3b0b) {
        scroll_y_ = uint16_t((scroll_y_ & 0xff) | ((value & 1) << 8));
        return;
    }
    if (address == 0x3d00) {
        flip_screen_ = (value & 1) == 0;
        return;
    }
    if (address == 0x3e00) {
        bank_ = value % 5;
        return;
    }
}

uint8_t Gng::sound_read(uint16_t address) {
    if (address <= 0x7fff || (address >= 0xc000 && address <= 0xc7ff))
        return mem_snd_[address];
    if (address == 0xc800) return soundlatch_;
    return 0;
}

void Gng::sound_write(uint16_t address, uint8_t value) {
    if (address <= 0x7fff) return;
    if (address >= 0xc000 && address <= 0xc7ff) {
        mem_snd_[address] = value;
        return;
    }
    if (address == 0xe000) {
        ym0_.control(value);
        return;
    }
    if (address == 0xe001) {
        ym0_.write(value);
        return;
    }
    if (address == 0xe002) {
        ym1_.control(value);
        return;
    }
    if (address == 0xe003) {
        ym1_.write(value);
        return;
    }
}

bool Gng::load_roms(const std::string& rom_path, std::string* error) {
    RomLoader loader;
    if (!loader.open(rom_path, error)) return false;

    static const std::vector<RomEntry> kMain = {
        {"gg3.bin", 0x8000, 0x8000, 0x9e01c65e},
        {"gg4.bin", 0x4000, 0x4000, 0x66606beb},
        {"gg5.bin", 0x8000, 0x10000, 0xd6397b2b},
    };
    std::vector<uint8_t> main_tmp(0x18000, 0);
    if (!loader.load(kMain, main_tmp, error)) return false;

    std::memcpy(memory_.data() + 0x8000, main_tmp.data() + 0x8000, 0x8000);
    for (int f = 0; f < 4; f++)
        std::memcpy(bank_rom_[size_t(f)].data(), main_tmp.data() + 0x10000 + f * 0x2000, 0x2000);
    std::memcpy(memory_.data() + 0x6000, main_tmp.data() + 0x6000, 0x2000);
    std::memcpy(bank_rom_[4].data(), main_tmp.data() + 0x4000, 0x2000);

    static const std::vector<RomEntry> kSound = {{"gg2.bin", 0x8000, 0, 0x615f5b6f}};
    std::vector<uint8_t> snd(0x8000, 0);
    if (!loader.load(kSound, snd, error)) return false;
    std::memcpy(mem_snd_.data(), snd.data(), 0x8000);

    // Stash gfx ROMs in temporary members via decode
    static const std::vector<RomEntry> kChar = {{"gg1.bin", 0x4000, 0, 0xecfccf07}};
    static const std::vector<RomEntry> kTiles = {
        {"gg11.bin", 0x4000, 0, 0xddd56fa9},
        {"gg10.bin", 0x4000, 0x4000, 0x7302529d},
        {"gg9.bin", 0x4000, 0x8000, 0x20035bda},
        {"gg8.bin", 0x4000, 0xc000, 0xf12ba271},
        {"gg7.bin", 0x4000, 0x10000, 0xe525207d},
        {"gg6.bin", 0x4000, 0x14000, 0x2d77e9b2},
    };
    static const std::vector<RomEntry> kSprites = {
        {"gg17.bin", 0x4000, 0, 0x93e50a8f},
        {"gg16.bin", 0x4000, 0x4000, 0x06d7e5ca},
        {"gg15.bin", 0x4000, 0x8000, 0xbc1fe02d},
        {"gg14.bin", 0x4000, 0xc000, 0x6aaf12f9},
        {"gg13.bin", 0x4000, 0x10000, 0xe80c3fca},
        {"gg12.bin", 0x4000, 0x14000, 0x7780a925},
    };

    gfx_char_.assign(0x4000, 0);
    if (!loader.load(kChar, gfx_char_, error)) return false;
    gfx_tiles_.assign(0x18000, 0);
    if (!loader.load(kTiles, gfx_tiles_, error)) return false;
    gfx_sprites_.assign(0x18000, 0);
    if (!loader.load(kSprites, gfx_sprites_, error)) return false;
    return true;
}

void Gng::decode_graphics() {
    // Chars 8x8 2bpp, 1024, planes 4,0  (Pascal: gfx_set_desc_data(2,0,16*8,4,0))
    {
        GfxLayout layout;
        layout.width = 8;
        layout.height = 8;
        layout.total = 1024;
        layout.planes = 2;
        layout.char_increment = 16 * 8;
        layout.plane_offsets = {4, 0};
        layout.x_offsets.assign(kPsX, kPsX + 8);
        layout.y_offsets.assign(kPsY, kPsY + 8);
        chars_.decode(layout, gfx_char_);
    }
    // Sprites 16x16 4bpp, 1024
    // planes: $c000*8+4, $c000*8+0, 4, 0
    {
        GfxLayout layout;
        layout.width = 16;
        layout.height = 16;
        layout.total = 1024;
        layout.planes = 4;
        layout.char_increment = 64 * 8;
        layout.plane_offsets = {0xc000 * 8 + 4, 0xc000 * 8 + 0, 4, 0};
        layout.x_offsets.assign(kPsX, kPsX + 16);
        layout.y_offsets.assign(kPsY, kPsY + 16);
        sprites_.decode(layout, gfx_sprites_);
    }
    // Tiles 16x16 3bpp, 1024
    // planes: $10000*8, $8000*8, 0
    {
        GfxLayout layout;
        layout.width = 16;
        layout.height = 16;
        layout.total = 1024;
        layout.planes = 3;
        layout.char_increment = 32 * 8;
        layout.plane_offsets = {0x10000 * 8, 0x8000 * 8, 0};
        layout.x_offsets.assign(kPtX, kPtX + 16);
        layout.y_offsets.assign(kPtY, kPtY + 16);
        tiles_.decode(layout, gfx_tiles_);
    }
}

void Gng::draw_background() {
    for (int f = 0; f < 0x400; f++) {
        const uint8_t atrib = memory_[0x2c00 + f];
        const int color = atrib & 7;
        if (!bg_dirty_[size_t(f)] && !color_dirty_[size_t(0x10 + color)]) continue;
        const int x = (f >> 5) << 4;
        const int y = (f & 0x1f) << 4;
        const int nchar = memory_[0x2800 + f] + ((atrib & 0xc0) << 2);
        const bool flip_x = (atrib & 0x10) != 0;
        const bool flip_y = (atrib & 0x20) != 0;
        const uint8_t* src = tiles_.element(nchar);
        const int color_base = color << 3;
        for (int row = 0; row < 16; row++) {
            for (int col = 0; col < 16; col++) {
                const int sx = flip_x ? (15 - col) : col;
                const int sy = flip_y ? (15 - row) : row;
                const uint8_t pen = src[sy * 16 + sx];
                // Transparent pens 0 and 6 → 0 in FG layer sense; opaque always drawn to BG
                const uint32_t rgb = (pen == 0 || pen == 6) ? 0u : palette_[size_t(color_base + pen)];
                layer_bg_[size_t((y + row) * 512 + (x + col))] = rgb;
                // FG priority layer: only tiles with atrib bit3 set are solid on top
                if ((atrib & 8) == 0)
                    layer_fg_[size_t((y + row) * 512 + (x + col))] = 0;
                else
                    layer_fg_[size_t((y + row) * 512 + (x + col))] =
                        (pen == 0 || pen == 6) ? 0u : palette_[size_t(color_base + pen)];
            }
        }
        bg_dirty_[size_t(f)] = false;
    }
}

void Gng::draw_foreground() {
    // Character layer (text), 8x8, colors 0x80+
    for (int f = 0; f < 0x400; f++) {
        const uint8_t atrib = memory_[0x2400 + f];
        const int color = atrib & 0x0f;
        if (!fg_dirty_[size_t(f)] && !color_dirty_[size_t(color)]) continue;
        const int y = (f >> 5) << 3;
        const int x = (f & 0x1f) << 3;
        const int nchar = memory_[0x2000 + f] + ((atrib & 0xc0) << 2);
        const uint8_t* src = chars_.element(nchar);
        const int color_base = (color << 2) + 0x80;
        for (int row = 0; row < 8; row++) {
            for (int col = 0; col < 8; col++) {
                const uint8_t pen = src[row * 8 + col];
                // Transparent pen 3
                const uint32_t rgb = (pen == 3) ? 0u : palette_[size_t(color_base + pen)];
                layer_char_[size_t((y + row) * 256 + (x + col))] = rgb;
            }
        }
        fg_dirty_[size_t(f)] = false;
    }
}

void Gng::draw_sprites() {
    // Draw into composite_ which already has scrolled BG
    for (int f = 0x7f; f >= 0; f--) {
        const uint8_t atrib = sprite_buffer_[size_t((f << 2) + 1)];
        const int nchar = sprite_buffer_[size_t(f << 2)] + ((atrib << 2) & 0x300);
        // color banks 64/80/96/112 (MAME: (attr>>4)&3 )
        const int color = (atrib & 0x30) + 64;
        // MAME: sx = spriteram[offs+3] - 0x100 * (attributes & 0x01)
        // (Pascal used +, which placed high-bit sprites off the right edge)
        int x = int(sprite_buffer_[size_t(3 + (f << 2))]) - 0x100 * (atrib & 1);
        int y = int(sprite_buffer_[size_t(2 + (f << 2))]);
        bool flip_x = (atrib & 4) != 0;
        bool flip_y = (atrib & 8) != 0;
        if (flip_screen_) {
            x = 240 - x;
            y = 240 - y;
            flip_x = !flip_x;
            flip_y = !flip_y;
        }
        const uint8_t* src = sprites_.element(nchar);
        for (int row = 0; row < 16; row++) {
            for (int col = 0; col < 16; col++) {
                const int sx = flip_x ? (15 - col) : col;
                const int sy = flip_y ? (15 - row) : row;
                const uint8_t pen = src[sy * 16 + sx];
                if (pen == 15) continue;  // transparent
                const int dx = x + col;
                const int dy = y + row;
                if (dx < 0 || dx >= 256 || dy < 0 || dy >= 256) continue;
                composite_[size_t(dy * 256 + dx)] = palette_[size_t(color + pen)];
            }
        }
    }
}

void Gng::update_video() {
    // Refresh dirty tiles when palette groups change
    bool any_color = false;
    for (bool d : color_dirty_)
        if (d) {
            any_color = true;
            break;
        }
    if (any_color) {
        bg_dirty_.fill(true);
        fg_dirty_.fill(true);
    }

    draw_background();
    draw_foreground();

    // Compose: scrolled BG → sprites → scrolled FG priority → chars
    // Transparent pens in layers are stored as 0; only non-zero pixels overwrite.
    std::fill(composite_.begin(), composite_.end(), palette_[0]);

    for (int sy = 0; sy < 256; sy++) {
        const int by = (sy + scroll_y_) & 0x1ff;
        for (int sx = 0; sx < 256; sx++) {
            const int bx = (sx + scroll_x_) & 0x1ff;
            const uint32_t p = layer_bg_[size_t(by * 512 + bx)];
            if (p) composite_[size_t(sy * 256 + sx)] = p;
        }
    }

    draw_sprites();

    // FG priority tiles (over sprites) — bit3 of tile attr
    for (int sy = 0; sy < 256; sy++) {
        const int by = (sy + scroll_y_) & 0x1ff;
        for (int sx = 0; sx < 256; sx++) {
            const int bx = (sx + scroll_x_) & 0x1ff;
            const uint32_t p = layer_fg_[size_t(by * 512 + bx)];
            if (p) composite_[size_t(sy * 256 + sx)] = p;
        }
    }

    // Character layer (no scroll)
    for (int i = 0; i < 256 * 256; i++) {
        if (layer_char_[size_t(i)]) composite_[size_t(i)] = layer_char_[size_t(i)];
    }

    // Crop y=16..239 → 224 lines (Pascal actualiza_trozo_final(0,16,256,224))
    for (int y = 0; y < kScreenHeight; y++) {
        const uint32_t* src = &composite_[size_t((y + 16) * 256)];
        uint32_t* dst = &framebuffer_[size_t(y * kScreenWidth)];
        std::memcpy(dst, src, size_t(kScreenWidth) * sizeof(uint32_t));
    }

    // Buffer sprites for next frame (Pascal copies at end of video update)
    std::memcpy(sprite_buffer_.data(), memory_.data() + 0x1e00, 0x200);
    color_dirty_.fill(false);
}

}  // namespace dsp
