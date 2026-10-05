#include "drivers/arcade/baraduke_hw.h"

#include <algorithm>
#include <cstring>

#include "core/rom_loader.h"
#include "cpu/irq_line.h"

namespace dsp {
namespace {

const std::vector<RomEntry> kBaradukeRom = {
    {"bd1_3.9c", 0x2000, 0x6000, 0xea2ea790},
    {"bd1_1.9a", 0x4000, 0x8000, 0x4e9f2bdc},
    {"bd1_2.9b", 0x4000, 0xc000, 0x40617fcd},
};
const std::vector<RomEntry> kBaradukeMcu = {
    {"bd1_4b.3b", 0x4000, 0x1000, 0xa47ecd32},
    {"cus60-60a1.mcu", 0x1000, 0x0000, 0x076ea82a},
};
const std::vector<RomEntry> kBaradukeChars = {{"bd1_5.3j", 0x2000, 0x0000, 0x706b7fee}};
const std::vector<RomEntry> kBaradukeTiles = {
    {"bd1_8.4p", 0x4000, 0x0000, 0xb0bb0710},
    {"bd1_7.4n", 0x4000, 0x4000, 0x0d7ebec9},
    {"bd1_6.4m", 0x4000, 0x8000, 0xe5da0896},
};
const std::vector<RomEntry> kBaradukeSprites = {
    {"bd1_9.8k", 0x4000, 0x0000, 0x87a29acc},
    {"bd1_10.8l", 0x4000, 0x4000, 0x72b6d20c},
    {"bd1_11.8m", 0x4000, 0x8000, 0x3076af9c},
    {"bd1_12.8n", 0x4000, 0xc000, 0x8b4c09a3},
};
const std::vector<RomEntry> kBaradukeProm = {
    {"bd1-1.1n", 0x800, 0x0000, 0x0d78ebc6},
    {"bd1-2.2m", 0x800, 0x0800, 0x03f7241f},
};

const std::vector<RomEntry> kMetroRom = {
    {"mc1-3.9c", 0x2000, 0x6000, 0x3390b33c},
    {"mc1-1.9a", 0x4000, 0x8000, 0x10b0977e},
    {"mc1-2.9b", 0x4000, 0xc000, 0x5c846f35},
};
const std::vector<RomEntry> kMetroMcu = {
    {"mc1-4.3b", 0x2000, 0x1000, 0x9c88f898},
    {"cus60-60a1.mcu", 0x1000, 0x0000, 0x076ea82a},
};
const std::vector<RomEntry> kMetroChars = {{"mc1-5.3j", 0x2000, 0x0000, 0x9b5ea33a}};
const std::vector<RomEntry> kMetroTiles = {
    {"mc1-7.4p", 0x4000, 0x0000, 0xc9dfa003},
    {"mc1-6.4n", 0x4000, 0x4000, 0x9686dc3c},
};
const std::vector<RomEntry> kMetroSprites = {
    {"mc1-8.8k", 0x4000, 0x0000, 0x265b31fa},
    {"mc1-9.8l", 0x4000, 0x4000, 0x541ec029},
};
const std::vector<RomEntry> kMetroProm = {
    {"mc1-1.1n", 0x800, 0x0000, 0x32a78a8b},
    {"mc1-2.2m", 0x800, 0x0800, 0x6f4dca7b},
};

GfxLayout char_layout() {
    GfxLayout l;
    l.width = 8;
    l.height = 8;
    l.total = 0x200;
    l.planes = 2;
    l.char_increment = 16 * 8;
    l.plane_offsets = {0, 4};
    l.x_offsets = {8 * 8, 8 * 8 + 1, 8 * 8 + 2, 8 * 8 + 3, 0, 1, 2, 3};
    l.y_offsets = {0 * 8, 1 * 8, 2 * 8, 3 * 8, 4 * 8, 5 * 8, 6 * 8, 7 * 8};
    return l;
}

GfxLayout tile_layout() {
    GfxLayout l;
    l.width = 8;
    l.height = 8;
    l.total = 0x400;
    l.planes = 3;
    l.char_increment = 16 * 8;
    l.plane_offsets = {0x8000 * 8, 0, 4};
    l.x_offsets = {0, 1, 2, 3, 8 + 0, 8 + 1, 8 + 2, 8 + 3};
    l.y_offsets = {0 * 8, 2 * 8, 4 * 8, 6 * 8, 8 * 8, 10 * 8, 12 * 8, 14 * 8};
    return l;
}

GfxLayout sprite_layout(int total) {
    GfxLayout l;
    l.width = 16;
    l.height = 16;
    l.total = total;
    l.planes = 4;
    l.char_increment = 128 * 8;
    l.plane_offsets = {0, 1, 2, 3};
    l.x_offsets = {0 * 4,  1 * 4,  2 * 4,  3 * 4,  4 * 4,  5 * 4,  6 * 4,  7 * 4,
                   8 * 4,  9 * 4, 10 * 4, 11 * 4, 12 * 4, 13 * 4, 14 * 4, 15 * 4};
    l.y_offsets = {8 * 8 * 0,  8 * 8 * 1,  8 * 8 * 2,  8 * 8 * 3,  8 * 8 * 4,  8 * 8 * 5,
                   8 * 8 * 6,  8 * 8 * 7,  8 * 8 * 8,  8 * 8 * 9,  8 * 8 * 10, 8 * 8 * 11,
                   8 * 8 * 12, 8 * 8 * 13, 8 * 8 * 14, 8 * 8 * 15};
    return l;
}

int resistor_weight(uint8_t nibble_bit0, uint8_t nibble_bit1, uint8_t nibble_bit2,
                    uint8_t nibble_bit3) {
    return int(nibble_bit0) * 0x0e + int(nibble_bit1) * 0x1f + int(nibble_bit2) * 0x43 +
           int(nibble_bit3) * 0x8f;
}

}  // namespace

BaradukeHw::BaradukeHw(Game game)
    : game_(game),
      main_cpu_(kMainClock),
      mcu_(kMcuClock, HD63701::Type::HD63701V) {
    framebuffer_.assign(size_t(kScreenWidth) * kScreenHeight, 0xff000000u);
    text_layer_.assign(size_t(kScreenWidth) * kScreenHeight, 0);
    tile_layer0_.assign(size_t(kTileW) * kTileH, 0);
    tile_layer1_.assign(size_t(kTileW) * kTileH, 0);

    main_cpu_.set_memory_handlers([this](uint16_t a) { return main_read(a); },
                                  [this](uint16_t a, uint8_t v) { main_write(a, v); });
    mcu_.set_memory_handlers([this](uint16_t a) { return mcu_read(a); },
                             [this](uint16_t a, uint8_t v) { mcu_write(a, v); });
    mcu_.set_port_read(0, [this]() { return in_port1(); });
    mcu_.set_port_write(0, [this](uint8_t v) { out_port1(v); });
    mcu_.set_cycle_handler([this](int c) { on_mcu_cycles(c); });
}

const char* BaradukeHw::title() const {
    return game_ == Game::MetroCross ? "Metro-Cross" : "Baraduke";
}

bool BaradukeHw::init(const std::string& rom_path, std::string* error) {
    if (!load_roms(rom_path, error)) return false;
    text_dirty_.fill(true);
    tile0_dirty_.fill(true);
    tile1_dirty_.fill(true);
    reset();
    return true;
}

bool BaradukeHw::load_roms(const std::string& rom_path, std::string* error) {
    RomLoader loader;
    if (!loader.open(rom_path, error)) return false;

    memory_.fill(0);
    mem_snd_.fill(0);

    if (game_ == Game::Baraduke) {
        std::vector<uint8_t> main_rom(0x10000, 0);
        if (!loader.load(kBaradukeRom, main_rom, error)) return false;
        std::copy(main_rom.begin(), main_rom.end(), memory_.begin());

        std::vector<uint8_t> mcu_buf(0x5000, 0);
        if (!loader.load(kBaradukeMcu, mcu_buf, error)) return false;
        // cus60-60a1.mcu @ offset 0 → internal ROM at $f000 (rom[$1000]).
        std::copy_n(mcu_buf.data(), 0x1000, mcu_.internal_rom().begin() + 0x1000);
        // bd1_4b.3b @ offset $1000 → MCU external program at $8000.
        std::copy_n(mcu_buf.data() + 0x1000, 0x4000, mem_snd_.begin() + 0x8000);

        std::vector<uint8_t> chars(0x2000), tiles(0xc000), sprites(0x10000), prom(0x1000);
        if (!loader.load(kBaradukeChars, chars, error)) return false;
        if (!loader.load(kBaradukeTiles, tiles, error)) return false;
        if (!loader.load(kBaradukeSprites, sprites, error)) return false;
        if (!loader.load(kBaradukeProm, prom, error)) return false;
        convert_chars(chars);
        convert_tiles(std::move(tiles));
        convert_sprites(sprites, 0x200);
        build_palette(prom);
        spritex_add_ = 184;
        spritey_add_ = -14;
    } else {
        std::vector<uint8_t> main_rom(0x10000, 0);
        if (!loader.load(kMetroRom, main_rom, error)) return false;
        std::copy(main_rom.begin(), main_rom.end(), memory_.begin());

        std::vector<uint8_t> mcu_buf(0x3000, 0);
        if (!loader.load(kMetroMcu, mcu_buf, error)) return false;
        std::copy_n(mcu_buf.data(), 0x1000, mcu_.internal_rom().begin() + 0x1000);
        std::copy_n(mcu_buf.data() + 0x1000, 0x2000, mem_snd_.begin() + 0x8000);

        std::vector<uint8_t> chars(0x2000), tiles(0xc000, 0xff), sprites(0x8000), prom(0x1000);
        if (!loader.load(kMetroChars, chars, error)) return false;
        if (!loader.load(kMetroTiles, tiles, error)) return false;
        // Pascal fills $8000-$bfff with $ff before convert_tiles.
        std::fill(tiles.begin() + 0x8000, tiles.end(), 0xff);
        if (!loader.load(kMetroSprites, sprites, error)) return false;
        if (!loader.load(kMetroProm, prom, error)) return false;
        convert_chars(chars);
        convert_tiles(std::move(tiles));
        convert_sprites(sprites, 0x100);
        build_palette(prom);
        spritex_add_ = -1;
        spritey_add_ = -32;
    }

    warnings_ = loader.warnings();
    return true;
}

void BaradukeHw::convert_chars(const std::vector<uint8_t>& rom) {
    chars_.decode(char_layout(), rom);
}

void BaradukeHw::convert_tiles(std::vector<uint8_t> rom) {
    if (rom.size() < 0x10000) rom.resize(0x10000, 0);
    // Unpack the third bitplane the same way as baraduke_hw.pas convert_tiles.
    for (int f = 0x2000; f <= 0x3fff; f++) {
        rom[size_t(0x8000 + f + 0x2000)] = rom[size_t(0x8000 + f)];
        rom[size_t(0x8000 + f + 0x4000)] = uint8_t(rom[size_t(0x8000 + f)] << 4);
    }
    for (int f = 0; f <= 0x1fff; f++) {
        rom[size_t(0x8000 + f + 0x2000)] = uint8_t(rom[size_t(0x8000 + f)] << 4);
    }

    // Each tile bank sits at offset 0 in its decode buffer; the shared third
    // plane remains at $8000 so the Pascal plane offsets apply unchanged.
    const GfxLayout layout = tile_layout();
    std::vector<uint8_t> bank0(0x10000, 0);
    std::vector<uint8_t> bank1(0x10000, 0);
    std::copy_n(rom.data(), 0x4000, bank0.data());
    std::copy_n(rom.data() + 0x4000, 0x4000, bank1.data());
    std::copy(rom.begin() + 0x8000, rom.end(), bank0.begin() + 0x8000);
    std::copy(rom.begin() + 0x8000, rom.end(), bank1.begin() + 0x8000);
    tiles0_.decode(layout, bank0);
    tiles1_.decode(layout, bank1);
}

void BaradukeHw::convert_sprites(const std::vector<uint8_t>& rom, int count) {
    sprites_.decode(sprite_layout(count), rom);
}

void BaradukeHw::build_palette(const std::vector<uint8_t>& prom) {
    for (int f = 0; f < 0x800; f++) {
        const uint8_t lo = prom[size_t(f)];
        const uint8_t hi = prom[size_t(f + 0x800)];
        const int r = resistor_weight(hi & 1, (hi >> 1) & 1, (hi >> 2) & 1, (hi >> 3) & 1);
        const int g = resistor_weight(lo & 1, (lo >> 1) & 1, (lo >> 2) & 1, (lo >> 3) & 1);
        const int b = resistor_weight((lo >> 4) & 1, (lo >> 5) & 1, (lo >> 6) & 1, (lo >> 7) & 1);
        palette_[size_t(f)] =
            0xff000000u | (uint32_t(r) << 16) | (uint32_t(g) << 8) | uint32_t(b);
    }
}

void BaradukeHw::reset() {
    main_cpu_.reset();
    mcu_.reset();
    cus30_.reset();
    in0_ = 0x1f;
    in1_ = 0x1f;
    in2_ = 0x1f;
    scroll_x0_ = 0;
    scroll_y0_ = 0;
    scroll_x1_ = 0;
    scroll_y1_ = 0;
    prio_ = false;
    copy_sprites_ = false;
    inputport_selected_ = 0;
    counter_ = 0;
    audio_accumulator_ = 0;
    audio_.clear();
}

void BaradukeHw::set_inputs(const MachineInputs& inputs) {
    in0_ = 0x1f;
    in1_ = 0x1f;
    in2_ = 0x1f;
    if (inputs.coin1) in0_ = uint8_t(in0_ & 0xfd);
    if (inputs.coin2) in0_ = uint8_t(in0_ & 0xfb);
    if (inputs.player1.start) in0_ = uint8_t(in0_ & 0xf7);
    if (inputs.player2.start) in0_ = uint8_t(in0_ & 0xef);

    if (inputs.player1.left) in1_ = uint8_t(in1_ & 0xfe);
    if (inputs.player1.right) in1_ = uint8_t(in1_ & 0xfd);
    if (inputs.player1.down) in1_ = uint8_t(in1_ & 0xfb);
    if (inputs.player1.up) in1_ = uint8_t(in1_ & 0xf7);
    if (inputs.player1.button1) in1_ = uint8_t(in1_ & 0xef);

    if (inputs.player2.left) in2_ = uint8_t(in2_ & 0xfe);
    if (inputs.player2.right) in2_ = uint8_t(in2_ & 0xfd);
    if (inputs.player2.down) in2_ = uint8_t(in2_ & 0xfb);
    if (inputs.player2.up) in2_ = uint8_t(in2_ & 0xf7);
    if (inputs.player2.button1) in2_ = uint8_t(in2_ & 0xef);
}

void BaradukeHw::set_dip_switch(int bank, uint8_t value) {
    if (bank == 0) dsw_a_ = value;
    else if (bank == 1) dsw_b_ = value;
    else if (bank == 2) dsw_c_ = value;
}

uint8_t BaradukeHw::main_read(uint16_t address) {
    if (address <= 0x3fff || (address >= 0x4800 && address <= 0x4fff) || address >= 0x6000) {
        return memory_[address];
    }
    if (address >= 0x4000 && address <= 0x43ff) {
        return cus30_.read(address & 0x3ff);
    }
    return 0xff;
}

void BaradukeHw::main_write(uint16_t address, uint8_t value) {
    if (address <= 0x1ff1 || (address >= 0x1ff3 && address <= 0x1fff)) {
        memory_[address] = value;
        return;
    }
    if (address == 0x1ff2) {
        memory_[address] = value;
        copy_sprites_ = true;
        return;
    }
    if (address >= 0x2000 && address <= 0x2fff) {
        if (memory_[address] != value) {
            tile0_dirty_[(address & 0xfff) >> 1] = true;
            memory_[address] = value;
        }
        return;
    }
    if (address >= 0x3000 && address <= 0x3fff) {
        if (memory_[address] != value) {
            tile1_dirty_[(address & 0xfff) >> 1] = true;
            memory_[address] = value;
        }
        return;
    }
    if (address >= 0x4000 && address <= 0x43ff) {
        cus30_.write(address & 0x3ff, value);
        return;
    }
    if (address >= 0x4800 && address <= 0x4fff) {
        if (memory_[address] != value) {
            text_dirty_[address & 0x3ff] = true;
            memory_[address] = value;
        }
        return;
    }
    if (address == 0x8000) return;  // watchdog
    if (address == 0x8800) {
        main_cpu_.set_irq(IrqLine::Clear);
        return;
    }
    if (address == 0xb000) {
        scroll_x0_ = uint16_t((scroll_x0_ & 0x00ff) | (uint16_t(value) << 8));
        const bool new_prio = ((scroll_x0_ & 0x0e00) >> 9) == 6;
        if (new_prio != prio_) {
            prio_ = new_prio;
            tile0_dirty_.fill(true);
            tile1_dirty_.fill(true);
        }
        return;
    }
    if (address == 0xb001) {
        scroll_x0_ = uint16_t((scroll_x0_ & 0xff00) | value);
        return;
    }
    if (address == 0xb002) {
        scroll_y0_ = value;
        return;
    }
    if (address == 0xb004) {
        scroll_x1_ = uint16_t((scroll_x1_ & 0x00ff) | (uint16_t(value) << 8));
        return;
    }
    if (address == 0xb005) {
        scroll_x1_ = uint16_t((scroll_x1_ & 0xff00) | value);
        return;
    }
    if (address == 0xb006) {
        scroll_y1_ = value;
        return;
    }
    // ROM / unused.
}

uint8_t BaradukeHw::mcu_read(uint16_t address) {
    if (address == 0x1105) {
        counter_ = uint16_t(counter_ + 1);
        return uint8_t((counter_ >> 4) & 0xff);
    }
    if ((address >= 0x1000 && address <= 0x1104) || (address >= 0x1106 && address <= 0x13ff)) {
        return cus30_.read(address & 0x3ff);
    }
    if (address >= 0x8000 && address <= 0xc7ff) {
        return mem_snd_[address];
    }
    return 0xff;
}

void BaradukeHw::mcu_write(uint16_t address, uint8_t value) {
    if (address >= 0x1000 && address <= 0x13ff) {
        cus30_.write(address & 0x3ff, value);
        return;
    }
    if (address >= 0x8000 && address <= 0xbfff) return;  // ROM
    if (address >= 0xc000 && address <= 0xc7ff) {
        mem_snd_[address] = value;
    }
}

uint8_t BaradukeHw::in_port1() {
    switch (inputport_selected_) {
        case 0:
            return uint8_t((dsw_a_ & 0xf8) >> 3);
        case 1:
            return uint8_t(((dsw_a_ & 7) << 2) | ((dsw_b_ & 0xc0) >> 6));
        case 2:
            return uint8_t((dsw_b_ & 0x3e) >> 1);
        case 3:
            return uint8_t(((dsw_b_ & 1) << 4) | (dsw_c_ & 0x0f));
        case 4:
            return in0_;
        case 5:
            return in2_;
        case 6:
            return in1_;
        default:
            return 0xff;
    }
}

void BaradukeHw::out_port1(uint8_t value) {
    if ((value & 0xe0) == 0x60) inputport_selected_ = uint8_t(value & 7);
}

void BaradukeHw::on_mcu_cycles(int cycles) {
    audio_accumulator_ += int64_t(cycles) * NamcoCus30::kSampleRate;
    while (audio_accumulator_ >= int64_t(kMcuClock)) {
        audio_accumulator_ -= int64_t(kMcuClock);
        audio_.push_back(cus30_.update());
    }
}

void BaradukeHw::copy_sprites_hw() {
    for (int i = 0; i < 0x80; i++) {
        for (int j = 10; j <= 15; j++) {
            memory_[size_t(0x1800 + i * 0x10 + j)] = memory_[size_t(0x1800 + i * 0x10 + j - 6)];
        }
    }
    copy_sprites_ = false;
}

void BaradukeHw::draw_text_layer() {
    for (int x = 0; x < 36; x++) {
        for (int y = 0; y < 28; y++) {
            const int sx = x - 2;
            const int sy = y + 2;
            int pos = 0;
            if ((sx & 0x20) != 0) {
                pos = sy + ((sx & 0x1f) << 5);
            } else {
                pos = sx + (sy << 5);
            }
            if (!text_dirty_[size_t(pos & 0x3ff)]) continue;
            text_dirty_[size_t(pos & 0x3ff)] = false;

            const uint8_t color = memory_[size_t(0x4c00 + pos)];
            const uint8_t nchar = memory_[size_t(0x4800 + pos)];
            const uint8_t* pixels = chars_.element(nchar);
            const int color_base = int(color) << 4;
            for (int row = 0; row < 8; row++) {
                const int dy = y * 8 + row;
                if (dy < 0 || dy >= kScreenHeight) continue;
                for (int col = 0; col < 8; col++) {
                    const int dx = x * 8 + col;
                    if (dx < 0 || dx >= kScreenWidth) continue;
                    const uint8_t pen = pixels[size_t(row * 8 + col)];
                    // gfx[0].trans[3] = transparent
                    text_layer_[size_t(dy * kScreenWidth + dx)] =
                        (pen == 3) ? 0
                                   : palette_[size_t((color_base + pen) & 0x7ff)];
                }
            }
        }
    }
}

void BaradukeHw::draw_tile_layer(int layer, bool transparent) {
    auto& dirty = (layer == 0) ? tile0_dirty_ : tile1_dirty_;
    auto& dest = (layer == 0) ? tile_layer0_ : tile_layer1_;
    const GfxSet& gfx = (layer == 0) ? tiles0_ : tiles1_;
    const int base = (layer == 0) ? 0x2000 : 0x3000;

    for (int f = 0; f < 0x800; f++) {
        if (!dirty[size_t(f)]) continue;
        dirty[size_t(f)] = false;
        const int x = f % 64;
        const int y = f / 64;
        const uint8_t atrib = memory_[size_t(base + 1 + f * 2)];
        const int nchar = memory_[size_t(base + f * 2)] + ((atrib & 3) << 8);
        const int color_base = int(atrib) << 3;
        const uint8_t* pixels = gfx.element(nchar);
        for (int row = 0; row < 8; row++) {
            for (int col = 0; col < 8; col++) {
                const uint8_t pen = pixels[size_t(row * 8 + col)];
                uint32_t pixel = 0;
                if (!(transparent && pen == 7)) {
                    pixel = palette_[size_t((color_base + pen) & 0x7ff)];
                }
                dest[size_t((y * 8 + row) * kTileW + x * 8 + col)] = pixel;
            }
        }
    }
}

void BaradukeHw::scroll_layer_to_screen(const std::vector<uint32_t>& layer, int scroll_x,
                                        int scroll_y) {
    for (int y = 0; y < kScreenHeight; y++) {
        const int sy = (scroll_y + y) & 0xff;
        uint32_t* dst = framebuffer_.data() + size_t(y * kScreenWidth);
        for (int x = 0; x < kScreenWidth; x++) {
            const int sx = (scroll_x + x) & 0x1ff;
            const uint32_t pixel = layer[size_t(sy * kTileW + sx)];
            if (pixel) dst[x] = pixel;
        }
    }
}

void BaradukeHw::draw_sprites(int priority) {
    static const uint8_t kGfxOffs[2][2] = {{0, 1}, {2, 3}};
    const int sprite_xoffs = int(memory_[0x7f5]) - 256 * int(memory_[0x7f4] & 1);
    const int sprite_yoffs = int(memory_[0x7f7]);

    for (int f = 0; f <= 0x7e; f++) {
        const uint8_t atrib1 = memory_[size_t(0x180a + f * 0x10)];
        if (priority != (atrib1 & 1)) continue;
        const uint8_t atrib2 = memory_[size_t(0x180e + f * 0x10)];
        uint16_t color = memory_[size_t(0x180c + f * 0x10)];
        const bool flipx = (atrib1 & 0x20) != 0;
        const bool flipy = (atrib2 & 1) != 0;
        const int sizex = (atrib1 & 0x80) >> 7;
        const int sizey = (atrib2 & 4) >> 2;
        const int sx =
            (int(memory_[size_t(0x180d + f * 0x10)]) + ((color & 1) << 8) + sprite_xoffs +
             spritex_add_) &
            0x1ff;
        const int sy =
            (240 - int(memory_[size_t(0x180f + f * 0x10)])) - sprite_yoffs - (16 * sizey) +
            spritey_add_;
        int nchar = int(memory_[size_t(0x180b + f * 0x10)]) * 4;
        if (((atrib1 & 0x10) != 0) && sizex == 0) nchar += 1;
        if (((atrib2 & 0x10) != 0) && sizey == 0) nchar += 2;
        const int color_base = int(color & 0xfe) << 3;

        for (int y = 0; y <= sizey; y++) {
            for (int x = 0; x <= sizex; x++) {
                const int code =
                    nchar + kGfxOffs[y ^ (sizey * int(flipy))][x ^ (sizex * int(flipx))];
                const uint8_t* pixels = sprites_.element(code);
                for (int row = 0; row < 16; row++) {
                    const int dy = sy + 16 * y + row;
                    if (dy < 0 || dy >= kScreenHeight) continue;
                    const int src_y = flipy ? (15 - row) : row;
                    for (int col = 0; col < 16; col++) {
                        const int dx = sx + 16 * x + col;
                        // Sprites live in a 512-wide wrap space; visible is 0..287.
                        const int screen_x = dx & 0x1ff;
                        if (screen_x >= kScreenWidth) continue;
                        const int src_x = flipx ? (15 - col) : col;
                        const uint8_t pen = pixels[size_t(src_y * 16 + src_x)];
                        if (pen == 15) continue;
                        framebuffer_[size_t(dy * kScreenWidth + screen_x)] =
                            palette_[size_t((color_base + pen) & 0x7ff)];
                    }
                }
            }
        }
    }
}

void BaradukeHw::update_video() {
    draw_text_layer();
    if (prio_) {
        draw_tile_layer(0, true);
        draw_tile_layer(1, false);
    } else {
        draw_tile_layer(0, false);
        draw_tile_layer(1, true);
    }

    std::fill(framebuffer_.begin(), framebuffer_.end(), 0xff000000u);

    if (prio_) {
        scroll_layer_to_screen(tile_layer1_, scroll_x1_ + 24, scroll_y1_ + 25);
        draw_sprites(0);
        scroll_layer_to_screen(tile_layer0_, scroll_x0_ + 26, scroll_y0_ + 25);
    } else {
        scroll_layer_to_screen(tile_layer0_, scroll_x0_ + 26, scroll_y0_ + 25);
        draw_sprites(0);
        scroll_layer_to_screen(tile_layer1_, scroll_x1_ + 24, scroll_y1_ + 25);
    }
    draw_sprites(1);

    // Text on top.
    for (int i = 0; i < kScreenWidth * kScreenHeight; i++) {
        if (text_layer_[size_t(i)]) framebuffer_[size_t(i)] = text_layer_[size_t(i)];
    }
}

void BaradukeHw::run_frame() {
    const int main_cycles = int(double(kMainClock) / kFramesPerSecond / kScanlines);
    const int mcu_cycles = int(double(kMcuClock) / kFramesPerSecond / kScanlines);

    for (int line = 0; line < kScanlines; line++) {
        if (line == 240) {
            update_video();
            main_cpu_.set_irq(IrqLine::Assert);
            mcu_.set_irq(IrqLine::Hold);
            if (copy_sprites_) copy_sprites_hw();
        }
        main_cpu_.run(main_cycles);
        mcu_.run(mcu_cycles);
    }
}

void BaradukeHw::drain_audio(std::vector<int16_t>& out) {
    out.swap(audio_);
    audio_.clear();
}

}  // namespace dsp
