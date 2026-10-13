#include "drivers/arcade/cabal.h"

#include <algorithm>
#include <cstring>

#include "core/rom_loader.h"
#include "cpu/irq_line.h"

namespace dsp {
namespace {

const std::vector<RomEntry> kMain = {
    {"13.7h", 0x10000, 0x00000, 0x00abbe0c},
    {"11.6h", 0x10000, 0x00001, 0x44736281},
    {"12.7j", 0x10000, 0x20000, 0xd763a47c},
    {"10.6j", 0x10000, 0x20001, 0x96d5e8af},
};
const std::vector<RomEntry> kChars = {{"5-6s", 0x4000, 0, 0x6a76955a}};
const std::vector<RomEntry> kSprites = {
    {"sp_rom1.bin", 0x10000, 0x00000, 0x34d3cac8}, {"sp_rom2.bin", 0x10000, 0x00001, 0x4e49c28e},
    {"sp_rom3.bin", 0x10000, 0x20000, 0x7065e840}, {"sp_rom4.bin", 0x10000, 0x20001, 0x6a0e739d},
    {"sp_rom5.bin", 0x10000, 0x40000, 0x0e1ec30e}, {"sp_rom6.bin", 0x10000, 0x40001, 0x581a50c1},
    {"sp_rom7.bin", 0x10000, 0x60000, 0x55c44764}, {"sp_rom8.bin", 0x10000, 0x60001, 0x702735c9},
};
const std::vector<RomEntry> kTiles = {
    {"bg_rom1.bin", 0x10000, 0x00000, 0x1023319b}, {"bg_rom2.bin", 0x10000, 0x00001, 0x3b6d2b09},
    {"bg_rom3.bin", 0x10000, 0x20000, 0x420b0801}, {"bg_rom4.bin", 0x10000, 0x20001, 0x77bc7a60},
    {"bg_rom5.bin", 0x10000, 0x40000, 0x543fcb37}, {"bg_rom6.bin", 0x10000, 0x40001, 0x0bc50075},
    {"bg_rom7.bin", 0x10000, 0x60000, 0xd28d921e}, {"bg_rom8.bin", 0x10000, 0x60001, 0x67e4fe47},
};
const RomEntry kSoundBoot{"4-3n", 0x2000, 0, 0x4038eff2};
const RomEntry kSoundBank{"3-3p", 0x8000, 0, 0xd9defcbf};
const std::vector<RomEntry> kAdpcm = {
    {"2-1s", 0x10000, 0x00000, 0x850406b4},
    {"1-1u", 0x10000, 0x10000, 0x8b3e0789},
};

uint8_t pal4bit(uint16_t nibble) {
    nibble &= 0xf;
    return uint8_t((nibble << 4) | nibble);
}

bool load_interleaved16(RomLoader& loader, const std::vector<RomEntry>& entries,
                        std::vector<uint8_t>& dest, std::string* error) {
    size_t need = 0;
    for (const auto& e : entries) need = std::max(need, size_t(e.offset) + size_t(e.length) * 2);
    dest.assign(need, 0xff);
    for (const auto& e : entries) {
        std::vector<uint8_t> data;
        if (!loader.try_read(e.name, data) || data.size() != e.length) {
            if (error) *error = std::string("missing or bad ROM ") + e.name;
            return false;
        }
        if (e.crc && crc32_of(data.data(), data.size()) != e.crc) {
            // Soft warning path is handled by the caller via loader.warnings if desired.
        }
        const uint32_t base = e.offset & ~1u;
        const uint32_t lane = e.offset & 1u;
        for (uint32_t i = 0; i < data.size(); ++i) dest[base + i * 2 + lane] = data[i];
    }
    return true;
}

void draw_tile(uint32_t* dst, int dst_w, int dst_h, const uint8_t* src, int size, int x, int y,
               bool flipx, bool flipy, const uint32_t* pal, uint32_t transparent_mask) {
    for (int ty = 0; ty < size; ++ty) {
        const int py = (y + ty) & (dst_h - 1);
        const uint8_t* row = src + (flipy ? size - 1 - ty : ty) * size;
        for (int tx = 0; tx < size; ++tx) {
            const int px = (x + tx) & (dst_w - 1);
            const uint8_t pen = row[flipx ? size - 1 - tx : tx];
            if (transparent_mask & (1u << pen)) continue;
            dst[size_t(py) * size_t(dst_w) + size_t(px)] = pal[pen];
        }
    }
}

}  // namespace

Cabal::Cabal() : main_cpu_(kMainClock), sound_(SeibuSound::kDefaultClock) {
    rom_.assign(0x40000, 0xff);
    framebuffer_.assign(size_t(kScreenWidth) * kScreenHeight, 0xff000000u);
    screen_.assign(256 * 256, 0xff000000u);

    main_cpu_.set_memory_handlers([this](uint32_t a) { return read16(a); },
                                  [this](uint32_t a, uint16_t v) { write16(a, v); });
    main_cpu_.set_byte_handlers([this](uint32_t a) { return read8(a); },
                                [this](uint32_t a, uint8_t v) { write8(a, v); });
    main_cpu_.set_address_mask(0xfffff);
}

Cabal::~Cabal() = default;

bool Cabal::init(const std::string& rom_path, std::string* error) {
    RomLoader loader;
    if (!loader.open(rom_path, error)) return false;

    if (!load_interleaved16(loader, kMain, rom_, error)) return false;

    std::vector<uint8_t> boot(0x2000, 0), bank(0x8000, 0);
    if (!loader.try_read(kSoundBoot.name, boot) || boot.size() != kSoundBoot.length) {
        if (error) *error = "missing sound ROM 4-3n";
        return false;
    }
    if (!loader.try_read(kSoundBank.name, bank) || bank.size() != kSoundBank.length) {
        if (error) *error = "missing sound ROM 3-3p";
        return false;
    }
    if (!sound_.init_roms(boot.data(), boot.size(), bank.data(), bank.size(), true, error)) {
        return false;
    }

    std::vector<uint8_t> adpcm(0x20000, 0);
    for (const auto& e : kAdpcm) {
        std::vector<uint8_t> data;
        if (!loader.try_read(e.name, data) || data.size() != e.length) {
            if (error) *error = std::string("missing ADPCM ROM ") + e.name;
            return false;
        }
        std::copy(data.begin(), data.end(), adpcm.begin() + std::ptrdiff_t(e.offset));
    }
    sound_.load_adpcm_roms(adpcm.data(), 0x10000);

    // Text: 8×8, 2bpp.
    std::vector<uint8_t> chars(0x4000, 0);
    if (!loader.load(kChars, chars, error)) return false;
    {
        GfxLayout l;
        l.width = l.height = 8;
        l.total = 0x400;
        l.planes = 2;
        l.char_increment = 16 * 8;
        l.plane_offsets = {0, 4};
        l.x_offsets = {3, 2, 1, 0, 8 + 3, 8 + 2, 8 + 1, 8 + 0};
        l.y_offsets = {0 * 16, 1 * 16, 2 * 16, 3 * 16, 4 * 16, 5 * 16, 6 * 16, 7 * 16};
        chars_.decode(l, chars);
    }

    // Sprites: 16×16, 4bpp (Y offsets run bottom→top like Pascal ps_y).
    std::vector<uint8_t> spr;
    if (!load_interleaved16(loader, kSprites, spr, error)) return false;
    {
        GfxLayout l;
        l.width = l.height = 16;
        l.total = 0x1000;
        l.planes = 4;
        l.char_increment = 64 * 16;
        l.plane_offsets = {2 * 4, 3 * 4, 0 * 4, 1 * 4};
        l.x_offsets = {3,      2,      1,      0,      16 + 3, 16 + 2, 16 + 1, 16 + 0,
                       32 + 3, 32 + 2, 32 + 1, 32 + 0, 48 + 3, 48 + 2, 48 + 1, 48 + 0};
        l.y_offsets = {30 * 32, 28 * 32, 26 * 32, 24 * 32, 22 * 32, 20 * 32, 18 * 32, 16 * 32,
                       14 * 32, 12 * 32, 10 * 32, 8 * 32,  6 * 32,  4 * 32,  2 * 32,  0 * 32};
        sprites_.decode(l, spr);
    }

    // Background tiles: 16×16, 4bpp.
    std::vector<uint8_t> tiles;
    if (!load_interleaved16(loader, kTiles, tiles, error)) return false;
    {
        GfxLayout l;
        l.width = l.height = 16;
        l.total = 0x1000;
        l.planes = 4;
        l.char_increment = 64 * 16;
        l.plane_offsets = {2 * 4, 3 * 4, 0 * 4, 1 * 4};
        l.x_offsets = {3,           2,           1,           0,           16 + 3,      16 + 2,
                       16 + 1,      16 + 0,      32 * 16 + 3, 32 * 16 + 2, 32 * 16 + 1, 32 * 16 + 0,
                       33 * 16 + 3, 33 * 16 + 2, 33 * 16 + 1, 33 * 16 + 0};
        for (int i = 0; i < 16; ++i) l.y_offsets.push_back(i * 32);
        tiles_.decode(l, tiles);
    }

    for (const auto& w : loader.warnings()) warnings_.push_back(w);
    reset();
    return true;
}

void Cabal::reset() {
    main_ram_.fill(0);
    fg_ram_.fill(0);
    bg_ram_.fill(0);
    pal_ram_.fill(0);
    palette_.fill(0xff000000u);
    flip_screen_ = false;
    in0_ = in1_ = 0xffff;
    dsw_ = 0xefff;
    main_debt_ = sound_debt_ = 0;
    main_cpu_.reset();
    sound_.reset();
    sound_.coin_input() = 0xfc;
}

uint8_t Cabal::read8(uint32_t address) {
    const uint16_t w = read16(address & ~1u);
    return (address & 1) ? uint8_t(w & 0xff) : uint8_t(w >> 8);
}

void Cabal::write8(uint32_t address, uint8_t value) {
    const uint32_t a = address & ~1u;
    uint16_t w = read16(a);
    if (address & 1) w = uint16_t((w & 0xff00) | value);
    else w = uint16_t((w & 0x00ff) | (uint16_t(value) << 8));
    write16(a, w);
}

uint16_t Cabal::read16(uint32_t address) {
    address &= 0xfffff;
    if (address <= 0x3ffff) {
        const uint32_t o = address & ~1u;
        return uint16_t((rom_[o] << 8) | rom_[o + 1]);
    }
    if (address >= 0x40000 && address <= 0x4ffff) {
        return main_ram_[(address & 0xffff) >> 1];
    }
    if (address >= 0x60000 && address <= 0x607ff) {
        return fg_ram_[(address & 0x7ff) >> 1];
    }
    if (address >= 0x80000 && address <= 0x803ff) {
        return bg_ram_[(address & 0x3ff) >> 1];
    }
    if (address == 0xa0000) return dsw_;
    if (address == 0xa0008) return in0_;
    if (address == 0xa000c) return 0xffff;  // trackball 0
    if (address == 0xa000a || address == 0xa000e) return 0;  // trackball 1
    if (address == 0xa0010) return in1_;
    if (address >= 0xe0000 && address <= 0xe07ff) {
        return pal_ram_[(address & 0x7ff) >> 1];
    }
    if (address >= 0xe8000 && address <= 0xe800d) {
        return sound_.main_read(int((address & 0xe) >> 1));
    }
    return 0xffff;
}

void Cabal::write16(uint32_t address, uint16_t value) {
    address &= 0xfffff;
    if (address <= 0x3ffff) return;
    if (address >= 0x40000 && address <= 0x4ffff) {
        main_ram_[(address & 0xffff) >> 1] = value;
        return;
    }
    if (address >= 0x60000 && address <= 0x607ff) {
        fg_ram_[(address & 0x7ff) >> 1] = value;
        return;
    }
    if (address >= 0x80000 && address <= 0x803ff) {
        bg_ram_[(address & 0x3ff) >> 1] = value;
        return;
    }
    if (address == 0xc0040) return;
    if (address == 0xc0080) {
        flip_screen_ = value != 0;
        return;
    }
    if (address >= 0xe0000 && address <= 0xe07ff) {
        const int idx = int((address & 0x7ff) >> 1);
        if (pal_ram_[size_t(idx)] != value) {
            pal_ram_[size_t(idx)] = value;
            set_palette_entry(idx, value);
        }
        return;
    }
    if (address >= 0xe8000 && address <= 0xe800d) {
        sound_.main_write(int((address & 0xe) >> 1), uint8_t(value & 0xff));
    }
}

void Cabal::set_palette_entry(int index, uint16_t value) {
    const uint8_t r = pal4bit(value);
    const uint8_t g = pal4bit(uint16_t(value >> 4));
    const uint8_t b = pal4bit(uint16_t(value >> 8));
    palette_[size_t(index) & 0x3ff] =
        0xff000000u | (uint32_t(r) << 16) | (uint32_t(g) << 8) | uint32_t(b);
}

void Cabal::update_video() {
    std::fill(screen_.begin(), screen_.end(), 0xff000000u);

    // Background 16×16 map.
    for (int f = 0; f < 0x100; ++f) {
        const uint16_t atrib = bg_ram_[size_t(f)];
        const int color = (atrib >> 12) & 0xf;
        const int nchar = atrib & 0xfff;
        const int x = (f % 16) * 16;
        const int y = (f / 16) * 16;
        draw_tile(screen_.data(), 256, 256, tiles_.element(nchar), 16, x, y, false, false,
                  &palette_[size_t(color << 4) + 512], 0);
    }

    // Sprites (from main RAM $1C00 words, back to front).
    for (int f = 0x1ff; f >= 0; --f) {
        const uint16_t y = main_ram_[size_t(f * 4 + 0x1c00)];
        if ((y & 0x100) == 0) continue;
        const uint16_t atrib = main_ram_[size_t(f * 4 + 0x1c01)];
        const uint16_t x = main_ram_[size_t(f * 4 + 0x1c02)];
        const int nchar = atrib & 0xfff;
        const int color = ((x & 0x7800) >> 7) + 256;
        const bool flipx = (x & 0x400) != 0;
        draw_tile(screen_.data(), 256, 256, sprites_.element(nchar), 16, x & 0xff, y & 0xff, flipx,
                  false, &palette_[size_t(color)], 1u << 15);
    }

    // Foreground text (transparent pen 3).
    for (int f = 0; f < 0x400; ++f) {
        const uint16_t atrib = fg_ram_[size_t(f)];
        const int color = (atrib >> 10) & 0x3f;
        const int nchar = atrib & 0x3ff;
        const int x = (f % 32) * 8;
        const int y = (f / 32) * 8;
        draw_tile(screen_.data(), 256, 256, chars_.element(nchar), 8, x, y, false, false,
                  &palette_[size_t(color << 2)], 1u << 3);
    }

    for (int row = 0; row < kScreenHeight; ++row) {
        for (int col = 0; col < kScreenWidth; ++col) {
            int sx = col;
            int sy = row + 16;
            if (flip_screen_) {
                sx = 255 - col;
                sy = 255 - (row + 16);
            }
            framebuffer_[size_t(row) * kScreenWidth + size_t(col)] =
                screen_[size_t(sy) * 256 + size_t(sx)];
        }
    }
}

void Cabal::run_frame() {
    const double main_cycles = double(kMainClock) / kFramesPerSecond / kScanlines;
    const double sound_cycles =
        double(SeibuSound::kDefaultClock) / kFramesPerSecond / kScanlines;
    for (int line = 0; line < kScanlines; ++line) {
        if (line == 240) {
            update_video();
            main_cpu_.set_irq(1, IrqLine::Hold);
        }
        main_debt_ += main_cycles;
        main_debt_ -= main_cpu_.run(int(main_debt_));
        sound_debt_ += sound_cycles;
        if (sound_debt_ >= 1.0) {
            sound_debt_ -= double(sound_.run(int(sound_debt_)));
        }
    }
}

void Cabal::set_inputs(const MachineInputs& in) {
    uint16_t in0 = 0xffff;
    uint16_t in1 = 0xffff;
    uint8_t coin = 0xfc;

    if (in.player1.up) in0 &= ~0x0100;
    if (in.player1.down) in0 &= ~0x0200;
    if (in.player1.left) in0 &= ~0x0400;
    if (in.player1.right) in0 &= ~0x0800;
    if (in.player2.up) in0 &= ~0x1000;
    if (in.player2.down) in0 &= ~0x2000;
    if (in.player2.left) in0 &= ~0x4000;
    if (in.player2.right) in0 &= ~0x8000;

    if (in.player1.button1) in1 &= ~0x0001;
    if (in.player1.button2) in1 &= ~0x0002;
    if (in.player2.button1) in1 &= ~0x0004;
    if (in.player2.button2) in1 &= ~0x0008;
    if (in.player2.button3) in1 &= ~0x1000;
    if (in.player1.button3) in1 &= ~0x2000;
    if (in.player2.start) in1 &= ~0x4000;
    if (in.player1.start) in1 &= ~0x8000;

    // Coins are active-high into the Seibu sound latch (Pascal OR).
    if (in.coin2) coin = uint8_t(coin | 1);
    if (in.coin1) coin = uint8_t(coin | 2);

    in0_ = in0;
    in1_ = in1;
    sound_.coin_input() = coin;
}

void Cabal::set_dip_switch(int bank, uint8_t value) {
    if (bank == 0) dsw_ = uint16_t((dsw_ & 0xff00) | value);
    else if (bank == 1) dsw_ = uint16_t((dsw_ & 0x00ff) | (uint16_t(value) << 8));
}

void Cabal::drain_audio(std::vector<int16_t>& out) { sound_.drain_audio(out); }

}  // namespace dsp
