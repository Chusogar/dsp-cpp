#include "drivers/arcade/shaolinsroad.h"

#include <algorithm>
#include <cstring>

#include "core/rom_loader.h"

namespace dsp {
namespace {

const std::vector<RomEntry> kMainRoms = {
    {"477l03.d9|477-l03.d9|shaolins.d9", 0x2000, 0x6000, 0x2598dfdd},
    {"477l04.d10|477-l04.d10|shaolins.d10", 0x4000, 0x8000, 0x0cf0351a},
    {"477l05.d11|477-l05.d11|shaolins.d11", 0x4000, 0xc000, 0x654037f8},
};

const std::vector<RomEntry> kCharRoms = {
    {"477j06.a10|shaolins.a10|kicker.a10", 0x2000, 0x0000, 0xff18a7ed},
    {"477j07.a11|shaolins.a11|kicker.a11", 0x2000, 0x2000, 0x5f53ae61},
};

const std::vector<RomEntry> kSpriteRoms = {
    {"477j02.h15|477-k02.h15|shaolins.h15", 0x4000, 0x0000, 0xb94e645b},
    {"477j01.h14|477-k01.h14|shaolins.h14", 0x4000, 0x4000, 0x61bbf797},
};

const std::vector<RomEntry> kPaletteRoms = {
    {"477j10.a12|shaolins.a12", 0x100, 0x000, 0xb09db4b4},
    {"477j11.a13|shaolins.a13", 0x100, 0x100, 0x270a2bf3},
    {"477j12.a14|shaolins.a14", 0x100, 0x200, 0x83e95ea8},
    {"477j09.b8|shaolins.b8", 0x100, 0x300, 0xaa900724},
    {"477j08.f16|shaolins.f16", 0x100, 0x400, 0x80009cf5},
};

GfxLayout char_layout() {
    GfxLayout layout;
    layout.width = 8;
    layout.height = 8;
    layout.total = 512;
    layout.planes = 4;
    layout.char_increment = 16 * 8;
    layout.rotate_cw = true;
    layout.plane_offsets = {512 * 16 * 8 + 4, 512 * 16 * 8 + 0, 4, 0};
    layout.x_offsets = {0, 1, 2, 3, 8 * 8 + 0, 8 * 8 + 1, 8 * 8 + 2, 8 * 8 + 3};
    layout.y_offsets = {0 * 8, 1 * 8, 2 * 8, 3 * 8, 4 * 8, 5 * 8, 6 * 8, 7 * 8};
    return layout;
}

GfxLayout sprite_layout() {
    GfxLayout layout;
    layout.width = 16;
    layout.height = 16;
    layout.total = 256;
    layout.planes = 4;
    layout.char_increment = 64 * 8;
    layout.rotate_cw = true;
    layout.plane_offsets = {256 * 64 * 8 + 4, 256 * 64 * 8 + 0, 4, 0};
    layout.x_offsets = {0,         1,         2,         3,         8 * 8 + 0,  8 * 8 + 1,
                        8 * 8 + 2, 8 * 8 + 3, 16 * 8 + 0, 16 * 8 + 1, 16 * 8 + 2, 16 * 8 + 3,
                        24 * 8 + 0, 24 * 8 + 1, 24 * 8 + 2, 24 * 8 + 3};
    layout.y_offsets = {0 * 8,  1 * 8,  2 * 8,  3 * 8,  4 * 8,  5 * 8,  6 * 8,  7 * 8,
                        32 * 8, 33 * 8, 34 * 8, 35 * 8, 36 * 8, 37 * 8, 38 * 8, 39 * 8};
    return layout;
}

}  // namespace

ShaolinsRoad::ShaolinsRoad()
    : main_cpu_(kMainClock), sn0_(kSn0Clock), sn1_(kSn1Clock) {
    framebuffer_.assign(size_t(kScreenWidth) * kScreenHeight, 0xff000000u);
    tilemap_.assign(256 * 256, 0xff000000u);
    composite_.assign(256 * 256, 0xff000000u);

    main_cpu_.set_memory_handlers(
        [this](uint16_t address) { return main_read(address); },
        [this](uint16_t address, uint8_t value) { main_write(address, value); });
    main_cpu_.set_cycle_handler([this](int cycles) { on_cpu_cycles(cycles); });
}

bool ShaolinsRoad::init(const std::string& rom_path, std::string* error) {
    RomLoader loader;
    if (!loader.open(rom_path, error)) return false;

    std::vector<uint8_t> main_rom(0x10000, 0);
    if (!loader.load(kMainRoms, main_rom, error)) return false;
    std::copy(main_rom.begin(), main_rom.end(), memory_.begin());

    std::vector<uint8_t> char_rom(0x4000, 0);
    if (!loader.load(kCharRoms, char_rom, error)) return false;

    std::vector<uint8_t> sprite_rom(0x8000, 0);
    if (!loader.load(kSpriteRoms, sprite_rom, error)) return false;

    std::vector<uint8_t> prom(0x500, 0);
    if (!loader.load(kPaletteRoms, prom, error)) return false;

    decode_graphics(char_rom, sprite_rom);
    build_palette(prom);
    warnings_ = loader.warnings();

    reset();
    return true;
}

void ShaolinsRoad::decode_graphics(const std::vector<uint8_t>& char_rom,
                                   const std::vector<uint8_t>& sprite_rom) {
    chars_.decode(char_layout(), char_rom);
    sprites_.decode(sprite_layout(), sprite_rom);
}

void ShaolinsRoad::build_palette(const std::vector<uint8_t>& prom) {
    const std::vector<int> resistances = {2200, 1000, 470, 220};
    auto weights = compute_resistor_weights(0, 255, -1.0,
                                            {{resistances, 470, 0},
                                             {resistances, 470, 0},
                                             {resistances, 470, 0}});

    for (size_t index = 0; index < 0x100; index++) {
        auto bits = [&prom](size_t offset) {
            uint8_t data = prom[offset];
            return std::vector<int>{(data >> 0) & 1, (data >> 1) & 1, (data >> 2) & 1,
                                    (data >> 3) & 1};
        };
        int red = combine_weights(weights[0], bits(index));
        int green = combine_weights(weights[1], bits(index + 0x100));
        int blue = combine_weights(weights[2], bits(index + 0x200));
        palette_[index] = 0xff000000u | (uint32_t(red) << 16) | (uint32_t(green) << 8) |
                          uint32_t(blue);
    }

    for (int low = 0; low < 0x100; low++) {
        for (int bank = 0; bank < 8; bank++) {
            size_t index = size_t(low + (bank << 8));
            char_lut_[index] = uint8_t((prom[size_t(low) + 0x300] & 0x0f) + (bank << 5) + 16);
            sprite_lut_[index] = uint8_t((prom[size_t(low) + 0x400] & 0x0f) + (bank << 5));
        }
    }
}

void ShaolinsRoad::reset() {
    main_cpu_.reset();
    sn0_.reset();
    sn1_.reset();
    palette_bank_ = 0;
    scroll_ = 0;
    nmi_enable_ = false;
    flip_screen_ = false;
    in0_ = 0xff;
    in1_ = 0xff;
    in2_ = 0xff;
    dirty_.fill(true);
    tilemap_.assign(256 * 256, 0xff000000u);
    composite_.assign(256 * 256, 0xff000000u);
    audio_accumulator_ = 0;
    audio_.clear();
}

uint8_t ShaolinsRoad::main_read(uint16_t address) {
    switch (address) {
        case 0x0500: return dsw_a_;
        case 0x0600: return dsw_b_;
        case 0x0700: return in0_;
        case 0x0701: return in1_;
        case 0x0702: return in2_;
        case 0x0703: return dsw_c_;
        default: break;
    }
    if ((address >= 0x2800 && address <= 0x2bff) || (address >= 0x3000 && address <= 0x33ff) ||
        address >= 0x3800) {
        return memory_[address];
    }
    return 0xff;
}

void ShaolinsRoad::main_write(uint16_t address, uint8_t value) {
    if (address >= 0x2800 && address <= 0x2bff) {
        memory_[address] = value;
        return;
    }
    if (address >= 0x3000 && address <= 0x33ff) {
        memory_[address] = value;
        return;
    }
    if (address >= 0x3800 && address <= 0x3fff) {
        if (memory_[address] != value) {
            dirty_[address & 0x3ff] = true;
            memory_[address] = value;
        }
        return;
    }
    if (address >= 0x4000) return;

    switch (address) {
        case 0x0000:
            flip_screen_ = (value & 1) != 0;
            nmi_enable_ = (value & 2) != 0;
            break;
        case 0x0100:
            break;
        case 0x0300:
            sn0_.write(value);
            break;
        case 0x0400:
            sn1_.write(value);
            break;
        case 0x1800:
            if (palette_bank_ != (value & 7)) {
                palette_bank_ = uint8_t(value & 7);
                dirty_.fill(true);
            }
            break;
        case 0x2000:
            scroll_ = uint8_t(~value);
            break;
        default:
            break;
    }
}

void ShaolinsRoad::on_cpu_cycles(int cycles) {
    audio_accumulator_ += int64_t(cycles) * SN76496::kSampleRate;
    while (audio_accumulator_ >= kMainClock) {
        audio_accumulator_ -= kMainClock;
        int32_t sample = (sn0_.update() + sn1_.update()) / 2;
        if (sample > 32767) sample = 32767;
        if (sample < -32768) sample = -32768;
        audio_.push_back(int16_t(sample));
    }
}

void ShaolinsRoad::draw_tile(int offset) {
    const int y = offset % 32;
    const int x = 31 - (offset / 32);
    const uint8_t attr = memory_[0x3800 + offset];
    const int code = memory_[0x3c00 + offset] + ((attr & 0x40) << 2);
    const int color = ((attr & 0x0f) + (0x10 * palette_bank_)) << 4;
    const bool flip_x = (attr & 0x20) != 0;
    const bool flip_y = (attr & 0x10) != 0;

    const uint8_t* pixels = chars_.element(code);
    for (int py = 0; py < 8; py++) {
        const int sy = flip_y ? (7 - py) : py;
        size_t row = size_t((y * 8 + py) * 256 + x * 8);
        for (int px = 0; px < 8; px++) {
            const int sx = flip_x ? (7 - px) : px;
            const uint8_t pen = pixels[sy * 8 + sx];
            const uint8_t entry = char_lut_[size_t(pen + color)];
            tilemap_[row + size_t(px)] = palette_[entry];
        }
    }
}

void ShaolinsRoad::draw_sprite(int index) {
    const uint8_t attr = memory_[0x2800 + index * 2];
    const uint8_t pos_x = memory_[0x2801 + index * 2];
    const uint8_t pos_y = memory_[0x3000 + index * 2];
    const uint8_t code = memory_[0x3001 + index * 2];
    if (attr == 0 && code == 0) return;

    const int color = ((attr & 0x0f) + (0x10 * palette_bank_)) << 4;
    const bool flip_x = (attr & 0x80) != 0;
    const bool flip_y = (attr & 0x40) == 0;

    const uint8_t* pixels = sprites_.element(code);
    for (int y = 0; y < 16; y++) {
        const int source_y = flip_y ? (15 - y) : y;
        const size_t row = size_t(((pos_y + y) & 0xff) * 256);
        for (int x = 0; x < 16; x++) {
            const int source_x = flip_x ? (15 - x) : x;
            const uint8_t pen = pixels[source_y * 16 + source_x];
            if (pen == 0) continue;
            composite_[row + size_t((pos_x + x) & 0xff)] =
                palette_[sprite_lut_[size_t(pen + color)]];
        }
    }
}

void ShaolinsRoad::update_video() {
    for (int offset = 0; offset < 0x400; offset++) {
        if (dirty_[size_t(offset)]) {
            draw_tile(offset);
            dirty_[size_t(offset)] = false;
        }
    }

    for (int y = 0; y < 256; y++) {
        const size_t src_row = size_t(y * 256);
        const size_t dst_row = size_t(y * 256);
        for (int x = 0; x < 256; x++) {
            composite_[dst_row + size_t(x)] =
                tilemap_[src_row + size_t((x + scroll_) & 0xff)];
        }
    }

    for (int index = 0x17; index >= 0; index--) {
        draw_sprite(index);
    }

    for (int y = 0; y < kScreenHeight; y++) {
        for (int x = 0; x < kScreenWidth; x++) {
            uint32_t pixel = composite_[size_t(y * 256 + x + 16)];
            size_t target = flip_screen_
                                ? size_t((kScreenHeight - 1 - y) * kScreenWidth +
                                         (kScreenWidth - 1 - x))
                                : size_t(y * kScreenWidth + x);
            framebuffer_[target] = pixel;
        }
    }
}

void ShaolinsRoad::run_frame() {
    const int cycles_per_line = int(kMainClock / kFramesPerSecond / kScanlines);
    for (int line = 0; line < kScanlines; line++) {
        if (line == 240) {
            main_cpu_.set_irq(IrqLine::Hold);
            update_video();
        } else if (nmi_enable_ && (line & 0x1f) == 0) {
            main_cpu_.set_nmi(IrqLine::Pulse);
        }
        main_cpu_.run(cycles_per_line);
    }
}

void ShaolinsRoad::set_inputs(const MachineInputs& inputs) {
    const InputState& p1 = inputs.player1;
    const InputState& p2 = inputs.player2;
    in0_ = 0xff;
    in1_ = 0xff;
    in2_ = 0xff;

    if (inputs.coin1) in0_ &= 0xfe;
    if (inputs.coin2) in0_ &= 0xfd;
    if (p1.start) in0_ &= 0xf7;
    if (p2.start) in0_ &= 0xef;

    if (p1.left) in1_ &= 0xfe;
    if (p1.right) in1_ &= 0xfd;
    if (p1.up) in1_ &= 0xfb;
    if (p1.down) in1_ &= 0xf7;
    if (p1.button1) in1_ &= 0xef;
    if (p1.button2) in1_ &= 0xdf;

    if (p2.left) in2_ &= 0xfe;
    if (p2.right) in2_ &= 0xfd;
    if (p2.up) in2_ &= 0xfb;
    if (p2.down) in2_ &= 0xf7;
    if (p2.button1) in2_ &= 0xef;
    if (p2.button2) in2_ &= 0xdf;
}

void ShaolinsRoad::set_dip_switch(int bank, uint8_t value) {
    switch (bank) {
        case 0: dsw_a_ = value; break;
        case 1: dsw_b_ = value; break;
        case 2: dsw_c_ = value; break;
        default: break;
    }
}

void ShaolinsRoad::drain_audio(std::vector<int16_t>& out) {
    out.insert(out.end(), audio_.begin(), audio_.end());
    audio_.clear();
}

}  // namespace dsp
