#include "drivers/arcade/cabal.h"

#include <algorithm>

#include "core/rom_loader.h"

namespace dsp {
namespace {

const std::vector<RomEntry> kProgram = {
    {"13.7h", 0x10000, 0x00000, 0x00abbe0c}, {"11.6h", 0x10000, 0x00001, 0x44736281},
    {"12.7j", 0x10000, 0x20000, 0xd763a47c}, {"10.6j", 0x10000, 0x20001, 0x96d5e8af}};
const std::vector<RomEntry> kChars = {{"5-6s", 0x4000, 0, 0x6a76955a}};
const std::vector<RomEntry> kSprites = {
    {"sp_rom1.bin", 0x10000, 0x00000, 0x34d3cac8}, {"sp_rom2.bin", 0x10000, 0x00001, 0x4e49c28e},
    {"sp_rom3.bin", 0x10000, 0x20000, 0x7065e840}, {"sp_rom4.bin", 0x10000, 0x20001, 0x6a0e739d},
    {"sp_rom5.bin", 0x10000, 0x40000, 0x0e1ec30e}, {"sp_rom6.bin", 0x10000, 0x40001, 0x581a50c1},
    {"sp_rom7.bin", 0x10000, 0x60000, 0x55c44764}, {"sp_rom8.bin", 0x10000, 0x60001, 0x702735c9}};
const std::vector<RomEntry> kTiles = {
    {"bg_rom1.bin", 0x10000, 0x00000, 0x1023319b}, {"bg_rom2.bin", 0x10000, 0x00001, 0x3b6d2b09},
    {"bg_rom3.bin", 0x10000, 0x20000, 0x420b0801}, {"bg_rom4.bin", 0x10000, 0x20001, 0x77bc7a60},
    {"bg_rom5.bin", 0x10000, 0x40000, 0x543fcb37}, {"bg_rom6.bin", 0x10000, 0x40001, 0x0bc50075},
    {"bg_rom7.bin", 0x10000, 0x60000, 0xd28d921e}, {"bg_rom8.bin", 0x10000, 0x60001, 0x67e4fe47}};
const std::vector<RomEntry> kSound = {{"4-3n", 0x2000, 0x0000, 0x4038eff2},
                                      {"3-3p", 0x8000, 0x8000, 0xd9defcbf}};
const std::vector<RomEntry> kAdpcm0 = {{"2-1s", 0x10000, 0, 0x850406b4}};
const std::vector<RomEntry> kAdpcm1 = {{"1-1u", 0x10000, 0, 0x8b3e0789}};

// Files holding the even or odd bytes of a 16-bit wide region.
bool load_interleaved(RomLoader& loader, const std::vector<RomEntry>& entries,
                      std::vector<uint8_t>& dest, std::string* error) {
    for (const RomEntry& e : entries) {
        std::vector<uint8_t> part(e.length, 0xff);
        if (!loader.load({{e.name, e.length, 0, e.crc}}, part, error)) return false;
        for (uint32_t i = 0; i < e.length; ++i) dest[e.offset + 2 * i] = part[i];
    }
    return true;
}

uint32_t pal4(uint16_t v) {
    const uint32_t r = v & 15, g = (v >> 4) & 15, b = (v >> 8) & 15;
    return 0xff000000u | (r * 17) << 16 | (g * 17) << 8 | (b * 17);
}

// 16x16 elements of 128 bytes, 4 planes in the nibbles of 16-bit words.
GfxLayout layout16(std::vector<int> x, std::vector<int> y) {
    GfxLayout l;
    l.width = l.height = 16;
    l.total = 0x1000;
    l.planes = 4;
    l.char_increment = 64 * 16;
    l.plane_offsets = {2 * 4, 3 * 4, 0 * 4, 1 * 4};
    l.x_offsets = std::move(x);
    l.y_offsets = std::move(y);
    return l;
}

}  // namespace

Cabal::Cabal() : screen_(256u * 256u, 0xff000000u), framebuffer_(size_t(kWidth) * kHeight, 0xff000000u) {
    main_cpu_.set_memory_handlers([this](uint32_t a) { return main_read(a); },
                                  [this](uint32_t a, uint16_t v) { main_write(a, v); });
}

bool Cabal::init(const std::string& rom_path, std::string* error) {
    RomLoader loader;
    if (!loader.open(rom_path, error)) return false;

    std::vector<uint8_t> image(0x40000, 0xff);
    if (!load_interleaved(loader, kProgram, image, error)) return false;
    for (size_t i = 0; i < rom_.size(); ++i) rom_[i] = uint16_t(image[2 * i] << 8 | image[2 * i + 1]);

    std::vector<uint8_t> sound(0x10000, 0xff);
    if (!loader.load(kSound, sound, error)) return false;
    sound_.load_program(sound, true);
    std::vector<uint8_t> adpcm0(0x10000, 0), adpcm1(0x10000, 0);
    if (!loader.load(kAdpcm0, adpcm0, error) || !loader.load(kAdpcm1, adpcm1, error)) return false;
    sound_.load_adpcm(adpcm0, adpcm1);

    {
        std::vector<uint8_t> gfx(0x4000, 0);
        if (!loader.load(kChars, gfx, error)) return false;
        GfxLayout l;
        l.width = l.height = 8;
        l.total = 0x400;
        l.planes = 2;
        l.char_increment = 16 * 8;
        l.plane_offsets = {0, 4};
        l.x_offsets = {3, 2, 1, 0, 8 + 3, 8 + 2, 8 + 1, 8 + 0};
        for (int i = 0; i < 8; ++i) l.y_offsets.push_back(i * 16);
        chars_.decode(l, gfx);
    }
    std::vector<uint8_t> gfx(0x80000, 0);
    if (!load_interleaved(loader, kSprites, gfx, error)) return false;
    {
        std::vector<int> y;
        for (int i = 15; i >= 0; --i) y.push_back(i * 2 * 32);  // stored upside down
        sprites_.decode(layout16({3, 2, 1, 0, 16 + 3, 16 + 2, 16 + 1, 16 + 0, 32 + 3, 32 + 2, 32 + 1,
                                  32 + 0, 48 + 3, 48 + 2, 48 + 1, 48 + 0},
                                 y),
                        gfx);
    }
    std::fill(gfx.begin(), gfx.end(), 0);
    if (!load_interleaved(loader, kTiles, gfx, error)) return false;
    {
        std::vector<int> y;
        for (int i = 0; i < 16; ++i) y.push_back(i * 32);
        tiles_.decode(layout16({3, 2, 1, 0, 16 + 3, 16 + 2, 16 + 1, 16 + 0, 32 * 16 + 3, 32 * 16 + 2,
                                32 * 16 + 1, 32 * 16 + 0, 33 * 16 + 3, 33 * 16 + 2, 33 * 16 + 1, 33 * 16 + 0},
                               y),
                      gfx);
    }

    warnings_ = loader.warnings();
    reset();
    return true;
}

void Cabal::reset() {
    ram_.fill(0);
    fg_ram_.fill(0);
    bg_ram_.fill(0);
    pal_ram_.fill(0);
    palette_.fill(0xff000000u);
    in0_ = in1_ = 0xffff;
    main_debt_ = sound_debt_ = 0;
    main_cpu_.reset();
    sound_.reset();
    sound_.set_coins(0xfc);
}

// ---------------------------------------------------------------------------
// Main CPU

uint16_t Cabal::main_read(uint32_t a) {
    a &= 0xfffff;
    if (a < 0x40000) return rom_[a >> 1];
    if (a >= 0x40000 && a < 0x50000) return ram_[(a & 0xffff) >> 1];
    if (a >= 0x60000 && a < 0x60800) return fg_ram_[(a & 0x7ff) >> 1];
    if (a >= 0x80000 && a < 0x80400) return bg_ram_[(a & 0x3ff) >> 1];
    if (a >= 0xe0000 && a < 0xe0800) return pal_ram_[(a & 0x7ff) >> 1];
    if (a >= 0xe8000 && a < 0xe800e) return sound_.main_read(int((a & 0xe) >> 1));
    switch (a) {
        case 0xa0000: return dsw_;
        case 0xa0008: return in0_;
        case 0xa000c: return 0xffff;  // trackball 0
        case 0xa000a:
        case 0xa000e: return 0;  // trackball 1
        case 0xa0010: return in1_;
        default: return 0xffff;
    }
}

void Cabal::main_write(uint32_t a, uint16_t v) {
    a &= 0xfffff;
    if (a < 0x40000) return;
    if (a >= 0x40000 && a < 0x50000) {
        ram_[(a & 0xffff) >> 1] = v;
    } else if (a >= 0x60000 && a < 0x60800) {
        fg_ram_[(a & 0x7ff) >> 1] = v;
    } else if (a >= 0x80000 && a < 0x80400) {
        bg_ram_[(a & 0x3ff) >> 1] = v;
    } else if (a >= 0xe0000 && a < 0xe0800) {
        const size_t i = (a & 0x7ff) >> 1;
        pal_ram_[i] = v;
        palette_[i] = pal4(v);
    } else if (a >= 0xe8000 && a < 0xe800e) {
        sound_.main_write(int((a & 0xe) >> 1), uint8_t(v));
    }
}

// ---------------------------------------------------------------------------
// Video

void Cabal::update_video() {
    // Background: 16x16 grid of 16x16 tiles, colours 512-767.
    for (int f = 0; f < 0x100; ++f) {
        const uint16_t attr = bg_ram_[size_t(f)];
        const uint8_t* g = tiles_.element(attr & 0xfff);
        const int base = 512 + ((attr >> 12) & 0xf) * 16;
        const int tx = (f & 15) * 16, ty = (f >> 4) * 16;
        for (int r = 0; r < 16; ++r)
            for (int c = 0; c < 16; ++c)
                screen_[size_t(ty + r) * 256 + size_t(tx + c)] = palette_[size_t(base + g[r * 16 + c])];
    }
    // Sprites (512, the last ones first): colours 256-511, pen 15 transparent.
    for (int f = 0x1ff; f >= 0; --f) {
        const uint16_t* s = &ram_[0x1c00 + size_t(f) * 4];
        if (!(s[0] & 0x100)) continue;
        const uint8_t* g = sprites_.element(s[1] & 0xfff);
        const int base = 256 + ((s[2] & 0x7800) >> 11) * 16;
        const bool flip_x = (s[2] & 0x400) != 0;
        const int sy = s[0] & 0xff;
        int sx = s[2] & 0x1ff;
        if (sx > 256) sx -= 512;
        for (int r = 0; r < 16; ++r) {
            const int py = sy + r;
            if (py >= 256) break;
            for (int c = 0; c < 16; ++c) {
                const int px = sx + c;
                if (px < 0 || px >= 256) continue;
                const int pen = g[r * 16 + (flip_x ? 15 - c : c)];
                if (pen != 15) screen_[size_t(py) * 256 + size_t(px)] = palette_[size_t(base + pen)];
            }
        }
    }
    // Text: 32x32 characters of 2 bpp, pen 3 transparent, colours 0-255.
    for (int f = 0; f < 0x400; ++f) {
        const uint16_t attr = fg_ram_[size_t(f)];
        const uint8_t* g = chars_.element(attr & 0x3ff);
        const int base = ((attr >> 10) & 0x3f) * 4;
        const int tx = (f & 31) * 8, ty = (f >> 5) * 8;
        for (int r = 0; r < 8; ++r)
            for (int c = 0; c < 8; ++c) {
                const int pen = g[r * 8 + c];
                if (pen != 3) screen_[size_t(ty + r) * 256 + size_t(tx + c)] = palette_[size_t(base + pen)];
            }
    }
    std::copy_n(screen_.begin() + 16 * 256, size_t(kWidth) * kHeight, framebuffer_.begin());
}

// ---------------------------------------------------------------------------

void Cabal::run_frame() {
    const double main_cycles = double(kMainClock) / kFramesPerSecond / kScanlines;
    const double sound_cycles = double(kSoundClock) / kFramesPerSecond / kScanlines;
    for (int line = 0; line < kScanlines; ++line) {
        if (line == 240) {
            update_video();
            main_cpu_.set_irq(1, IrqLine::Hold);
        }
        main_debt_ += main_cycles;
        main_debt_ -= main_cpu_.run(int(main_debt_));
        sound_debt_ += sound_cycles;
        sound_debt_ -= sound_.run(int(sound_debt_));
    }
}

void Cabal::set_inputs(const MachineInputs& in) {
    uint16_t in0 = 0xffff;
    if (in.player1.up) in0 &= ~0x0100;
    if (in.player1.down) in0 &= ~0x0200;
    if (in.player1.left) in0 &= ~0x0400;
    if (in.player1.right) in0 &= ~0x0800;
    if (in.player2.up) in0 &= ~0x1000;
    if (in.player2.down) in0 &= ~0x2000;
    if (in.player2.left) in0 &= ~0x4000;
    if (in.player2.right) in0 &= ~0x8000;
    in0_ = in0;
    uint16_t in1 = 0xffff;
    if (in.player1.button1) in1 &= ~0x0001;
    if (in.player1.button2) in1 &= ~0x0002;
    if (in.player2.button1) in1 &= ~0x0004;
    if (in.player2.button2) in1 &= ~0x0008;
    if (in.player2.button3) in1 &= ~0x1000;
    if (in.player1.button3) in1 &= ~0x2000;
    if (in.player2.start) in1 &= ~0x4000;
    if (in.player1.start) in1 &= ~0x8000;
    in1_ = in1;
    // The coins go to the sound CPU (active high over a 0xfc base).
    sound_.set_coins(uint8_t(0xfc | (in.coin2 ? 1 : 0) | (in.coin1 ? 2 : 0)));
}

void Cabal::set_dip_switch(int bank, uint8_t value) {
    if (bank == 0) dsw_ = uint16_t((dsw_ & 0xff00) | value);
    else if (bank == 1) dsw_ = uint16_t((dsw_ & 0x00ff) | (value << 8));
}

}  // namespace dsp
