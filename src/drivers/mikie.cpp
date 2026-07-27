#include "drivers/mikie.h"

#include <algorithm>

#include "core/rom_loader.h"

namespace dsp {
namespace {

const std::vector<RomEntry> kMainRoms = {
    {"n14.11c", 0x2000, 0x6000, 0xf698e6dd},
    {"o13.12a", 0x4000, 0x8000, 0x826e7035},
    {"o17.12d", 0x4000, 0xc000, 0x161c25c8},
};

const std::vector<RomEntry> kSoundRoms = {
    {"n10.6e", 0x2000, 0x0000, 0x2cf9d670},
};

const std::vector<RomEntry> kCharRoms = {
    {"o11.8i", 0x4000, 0x0000, 0x3c82aaf3},
};

const std::vector<RomEntry> kSpriteRoms = {
    {"001.f1", 0x4000, 0x0000, 0xa2ba0df5},
    {"003.f3", 0x4000, 0x4000, 0x9775ab32},
    {"005.h1", 0x4000, 0x8000, 0xba44aeef},
    {"007.h3", 0x4000, 0xc000, 0x31afc153},
};

const std::vector<RomEntry> kPaletteRoms = {
    {"d19.1i", 0x100, 0x000, 0x8b83e7cf},   // red
    {"d21.3i", 0x100, 0x100, 0x3556304a},   // green
    {"d20.2i", 0x100, 0x200, 0x676a0669},   // blue
    {"d22.12h", 0x100, 0x300, 0x872be05c},  // character lookup table
    {"d18.f9", 0x100, 0x400, 0x7396b374},   // sprite lookup table
};

GfxLayout char_layout() {
    GfxLayout layout;
    layout.width = 8;
    layout.height = 8;
    layout.total = 512;
    layout.planes = 4;
    layout.char_increment = 32 * 8;
    layout.rotate_cw = true;
    layout.plane_offsets = {0, 1, 2, 3};
    layout.x_offsets = {0 * 4, 1 * 4, 2 * 4, 3 * 4, 4 * 4, 5 * 4, 6 * 4, 7 * 4};
    layout.y_offsets = {0 * 4 * 8, 1 * 4 * 8, 2 * 4 * 8, 3 * 4 * 8,
                        4 * 4 * 8, 5 * 4 * 8, 6 * 4 * 8, 7 * 4 * 8};
    return layout;
}

// The sprite ROMs hold two banks of 256 elements with different plane offsets.
GfxLayout sprite_layout(int bank) {
    GfxLayout layout;
    layout.width = 16;
    layout.height = 16;
    layout.total = 256;
    layout.planes = 4;
    layout.char_increment = 128 * 8;
    layout.rotate_cw = true;
    layout.plane_offsets = {0 + bank * 8, 4 + bank * 8, bank * 8 + 0x8000 * 8,
                            bank * 8 + 0x8000 * 8 + 4};
    layout.x_offsets = {32 * 8 + 0, 32 * 8 + 1, 32 * 8 + 2, 32 * 8 + 3,
                        16 * 8 + 0, 16 * 8 + 1, 16 * 8 + 2, 16 * 8 + 3,
                        0,          1,          2,          3,
                        48 * 8 + 0, 48 * 8 + 1, 48 * 8 + 2, 48 * 8 + 3};
    layout.y_offsets = {0 * 16,  1 * 16,  2 * 16,  3 * 16,  4 * 16,  5 * 16,  6 * 16,  7 * 16,
                        32 * 16, 33 * 16, 34 * 16, 35 * 16, 36 * 16, 37 * 16, 38 * 16, 39 * 16};
    return layout;
}

constexpr uint32_t kTransparent = 0;

}  // namespace

Mikie::Mikie()
    : main_cpu_(kMainClock), sound_cpu_(kSoundClock), sn0_(kSn0Clock), sn1_(kSn1Clock) {
    framebuffer_.assign(size_t(kScreenWidth) * kScreenHeight, 0xff000000u);

    main_cpu_.set_memory_handlers(
        [this](uint16_t address) { return main_read(address); },
        [this](uint16_t address, uint8_t value) { main_write(address, value); });
    sound_cpu_.set_memory_handlers(
        [this](uint16_t address) { return sound_read(address); },
        [this](uint16_t address, uint8_t value) { sound_write(address, value); });
    sound_cpu_.set_io_handlers([](uint16_t) { return uint8_t(0xff); }, [](uint16_t, uint8_t) {});
    sound_cpu_.set_cycle_handler([this](int cycles) { on_sound_cycles(cycles); });
}

bool Mikie::init(const std::string& rom_path, std::string* error) {
    RomLoader loader;
    if (!loader.open(rom_path, error)) return false;

    std::vector<uint8_t> main_rom(0x10000, 0);
    if (!loader.load(kMainRoms, main_rom, error)) return false;
    std::copy(main_rom.begin(), main_rom.end(), memory_.begin());

    std::vector<uint8_t> sound_rom(0x2000, 0);
    if (!loader.load(kSoundRoms, sound_rom, error)) return false;
    std::copy(sound_rom.begin(), sound_rom.end(), sound_memory_.begin());

    std::vector<uint8_t> char_rom(0x4000, 0);
    if (!loader.load(kCharRoms, char_rom, error)) return false;

    std::vector<uint8_t> sprite_rom(0x10000, 0);
    if (!loader.load(kSpriteRoms, sprite_rom, error)) return false;

    std::vector<uint8_t> prom(0x500, 0);
    if (!loader.load(kPaletteRoms, prom, error)) return false;

    decode_graphics(char_rom, sprite_rom);
    build_palette(prom);
    warnings_ = loader.warnings();

    reset();
    return true;
}

void Mikie::decode_graphics(const std::vector<uint8_t>& char_rom,
                            const std::vector<uint8_t>& sprite_rom) {
    chars_.decode(char_layout(), char_rom);
    sprites_.create(16, 16, 512);
    for (int bank = 0; bank < 2; bank++) {
        sprites_.decode_elements(sprite_layout(bank), sprite_rom, bank * 256);
    }
}

void Mikie::build_palette(const std::vector<uint8_t>& prom) {
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

    // Colour lookup tables, one entry per (palette bank, colour, pen) triplet.
    for (int low = 0; low < 0x100; low++) {
        for (int bank = 0; bank < 8; bank++) {
            size_t index = size_t(low + (bank << 8));
            char_lut_[index] = uint8_t((prom[size_t(low) + 0x300] & 0x0f) + (bank << 5) + 16);
            sprite_lut_[index] = uint8_t((prom[size_t(low) + 0x400] & 0x0f) + (bank << 5));
        }
    }
}

void Mikie::reset() {
    main_cpu_.reset();
    sound_cpu_.reset();
    sn0_.reset();
    sn1_.reset();
    palette_bank_ = 0;
    sound_latch_ = 0;
    sound_irq_trigger_ = 0;
    irq_enable_ = false;
    flip_screen_ = false;
    in0_ = 0xff;
    in1_ = 0xff;
    in2_ = 0xff;
    dirty_.fill(true);
    background_.fill(0xff000000u);
    foreground_.fill(kTransparent);
    composite_.fill(0xff000000u);
    sound_cycles_ = 0;
    audio_accumulator_ = 0;
    audio_.clear();
}

uint8_t Mikie::main_read(uint16_t address) {
    switch (address) {
        case 0x2400: return in2_;
        case 0x2401: return in0_;
        case 0x2402: return in1_;
        case 0x2403: return dsw_c_;
        case 0x2500: return dsw_a_;
        case 0x2501: return dsw_b_;
        default: break;
    }
    if (address <= 0x00ff || address >= 0x2800) return memory_[address];
    return 0xff;
}

void Mikie::main_write(uint16_t address, uint8_t value) {
    if (address <= 0x00ff || (address >= 0x2800 && address <= 0x37ff)) {
        memory_[address] = value;
        return;
    }
    if (address >= 0x3800 && address <= 0x3fff) {  // video RAM: attributes and codes
        if (memory_[address] != value) {
            dirty_[address & 0x3ff] = true;
            memory_[address] = value;
        }
        return;
    }
    switch (address) {
        case 0x2002:  // sound CPU interrupt on a 0 -> 1 transition
            if (sound_irq_trigger_ == 0 && value == 1) sound_cpu_.set_irq(IrqLine::Hold);
            sound_irq_trigger_ = value;
            break;
        case 0x2006: flip_screen_ = (value & 1) != 0; break;
        case 0x2007:
            irq_enable_ = value != 0;
            if (!irq_enable_) main_cpu_.set_irq(IrqLine::Clear);
            break;
        case 0x2100: break;  // watchdog
        case 0x2200:
            if (palette_bank_ != (value & 7)) {
                palette_bank_ = uint8_t(value & 7);
                dirty_.fill(true);
            }
            break;
        case 0x2400: sound_latch_ = value; break;
        default: break;  // 0x4000-0xffff is ROM
    }
}

uint8_t Mikie::sound_read(uint16_t address) {
    if (address <= 0x43ff) return sound_memory_[address];
    if (address == 0x8003) return sound_latch_;
    // Free running timer built from the sound CPU cycle counter.
    if (address == 0x8005) return uint8_t(sound_cycles_ >> 9);
    return 0xff;
}

void Mikie::sound_write(uint16_t address, uint8_t value) {
    if (address >= 0x4000 && address <= 0x43ff) {
        sound_memory_[address] = value;
        return;
    }
    if (address == 0x8002) sn0_.write(value);
    if (address == 0x8004) sn1_.write(value);
}

void Mikie::on_sound_cycles(int cycles) {
    sound_cycles_ += uint64_t(cycles);
    audio_accumulator_ += int64_t(cycles) * SN76496::kSampleRate;
    while (audio_accumulator_ >= kSoundClock) {
        audio_accumulator_ -= kSoundClock;
        int32_t sample = (sn0_.update() + sn1_.update()) / 2;
        audio_.push_back(int16_t(std::clamp(sample, -32768, 32767)));
    }
}

void Mikie::draw_tile(int offset) {
    uint8_t attrib = memory_[0x3800 + offset];
    int tile_x = offset / 32;
    int tile_y = 31 - (offset % 32);
    int color = ((attrib & 0x0f) + 0x10 * palette_bank_) << 4;
    int code = memory_[0x3c00 + offset] + ((attrib & 0x20) << 3);
    bool flip_x = (attrib & 0x80) == 0;
    bool flip_y = (attrib & 0x40) == 0;
    bool above_sprites = (attrib & 0x10) != 0;

    const uint8_t* pixels = chars_.element(code);
    for (int y = 0; y < 8; y++) {
        int source_y = flip_y ? (7 - y) : y;
        size_t row = size_t((tile_y * 8 + y) * 256 + tile_x * 8);
        for (int x = 0; x < 8; x++) {
            int source_x = flip_x ? (7 - x) : x;
            uint8_t entry = char_lut_[size_t(pixels[source_y * 8 + source_x] + color)];
            background_[row + size_t(x)] = palette_[entry];
            // Pixels of a priority tile are drawn again on top of the sprites.
            foreground_[row + size_t(x)] =
                (above_sprites && entry != 0) ? palette_[entry] : kTransparent;
        }
    }
}

void Mikie::draw_sprite(int index) {
    const uint8_t* entry = &memory_[0x2800 + index * 4];
    uint8_t attrib = entry[0];
    uint8_t code_byte = entry[2];
    int code = ((attrib & 0x40) << 1) + (code_byte & 0x3f) + ((code_byte & 0x80) >> 1) +
               ((code_byte & 0x40) << 2);
    int color = ((attrib & 0x0f) + 0x10 * palette_bank_) << 4;
    int pos_x = (244 - entry[1]) & 0xff;
    bool flip_x = (attrib & 0x20) == 0;
    bool flip_y;
    int pos_y;
    if (!flip_screen_) {
        pos_y = (240 - entry[3]) & 0xff;
        flip_y = (attrib & 0x10) != 0;
    } else {
        pos_y = entry[3];
        flip_y = (attrib & 0x10) == 0;
        pos_x = (pos_x - 2) & 0xff;
    }

    const uint8_t* pixels = sprites_.element(code);
    for (int y = 0; y < 16; y++) {
        int source_y = flip_y ? (15 - y) : y;
        size_t row = size_t(((pos_y + y) & 0xff) * 256);
        for (int x = 0; x < 16; x++) {
            int source_x = flip_x ? (15 - x) : x;
            uint8_t pen = pixels[source_y * 16 + source_x];
            if (pen == 0) continue;  // transparent
            composite_[row + size_t((pos_x + x) & 0xff)] =
                palette_[sprite_lut_[size_t(pen + color)]];
        }
    }
}

void Mikie::update_video() {
    for (int offset = 0x3ff; offset >= 0; offset--) {
        if (!dirty_[size_t(offset)]) continue;
        draw_tile(offset);
        dirty_[size_t(offset)] = false;
    }

    composite_ = background_;
    for (int index = 0; index < 0x24; index++) draw_sprite(index);
    for (size_t index = 0; index < composite_.size(); index++) {
        if (foreground_[index] != kTransparent) composite_[index] = foreground_[index];
    }

    // Visible area: 224x256 starting at x = 16 of the 256x256 work surface.
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

void Mikie::run_frame() {
    const int main_cycles = int(kMainClock / kFramesPerSecond / kScanlines);
    const int sound_cycles = int(kSoundClock / kFramesPerSecond / kScanlines);
    for (int line = 0; line < kScanlines; line++) {
        if (line == 240) {
            if (irq_enable_) main_cpu_.set_irq(IrqLine::Assert);
            update_video();
        }
        main_cpu_.run(main_cycles);
        sound_cpu_.run(sound_cycles);
    }
}

void Mikie::set_inputs(const MachineInputs& inputs) {
    const InputState& player1 = inputs.player1;
    const InputState& player2 = inputs.player2;
    in0_ = 0xff;
    in1_ = 0xff;
    in2_ = 0xff;
    if (player1.left) in0_ &= 0xfe;
    if (player1.right) in0_ &= 0xfd;
    if (player1.up) in0_ &= 0xfb;
    if (player1.down) in0_ &= 0xf7;
    if (player1.button1) in0_ &= 0xef;
    if (player1.button2) in0_ &= 0xdf;

    if (player2.left) in1_ &= 0xfe;
    if (player2.right) in1_ &= 0xfd;
    if (player2.up) in1_ &= 0xfb;
    if (player2.down) in1_ &= 0xf7;
    if (player2.button1) in1_ &= 0xef;
    if (player2.button2) in1_ &= 0xdf;

    if (inputs.coin1) in2_ &= 0xfe;
    if (inputs.coin2) in2_ &= 0xfd;
    if (player1.start) in2_ &= 0xf7;
    if (player2.start) in2_ &= 0xef;
}

void Mikie::set_dip_switch(int bank, uint8_t value) {
    switch (bank) {
        case 0: dsw_a_ = value; break;
        case 1: dsw_b_ = value; break;
        case 2: dsw_c_ = value; break;
        default: break;
    }
}

void Mikie::drain_audio(std::vector<int16_t>& out) {
    out.insert(out.end(), audio_.begin(), audio_.end());
    audio_.clear();
}

}  // namespace dsp
