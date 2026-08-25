#include "drivers/trackfld.h"

#include <algorithm>

#include "core/rom_loader.h"
#include "machine/konami_decrypt.h"

namespace dsp {
namespace {

const std::vector<RomEntry> kMainRoms = {
    {"a01_e01.bin", 0x2000, 0x6000, 0x2882f6d4},
    {"a02_e02.bin", 0x2000, 0x8000, 0x1743b5ee},
    {"a03_k03.bin", 0x2000, 0xa000, 0x6c0d1ee9},
    {"a04_e04.bin", 0x2000, 0xc000, 0x21d6c448},
    {"a05_e05.bin", 0x2000, 0xe000, 0xf08c7b7e},
};

const std::vector<RomEntry> kSoundRoms = {
    {"c2_d13.bin|gold.2d|361-d13.c03", 0x2000, 0x0000, 0x95bf79b6},
};

const std::vector<RomEntry> kCharRoms = {
    {"h16_e12.bin|gold.2k", 0x2000, 0x0000, 0x50075768},
    {"h15_e11.bin|gold.4k", 0x2000, 0x2000, 0xdda9e29f},
    {"h14_e10.bin|gold.5k", 0x2000, 0x4000, 0xc2166a5c},
};

const std::vector<RomEntry> kSpriteRoms = {
    {"c11_d06.bin|gold.20a|361_d06.a20", 0x2000, 0x0000, 0x82e2185a},
    {"c12_d07.bin|gold.21a|361_d07.a21", 0x2000, 0x2000, 0x800ff1f1},
    {"c13_d08.bin|gold.17a", 0x2000, 0x4000, 0xd9faf183},
    {"c14_d09.bin|gold.19a", 0x2000, 0x6000, 0x5886c802},
};

const std::vector<RomEntry> kVlmRoms = {
    {"c9_d15.bin|gold.d9", 0x2000, 0x0000, 0xf546a56b},
};

const std::vector<RomEntry> kPaletteRoms = {
    {"361b16.f1|gold.2g", 0x20, 0x000, 0xd55f30b5},
    {"361b17.b16|gold.18d", 0x100, 0x020, 0xd2ba4d32},
    {"361b18.e15|gold.4j", 0x100, 0x120, 0x053e5861},
};

GfxLayout char_layout() {
    GfxLayout layout;
    layout.width = 8;
    layout.height = 8;
    layout.total = 0x300;
    layout.planes = 4;
    layout.char_increment = 32 * 8;
    layout.plane_offsets = {0, 1, 2, 3};
    layout.x_offsets = {0 * 4, 1 * 4, 2 * 4, 3 * 4, 4 * 4, 5 * 4, 6 * 4, 7 * 4};
    layout.y_offsets = {0 * 32, 1 * 32, 2 * 32, 3 * 32, 4 * 32, 5 * 32, 6 * 32, 7 * 32};
    return layout;
}

GfxLayout sprite_layout() {
    GfxLayout layout;
    layout.width = 16;
    layout.height = 16;
    layout.total = 0x100;
    layout.planes = 4;
    layout.char_increment = 64 * 8;
    layout.plane_offsets = {0x100 * 64 * 8 + 4, 0x100 * 64 * 8, 4, 0};
    layout.x_offsets = {0,         1,         2,         3,         8 * 8 + 0,  8 * 8 + 1,
                        8 * 8 + 2, 8 * 8 + 3, 16 * 8 + 0, 16 * 8 + 1, 16 * 8 + 2, 16 * 8 + 3,
                        24 * 8 + 0, 24 * 8 + 1, 24 * 8 + 2, 24 * 8 + 3};
    layout.y_offsets = {0 * 8,  1 * 8,  2 * 8,  3 * 8,  4 * 8,  5 * 8,  6 * 8,  7 * 8,
                        32 * 8, 33 * 8, 34 * 8, 35 * 8, 36 * 8, 37 * 8, 38 * 8, 39 * 8};
    return layout;
}

}  // namespace

TrackFld::TrackFld()
    : main_cpu_(kMainClock),
      sound_cpu_(kSoundClock),
      sn_(kSnClock),
      vlm_(kVlmClock, 0x2000, 4.0f),
      dac_(0.80f) {
    framebuffer_.assign(size_t(kScreenWidth) * kScreenHeight, 0xff000000u);

    main_cpu_.set_memory_handlers(
        [this](uint16_t address) { return main_read(address); },
        [this](uint16_t address, uint8_t value) { main_write(address, value); });
    main_cpu_.set_opcode_read([this](uint16_t address) { return main_opcode_read(address); });
    sound_cpu_.set_memory_handlers(
        [this](uint16_t address) { return sound_read(address); },
        [this](uint16_t address, uint8_t value) { sound_write(address, value); });
    sound_cpu_.set_io_handlers([](uint16_t) { return uint8_t(0xff); }, [](uint16_t, uint8_t) {});
    sound_cpu_.set_cycle_handler([this](int cycles) { on_sound_cycles(cycles); });
}

bool TrackFld::init(const std::string& rom_path, std::string* error) {
    RomLoader loader;
    if (!loader.open(rom_path, error)) return false;

    std::vector<uint8_t> main_rom(0x10000, 0);
    if (!loader.load(kMainRoms, main_rom, error)) return false;
    std::copy(main_rom.begin(), main_rom.end(), memory_.begin());
    konami1_decode(memory_.data() + 0x6000, opcodes_.data(), 0xa000);

    std::vector<uint8_t> sound_rom(0x2000, 0);
    if (!loader.load(kSoundRoms, sound_rom, error)) return false;
    std::copy(sound_rom.begin(), sound_rom.end(), sound_memory_.begin());

    std::vector<uint8_t> char_rom(0x6000, 0);
    if (!loader.load(kCharRoms, char_rom, error)) return false;

    std::vector<uint8_t> sprite_rom(0x8000, 0);
    if (!loader.load(kSpriteRoms, sprite_rom, error)) return false;

    std::vector<uint8_t> vlm_rom(0x2000, 0);
    if (!loader.load(kVlmRoms, vlm_rom, error)) return false;
    vlm_.set_rom(vlm_rom);

    std::vector<uint8_t> prom(0x220, 0);
    if (!loader.load(kPaletteRoms, prom, error)) return false;

    decode_graphics(char_rom, sprite_rom);
    build_palette(prom);
    warnings_ = loader.warnings();

    reset();
    return true;
}

void TrackFld::decode_graphics(const std::vector<uint8_t>& char_rom,
                               const std::vector<uint8_t>& sprite_rom) {
    chars_.decode(char_layout(), char_rom);
    sprites_.decode(sprite_layout(), sprite_rom);
}

void TrackFld::build_palette(const std::vector<uint8_t>& prom) {
    const std::vector<int> resistances_rg = {1000, 470, 220};
    const std::vector<int> resistances_b = {470, 220};
    auto weights = compute_resistor_weights(0, 255, -1.0,
                                            {{resistances_rg, 0, 1000},
                                             {resistances_rg, 0, 1000},
                                             {resistances_b, 0, 1000}});

    for (size_t index = 0; index < 0x20; index++) {
        uint8_t data = prom[index];
        int red = combine_weights(weights[0], {(data >> 0) & 1, (data >> 1) & 1, (data >> 2) & 1});
        int green = combine_weights(weights[1], {(data >> 3) & 1, (data >> 4) & 1, (data >> 5) & 1});
        int blue = combine_weights(weights[2], {(data >> 6) & 1, (data >> 7) & 1});
        palette_[index] = 0xff000000u | (uint32_t(red) << 16) | (uint32_t(green) << 8) |
                          uint32_t(blue);
    }

    for (int index = 0; index < 0x100; index++) {
        char_lut_[size_t(index)] = uint8_t((prom[size_t(0x120 + index)] & 0x0f) | 0x10);
        sprite_lut_[size_t(index)] = uint8_t(prom[size_t(0x20 + index)] & 0x0f);
    }
}

void TrackFld::reset() {
    main_cpu_.reset();
    sound_cpu_.reset();
    sn_.reset();
    vlm_.reset();
    dac_.reset();
    sound_latch_ = 0;
    chip_latch_ = 0;
    sound_irq_trigger_ = 0;
    last_addr_ = 0;
    irq_enable_ = false;
    flip_screen_ = false;
    in0_ = 0xff;
    in1_ = 0xff;
    in2_ = 0xff;
    dirty_.fill(true);
    tilemap_.fill(0xff000000u);
    composite_.fill(0xff000000u);
    scroll_x_.fill(0);
    sound_cycles_ = 0;
    vlm_cycle_acc_ = 0;
    audio_accumulator_ = 0;
    audio_.clear();
}

uint8_t TrackFld::main_opcode_read(uint16_t address) {
    if (address >= 0x6000) return opcodes_[address - 0x6000];
    return main_read(address);
}

uint8_t TrackFld::main_read(uint16_t address) {
    if (address >= 0x1200 && address <= 0x127f) return dsw_b_;
    if (address >= 0x1280 && address <= 0x128f) {
        switch (address & 3) {
            case 0: return in2_;
            case 1: return in0_;
            case 2: return in1_;
            default: return dsw_a_;
        }
    }
    if ((address >= 0x1800 && address <= 0x1fff) || (address >= 0x2800 && address <= 0x3fff)) {
        return memory_[address];
    }
    if (address >= 0x6000) return memory_[address];
    return 0;
}

void TrackFld::update_scroll(uint16_t address) {
    uint16_t row = address & 0x1f;
    scroll_x_[row] = uint16_t(memory_[0x1840 + row] + ((memory_[0x1c40 + row] & 1) << 8));
}

void TrackFld::main_write(uint16_t address, uint8_t value) {
    if (address >= 0x1080 && address <= 0x10ff) {
        switch (address & 7) {
            case 0: flip_screen_ = (value & 1) != 0; break;
            case 1:
                if (sound_irq_trigger_ == 0 && value != 0) sound_cpu_.set_irq(IrqLine::Hold);
                sound_irq_trigger_ = value;
                break;
            case 7: irq_enable_ = value != 0; break;
            default: break;
        }
        return;
    }
    if (address >= 0x1100 && address <= 0x117f) {
        sound_latch_ = value;
        return;
    }
    if ((address >= 0x1840 && address <= 0x185f) || (address >= 0x1c40 && address <= 0x1c5f)) {
        memory_[address] = value;
        update_scroll(address);
        return;
    }
    if ((address >= 0x1800 && address <= 0x183f) || (address >= 0x1860 && address <= 0x1c3f) ||
        (address >= 0x1c60 && address <= 0x1fff) || (address >= 0x2800 && address <= 0x2fff)) {
        memory_[address] = value;
        return;
    }
    if (address >= 0x3000 && address <= 0x3fff) {
        dirty_[address & 0x7ff] = true;
        memory_[address] = value;
        return;
    }
}

uint8_t TrackFld::sound_read(uint16_t address) {
    if (address <= 0x1fff) return sound_memory_[address];
    if (address >= 0x4000 && address <= 0x5fff) return sound_memory_[0x4000 + (address & 0x3ff)];
    if (address >= 0x6000 && address <= 0x7fff) return sound_latch_;
    if (address >= 0x8000 && address <= 0x9fff) return uint8_t((sound_cycles_ >> 10) & 0x0f);
    if (address >= 0xe000 && (address & 7) == 2) return uint8_t(vlm_.get_bsy() << 4);
    return 0;
}

void TrackFld::sound_write(uint16_t address, uint8_t value) {
    if (address >= 0x4000 && address <= 0x4fff) {
        sound_memory_[0x4000 + (address & 0x3ff)] = value;
        return;
    }
    if (address >= 0xa000 && address <= 0xbfff) {
        chip_latch_ = value;
        return;
    }
    if (address >= 0xc000 && address <= 0xdfff) {
        sn_.write(chip_latch_);
        return;
    }
    if (address >= 0xe000) {
        switch (address & 7) {
            case 0: dac_.data8_w(value); break;
            case 3: {
                uint16_t offset = address & 0x3ff;
                uint16_t changes = uint16_t(offset ^ last_addr_);
                if (changes & 0x100) vlm_.set_st(uint8_t((offset & 0x100) >> 8));
                if (changes & 0x200) vlm_.set_rst(uint8_t((offset & 0x200) >> 9));
                last_addr_ = offset;
                break;
            }
            case 4: vlm_.data_w(value); break;
            default: break;
        }
    }
}

void TrackFld::on_sound_cycles(int cycles) {
    sound_cycles_ += uint64_t(cycles);
    const int vlm_period = vlm_.cycles_per_sample(kSoundClock);
    vlm_cycle_acc_ += cycles;
    while (vlm_cycle_acc_ >= vlm_period) {
        vlm_cycle_acc_ -= vlm_period;
        vlm_.update_stream();
    }
    audio_accumulator_ += int64_t(cycles) * SN76496::kSampleRate;
    while (audio_accumulator_ >= int64_t(kSoundClock)) {
        audio_accumulator_ -= int64_t(kSoundClock);
        int32_t sample = sn_.update() + dac_.update() + vlm_.update();
        audio_.push_back(int16_t(std::clamp(sample, int32_t(-32768), int32_t(32767))));
    }
}

void TrackFld::draw_tile(int offset) {
    uint8_t attrib = memory_[0x3800 + offset];
    int tile_x = offset % 64;
    int tile_y = offset / 64;
    int color = (attrib & 0x0f) << 4;
    int code = memory_[0x3000 + offset] + ((attrib & 0xc0) << 2);
    bool flip_x = (attrib & 0x10) != 0;
    bool flip_y = (attrib & 0x20) != 0;

    const uint8_t* pixels = chars_.element(code);
    for (int y = 0; y < 8; y++) {
        int source_y = flip_y ? (7 - y) : y;
        size_t row = size_t((tile_y * 8 + y) * 512 + tile_x * 8);
        for (int x = 0; x < 8; x++) {
            int source_x = flip_x ? (7 - x) : x;
            uint8_t entry = char_lut_[size_t(pixels[source_y * 8 + source_x] + color)];
            tilemap_[row + size_t(x)] = palette_[entry];
        }
    }
}

void TrackFld::draw_sprite(int index) {
    uint8_t attrib = memory_[0x1800 + index * 2];
    int code = memory_[0x1c01 + index * 2] + ((attrib & 0x20) << 3);
    int color = (attrib & 0x0f) << 4;
    int pos_y = (241 - memory_[0x1801 + index * 2]) & 0xff;
    int pos_x = (memory_[0x1c00 + index * 2] - 1) & 0xff;
    bool flip_x = (attrib & 0x40) == 0;
    bool flip_y = (attrib & 0x80) != 0;

    const uint8_t* pixels = sprites_.element(code);
    for (int y = 0; y < 16; y++) {
        int source_y = flip_y ? (15 - y) : y;
        size_t row = size_t(((pos_y + y) & 0xff) * 256);
        for (int x = 0; x < 16; x++) {
            int source_x = flip_x ? (15 - x) : x;
            uint8_t pen = pixels[source_y * 16 + source_x];
            if (pen == 0) continue;
            composite_[row + size_t((pos_x + x) & 0xff)] =
                palette_[sprite_lut_[size_t(pen + color)]];
        }
    }
}

void TrackFld::update_video() {
    for (int offset = 0; offset < 0x800; offset++) {
        if (!dirty_[size_t(offset)]) continue;
        draw_tile(offset);
        dirty_[size_t(offset)] = false;
    }

    for (int y = 0; y < 256; y++) {
        int scroll = scroll_x_[size_t(y / 8)] & 0x1ff;
        size_t dest_row = size_t(y * 256);
        size_t src_row = size_t(y * 512);
        for (int x = 0; x < 256; x++) {
            composite_[dest_row + size_t(x)] = tilemap_[src_row + size_t((scroll + x) & 0x1ff)];
        }
    }

    for (int index = 0x1f; index >= 0; index--) draw_sprite(index);

    for (int y = 0; y < kScreenHeight; y++) {
        int source_y = y + 16;
        for (int x = 0; x < kScreenWidth; x++) {
            uint32_t pixel = composite_[size_t(source_y * 256 + x)];
            size_t target = flip_screen_ ? size_t((kScreenHeight - 1 - y) * kScreenWidth +
                                                  (kScreenWidth - 1 - x))
                                         : size_t(y * kScreenWidth + x);
            framebuffer_[target] = pixel;
        }
    }
}

void TrackFld::run_frame() {
    const int main_cycles = int(kMainClock / kFramesPerSecond / kScanlines);
    const int sound_cycles = int(kSoundClock / kFramesPerSecond / kScanlines);
    for (int line = 0; line < kScanlines; line++) {
        if (line == 240) {
            if (irq_enable_) main_cpu_.set_irq(IrqLine::Hold);
            update_video();
        }
        main_cpu_.run(main_cycles);
        sound_cpu_.run(sound_cycles);
    }
}

void TrackFld::set_inputs(const MachineInputs& inputs) {
    const InputState& player1 = inputs.player1;
    const InputState& player2 = inputs.player2;
    in0_ = 0xff;
    in1_ = 0xff;
    in2_ = 0xff;
    if (player1.button1) in0_ &= 0xfe;
    if (player1.button2) in0_ &= 0xfd;
    if (player1.button3) in0_ &= 0xfb;
    if (player2.button1) in0_ &= 0xef;
    if (player2.button2) in0_ &= 0xdf;
    if (player2.button3) in0_ &= 0xbf;

    if (inputs.coin1) in2_ &= 0xfe;
    if (inputs.coin2) in2_ &= 0xfd;
    if (player1.start) in2_ &= 0xf7;
    if (player2.start) in2_ &= 0xef;
}

void TrackFld::set_dip_switch(int bank, uint8_t value) {
    switch (bank) {
        case 0: dsw_a_ = value; break;
        case 1: dsw_b_ = value; break;
        default: break;
    }
}

void TrackFld::drain_audio(std::vector<int16_t>& out) {
    out.insert(out.end(), audio_.begin(), audio_.end());
    audio_.clear();
}

}  // namespace dsp
