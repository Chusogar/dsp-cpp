#include "drivers/commando.h"

#include "core/rom_loader.h"

#include <algorithm>
#include <cstring>

namespace dsp {
namespace {

uint8_t bitswap8(uint8_t v, int b7, int b6, int b5, int b4, int b3, int b2, int b1, int b0) {
    auto bit = [&](int b) { return uint8_t((v >> b) & 1); };
    return uint8_t((bit(b7) << 7) | (bit(b6) << 6) | (bit(b5) << 5) | (bit(b4) << 4) |
                   (bit(b3) << 3) | (bit(b2) << 2) | (bit(b1) << 1) | bit(b0));
}

uint8_t pal4bit(uint8_t n) {
    n &= 0x0f;
    return uint8_t(n | (n << 4));
}

bool load_raw(RomLoader& loader, const std::vector<RomEntry>& entries, std::vector<uint8_t>& dest,
              std::string* error) {
    return loader.load(entries, dest, error);
}

// Chars 8x8 2bpp — gfx_set_desc_data(2,0,16*8,4,0); convert_gfx(...,false,true)
GfxLayout char_layout() {
    GfxLayout layout;
    layout.width = 8;
    layout.height = 8;
    layout.total = 1024;
    layout.planes = 2;
    layout.char_increment = 16 * 8;
    layout.plane_offsets = {4, 0};
    layout.x_offsets = {0, 1, 2, 3, 8 + 0, 8 + 1, 8 + 2, 8 + 3};
    layout.y_offsets = {0 * 16, 1 * 16, 2 * 16, 3 * 16, 4 * 16, 5 * 16, 6 * 16, 7 * 16};
    layout.rotate_cw = false;
    // Pascal convert_gfx last true ≈ vertical flip of each element (Y inverted)
    // Handled by tilemap addressing (y = 31 - ...).
    return layout;
}

// Sprites 16x16 4bpp
GfxLayout sprite_layout() {
    GfxLayout layout;
    layout.width = 16;
    layout.height = 16;
    layout.total = 768;
    layout.planes = 4;
    layout.char_increment = 64 * 8;
    layout.plane_offsets = {0xc000 * 8 + 4, 0xc000 * 8 + 0, 4, 0};
    layout.x_offsets = {0, 1, 2, 3, 8 + 0, 8 + 1, 8 + 2, 8 + 3,
                        32 * 8 + 0, 32 * 8 + 1, 32 * 8 + 2, 32 * 8 + 3,
                        33 * 8 + 0, 33 * 8 + 1, 33 * 8 + 2, 33 * 8 + 3};
    layout.y_offsets = {0 * 16, 1 * 16, 2 * 16,  3 * 16,  4 * 16,  5 * 16,  6 * 16,  7 * 16,
                        8 * 16, 9 * 16, 10 * 16, 11 * 16, 12 * 16, 13 * 16, 14 * 16, 15 * 16};
    return layout;
}

// BG tiles 16x16 3bpp
GfxLayout tile_layout() {
    GfxLayout layout;
    layout.width = 16;
    layout.height = 16;
    layout.total = 1024;
    layout.planes = 3;
    layout.char_increment = 32 * 8;
    layout.plane_offsets = {0, 0x8000 * 8, 0x8000 * 8 * 2};
    layout.x_offsets = {0, 1, 2, 3, 4, 5, 6, 7,
                        16 * 8 + 0, 16 * 8 + 1, 16 * 8 + 2, 16 * 8 + 3,
                        16 * 8 + 4, 16 * 8 + 5, 16 * 8 + 6, 16 * 8 + 7};
    layout.y_offsets = {0 * 8, 1 * 8, 2 * 8,  3 * 8,  4 * 8,  5 * 8,  6 * 8,  7 * 8,
                        8 * 8, 9 * 8, 10 * 8, 11 * 8, 12 * 8, 13 * 8, 14 * 8, 15 * 8};
    return layout;
}

void blit_tile(std::vector<uint32_t>& dest, int dest_w, int dest_h, int dx, int dy,
               const uint8_t* pixels, int pw, int ph, const uint32_t* palette, int color_base,
               bool flipx, bool flipy, int transparent = -1) {
    for (int y = 0; y < ph; y++) {
        const int sy = dy + y;
        if (sy < 0 || sy >= dest_h) continue;
        const int src_y = flipy ? (ph - 1 - y) : y;
        for (int x = 0; x < pw; x++) {
            const int sx = dx + x;
            if (sx < 0 || sx >= dest_w) continue;
            const int src_x = flipx ? (pw - 1 - x) : x;
            const uint8_t pix = pixels[src_y * pw + src_x];
            if (transparent >= 0 && int(pix) == transparent) continue;
            dest[size_t(sy * dest_w + sx)] = palette[size_t(color_base + pix)];
        }
    }
}

}  // namespace

Commando::Commando()
    : main_cpu_(kMainClock),
      sound_cpu_(kSoundClock),
      ym0_(kYmClock),
      ym1_(kYmClock),
      layer_bg_(512 * 512, 0xff000000u),
      layer_fg_(256 * 256, 0),
      composite_(256 * 256, 0xff000000u),
      framebuffer_(size_t(kScreenWidth * kScreenHeight), 0xff000000u) {
    main_cpu_.set_memory_handlers(
        [this](uint16_t a) { return main_read(a); },
        [this](uint16_t a, uint8_t v) { main_write(a, v); });
    main_cpu_.set_opcode_read([this](uint16_t a) { return main_opcode(a); });

    sound_cpu_.set_memory_handlers(
        [this](uint16_t a) { return sound_read(a); },
        [this](uint16_t a, uint8_t v) { sound_write(a, v); });
    sound_cpu_.set_cycle_handler([this](int c) { on_sound_cycles(c); });
}

bool Commando::load_roms(const std::string& rom_path, std::string* error) {
    RomLoader loader;
    if (!loader.open(rom_path, error)) return false;

    std::vector<uint8_t> rom(0x10000, 0);
    // Prefer MAME parent names; also accept cm04.9m style
    if (!load_raw(loader,
                  {{"cm04.9m", 0x8000, 0, 0x8438b694},
                   {"cm03.8m", 0x4000, 0x8000, 0x35486542}},
                  rom, error)) {
        // try alternate names
        if (error) error->clear();
        if (!load_raw(loader,
                      {{"command.003", 0x8000, 0, 0x8438b694},
                       {"command.004", 0x4000, 0x8000, 0x35486542}},
                      rom, error))
            return false;
    }
    std::copy(rom.begin(), rom.end(), memory_.begin());

    // Opcode decrypt: bitswap8(mem, 3,2,1,4,7,6,5,0); byte0 unchanged
    opcodes_[0] = memory_[0];
    for (int f = 1; f < 0xc000; f++)
        opcodes_[size_t(f)] = bitswap8(memory_[size_t(f)], 3, 2, 1, 4, 7, 6, 5, 0);

    std::vector<uint8_t> snd(0x4000, 0);
    if (!load_raw(loader, {{"cm02.9f", 0x4000, 0, 0xf9cc4a74}}, snd, error)) {
        if (error) error->clear();
        if (!load_raw(loader, {{"command.002", 0x4000, 0, 0xf9cc4a74}}, snd, error))
            return false;
    }
    std::copy(snd.begin(), snd.end(), mem_snd_.begin());

    gfx_char_.assign(0x4000, 0);
    if (!load_raw(loader, {{"vt01.5d", 0x4000, 0, 0x505726e0}}, gfx_char_, error)) {
        if (error) error->clear();
        if (!load_raw(loader, {{"vt_01.rom", 0x4000, 0, 0x505726e0}}, gfx_char_, error))
            return false;
    }

    gfx_spr_.assign(0x18000, 0);
    if (!load_raw(loader,
                  {{"vt05.7e", 0x4000, 0, 0x79f16e3d},
                   {"vt06.8e", 0x4000, 0x4000, 0x26fee521},
                   {"vt07.9e", 0x4000, 0x8000, 0xca88bdfd},
                   {"vt08.7h", 0x4000, 0xc000, 0x2019c883},
                   {"vt09.8h", 0x4000, 0x10000, 0x98703982},
                   {"vt10.9h", 0x4000, 0x14000, 0xf069d2f8}},
                  gfx_spr_, error))
        return false;

    gfx_tile_.assign(0x18000, 0);
    if (!load_raw(loader,
                  {{"vt11.5a", 0x4000, 0, 0x7b2e1b48},
                   {"vt12.6a", 0x4000, 0x4000, 0x81b417d3},
                   {"vt13.7a", 0x4000, 0x8000, 0x5612dbd2},
                   {"vt14.8a", 0x4000, 0xc000, 0x2b2dee36},
                   {"vt15.9a", 0x4000, 0x10000, 0xde70babf},
                   {"vt16.10a", 0x4000, 0x14000, 0x14178237}},
                  gfx_tile_, error))
        return false;

    std::vector<uint8_t> prom(0x300, 0);
    if (!load_raw(loader,
                  {{"vtb1.1d", 0x100, 0, 0x3aba15a1},
                   {"vtb2.2d", 0x100, 0x100, 0x88865754},
                   {"vtb3.3d", 0x100, 0x200, 0x4c14c3f6}},
                  prom, error))
        return false;

    warnings_ = loader.warnings();
    decode_graphics();
    build_palette(prom);
    return true;
}

void Commando::decode_graphics() {
    chars_.decode(char_layout(), gfx_char_);
    sprites_.decode(sprite_layout(), gfx_spr_);
    tiles_.decode(tile_layout(), gfx_tile_);
}

void Commando::build_palette(const std::vector<uint8_t>& prom) {
    for (int f = 0; f < 256; f++) {
        const uint8_t r = pal4bit(prom[size_t(f)]);
        const uint8_t g = pal4bit(prom[size_t(f + 0x100)]);
        const uint8_t b = pal4bit(prom[size_t(f + 0x200)]);
        palette_[size_t(f)] =
            0xff000000u | (uint32_t(r) << 16) | (uint32_t(g) << 8) | uint32_t(b);
    }
}

bool Commando::init(const std::string& rom_path, std::string* error) {
    if (!load_roms(rom_path, error)) return false;
    reset();
    return true;
}

void Commando::reset() {
    main_cpu_.reset();
    sound_cpu_.reset();
    ym0_.reset();
    ym1_.reset();
    scroll_x_ = 0;
    scroll_y_ = 0;
    sound_command_ = 0;
    sound_reset_ = false;
    flip_screen_ = false;
    in0_ = in1_ = in2_ = 0xff;
    bg_dirty_.fill(true);
    fg_dirty_.fill(true);
    sprite_buffer_.fill(0);
    audio_accum_ = 0;
    sound_irq_counter_ = 0;
    audio_.clear();
    main_cpu_.set_irq(IrqLine::Clear, 0xd7);
    sound_cpu_.set_irq(IrqLine::Clear);
}

void Commando::set_inputs(const MachineInputs& inputs) {
    in0_ = 0xff;
    in1_ = 0xff;
    in2_ = 0xff;
    auto c0 = [&](uint8_t m) { in0_ = uint8_t(in0_ & ~m); };
    auto c1 = [&](uint8_t m) { in1_ = uint8_t(in1_ & ~m); };
    auto c2 = [&](uint8_t m) { in2_ = uint8_t(in2_ & ~m); };

    if (inputs.player1.start) c0(0x01);
    if (inputs.player2.start) c0(0x02);
    if (inputs.coin2) c0(0x40);
    if (inputs.coin1) c0(0x80);

    if (inputs.player1.right) c1(0x01);
    if (inputs.player1.left) c1(0x02);
    if (inputs.player1.down) c1(0x04);
    if (inputs.player1.up) c1(0x08);
    if (inputs.player1.button1) c1(0x10);
    if (inputs.player1.button2) c1(0x20);

    if (inputs.player2.right) c2(0x01);
    if (inputs.player2.left) c2(0x02);
    if (inputs.player2.down) c2(0x04);
    if (inputs.player2.up) c2(0x08);
    if (inputs.player2.button1) c2(0x10);
    if (inputs.player2.button2) c2(0x20);
}

void Commando::set_dip_switch(int bank, uint8_t value) {
    if (bank == 0) dsw_a_ = value;
    else if (bank == 1) dsw_b_ = value;
}

uint8_t Commando::main_opcode(uint16_t address) {
    if (address <= 0xbfff) return opcodes_[address];
    return main_read(address);
}

uint8_t Commando::main_read(uint16_t address) {
    if (address <= 0xbfff) return memory_[address];
    switch (address) {
        case 0xc000: return in0_;
        case 0xc001: return in1_;
        case 0xc002: return in2_;
        case 0xc003: return dsw_a_;
        case 0xc004: return dsw_b_;
        default: break;
    }
    if (address >= 0xd000) return memory_[address];
    return 0xff;
}

void Commando::main_write(uint16_t address, uint8_t value) {
    if (address <= 0xbfff) return;  // ROM
    if (address == 0xc800) {
        sound_command_ = value;
        return;
    }
    if (address == 0xc804) {
        sound_reset_ = (value & 0x10) != 0;
        flip_screen_ = (value & 0x80) != 0;
        return;
    }
    if (address == 0xc808) {
        scroll_y_ = uint16_t((scroll_y_ & 0x100) | value);
        return;
    }
    if (address == 0xc809) {
        scroll_y_ = uint16_t((scroll_y_ & 0xff) | ((value & 1) << 8));
        return;
    }
    if (address == 0xc80a) {
        scroll_x_ = uint16_t((scroll_x_ & 0x100) | value);
        return;
    }
    if (address == 0xc80b) {
        scroll_x_ = uint16_t((scroll_x_ & 0xff) | ((value & 1) << 8));
        return;
    }
    if (address >= 0xd000 && address <= 0xd7ff) {
        if (memory_[address] != value) {
            memory_[address] = value;
            fg_dirty_[address & 0x3ff] = true;
        }
        return;
    }
    if (address >= 0xd800 && address <= 0xdfff) {
        if (memory_[address] != value) {
            memory_[address] = value;
            bg_dirty_[address & 0x3ff] = true;
        }
        return;
    }
    if (address >= 0xe000) {
        memory_[address] = value;
        return;
    }
}

uint8_t Commando::sound_read(uint16_t address) {
    if (address <= 0x47ff) return mem_snd_[address];
    if (address == 0x6000) return sound_command_;
    return 0xff;
}

void Commando::sound_write(uint16_t address, uint8_t value) {
    if (address <= 0x47ff) {
        if (address >= 0x4000) mem_snd_[address] = value;
        return;
    }
    switch (address) {
        case 0x8000: ym0_.control(value); break;
        case 0x8001: ym0_.write(value); break;
        case 0x8002: ym1_.control(value); break;
        case 0x8003: ym1_.write(value); break;
        default: break;
    }
}

void Commando::on_sound_cycles(int cycles) {
    // IRQ 4 times per frame: 3000000/(4*60)
    sound_irq_counter_ += cycles;
    const int period = int(kSoundClock / (4 * 60));
    while (sound_irq_counter_ >= period) {
        sound_irq_counter_ -= period;
        if (!sound_reset_) sound_cpu_.set_irq(IrqLine::Hold);
    }

    audio_accum_ += int64_t(cycles) * YM2203::kSampleRate;
    while (audio_accum_ >= int64_t(kSoundClock)) {
        audio_accum_ -= int64_t(kSoundClock);
        int32_t sample = ym0_.update() + ym1_.update();
        sample = std::clamp(sample, int32_t(-32768), int32_t(32767));
        audio_.push_back(int16_t(sample));
    }
}

void Commando::draw_bg_tile(int /*offset*/) {
    // Full rebuild uses SCAN_COLS: index = tx * 32 + ty (MAME)
}

void Commando::draw_fg_char(int /*offset*/) {
}

void Commando::draw_sprites() {
    for (int offs = 0x200 - 4; offs >= 0; offs -= 4) {
        const uint8_t attr = sprite_buffer_[size_t(offs + 1)];
        const int bank = (attr & 0xc0) >> 6;
        if (bank >= 3) continue;
        const int nchar = int(sprite_buffer_[size_t(offs)]) + 256 * bank;
        const int color = 128 + (((attr & 0x30) >> 4) * 16);
        bool flipx = (attr & 0x04) != 0;
        bool flipy = (attr & 0x08) != 0;
        int sx = int(sprite_buffer_[size_t(offs + 3)]) - ((attr & 0x01) << 8);
        int sy = int(sprite_buffer_[size_t(offs + 2)]);
        if (flip_screen_) {
            sx = 240 - sx;
            sy = 240 - sy;
            flipx = !flipx;
            flipy = !flipy;
        }
        const uint8_t* pixels = sprites_.element(nchar);
        for (int y = 0; y < 16; y++) {
            const int dy = sy + y;
            if (dy < 0 || dy >= 256) continue;
            const int src_y = flipy ? (15 - y) : y;
            for (int x = 0; x < 16; x++) {
                const int dx = sx + x;
                if (dx < 0 || dx >= 256) continue;
                const int src_x = flipx ? (15 - x) : x;
                const uint8_t pix = pixels[src_y * 16 + src_x];
                if (pix == 15) continue;
                composite_[size_t(dy * 256 + dx)] = palette_[size_t(color + pix)];
            }
        }
    }
}

void Commando::update_video() {
    // Background 32x32 tiles of 16x16 — TILEMAP_SCAN_COLS
    for (int tx = 0; tx < 32; tx++) {
        for (int ty = 0; ty < 32; ty++) {
            const int index = tx * 32 + ty;
            const uint8_t attr = memory_[0xdc00 + index];
            const int nchar = int(memory_[0xd800 + index]) + ((attr & 0xc0) << 2);
            const int color = (attr & 0x0f) << 3;
            const bool flipx = (attr & 0x20) != 0;
            const bool flipy = (attr & 0x10) != 0;
            const uint8_t* pixels = tiles_.element(nchar);
            const int dx0 = tx * 16;
            const int dy0 = ty * 16;
            for (int y = 0; y < 16; y++) {
                const int src_y = flipy ? (15 - y) : y;
                for (int x = 0; x < 16; x++) {
                    const int src_x = flipx ? (15 - x) : x;
                    const uint8_t pix = pixels[src_y * 16 + src_x];
                    layer_bg_[size_t((dy0 + y) * 512 + (dx0 + x))] =
                        palette_[size_t(color + pix)];
                }
            }
        }
    }

    // Foreground 32x32 chars of 8x8 — TILEMAP_SCAN_ROWS
    layer_fg_.assign(256 * 256, 0);
    for (int ty = 0; ty < 32; ty++) {
        for (int tx = 0; tx < 32; tx++) {
            const int index = ty * 32 + tx;
            const uint8_t attr = memory_[0xd400 + index];
            const int nchar = int(memory_[0xd000 + index]) + ((attr & 0xc0) << 2);
            const int color = 192 + ((attr & 0x0f) << 2);
            const bool flipx = (attr & 0x20) != 0;
            const bool flipy = (attr & 0x10) != 0;
            const uint8_t* pixels = chars_.element(nchar);
            const int dx0 = tx * 8;
            const int dy0 = ty * 8;
            for (int y = 0; y < 8; y++) {
                const int src_y = flipy ? (7 - y) : y;
                for (int x = 0; x < 8; x++) {
                    const int src_x = flipx ? (7 - x) : x;
                    const uint8_t pix = pixels[src_y * 8 + src_x];
                    if (pix == 3) continue;
                    layer_fg_[size_t((dy0 + y) * 256 + (dx0 + x))] =
                        palette_[size_t(color + pix)];
                }
            }
        }
    }

    // Compose 256x256: BG scrolled + sprites + FG
    // MAME scroll: set_scrollx(0, scroll_x), set_scrolly(0, scroll_y)
    const int sx = int(scroll_x_);
    const int sy = int(scroll_y_);
    composite_.assign(256 * 256, 0xff000000u);
    for (int y = 0; y < 256; y++) {
        for (int x = 0; x < 256; x++) {
            const int src_x = (x + sx) & 0x1ff;
            const int src_y = (y + sy) & 0x1ff;
            composite_[size_t(y * 256 + x)] = layer_bg_[size_t(src_y * 512 + src_x)];
        }
    }

    draw_sprites();

    for (int y = 0; y < 256; y++) {
        for (int x = 0; x < 256; x++) {
            const uint32_t p = layer_fg_[size_t(y * 256 + x)];
            if (p != 0) composite_[size_t(y * 256 + x)] = p;
        }
    }

    // Visible area: 256x224 starting at y=16, then ROT270 → 224x256
    // ROT270 CW: (x,y) in 256x224 → (y, 255-x) in 224x256
    // Source window: x=0..255, y=16..239
    for (int y = 0; y < 224; y++) {
        for (int x = 0; x < 256; x++) {
            const uint32_t pix = composite_[size_t((y + 16) * 256 + x)];
            const int dx = y;            // 0..223
            const int dy = 255 - x;      // 0..255
            framebuffer_[size_t(dy * kScreenWidth + dx)] = pix;
        }
    }

    std::memcpy(sprite_buffer_.data(), &memory_[0xfe00], 0x200);
}

void Commando::run_frame() {
    const int cycles_main =
        int(double(kMainClock) / (kFramesPerSecond * kScanlines) + 0.5);
    const int cycles_sound =
        int(double(kSoundClock) / (kFramesPerSecond * kScanlines) + 0.5);

    for (int line = 0; line < kScanlines; line++) {
        if (line == 240) {
            // HOLD_LINE with vector $d7 (RST 10h style IM0)
            main_cpu_.set_irq(IrqLine::Hold, 0xd7);
            update_video();
        }
        main_cpu_.run(cycles_main);
        if (!sound_reset_) sound_cpu_.run(cycles_sound);
    }
}

void Commando::drain_audio(std::vector<int16_t>& out) {
    out.insert(out.end(), audio_.begin(), audio_.end());
    audio_.clear();
}

}  // namespace dsp
