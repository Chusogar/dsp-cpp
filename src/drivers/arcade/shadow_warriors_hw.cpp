#include "drivers/arcade/shadow_warriors_hw.h"

#include <algorithm>
#include <cstring>

#include "core/rom_loader.h"

namespace dsp {
namespace {

// MAME `ROM_START(shadoww)` set (src/mame/taito/shadoww.cpp). Filenames/CRCs
// match the supplied ROM zip and the dsp-emulator shadow_warriors_hw.pas.
const RomEntry kMainHigh{"shadowa_1.3s", 0x20000, 0, 0x8290d567};
const RomEntry kMainLow{"shadowa_2.4s", 0x20000, 0, 0xf3f08921};
const RomEntry kSoundRom{"gaiden_3.4b", 0x10000, 0, 0x75fd3e6a};
const RomEntry kOkiRom{"4.4a", 0x20000, 0, 0xb0e0faf9};
const RomEntry kCharRom{"gaiden_5.7a", 0x10000, 0, 0x8d4035f7};

const std::vector<RomEntry> kBgRoms = {
    {"14.3a", 0x20000, 0x00000, 0x1ecfddaa}, {"15.3b", 0x20000, 0x20000, 0x1291a696},
    {"16.1a", 0x20000, 0x40000, 0x140b47ca}, {"17.1b", 0x20000, 0x60000, 0x7638cccb},
};
const std::vector<RomEntry> kFgRoms = {
    {"18.6a", 0x20000, 0x00000, 0x3fadafd6}, {"19.6b", 0x20000, 0x20000, 0xddae9d5b},
    {"20.4b", 0x20000, 0x40000, 0x08cf7a93}, {"21.4b", 0x20000, 0x60000, 0x1ac892f5},
};
const std::vector<RomEntry> kSpriteRoms = {
    {"6.3m", 0x20000, 0x00000, 0xe7ccdf9f},   {"7.1m", 0x20000, 0x00001, 0x016bec95},
    {"8.3n", 0x20000, 0x40000, 0x7ef7f880},   {"9.1n", 0x20000, 0x40001, 0x6e9b7fd3},
    {"10.3r", 0x20000, 0x80000, 0xa6451dec},  {"11.1r", 0x20000, 0x80001, 0x7fbfdf5e},
    {"12.3s", 0x20000, 0xc0000, 0x94a836d8},  {"13.1s", 0x20000, 0xc0001, 0xe9caea3b},
};

bool load_bytes(RomLoader& loader, const RomEntry& entry, std::vector<uint8_t>& data,
                std::string* error) {
    data.assign(entry.length, 0);
    RomEntry single{entry.name, entry.length, 0, entry.crc};
    std::string name_error;
    if (loader.load({single}, data, &name_error)) return true;
    for (const std::string& name : loader.filenames()) {
        std::vector<uint8_t> candidate;
        if (!loader.try_read(name, candidate)) continue;
        if (candidate.size() != entry.length) continue;
        if (entry.crc != 0 && crc32_of(candidate.data(), candidate.size()) == entry.crc) {
            data = std::move(candidate);
            return true;
        }
    }
    if (error) {
        *error = name_error.empty() ? std::string("missing ROM file: ") + entry.name : name_error;
    }
    return false;
}

bool load_raw(RomLoader& loader, const std::vector<RomEntry>& entries, std::vector<uint8_t>& dest,
              std::string* error) {
    for (const RomEntry& entry : entries) {
        std::vector<uint8_t> data;
        if (!load_bytes(loader, entry, data, error)) return false;
        if (entry.offset + entry.length > dest.size()) dest.resize(entry.offset + entry.length);
        std::copy(data.begin(), data.end(), dest.begin() + std::ptrdiff_t(entry.offset));
    }
    return true;
}

// pg_x / pg_y from shadow_warriors_hw.pas. 4bpp, nibble plane interleave.
const std::vector<int> kPixelX = {0,  4,  8,  12, 16, 20, 24, 28,
                                  256, 260, 264, 268, 272, 276, 280, 284};
const std::vector<int> kPixelY = {0,  32, 64,  96,  128, 160, 192, 224,
                                  512, 544, 576, 608,  640, 672,  704, 736};

GfxLayout char_layout() {
    GfxLayout layout;
    layout.width = 8;
    layout.height = 8;
    layout.total = 0x800;
    layout.planes = 4;
    layout.char_increment = 8 * 8 * 4;
    layout.plane_offsets = {0, 1, 2, 3};
    layout.x_offsets.assign(kPixelX.begin(), kPixelX.begin() + 8);
    layout.y_offsets.assign(kPixelY.begin(), kPixelY.begin() + 8);
    return layout;
}

GfxLayout tile_layout() {
    GfxLayout layout;
    layout.width = 16;
    layout.height = 16;
    layout.total = 0x1000;
    layout.planes = 4;
    layout.char_increment = 4 * 8 * 32;
    layout.plane_offsets = {0, 1, 2, 3};
    layout.x_offsets = kPixelX;
    layout.y_offsets = kPixelY;
    return layout;
}

GfxLayout sprite_layout() {
    GfxLayout layout;
    layout.width = 8;
    layout.height = 8;
    layout.total = 0x8000;
    layout.planes = 4;
    layout.char_increment = 8 * 8 * 4;
    layout.plane_offsets = {0, 1, 2, 3};
    layout.x_offsets.assign(kPixelX.begin(), kPixelX.begin() + 8);
    layout.y_offsets.assign(kPixelY.begin(), kPixelY.begin() + 8);
    return layout;
}

inline uint8_t pal4bit(uint16_t value) { return uint8_t((value & 0x0f) * 0x11); }

inline uint32_t lit(uint32_t rgb) { return 0xff000000u | (rgb & 0xffffffu); }

// 50% 5-6-5 RGB blend from pal_engine (temp values are the raw RGB565 words).
inline uint16_t blend565(uint16_t a, uint16_t b) {
    uint16_t t1 = uint16_t((((a & 0xf800) + (b & 0xf800)) >> 1) & 0xf800);
    uint16_t t2 = uint16_t((((a & 0x7e0) + (b & 0x7e0)) >> 1) & 0x7e0);
    uint16_t t3 = uint16_t((((a & 0x1f) + (b & 0x1f)) >> 1) & 0x1f);
    return uint16_t(t1 | t2 | t3);
}

}  // namespace

ShadowWarriors::ShadowWarriors()
    : main_cpu_(kMainClock), sound_cpu_(kSoundClock), ym0_(kSoundClock), ym1_(kSoundClock),
      okim_(kOkimClock, true) {
    const size_t work = size_t(kWorkSize) * kWorkSize;
    text_layer_.assign(256u * 256u, 0);
    bg_layer_.assign(1024u * 512u, 0);
    fg_layer_.assign(1024u * 512u, 0);
    composite_.assign(work, 0);
    framebuffer_.assign(size_t(kScreenWidth) * size_t(kScreenHeight), 0xff000000u);

    main_cpu_.set_memory_handlers([this](uint32_t address) { return main_read(address); },
                                  [this](uint32_t address, uint16_t value) {
                                      main_write(address, value);
                                  });
    main_cpu_.set_byte_handlers([this](uint32_t address) { return main_read_byte(address); },
                                [this](uint32_t address, uint8_t value) {
                                    main_write_byte(address, value);
                                });
    sound_cpu_.set_memory_handlers(
        [this](uint16_t address) { return sound_read(address); },
        [this](uint16_t address, uint8_t value) { sound_write(address, value); });
    sound_cpu_.set_cycle_handler([this](int cycles) { on_sound_cycles(cycles); });
    ym0_.set_irq_handler([this](bool state) {
        sound_cpu_.set_irq(state ? IrqLine::Assert : IrqLine::Clear);
    });
}

bool ShadowWarriors::init(const std::string& rom_path, std::string* error) {
    if (!load_roms(rom_path, error)) return false;
    reset();
    return true;
}

void ShadowWarriors::reset() {
    main_cpu_.reset();
    sound_cpu_.reset();
    ym0_.reset();
    ym1_.reset();
    okim_.reset();

    ram_.fill(0);
    video_ram1_.fill(0);
    video_ram2_.fill(0);
    video_ram3_.fill(0);
    sprite_ram_.fill(0);
    palette_ram_.fill(0);
    palette_.fill(0xff000000u);

    scroll_x_txt_ = scroll_y_txt_ = 0;
    scroll_y_txt_off_ = 0;
    scroll_x_bg_ = scroll_y_bg_ = 0;
    scroll_y_bg_off_ = 0;
    scroll_x_fg_ = scroll_y_fg_ = 0;
    scroll_y_fg_off_ = 0;

    flip_main_ = false;
    sound_latch_ = 0;

    in0_ = 0xff;
    in1_ = 0xffff;
    dsw_a_ = 0xffff;

    audio_accumulator_ = 0;
    oki_accumulator_ = 0;
    oki_last_ = 0;
    audio_.clear();
    framebuffer_.assign(size_t(kScreenWidth) * size_t(kScreenHeight), 0xff000000u);
}

bool ShadowWarriors::load_roms(const std::string& rom_path, std::string* error) {
    RomLoader loader;
    if (!loader.open(rom_path, error)) return false;

    // ROM_LOAD16_BYTE: even offset = high byte of each 68000 word.
    rom_.assign(0x20000, 0);
    std::vector<uint8_t> data;
    if (!load_bytes(loader, kMainHigh, data, error)) return false;
    for (uint32_t index = 0; index < kMainHigh.length; index++) {
        rom_[index] = uint16_t(uint16_t(data[index]) << 8);
    }
    if (!load_bytes(loader, kMainLow, data, error)) return false;
    for (uint32_t index = 0; index < kMainLow.length; index++) {
        rom_[index] = uint16_t((rom_[index] & 0xff00) | data[index]);
    }

    std::vector<uint8_t> sound_bytes(0x10000, 0);
    if (!load_bytes(loader, kSoundRom, sound_bytes, error)) return false;
    std::copy(sound_bytes.begin(), sound_bytes.end(), mem_snd_.begin());

    std::vector<uint8_t> oki(0x20000, 0);
    if (!load_bytes(loader, kOkiRom, oki, error)) return false;
    okim_.set_rom(std::move(oki));

    std::vector<uint8_t> char_rom(0x20000, 0);
    if (!load_bytes(loader, kCharRom, char_rom, error)) return false;
    std::vector<uint8_t> bg_rom(0x80000, 0);
    if (!load_raw(loader, kBgRoms, bg_rom, error)) return false;
    std::vector<uint8_t> fg_rom(0x80000, 0);
    if (!load_raw(loader, kFgRoms, fg_rom, error)) return false;

    // Sprite ROMs are ROM_LOAD16_BYTE files (byte-interleaved into 1 MB).
    std::vector<uint8_t> sprite_rom(0x100010, 0);
    for (const RomEntry& entry : kSpriteRoms) {
        std::vector<uint8_t> part;
        if (!load_bytes(loader, entry, part, error)) return false;
        const size_t word_base = size_t(entry.offset) & ~size_t(1u);
        const bool high = (entry.offset & 1u) == 0;
        for (uint32_t index = 0; index < entry.length; index++) {
            const size_t byte_pos = word_base + size_t(index) * 2 + (high ? 0 : 1);
            if (byte_pos < sprite_rom.size()) sprite_rom[byte_pos] = part[index];
        }
    }
    sprite_rom.resize(0x100000);

    decode_graphics(char_rom, bg_rom, fg_rom, sprite_rom);

    for (const std::string& warning : loader.warnings()) warnings_.push_back(warning);
    return true;
}

void ShadowWarriors::decode_graphics(const std::vector<uint8_t>& char_rom,
                                     const std::vector<uint8_t>& bg_rom,
                                     const std::vector<uint8_t>& fg_rom,
                                     const std::vector<uint8_t>& sprite_rom) {
    gfx_char_.decode(char_layout(), char_rom);
    gfx_bg_.decode(tile_layout(), bg_rom);
    gfx_fg_.decode(tile_layout(), fg_rom);
    gfx_sprite_.decode(sprite_layout(), sprite_rom);
}

void ShadowWarriors::set_palette(int index, uint16_t value) {
    // 15-bit BGR (pal4bit). Red in bits 0-3, green 4-7, blue 8-11.
    const uint8_t red = pal4bit(value);
    const uint8_t green = pal4bit(uint16_t(value >> 4));
    const uint8_t blue = pal4bit(uint16_t(value >> 8));
    palette_[size_t(index)] =
        0xff000000u | (uint32_t(red) << 16) | (uint32_t(green) << 8) | blue;
}

uint16_t ShadowWarriors::main_read(uint32_t address) {
    address &= 0xffffffu;
    if (address <= 0x3ffff) {
        const uint32_t word = address >> 1;
        return word < rom_.size() ? rom_[word] : 0;
    }
    if (address >= 0x60000 && address <= 0x63fff) {
        return ram_[(address & 0x3fff) >> 1];
    }
    if (address >= 0x70000 && address <= 0x70fff) {
        return video_ram1_[(address & 0xfff) >> 1];
    }
    if (address >= 0x72000 && address <= 0x73fff) {
        return video_ram2_[(address & 0x1fff) >> 1];
    }
    if (address >= 0x74000 && address <= 0x75fff) {
        return video_ram3_[(address & 0x1fff) >> 1];
    }
    if (address >= 0x76000 && address <= 0x77fff) {
        return sprite_ram_[(address & 0x1fff) >> 1];
    }
    if (address >= 0x78000 && address <= 0x79fff) {
        return palette_ram_[(address & 0x1fff) >> 1];
    }
    if ((address & ~1u) == 0x7a000) return in0_;
    if ((address & ~1u) == 0x7a002) return in1_;
    if ((address & ~1u) == 0x7a004) return dsw_a_;
    return 0;
}

void ShadowWarriors::main_write(uint32_t address, uint16_t value) {
    address &= 0xffffffu;
    if (address <= 0x3ffff) return;
    if (address >= 0x60000 && address <= 0x63fff) {
        ram_[(address & 0x3fff) >> 1] = value;
        return;
    }
    if (address >= 0x70000 && address <= 0x70fff) {
        video_ram1_[(address & 0xfff) >> 1] = value;
        return;
    }
    if (address >= 0x72000 && address <= 0x73fff) {
        video_ram2_[(address & 0x1fff) >> 1] = value;
        return;
    }
    if (address >= 0x74000 && address <= 0x75fff) {
        video_ram3_[(address & 0x1fff) >> 1] = value;
        return;
    }
    if (address >= 0x76000 && address <= 0x77fff) {
        sprite_ram_[(address & 0x1fff) >> 1] = value;
        return;
    }
    if (address >= 0x78000 && address <= 0x79fff) {
        const int index = int((address & 0x1fff) >> 1);
        if (palette_ram_[size_t(index)] != value) {
            palette_ram_[size_t(index)] = value;
            if (index < int(palette_.size())) set_palette(index, value);
        }
        return;
    }
    if ((address & ~1u) == 0x7a104) scroll_y_txt_ = value;
    if ((address & ~1u) == 0x7a108) scroll_y_txt_off_ = uint8_t(value & 0xff);
    if ((address & ~1u) == 0x7a10c) scroll_x_txt_ = value;
    if ((address & ~1u) == 0x7a204) scroll_y_fg_ = value;
    if ((address & ~1u) == 0x7a208) scroll_y_fg_off_ = uint8_t(value & 0xff);
    if ((address & ~1u) == 0x7a20c) scroll_x_fg_ = value;
    if ((address & ~1u) == 0x7a304) scroll_y_bg_ = value;
    if ((address & ~1u) == 0x7a308) scroll_y_bg_off_ = uint8_t(value & 0xff);
    if ((address & ~1u) == 0x7a30c) scroll_x_bg_ = value;
    if ((address & ~1u) == 0x7a800) return;                      // watchdog
    if ((address & ~1u) == 0x7a802) {                            // sound latch
        sound_latch_ = uint8_t(value & 0xff);
        sound_cpu_.set_nmi(IrqLine::Assert);
        return;
    }
    if ((address & ~1u) == 0x7a806) main_cpu_.set_irq(5, IrqLine::Clear);
    if ((address & ~1u) == 0x7a808) flip_main_ = (value & 1) != 0;
}

uint8_t ShadowWarriors::main_read_byte(uint32_t address) {
    address &= 0xffffffu;
    const uint16_t word = main_read(address);
    return (address & 1u) ? uint8_t(word) : uint8_t(word >> 8);
}

void ShadowWarriors::main_write_byte(uint32_t address, uint8_t value) {
    address &= 0xffffffu;
    uint16_t word = main_read(address);
    if (address & 1u) word = uint16_t((word & 0xff00) | value);
    else word = uint16_t((word & 0x00ff) | (uint16_t(value) << 8));
    main_write(address, word);
}

uint8_t ShadowWarriors::sound_read(uint16_t address) {
    if (address <= 0xf7ff) return mem_snd_[address];
    if (address == 0xf800) return okim_.read();
    if (address == 0xfc20) {
        sound_cpu_.set_nmi(IrqLine::Clear);
        return sound_latch_;
    }
    return 0xff;
}

void ShadowWarriors::sound_write(uint16_t address, uint8_t value) {
    if (address <= 0xefff) return;  // ROM
    if (address >= 0xf000 && address <= 0xf7ff) {
        mem_snd_[address] = value;
        return;
    }
    if (address == 0xf800) {
        okim_.write(value);
        return;
    }
    if (address == 0xf810) { ym0_.control(value); return; }
    if (address == 0xf811) { ym0_.write(value); return; }
    if (address == 0xf820) { ym1_.control(value); return; }
    if (address == 0xf821) { ym1_.write(value); return; }
}

void ShadowWarriors::on_sound_cycles(int cycles) {
    // OKI runs at its own sample clock; keep its most recent output to mix in.
    oki_accumulator_ += int64_t(cycles) * int64_t(okim_.sample_frequency());
    while (oki_accumulator_ >= int64_t(kSoundClock)) {
        oki_accumulator_ -= int64_t(kSoundClock);
        oki_last_ = okim_.update();
    }

    audio_accumulator_ += int64_t(cycles) * int64_t(YM2203::kSampleRate);
    while (audio_accumulator_ >= int64_t(kSoundClock)) {
        audio_accumulator_ -= int64_t(kSoundClock);
        const int32_t sample = ym0_.update() + ym1_.update() + oki_last_;
        audio_.push_back(int16_t(std::clamp(sample, -32768, 32767)));
    }
}

void ShadowWarriors::draw_tilemap() {
    // bg layer (video_ram3, gfx_bg 16x16), 64x32 tile grid.
    for (int f = 0; f < 0x800; f++) {
        const uint16_t attrib = video_ram3_[size_t(f)];
        const uint16_t nchar = video_ram3_[size_t(f + 0x800)] & 0x0fff;
        const int color = ((attrib >> 4) & 0x0f) * 16 + 0x300;
        const int x = f % 64, y = f / 64;
        const uint8_t* pixels = gfx_bg_.element(nchar);
        for (int row = 0; row < 16; row++) {
            const int dest = (y * 16 + row) * 1024 + x * 16;
            for (int column = 0; column < 16; column++) {
                const uint8_t pen = pixels[size_t(row * 16 + column)];
                bg_layer_[size_t(dest + column)] = pen ? palette_[size_t(color + pen)] : 0;
            }
        }
    }
    // fg layer (video_ram2, gfx_fg 16x16).
    for (int f = 0; f < 0x800; f++) {
        const uint16_t attrib = video_ram2_[size_t(f)];
        const uint16_t nchar = video_ram2_[size_t(f + 0x800)] & 0x0fff;
        const int color = ((attrib >> 4) & 0x0f) * 16 + 0x200;
        const int x = f % 64, y = f / 64;
        const uint8_t* pixels = gfx_fg_.element(nchar);
        for (int row = 0; row < 16; row++) {
            const int dest = (y * 16 + row) * 1024 + x * 16;
            for (int column = 0; column < 16; column++) {
                const uint8_t pen = pixels[size_t(row * 16 + column)];
                fg_layer_[size_t(dest + column)] = pen ? palette_[size_t(color + pen)] : 0;
            }
        }
    }
    // text layer (video_ram1, gfx_char 8x8), 32x32 grid.
    for (int f = 0; f < 0x400; f++) {
        const uint16_t attrib = video_ram1_[size_t(f)];
        const uint16_t nchar = video_ram1_[size_t(f + 0x400)] & 0x07ff;
        const int color = ((attrib >> 4) & 0x0f) * 16 + 0x100;
        const int x = f % 32, y = f / 32;
        const uint8_t* pixels = gfx_char_.element(nchar);
        for (int row = 0; row < 8; row++) {
            const int dest = (y * 8 + row) * 256 + x * 8;
            for (int column = 0; column < 8; column++) {
                const uint8_t pen = pixels[size_t(row * 8 + column)];
                text_layer_[size_t(dest + column)] = pen ? palette_[size_t(color + pen)] : 0;
            }
        }
    }
}

void ShadowWarriors::scroll_layer(const std::vector<uint32_t>& layer, int layer_width,
                                  uint16_t scroll_x, uint16_t scroll_y, int mask_x, int mask_y) {
    // Pascal scroll_x_y → dest at (ADD_SPRITE,ADD_SPRITE)=(64,64), size 256×224.
    // We also fill the 16 rows that actualiza_trozo_final crops into (dest 288..303)
    // so the bottom of the 224-line framebuffer is tile data, not only fill_full_screen.
    static constexpr int kAdd = 64;
    static constexpr int kRows = kScreenHeight + 16;  // 240: covers crop (64,80)+(0,224)
    scroll_x = uint16_t(scroll_x & mask_x);
    scroll_y = uint16_t(scroll_y & mask_y);
    for (int k = 0; k < kRows; k++) {
        const int lr = (int(scroll_y) + k) & mask_y;
        uint32_t* dst_row = composite_.data() + size_t(kAdd + k) * kWorkSize;
        const size_t src_row = size_t(lr) * size_t(layer_width);
        for (int j = 0; j < kScreenWidth; j++) {
            const int lc = (int(scroll_x) + j) & mask_x;
            const uint32_t pixel = layer[src_row + size_t(lc)];
            if (pixel) dst_row[size_t(kAdd + j)] = pixel;
        }
    }
}

void ShadowWarriors::draw_sprites(int priority) {
    // layout[row][col] offset table from draw_sprites in the Pascal driver.
    static const uint8_t kLayout[8][8] = {
        {0, 1, 4, 5, 16, 17, 20, 21},  {2, 3, 6, 7, 18, 19, 22, 23},
        {8, 9, 12, 13, 24, 25, 28, 29}, {10, 11, 14, 15, 26, 27, 30, 31},
        {32, 33, 36, 37, 48, 49, 52, 53}, {34, 35, 38, 39, 50, 51, 54, 55},
        {40, 41, 44, 45, 56, 57, 60, 61}, {42, 43, 46, 47, 58, 59, 62, 63}};

    for (int index = 0; index < 0x100; index++) {
        const uint16_t attrib = sprite_ram_[size_t(index * 8)];
        if (((attrib >> 6) & 3) != priority) continue;
        if ((attrib & 4) == 0) continue;
        const bool flip_x = (attrib & 1) != 0;
        const bool flip_y = (attrib & 2) != 0;
        const bool blend = (attrib & 0x20) != 0;

        uint16_t color = sprite_ram_[size_t(index * 8 + 2)];
        const int size = 1 << (color & 3);
        uint16_t nchar = sprite_ram_[size_t(index * 8 + 1)];
        if (size >= 2) nchar &= 0x7ffe;
        if (size >= 2) nchar &= 0x7ffd;  // sizey >= 2 uses the same mask
        if (size >= 4) nchar &= 0x7ffb;
        if (size >= 4) nchar &= 0x7ff7;
        if (size >= 8) nchar &= 0x7fef;
        if (size >= 8) nchar &= 0x7fdf;
        // Pascal: color := color and $f0;
        // Opaque sprites → palette[pen + color]; blend sprites → palette[pen + color + $400].
        const int color_bank = int(color & 0xf0);
        const int color_base = color_bank + (blend ? 0x400 : 0);

        const int pos_x = sprite_ram_[size_t(index * 8 + 4)] & 0x1ff;
        const int pos_y = sprite_ram_[size_t(index * 8 + 3)] & 0x1ff;

        for (int row = 0; row < size; row++) {
            const int sy = flip_y ? 8 * (size - 1 - row) : 8 * row;
            for (int col = 0; col < size; col++) {
                const int sx = flip_x ? 8 * (size - 1 - col) : 8 * col;
                const uint8_t* pixels = gfx_sprite_.element(nchar + kLayout[row][col]);
                for (int py = 0; py < 8; py++) {
                    // Pascal: (posy + …) + ADD_SPRITE  (ADD_SPRITE = 64)
                    const int comp_y = 64 + ((pos_y + sy + py) & 0x1ff);
                    if (comp_y < 0 || comp_y >= kWorkSize) continue;
                    const int source_y = flip_y ? 7 - py : py;
                    for (int px = 0; px < 8; px++) {
                        const int comp_x = 64 + ((pos_x + sx + px) & 0x1ff);
                        if (comp_x < 0 || comp_x >= kWorkSize) continue;
                        const int source_x = flip_x ? 7 - px : px;
                        const uint8_t pen = pixels[size_t(source_y * 8 + source_x)];
                        if (pen == 0) continue;
                        size_t pos = size_t(comp_y) * kWorkSize + size_t(comp_x);
                        uint32_t color_word = palette_[size_t(color_base + pen)];
                        if (blend) {
                            // Pascal averages RGB565 of background pixel and sprite pen.
                            const uint32_t bg = composite_[pos];
                            const uint8_t r = uint8_t((((bg >> 16) & 0xff) + ((color_word >> 16) & 0xff)) >> 1);
                            const uint8_t g = uint8_t((((bg >> 8) & 0xff) + ((color_word >> 8) & 0xff)) >> 1);
                            const uint8_t b = uint8_t((((bg >> 0) & 0xff) + ((color_word >> 0) & 0xff)) >> 1);
                            color_word = 0xff000000u | (uint32_t(r) << 16) | (uint32_t(g) << 8) | b;
                        }
                        composite_[pos] = color_word;
                    }
                }
            }
        }
    }
}

void ShadowWarriors::update_video() {
    // Matches update_video_shadoww in shadow_warriors_hw.pas exactly:
    //   fill_full_screen(4,$200);
    //   draw_sprites(3);
    //   scroll_x_y(2,4, scroll_x_bg, scroll_y_bg-scroll_y_bg_off+16);
    //   draw_sprites(2);
    //   scroll_x_y(3,4, scroll_x_fg, scroll_y_fg-scroll_y_fg_off+16);
    //   draw_sprites(1);
    //   scroll_x_y(1,4, scroll_x_txt, scroll_y_txt-scroll_y_txt_off+16);
    //   draw_sprites(0);
    //   actualiza_trozo_final(0,16,256,224,4);
    draw_tilemap();

    const uint16_t bg_sy = uint16_t(scroll_y_bg_ - scroll_y_bg_off_ + 16);
    const uint16_t fg_sy = uint16_t(scroll_y_fg_ - scroll_y_fg_off_ + 16);
    const uint16_t tx_sy = uint16_t(scroll_y_txt_ - scroll_y_txt_off_ + 16);

    // fill_full_screen(4, $200)
    composite_.assign(size_t(kWorkSize) * kWorkSize, palette_[0x200]);

    draw_sprites(3);
    scroll_layer(bg_layer_, 1024, scroll_x_bg_, bg_sy, 0x3ff, 0x1ff);
    draw_sprites(2);
    scroll_layer(fg_layer_, 1024, scroll_x_fg_, fg_sy, 0x3ff, 0x1ff);
    draw_sprites(1);
    scroll_layer(text_layer_, 256, scroll_x_txt_, tx_sy, 0xff, 0xff);
    draw_sprites(0);

    // actualiza_trozo_final(0,16,256,224,4)
    // origin = (0+ADD_SPRITE, 16+ADD_SPRITE) = (64, 80)
    static constexpr int kCropX = 64;
    static constexpr int kCropY = 80;
    if (flip_main_) {
        for (int y = 0; y < kScreenHeight; y++) {
            for (int x = 0; x < kScreenWidth; x++) {
                framebuffer_[size_t(y * kScreenWidth + x)] =
                    composite_[size_t((223 - y + kCropY) * kWorkSize + (255 - x + kCropX))];
            }
        }
        return;
    }
    for (int y = 0; y < kScreenHeight; y++) {
        const size_t src_row = size_t(y + kCropY) * kWorkSize + kCropX;
        const size_t dst = size_t(y) * kScreenWidth;
        for (int x = 0; x < kScreenWidth; x++) {
            framebuffer_[dst + size_t(x)] = composite_[src_row + size_t(x)];
        }
    }
}

void ShadowWarriors::run_frame() {
    const int main_cycles = int(double(kMainClock) / kFramesPerSecond / kScanlines + 0.5);
    const int sound_cycles = int(double(kSoundClock) / kFramesPerSecond / kScanlines + 0.5);

    for (int line = 0; line < kScanlines; line++) {
        if (line == 240) {
            update_video();
            main_cpu_.set_irq(5, IrqLine::Hold);
        }
        main_cpu_.run(main_cycles);
        sound_cpu_.run(sound_cycles);
    }
}

void ShadowWarriors::set_inputs(const MachineInputs& inputs) {
    // SYSTEM @ 0x7a000: start1, start2, coin1, coin2 (active low)
    in0_ = 0xff;
    if (inputs.player1.start) in0_ = uint8_t(in0_ & ~0x01);
    if (inputs.player2.start) in0_ = uint8_t(in0_ & ~0x02);
    if (inputs.coin1) in0_ = uint8_t(in0_ & ~0x40);
    if (inputs.coin2) in0_ = uint8_t(in0_ & ~0x80);

    // P1_P2 @ 0x7a002: P1 in low byte, P2 in high byte (active low)
    in1_ = 0xffff;
    auto apply = [](const InputState& state, uint16_t& reg, int shift) {
        uint16_t mask = uint16_t(0xff << shift);
        uint16_t bits = 0xff;
        if (state.left) bits = uint16_t(bits & ~0x01);
        if (state.right) bits = uint16_t(bits & ~0x02);
        if (state.down) bits = uint16_t(bits & ~0x04);
        if (state.up) bits = uint16_t(bits & ~0x08);
        if (state.button1) bits = uint16_t(bits & ~0x10);
        if (state.button2) bits = uint16_t(bits & ~0x20);
        if (state.button3) bits = uint16_t(bits & ~0x40);
        reg = uint16_t((reg & ~mask) | (uint16_t(bits) << shift));
    };
    apply(inputs.player1, in1_, 0);
    apply(inputs.player2, in1_, 8);
}

void ShadowWarriors::set_dip_switch(int bank, uint8_t value) {
    if (bank == 0) dsw_a_ = value;
}

void ShadowWarriors::drain_audio(std::vector<int16_t>& out) {
    out.insert(out.end(), audio_.begin(), audio_.end());
    audio_.clear();
}

}  // namespace dsp