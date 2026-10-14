#include "drivers/arcade/chinagate.h"

#include <algorithm>
#include <cstring>

#include "core/rom_loader.h"

namespace dsp {
namespace {

const std::vector<RomEntry> kMainRom = {{"cgate51.bin", 0x20000, 0, 0x439a3b19}};
const std::vector<RomEntry> kSubRom = {{"23j4-0.48", 0x20000, 0, 0x2914af38}};
const std::vector<RomEntry> kSoundRom = {{"23j0-0.40", 0x8000, 0, 0x9ffcadb6}};
const std::vector<RomEntry> kCharRom = {{"cgate18.bin", 0x20000, 0, 0x8d88d64d}};
const std::vector<RomEntry> kTileRoms = {
    {"chinagat_a-13", 0x10000, 0x00000, 0xb745cac4},
    {"chinagat_a-12", 0x10000, 0x10000, 0x3c864299},
    {"chinagat_a-15", 0x10000, 0x20000, 0x2f268f37},
    {"chinagat_a-14", 0x10000, 0x30000, 0xaef814c8},
};
const std::vector<RomEntry> kSpriteRoms = {
    {"23j7-0.103", 0x20000, 0x00000, 0x2f445030},
    {"23j8-0.102", 0x20000, 0x20000, 0x237f725a},
    {"23j9-0.101", 0x20000, 0x40000, 0x8caf6097},
    {"23ja-0.100", 0x20000, 0x60000, 0xf678594f},
};
const std::vector<RomEntry> kAdpcmRoms = {
    {"23j1-0.53", 0x20000, 0x00000, 0xf91f1001},
    {"23j2-0.52", 0x20000, 0x20000, 0x8b6f26e9},
};

const std::vector<int> kTileX = {3,          2,          1,          0,
                                 16 * 8 + 3, 16 * 8 + 2, 16 * 8 + 1, 16 * 8 + 0,
                                 32 * 8 + 3, 32 * 8 + 2, 32 * 8 + 1, 32 * 8 + 0,
                                 48 * 8 + 3, 48 * 8 + 2, 48 * 8 + 1, 48 * 8 + 0};
const std::vector<int> kTileY = {0 * 8,  1 * 8,  2 * 8,  3 * 8, 4 * 8,  5 * 8,  6 * 8,  7 * 8,
                                 8 * 8,  9 * 8,  10 * 8, 11 * 8, 12 * 8, 13 * 8, 14 * 8, 15 * 8};

GfxLayout char_layout() {
    GfxLayout layout;
    layout.width = 8;
    layout.height = 8;
    layout.total = 0x1000;
    layout.planes = 4;
    layout.char_increment = 32 * 8;
    layout.plane_offsets = {0, 2, 4, 6};
    layout.x_offsets = {1, 0, 8 * 8 + 1, 8 * 8 + 0, 16 * 8 + 1, 16 * 8 + 0, 24 * 8 + 1, 24 * 8 + 0};
    layout.y_offsets = std::vector<int>(kTileY.begin(), kTileY.begin() + 8);
    return layout;
}

GfxLayout tile_layout(int total, int plane_bytes) {
    GfxLayout layout;
    layout.width = 16;
    layout.height = 16;
    layout.total = total;
    layout.planes = 4;
    layout.char_increment = 64 * 8;
    layout.plane_offsets = {plane_bytes * 8, plane_bytes * 8 + 4, 0, 4};
    layout.x_offsets = kTileX;
    layout.y_offsets = kTileY;
    return layout;
}

inline uint8_t pal4bit(uint8_t value) { return uint8_t((value & 0x0f) * 0x11); }

}  // namespace

ChinaGate::ChinaGate() {
    framebuffer_.assign(size_t(kScreenWidth) * kScreenHeight, 0xff000000u);
    banked_rom_.assign(6 * 0x4000, 0);
    banked_rom_sub_.assign(6 * 0x4000, 0);

    main_cpu_.set_memory_handlers([this](uint16_t a) { return main_read(a); },
                                  [this](uint16_t a, uint8_t v) { main_write(a, v); });
    sub_cpu_.set_memory_handlers([this](uint16_t a) { return sub_read(a); },
                                 [this](uint16_t a, uint8_t v) { sub_write(a, v); });
    sound_cpu_.set_memory_handlers([this](uint16_t a) { return sound_read(a); },
                                   [this](uint16_t a, uint8_t v) { sound_write(a, v); });
    sound_cpu_.set_io_handlers([](uint16_t) { return uint8_t(0xff); }, [](uint16_t, uint8_t) {});
    sound_cpu_.set_cycle_handler([this](int cycles) { on_sound_cycles(cycles); });
    ym_.set_irq_handler([this](bool state) {
        sound_cpu_.set_irq(state ? IrqLine::Assert : IrqLine::Clear);
    });
}

bool ChinaGate::init(const std::string& rom_path, std::string* error) {
    RomLoader loader;
    if (!loader.open(rom_path, error)) return false;

    std::vector<uint8_t> main_rom(0x20000, 0);
    if (!loader.load(kMainRom, main_rom, error)) return false;
    std::copy_n(main_rom.begin() + 0x18000, 0x8000, memory_.begin() + 0x8000);
    for (int bank = 0; bank < 6; ++bank) {
        std::copy_n(main_rom.begin() + bank * 0x4000, 0x4000, banked_rom_.begin() + bank * 0x4000);
    }

    std::vector<uint8_t> sub_rom(0x20000, 0);
    if (!loader.load(kSubRom, sub_rom, error)) return false;
    std::copy_n(sub_rom.begin() + 0x18000, 0x8000, sub_fixed_.begin() + 0x8000);
    for (int bank = 0; bank < 6; ++bank) {
        std::copy_n(sub_rom.begin() + bank * 0x4000, 0x4000,
                    banked_rom_sub_.begin() + bank * 0x4000);
    }

    std::vector<uint8_t> sound_rom(0x8000, 0);
    if (!loader.load(kSoundRom, sound_rom, error)) return false;
    std::copy(sound_rom.begin(), sound_rom.end(), sound_memory_.begin());

    std::vector<uint8_t> char_rom(0x20000, 0);
    if (!loader.load(kCharRom, char_rom, error)) return false;
    chars_.decode(char_layout(), char_rom);

    std::vector<uint8_t> tile_rom(0x40000, 0);
    if (!loader.load(kTileRoms, tile_rom, error)) return false;
    tiles_.decode(tile_layout(0x800, 0x20000), tile_rom);

    std::vector<uint8_t> sprite_rom(0x80000, 0);
    if (!loader.load(kSpriteRoms, sprite_rom, error)) return false;
    sprites_.decode(tile_layout(0x1000, 0x40000), sprite_rom);

    std::vector<uint8_t> adpcm(0x40000, 0);
    if (!loader.load(kAdpcmRoms, adpcm, error)) return false;
    oki_.set_rom(std::move(adpcm));

    warnings_ = loader.warnings();
    reset();
    return true;
}

void ChinaGate::reset() {
    main_cpu_.reset();
    sub_cpu_.reset();
    sound_cpu_.reset();
    ym_.reset();
    oki_.reset();
    in0_ = 0x0e;
    in1_ = 0xff;
    in2_ = 0xff;
    sound_latch_ = 0;
    rom_bank_ = 0;
    rom_bank_sub_ = 0;
    scroll_x_ = 0;
    scroll_y_ = 0;
    flip_screen_ = false;
    audio_accumulator_ = 0;
    audio_.clear();
    background_.fill(0xff000000u);
    composite_.fill(0xff000000u);
}

uint8_t ChinaGate::main_read(uint16_t address) {
    if (address <= 0x1fff || (address >= 0x2000 && address <= 0x2fff) ||
        (address >= 0x3800 && address <= 0x397f) || address >= 0x8000) {
        return memory_[address];
    }
    switch (address) {
        case 0x3f00: return in0_;
        case 0x3f01: return dsw_a_;
        case 0x3f02: return dsw_b_;
        case 0x3f03: return in1_;
        case 0x3f04: return in2_;
        default: break;
    }
    if (address >= 0x4000 && address <= 0x7fff) {
        return banked_rom_[size_t(rom_bank_ % 6) * 0x4000 + (address & 0x3fff)];
    }
    return 0xff;
}

void ChinaGate::main_write(uint16_t address, uint8_t value) {
    if (address <= 0x1fff || (address >= 0x3800 && address <= 0x397f)) {
        memory_[address] = value;
        return;
    }
    if (address >= 0x2000 && address <= 0x2fff) {
        memory_[address] = value;
        return;
    }
    if (address >= 0x3000 && address <= 0x37ff) {
        const uint16_t offset = uint16_t(address & 0x7ff);
        if (palette_ram_[offset] != value) {
            palette_ram_[offset] = value;
            update_palette(uint16_t(offset & 0x3ff));
        }
        return;
    }
    switch (address) {
        case 0x3e00:
            sound_latch_ = value;
            sound_cpu_.set_nmi(IrqLine::Assert);
            return;
        case 0x3e01:
            main_cpu_.set_nmi(IrqLine::Clear);
            return;
        case 0x3e02:
            main_cpu_.set_firq(IrqLine::Clear);
            return;
        case 0x3e03:
            main_cpu_.set_irq(IrqLine::Clear);
            return;
        case 0x3e04:
            sub_cpu_.set_irq(IrqLine::Assert);
            return;
        case 0x3e06:
            scroll_y_ = uint16_t((scroll_y_ & 0x100) | value);
            return;
        case 0x3e07:
            scroll_x_ = uint16_t((scroll_x_ & 0x100) | value);
            return;
        case 0x3f00:
            scroll_x_ = uint16_t((scroll_x_ & 0xff) | ((value & 0x01) << 8));
            scroll_y_ = uint16_t((scroll_y_ & 0xff) | ((value & 0x02) << 7));
            flip_screen_ = (value & 0x04) == 0;
            return;
        case 0x3f01:
            rom_bank_ = uint8_t(value & 0x07);
            return;
        default:
            break;
    }
}

uint8_t ChinaGate::sub_read(uint16_t address) {
    if (address <= 0x1fff) return memory_[address];
    if (address >= 0x4000 && address <= 0x7fff) {
        return banked_rom_sub_[size_t(rom_bank_sub_ % 6) * 0x4000 + (address & 0x3fff)];
    }
    if (address >= 0x8000) return sub_fixed_[address];
    return 0xff;
}

void ChinaGate::sub_write(uint16_t address, uint8_t value) {
    if (address <= 0x1fff) {
        memory_[address] = value;
        return;
    }
    if (address == 0x2000) {
        rom_bank_sub_ = uint8_t(value & 0x07);
        return;
    }
    if (address == 0x2800) {
        sub_cpu_.set_irq(IrqLine::Clear);
        return;
    }
}

uint8_t ChinaGate::sound_read(uint16_t address) {
    if (address <= 0x87ff) return sound_memory_[address];
    switch (address) {
        case 0x8801: return ym_.status();
        case 0x9800: return oki_.read();
        case 0xa000:
            sound_cpu_.set_nmi(IrqLine::Clear);
            return sound_latch_;
        default: return 0xff;
    }
}

void ChinaGate::sound_write(uint16_t address, uint8_t value) {
    if (address >= 0x8000 && address <= 0x87ff) {
        sound_memory_[address] = value;
        return;
    }
    switch (address) {
        case 0x8800: ym_.select_register(value); break;
        case 0x8801: ym_.write(value); break;
        case 0x9800: oki_.write(value); break;
        default: break;
    }
}

void ChinaGate::on_sound_cycles(int cycles) {
    ym_.run_timers(cycles);
    audio_accumulator_ += int64_t(cycles) * YM2151::kSampleRate;
    while (audio_accumulator_ >= int64_t(kSoundClock)) {
        audio_accumulator_ -= int64_t(kSoundClock);
        const int32_t sample = ym_.update() + oki_.update() / 2;
        audio_.push_back(int16_t(std::clamp(sample, int32_t(-32768), int32_t(32767))));
    }
}

void ChinaGate::update_palette(uint16_t offset) {
    const uint8_t low = palette_ram_[offset];
    const uint8_t high = palette_ram_[size_t(offset) + 0x400];
    palette_[offset] = 0xff000000u | (uint32_t(pal4bit(low)) << 16) |
                       (uint32_t(pal4bit(uint8_t(low >> 4))) << 8) | uint32_t(pal4bit(high));
}

void ChinaGate::draw_background() {
    for (int index = 0; index < 0x400; ++index) {
        const int x = index % 32;
        const int y = index / 32;
        const int position =
            (x & 0x0f) + ((y & 0x0f) << 4) + ((x & 0x10) << 4) + ((y & 0x10) << 5);
        const uint8_t attributes = memory_[size_t(position * 2 + 0x2800)];
        const int color = 0x100 + (((attributes & 0x38) >> 3) << 4);
        const int code = memory_[size_t(position * 2 + 0x2801)] + ((attributes & 0x07) << 8);
        const bool flip_x = (attributes & 0x40) != 0;
        const bool flip_y = (attributes & 0x80) != 0;
        const uint8_t* pixels = tiles_.element(code);
        for (int row = 0; row < 16; ++row) {
            const int source_y = flip_y ? 15 - row : row;
            const size_t target = size_t((y * 16 + row) * 512 + x * 16);
            for (int column = 0; column < 16; ++column) {
                const int source_x = flip_x ? 15 - column : column;
                background_[target + size_t(column)] =
                    palette_[size_t(color + pixels[source_y * 16 + source_x]) & 0x3ff];
            }
        }
    }
}

void ChinaGate::draw_foreground() {
    for (int index = 0; index < 0x400; ++index) {
        const int x = index % 32;
        const int y = index / 32;
        const uint8_t attributes = memory_[size_t(0x2000 + index * 2)];
        const int color = ((attributes & 0xf0) >> 4) << 4;
        const int code = memory_[size_t(0x2001 + index * 2)] + ((attributes & 0x0f) << 8);
        const uint8_t* pixels = chars_.element(code);
        for (int row = 0; row < 8; ++row) {
            const size_t target = size_t((y * 8 + row) * 256 + x * 8);
            for (int column = 0; column < 8; ++column) {
                const uint8_t pen = pixels[row * 8 + column];
                if (pen == 0) continue;
                composite_[target + size_t(column)] = palette_[size_t(color + pen) & 0x3ff];
            }
        }
    }
}

void ChinaGate::draw_sprite_element(int code, int color, bool flip_x, bool flip_y, int pos_x,
                                    int pos_y) {
    const uint8_t* pixels = sprites_.element(code);
    for (int row = 0; row < 16; ++row) {
        const int y = pos_y + row;
        if (y < 0 || y >= 256) continue;
        const int source_y = flip_y ? 15 - row : row;
        for (int column = 0; column < 16; ++column) {
            const int x = pos_x + column;
            if (x < 0 || x >= 256) continue;
            const int source_x = flip_x ? 15 - column : column;
            const uint8_t pen = pixels[source_y * 16 + source_x];
            if (pen == 0) continue;
            composite_[size_t(y * 256 + x)] = palette_[size_t(color + pen) & 0x3ff];
        }
    }
}

void ChinaGate::draw_sprites() {
    for (int index = 0; index < 0x40; ++index) {
        const uint8_t* entry = &memory_[size_t(0x3800 + index * 5)];
        const uint8_t attributes = entry[1];
        if ((attributes & 0x80) == 0) continue;
        const int x = 240 - entry[4] + ((attributes & 0x02) << 7);
        const int y = 240 - entry[0] + ((attributes & 0x01) << 8);
        const int size = (attributes & 0x30) >> 4;
        const bool flip_x = (attributes & 0x08) != 0;
        const bool flip_y = (attributes & 0x04) != 0;
        const int color = (entry[2] & 0x70) + 0x80;
        int code = entry[3] + ((entry[2] & 0x0f) << 8);
        code &= ~size;
        switch (size) {
            case 1:
                draw_sprite_element(code, color, flip_x, flip_y, x, y - 16);
                draw_sprite_element(code + 1, color, flip_x, flip_y, x, y);
                break;
            case 2:
                draw_sprite_element(code, color, flip_x, flip_y, x - 16, y);
                draw_sprite_element(code + 1, color, flip_x, flip_y, x, y);
                break;
            case 3:
                draw_sprite_element(code, color, flip_x, flip_y, x - 16, y - 16);
                draw_sprite_element(code + 1, color, flip_x, flip_y, x, y - 16);
                draw_sprite_element(code + 2, color, flip_x, flip_y, x - 16, y);
                draw_sprite_element(code + 3, color, flip_x, flip_y, x, y);
                break;
            default:
                draw_sprite_element(code, color, flip_x, flip_y, x, y);
                break;
        }
    }
}

void ChinaGate::update_video() {
    draw_background();
    for (int y = 0; y < 256; ++y) {
        const size_t source = size_t(((y + scroll_y_) & 0x1ff) * 512);
        const size_t target = size_t(y * 256);
        for (int x = 0; x < 256; ++x) {
            composite_[target + size_t(x)] = background_[source + ((x + scroll_x_) & 0x1ff)];
        }
    }
    draw_sprites();
    draw_foreground();

    for (int y = 0; y < kScreenHeight; ++y) {
        for (int x = 0; x < kScreenWidth; ++x) {
            const uint32_t pixel = composite_[size_t((y + 8) * 256 + x)];
            const size_t target =
                flip_screen_ ? size_t((kScreenHeight - 1 - y) * kScreenWidth + (kScreenWidth - 1 - x))
                             : size_t(y * kScreenWidth + x);
            framebuffer_[target] = pixel;
        }
    }
}

void ChinaGate::run_frame() {
    const int main_slice = int(kMainClock / kFramesPerSecond / (kScanlines * kCpuSync) + 0.5);
    const int sub_slice = int(kSubClock / kFramesPerSecond / (kScanlines * kCpuSync) + 0.5);
    const int sound_slice = int(kSoundClock / kFramesPerSecond / (kScanlines * kCpuSync) + 0.5);

    for (int line = 0; line < kScanlines; ++line) {
        if (line == 8) in0_ = uint8_t(in0_ & 0xfe);
        if (line >= 16 && line <= 224 && (line % 16) == 0) main_cpu_.set_firq(IrqLine::Assert);
        if (line == 248) {
            main_cpu_.set_nmi(IrqLine::Assert);
            update_video();
            in0_ = uint8_t(in0_ | 0x01);
        }

        for (int slice = 0; slice < kCpuSync; ++slice) {
            main_cpu_.run(main_slice);
            sub_cpu_.run(sub_slice);
            sound_cpu_.run(sound_slice);
        }
    }
}

void ChinaGate::set_inputs(const MachineInputs& in) {
    in0_ = uint8_t(0x0e | (in0_ & 0x01));
    in1_ = 0xff;
    in2_ = 0xff;

    if (in.player1.right) in1_ &= ~0x01;
    if (in.player1.left) in1_ &= ~0x02;
    if (in.player1.up) in1_ &= ~0x04;
    if (in.player1.down) in1_ &= ~0x08;
    if (in.player1.button1) in1_ &= ~0x10;
    if (in.player1.button2) in1_ &= ~0x20;
    if (in.player1.button3) in1_ &= ~0x40;
    if (in.player1.start) in1_ &= ~0x80;

    if (in.player2.right) in2_ &= ~0x01;
    if (in.player2.left) in2_ &= ~0x02;
    if (in.player2.up) in2_ &= ~0x04;
    if (in.player2.down) in2_ &= ~0x08;
    if (in.player2.button1) in2_ &= ~0x10;
    if (in.player2.button2) in2_ &= ~0x20;
    if (in.player2.button3) in2_ &= ~0x40;
    if (in.player2.start) in2_ &= ~0x80;

    if (in.coin1) in0_ &= ~0x02;
    if (in.coin2) in0_ &= ~0x04;
}

void ChinaGate::set_dip_switch(int bank, uint8_t value) {
    if (bank == 0) dsw_a_ = value;
    else if (bank == 1) dsw_b_ = value;
}

void ChinaGate::drain_audio(std::vector<int16_t>& out) {
    out.insert(out.end(), audio_.begin(), audio_.end());
    audio_.clear();
}

}  // namespace dsp
