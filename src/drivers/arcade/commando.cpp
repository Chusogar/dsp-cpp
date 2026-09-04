#include "drivers/arcade/commando.h"

#include <algorithm>
#include <cstring>

#include "core/rom_loader.h"

namespace dsp {
namespace {

uint8_t bitswap8(uint8_t v, int b7, int b6, int b5, int b4, int b3, int b2, int b1, int b0) {
    auto bit = [&](int n) { return (v >> n) & 1; };
    return uint8_t((bit(b7) << 7) | (bit(b6) << 6) | (bit(b5) << 5) | (bit(b4) << 4) | (bit(b3) << 3) |
                   (bit(b2) << 2) | (bit(b1) << 1) | bit(b0));
}

uint32_t pal4bit(uint8_t n) {
    n &= 0xf;
    return uint32_t(n) * 255 / 15;
}
uint32_t argb(uint8_t r, uint8_t g, uint8_t b) { return 0xff000000u | (uint32_t(r) << 16) | (uint32_t(g) << 8) | b; }

const std::vector<RomEntry> kMainRom = {
    {"cm04.9m", 0x8000, 0, 0x8438b694},
    {"cm03.8m", 0x4000, 0x8000, 0x35486542},
};
const std::vector<RomEntry> kSoundRom = {{"cm02.9f", 0x4000, 0, 0xf9cc4a74}};
const std::vector<RomEntry> kPalRom = {
    {"vtb1.1d", 0x100, 0, 0x3aba15a1},
    {"vtb2.2d", 0x100, 0x100, 0x88865754},
    {"vtb3.3d", 0x100, 0x200, 0x4c14c3f6},
};
const std::vector<RomEntry> kCharRom = {{"vt01.5d", 0x4000, 0, 0x505726e0}};
const std::vector<RomEntry> kSpriteRom = {
    {"vt05.7e", 0x4000, 0, 0x79f16e3d},     {"vt06.8e", 0x4000, 0x4000, 0x26fee521},
    {"vt07.9e", 0x4000, 0x8000, 0xca88bdfd}, {"vt08.7h", 0x4000, 0xc000, 0x2019c883},
    {"vt09.8h", 0x4000, 0x10000, 0x98703982}, {"vt10.9h", 0x4000, 0x14000, 0xf069d2f8},
};
const std::vector<RomEntry> kTileRom = {
    {"vt11.5a", 0x4000, 0, 0x7b2e1b48},      {"vt12.6a", 0x4000, 0x4000, 0x81b417d3},
    {"vt13.7a", 0x4000, 0x8000, 0x5612dbd2},  {"vt14.8a", 0x4000, 0xc000, 0x2b2dee36},
    {"vt15.9a", 0x4000, 0x10000, 0xde70babf}, {"vt16.10a", 0x4000, 0x14000, 0x14178237},
};

}  // namespace

std::vector<uint8_t> Commando::rotate_ccw(const GfxSet& src, int count, int size) {
    std::vector<uint8_t> out(size_t(count) * size_t(size) * size_t(size));
    for (int n = 0; n < count; n++) {
        const uint8_t* s = src.element(n);
        uint8_t* d = &out[size_t(n) * size_t(size) * size_t(size)];
        for (int row = 0; row < size; row++)
            for (int col = 0; col < size; col++)
                d[row * size + col] = s[col * size + (size - 1 - row)];
    }
    return out;
}

Commando::Commando()
    : main_cpu_(kCpuClock), sound_cpu_(kCpuClock), ym0_(1500000), ym1_(1500000) {
    framebuffer_.assign(size_t(kScreenWidth) * size_t(kScreenHeight), 0xff000000u);
    bg_canvas_.assign(512u * 512u, 0xff000000u);
    char_canvas_.assign(256u * 256u, 0);

    main_cpu_.set_memory_handlers([this](uint16_t a) { return main_read(a); },
                                  [this](uint16_t a, uint8_t v) { main_write(a, v); });
    main_cpu_.set_opcode_read([this](uint16_t a) { return main_read_opcode(a); });

    sound_cpu_.set_memory_handlers([this](uint16_t a) { return sound_read(a); },
                                   [this](uint16_t a, uint8_t v) { sound_write(a, v); });
    sound_cpu_.set_cycle_handler([this](int cycles) { on_sound_cycles(cycles); });

    main_cycles_per_line_ = int(kCpuClock / uint32_t(kScanlines * kFramesPerSecond));
}

bool Commando::init(const std::string& rom_path, std::string* error) {
    RomLoader loader;
    if (!loader.open(rom_path, error)) return false;

    std::vector<uint8_t> main_bytes(0xc000, 0);
    if (!loader.load(kMainRom, main_bytes, error)) return false;
    std::copy(main_bytes.begin(), main_bytes.end(), rom_data_.begin());
    rom_opcode_[0] = rom_data_[0];
    for (size_t f = 1; f < rom_data_.size(); f++)
        rom_opcode_[f] = bitswap8(rom_data_[f], 3, 2, 1, 4, 7, 6, 5, 0);

    std::vector<uint8_t> sound_bytes(0x4000, 0);
    if (!loader.load(kSoundRom, sound_bytes, error)) return false;
    std::copy(sound_bytes.begin(), sound_bytes.end(), sound_ram_.begin());

    // Chars: 8x8, 2bpp, nibble-interleaved planes, 4 pixels/byte.
    std::vector<uint8_t> char_rom;
    if (!loader.load(kCharRom, char_rom, error)) return false;
    {
        GfxLayout layout;
        layout.width = 8;
        layout.height = 8;
        layout.total = 1024;
        layout.planes = 2;
        layout.char_increment = 16 * 8;
        layout.plane_offsets = {4, 0};
        layout.x_offsets = {0, 1, 2, 3, 8, 9, 10, 11};
        layout.y_offsets = {0 * 16, 1 * 16, 2 * 16, 3 * 16, 4 * 16, 5 * 16, 6 * 16, 7 * 16};
        chars_.decode(layout, char_rom);
        chars_rotated_ = rotate_ccw(chars_, layout.total, 8);
    }

    // Sprites: 16x16, 4bpp, two nibble-interleaved plane pairs 0xC000 bytes apart.
    std::vector<uint8_t> spr_rom;
    if (!loader.load(kSpriteRom, spr_rom, error)) return false;
    {
        GfxLayout layout;
        layout.width = 16;
        layout.height = 16;
        layout.total = 768;
        layout.planes = 4;
        layout.char_increment = 64 * 8;
        layout.plane_offsets = {0xc000 * 8 + 4, 0xc000 * 8 + 0, 4, 0};
        layout.x_offsets = {0, 1, 2, 3, 8, 9, 10, 11, 32 * 8, 32 * 8 + 1, 32 * 8 + 2, 32 * 8 + 3,
                             33 * 8, 33 * 8 + 1, 33 * 8 + 2, 33 * 8 + 3};
        layout.y_offsets.resize(16);
        for (int i = 0; i < 16; i++) layout.y_offsets[size_t(i)] = i * 16;
        sprites_.decode(layout, spr_rom);
        sprites_rotated_ = rotate_ccw(sprites_, layout.total, 16);
    }

    // Background tiles: 16x16, 3bpp, standard contiguous bitplanes (no
    // nibble interleave), split into a left/right 8-wide half each.
    std::vector<uint8_t> tile_rom;
    if (!loader.load(kTileRom, tile_rom, error)) return false;
    {
        GfxLayout layout;
        layout.width = 16;
        layout.height = 16;
        layout.total = 1024;
        layout.planes = 3;
        layout.char_increment = 32 * 8;
        layout.plane_offsets = {0, 0x8000 * 8, 0x8000 * 8 * 2};
        layout.x_offsets = {0, 1, 2, 3, 4, 5, 6, 7, 16 * 8, 16 * 8 + 1, 16 * 8 + 2, 16 * 8 + 3,
                             16 * 8 + 4, 16 * 8 + 5, 16 * 8 + 6, 16 * 8 + 7};
        layout.y_offsets.resize(16);
        for (int i = 0; i < 16; i++) layout.y_offsets[size_t(i)] = i * 8;
        tiles_.decode(layout, tile_rom);
        tiles_rotated_ = rotate_ccw(tiles_, layout.total, 16);
    }

    std::vector<uint8_t> prom;
    if (!loader.load(kPalRom, prom, error)) return false;
    for (int i = 0; i < 256; i++)
        palette_[size_t(i)] = argb(uint8_t(pal4bit(prom[size_t(i)])), uint8_t(pal4bit(prom[size_t(i) + 0x100])),
                                    uint8_t(pal4bit(prom[size_t(i) + 0x200])));
    for (int i = 0; i < 64; i++) {
        sprite_lut_[size_t(i)] = uint8_t(i + 128);
        char_lut_[size_t(i)] = uint8_t(i + 192);
    }

    reset();
    return true;
}

void Commando::reset() {
    main_cpu_.reset();
    sound_cpu_.reset();
    ym0_.reset();
    ym1_.reset();
    scroll_x_ = scroll_y_ = 0;
    sound_command_ = 0;
    flip_screen_ = false;
    sound_reset_held_ = true;
    in0_ = in1_ = in2_ = 0xff;
}

// ---------------------------------------------------------------------------
// Main CPU
// ---------------------------------------------------------------------------

uint8_t Commando::main_read(uint16_t addr) {
    if (addr <= 0xbfff) return rom_data_[addr];
    if (addr == 0xc000) return in0_;
    if (addr == 0xc001) return in1_;
    if (addr == 0xc002) return in2_;
    if (addr == 0xc003) return dsw_a_;
    if (addr == 0xc004) return dsw_b_;
    if (addr >= 0xd000 && addr <= 0xd7ff) return char_ram_[addr & 0x7ff];
    if (addr >= 0xd800 && addr <= 0xdfff) return tile_ram_[addr & 0x7ff];
    if (addr >= 0xe000) return ram_[addr & 0x1fff];
    return 0xff;
}

uint8_t Commando::main_read_opcode(uint16_t addr) {
    if (addr <= 0xbfff) return rom_opcode_[addr];
    return main_read(addr);
}

void Commando::main_write(uint16_t addr, uint8_t value) {
    if (addr <= 0xbfff) return;  // ROM
    if (addr == 0xc800) {
        sound_command_ = value;
        return;
    }
    if (addr == 0xc804) {
        const bool hold = (value & 0x10) != 0;
        if (sound_reset_held_ && !hold) sound_cpu_.reset();
        sound_reset_held_ = hold;
        flip_screen_ = (value & 0x80) != 0;
        return;
    }
    if (addr == 0xc808) { scroll_y_ = (scroll_y_ & 0x100) | value; return; }
    if (addr == 0xc809) { scroll_y_ = (scroll_y_ & 0xff) | ((value & 1) << 8); return; }
    if (addr == 0xc80a) { scroll_x_ = (scroll_x_ & 0x100) | value; return; }
    if (addr == 0xc80b) { scroll_x_ = (scroll_x_ & 0xff) | ((value & 1) << 8); return; }
    if (addr >= 0xd000 && addr <= 0xd7ff) { char_ram_[addr & 0x7ff] = value; return; }
    if (addr >= 0xd800 && addr <= 0xdfff) { tile_ram_[addr & 0x7ff] = value; return; }
    if (addr >= 0xe000) { ram_[addr & 0x1fff] = value; return; }
}

// ---------------------------------------------------------------------------
// Sound CPU
// ---------------------------------------------------------------------------

uint8_t Commando::sound_read(uint16_t addr) {
    if (addr <= 0x47ff) return sound_ram_[addr];
    if (addr == 0x6000) return sound_command_;
    return 0xff;
}

void Commando::sound_write(uint16_t addr, uint8_t value) {
    if (addr <= 0x3fff) return;  // ROM
    if (addr <= 0x47ff) { sound_ram_[addr] = value; return; }
    if (addr == 0x8000) { ym0_.control(value); return; }
    if (addr == 0x8001) { ym0_.write(value); return; }
    if (addr == 0x8002) { ym1_.control(value); return; }
    if (addr == 0x8003) { ym1_.write(value); return; }
}

void Commando::on_sound_cycles(int cycles) {
    // 60Hz * 4 periodic IRQ that drives the FM music tempo.
    sound_irq_accumulator_ += cycles;
    const int64_t period = int64_t(kCpuClock) / (4 * 60);
    while (sound_irq_accumulator_ >= period) {
        sound_irq_accumulator_ -= period;
        sound_cpu_.set_irq(IrqLine::Hold);
    }

    audio_accumulator_ += int64_t(cycles) * int64_t(YM2203::kSampleRate);
    while (audio_accumulator_ >= int64_t(kCpuClock)) {
        audio_accumulator_ -= int64_t(kCpuClock);
        const int32_t s = ym0_.update() + ym1_.update();
        audio_.push_back(int16_t(std::max(-32768, std::min(32767, s))));
    }
}

void Commando::drain_audio(std::vector<int16_t>& out) {
    out.insert(out.end(), audio_.begin(), audio_.end());
    audio_.clear();
}

// ---------------------------------------------------------------------------
// Inputs
// ---------------------------------------------------------------------------

void Commando::set_inputs(const MachineInputs& inputs) {
    uint8_t v0 = 0xff;
    if (inputs.player1.start) v0 &= ~0x01;
    if (inputs.player2.start) v0 &= ~0x02;
    if (inputs.coin2) v0 &= ~0x40;
    if (inputs.coin1) v0 &= ~0x80;
    in0_ = v0;

    uint8_t v1 = 0xff;
    if (inputs.player1.right) v1 &= ~0x01;
    if (inputs.player1.left) v1 &= ~0x02;
    if (inputs.player1.down) v1 &= ~0x04;
    if (inputs.player1.up) v1 &= ~0x08;
    if (inputs.player1.button1) v1 &= ~0x10;
    if (inputs.player1.button2) v1 &= ~0x20;
    in1_ = v1;

    uint8_t v2 = 0xff;
    if (inputs.player2.right) v2 &= ~0x01;
    if (inputs.player2.left) v2 &= ~0x02;
    if (inputs.player2.down) v2 &= ~0x04;
    if (inputs.player2.up) v2 &= ~0x08;
    if (inputs.player2.button1) v2 &= ~0x10;
    if (inputs.player2.button2) v2 &= ~0x20;
    in2_ = v2;
}

void Commando::set_dip_switch(int bank, uint8_t value) {
    if (bank == 0) dsw_a_ = value;
    if (bank == 1) dsw_b_ = value;
}

// ---------------------------------------------------------------------------
// Video
// ---------------------------------------------------------------------------

void Commando::render_frame() {
    // Background: 32x32 cells of 16x16, opaque.
    for (int f = 0; f < 0x400; f++) {
        const int x = f % 32;
        const int y = 31 - (f / 32);
        const uint8_t attr = tile_ram_[size_t(0x400) + size_t(f)];
        const int nchar = tile_ram_[size_t(f)] + ((attr & 0xc0) << 2);
        const int color = (attr & 0xf) << 3;
        const bool flip_x = (attr & 0x20) != 0;
        const bool flip_y = (attr & 0x10) != 0;
        const uint8_t* px = &tiles_rotated_[size_t(nchar & 0x3ff) * 256];
        for (int py = 0; py < 16; py++) {
            const int dy = y * 16 + (flip_y ? 15 - py : py);
            for (int pxi = 0; pxi < 16; pxi++) {
                const int dx = x * 16 + (flip_x ? 15 - pxi : pxi);
                const uint8_t p = px[py * 16 + pxi];
                bg_canvas_[size_t(dy) * 512 + size_t(dx)] = palette_[size_t(color + p) & 0xff];
            }
        }
    }

    // Text/char overlay: 32x32 cells of 8x8, pixel value 3 is transparent.
    std::fill(char_canvas_.begin(), char_canvas_.end(), 0u);
    for (int f = 0; f < 0x400; f++) {
        const int x = f / 32;
        const int y = 31 - (f % 32);
        const uint8_t attr = char_ram_[size_t(0x400) + size_t(f)];
        const int nchar = char_ram_[size_t(f)] + ((attr & 0xc0) << 2);
        const int color = (attr & 0xf) << 2;
        const bool flip_x = (attr & 0x20) != 0;
        const bool flip_y = (attr & 0x10) != 0;
        const uint8_t* px = &chars_rotated_[size_t(nchar & 0x3ff) * 64];
        for (int py = 0; py < 8; py++) {
            const int dy = y * 8 + (flip_y ? 7 - py : py);
            if (dy < 0 || dy >= 256) continue;
            for (int pxi = 0; pxi < 8; pxi++) {
                const uint8_t p = px[py * 8 + pxi];
                if (p == 3) continue;  // transparent
                const int dx = x * 8 + (flip_x ? 7 - pxi : pxi);
                if (dx < 0 || dx >= 256) continue;
                const uint8_t idx = char_lut_[size_t(color + p) & 0x3f];
                char_canvas_[size_t(dy) * 256 + size_t(dx)] = palette_[idx];
            }
        }
    }

    // Compose: scrolled background, then sprites, then the text overlay on top.
    for (int fy = 0; fy < kScreenHeight; fy++) {
        for (int fx = 0; fx < kScreenWidth; fx++) {
            const int sx = fx + 16;
            const int sy = fy;
            const uint32_t bx = wrap(sx + scroll_x_, 512);
            const uint32_t by = wrap(sy + (256 - scroll_y_), 512);
            framebuffer_[size_t(fy) * size_t(kScreenWidth) + size_t(fx)] = bg_canvas_[size_t(by) * 512 + bx];
        }
    }

    for (int f = 0; f < 0x80; f++) {
        const size_t base = size_t(f) * 4;
        const uint8_t attr = sprite_ram_[base + 1];
        const int bank = (attr >> 6) & 3;
        if (bank >= 3) continue;
        const int nchar = sprite_ram_[base] + (bank << 8);
        const int color = attr & 0x30;
        const int x = sprite_ram_[base + 2] - 16;
        const int y = 240 - (sprite_ram_[base + 3] + ((attr & 1) << 8));
        const bool flip_x = (attr & 8) != 0;
        const bool flip_y = (attr & 4) != 0;
        const uint8_t* px = &sprites_rotated_[size_t(nchar & 0x2ff) * 256];
        for (int py = 0; py < 16; py++) {
            const int dy = y + (flip_y ? 15 - py : py);
            if (dy < 0 || dy >= kScreenHeight) continue;
            for (int pxi = 0; pxi < 16; pxi++) {
                const uint8_t p = px[py * 16 + pxi];
                if (p == 15) continue;  // transparent
                const int dx = x + (flip_x ? 15 - pxi : pxi);
                if (dx < 0 || dx >= kScreenWidth) continue;
                const uint8_t idx = sprite_lut_[size_t(color + p) & 0x3f];
                framebuffer_[size_t(dy) * size_t(kScreenWidth) + size_t(dx)] = palette_[idx];
            }
        }
    }

    for (int fy = 0; fy < kScreenHeight; fy++) {
        for (int fx = 0; fx < kScreenWidth; fx++) {
            const uint32_t c = char_canvas_[size_t(fy) * 256 + size_t(fx + 16)];
            if (c != 0) framebuffer_[size_t(fy) * size_t(kScreenWidth) + size_t(fx)] = c;
        }
    }

    //if (flip_screen_) std::reverse(framebuffer_.begin(), framebuffer_.end());

    std::copy(ram_.end() - 0x200, ram_.end(), sprite_ram_.begin());
}

void Commando::run_frame() {
    for (int line = 0; line < kScanlines; line++) {
        if (line == 246) {
            main_cpu_.set_irq(IrqLine::Hold, 0xd7);
            render_frame();
        }
        main_cpu_.run(main_cycles_per_line_);
        if (!sound_reset_held_) sound_cpu_.run(main_cycles_per_line_);
    }
}

}  // namespace dsp
