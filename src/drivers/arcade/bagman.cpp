#include "drivers/arcade/bagman.h"

#include <algorithm>

#include "core/rom_loader.h"

namespace dsp {
namespace {

const std::vector<RomEntry> kMainRoms = {
    {"e9_b05.bin", 0x1000, 0x0000, 0xe0156191},
    {"f9_b06.bin", 0x1000, 0x1000, 0x7b758982},
    {"f9_b07.bin", 0x1000, 0x2000, 0x302a077b},
    {"k9_b08.bin", 0x1000, 0x3000, 0xf04293cb},
    {"m9_b09s.bin", 0x1000, 0x4000, 0x68e83e4f},
    {"n9_b10.bin", 0x1000, 0x5000, 0x1d6579f7},
};

const std::vector<RomEntry> kPaletteRoms = {
    {"p3.bin", 0x20, 0x00, 0x2a855523},
    {"r3.bin", 0x20, 0x20, 0xae6f1019},
};

const std::vector<RomEntry> kCharRoms = {
    {"e1_b02.bin", 0x1000, 0x0000, 0x4a0a6b55},
    {"j1_b04.bin", 0x1000, 0x1000, 0xc680ef04},
};

const std::vector<RomEntry> kSpriteRoms = {
    {"c1_b01.bin", 0x1000, 0x0000, 0x705193b2},
    {"f1_b03s.bin", 0x1000, 0x1000, 0xdba1eda7},
};

GfxLayout char_layout() {
    GfxLayout layout;
    layout.width = 8;
    layout.height = 8;
    layout.total = 512;
    layout.planes = 2;
    layout.char_increment = 8 * 8;
    layout.rotate_cw = true;
    layout.plane_offsets = {0, 512 * 8 * 8};
    layout.x_offsets = {0, 1, 2, 3, 4, 5, 6, 7};
    layout.y_offsets = {0, 8, 16, 24, 32, 40, 48, 56};
    return layout;
}

GfxLayout sprite_layout() {
    GfxLayout layout;
    layout.width = 16;
    layout.height = 16;
    layout.total = 128;
    layout.planes = 2;
    layout.char_increment = 32 * 8;
    layout.rotate_cw = true;
    layout.plane_offsets = {0, 128 * 16 * 16};
    layout.x_offsets = {0, 1, 2, 3, 4, 5, 6, 7, 64, 65, 66, 67, 68, 69, 70, 71};
    layout.y_offsets = {0,   8,   16,  24,  32,  40,  48,  56,
                        128, 136, 144, 152, 160, 168, 176, 184};
    return layout;
}

}  // namespace

Bagman::Bagman() : cpu_(kCpuClock), psg_(kAyClock) {
    framebuffer_.assign(size_t(kScreenWidth) * kScreenHeight, 0xff000000u);

    cpu_.set_memory_handlers([this](uint16_t address) { return read_byte(address); },
                             [this](uint16_t address, uint8_t value) { write_byte(address, value); });
    cpu_.set_io_handlers([this](uint16_t port) { return read_port(port); },
                         [this](uint16_t port, uint8_t value) { write_port(port, value); });
    cpu_.set_cycle_handler([this](int cycles) { on_cycles(cycles); });

    psg_.set_port_handlers([this] { return in0_; }, [this] { return in1_; }, nullptr, nullptr);
}

bool Bagman::init(const std::string& rom_path, std::string* error) {
    RomLoader loader;
    if (!loader.open(rom_path, error)) return false;

    std::vector<uint8_t> main_rom(0x6000, 0);
    if (!loader.load(kMainRoms, main_rom, error)) return false;
    std::copy(main_rom.begin(), main_rom.end(), memory_.begin());

    std::vector<uint8_t> char_rom(0x2000, 0);
    if (!loader.load(kCharRoms, char_rom, error)) return false;

    std::vector<uint8_t> sprite_rom(0x2000, 0);
    if (!loader.load(kSpriteRoms, sprite_rom, error)) return false;

    std::vector<uint8_t> prom(0x40, 0);
    if (!loader.load(kPaletteRoms, prom, error)) return false;

    decode_graphics(char_rom, sprite_rom);
    build_palette(prom);
    warnings_ = loader.warnings();

    reset();
    return true;
}

void Bagman::decode_graphics(const std::vector<uint8_t>& char_rom,
                             const std::vector<uint8_t>& sprite_rom) {
    // The character ROMs hold both the first character bank and the sprites,
    // the sprite ROMs hold the second character bank (see bagman_hw.pas).
    chars_.decode(char_layout(), char_rom);
    sprites_.decode(sprite_layout(), char_rom);
    chars_bank1_.decode(char_layout(), sprite_rom);
}

void Bagman::build_palette(const std::vector<uint8_t>& prom) {
    const std::vector<int> resistances_rg = {1000, 470, 220};
    const std::vector<int> resistances_b = {470, 220};
    auto weights = compute_resistor_weights(0, 255, -1.0,
                                            {{resistances_rg, 470, 0},
                                             {resistances_rg, 470, 0},
                                             {resistances_b, 470, 0}});

    palette_.fill(0xff000000u);
    for (size_t index = 0; index < 0x40; index++) {
        uint8_t data = prom[index];
        int red = combine_weights(weights[0], {(data >> 0) & 1, (data >> 1) & 1, (data >> 2) & 1});
        int green = combine_weights(weights[1], {(data >> 3) & 1, (data >> 4) & 1, (data >> 5) & 1});
        int blue = combine_weights(weights[2], {(data >> 6) & 1, (data >> 7) & 1});
        palette_[index] = 0xff000000u | (uint32_t(red) << 16) | (uint32_t(green) << 8) |
                          uint32_t(blue);
    }
}

void Bagman::reset() {
    cpu_.reset();
    psg_.reset();
    pal_.reset();
    irq_enable_ = true;
    video_enable_ = true;
    flip_screen_ = false;
    in0_ = 0xff;
    in1_ = 0xff;
    dirty_.fill(true);
    tilemap_.fill(0xff000000u);
    composite_.fill(0xff000000u);
    audio_accumulator_ = 0;
    audio_.clear();
}

uint8_t Bagman::read_byte(uint16_t address) {
    if (address <= 0x67ff || (address >= 0x9000 && address <= 0x93ff) ||
        (address >= 0x9800 && address <= 0x9bff) || address >= 0xc000) {
        return memory_[address];
    }
    if (address == 0xa000) return pal_.read();
    if (address == 0xb000) return dsw_;
    return 0xff;
}

void Bagman::write_byte(uint16_t address, uint8_t value) {
    if (address <= 0x5fff || address >= 0xc000) return;  // ROM
    if (address >= 0x6000 && address <= 0x67ff) {
        memory_[address] = value;
        return;
    }
    if ((address >= 0x9000 && address <= 0x93ff) || (address >= 0x9800 && address <= 0x9bff)) {
        if (memory_[address] != value) {
            dirty_[address & 0x3ff] = true;
            memory_[address] = value;
        }
        return;
    }
    switch (address) {
        case 0xa000:
            irq_enable_ = (value & 1) != 0;
            break;
        case 0xa001:
        case 0xa002:
            flip_screen_ = (value & 1) != 1;
            break;
        case 0xa003:
            if (video_enable_ != ((value & 1) != 0)) {
                video_enable_ = (value & 1) != 0;
                if (video_enable_) dirty_.fill(true);
            }
            break;
        default:
            if (address >= 0xa800 && address <= 0xa805) pal_.write(uint8_t(address & 7), value);
            break;
    }
}

uint8_t Bagman::read_port(uint16_t port) {
    if ((port & 0xff) == 0x0c) return psg_.read();
    return 0xff;
}

void Bagman::write_port(uint16_t port, uint8_t value) {
    switch (port & 0xff) {
        case 0x08: psg_.control(value); break;
        case 0x09: psg_.write(value); break;
        default: break;
    }
}

void Bagman::on_cycles(int cycles) {
    // One audio sample every kCpuClock / 44100 T states.
    audio_accumulator_ += int64_t(cycles) * AY8910::kSampleRate;
    while (audio_accumulator_ >= kCpuClock) {
        audio_accumulator_ -= kCpuClock;
        int32_t sample = psg_.update();
        audio_.push_back(int16_t(std::clamp(sample, -32768, 32767)));
    }
}

void Bagman::draw_tile(int offset) {
    uint8_t attrib = memory_[0x9800 + offset];
    int tile_x = 31 - (offset / 32);
    int tile_y = offset % 32;
    int code = memory_[0x9000 + offset] + ((attrib & 0x20) << 3);
    const GfxSet& gfx = ((attrib & 0x10) != 0) ? chars_bank1_ : chars_;
    int color = (attrib & 0x0f) << 2;

    const uint8_t* pixels = gfx.element(code);
    for (int y = 0; y < 8; y++) {
        uint32_t* target = &tilemap_[size_t((tile_y * 8 + y) * 256 + tile_x * 8)];
        for (int x = 0; x < 8; x++) {
            target[x] = palette_[size_t(pixels[y * 8 + x] + color)];
        }
    }
}

void Bagman::draw_sprite(int index) {
    const uint8_t* entry = &memory_[0x9800 + index * 4];
    uint8_t attrib = entry[0];
    int color = (entry[1] & 0x1f) << 2;
    int code = (attrib & 0x3f) + ((entry[1] & 0x20) << 1);
    int pos_x = entry[2];
    int pos_y = entry[3];
    if (pos_x == 0 || pos_y == 0) return;

    bool flip_x = (attrib & 0x80) != 0;
    bool flip_y = (attrib & 0x40) != 0;
    const uint8_t* pixels = sprites_.element(code);

    for (int y = 0; y < 16; y++) {
        int screen_y = pos_y - 1 + y;
        if (screen_y < 0 || screen_y >= 256) continue;
        int source_y = flip_y ? (15 - y) : y;
        for (int x = 0; x < 16; x++) {
            int source_x = flip_x ? (15 - x) : x;
            uint8_t pixel = pixels[source_y * 16 + source_x];
            if (pixel == 0) continue;  // transparent
            int screen_x = (pos_x + 1 + x) & 0xff;
            composite_[size_t(screen_y * 256 + screen_x)] = palette_[size_t(pixel + color)];
        }
    }
}

void Bagman::update_video() {
    if (video_enable_) {
        for (int offset = 0; offset < 0x400; offset++) {
            if (!dirty_[size_t(offset)]) continue;
            draw_tile(offset);
            dirty_[size_t(offset)] = false;
        }
        composite_ = tilemap_;
        for (int index = 7; index >= 0; index--) draw_sprite(index);
    } else {
        composite_.fill(0xff000000u);
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

void Bagman::run_frame() {
    const int cycles_per_line = int(kCpuClock / kFramesPerSecond / kScanlines);
    for (int line = 0; line < kScanlines; line++) {
        if (line == 240) {
            if (irq_enable_) cpu_.set_irq(IrqLine::Hold);
            update_video();
        }
        cpu_.run(cycles_per_line);
    }
}

void Bagman::set_inputs(const MachineInputs& inputs) {
    const InputState& player1 = inputs.player1;
    const InputState& player2 = inputs.player2;
    in0_ = 0xff;
    in1_ = 0xff;
    if (inputs.coin1) in0_ &= 0xfe;
    if (inputs.coin2) in0_ &= 0xfd;
    if (player1.start) in0_ &= 0xfb;
    if (player1.left) in0_ &= 0xf7;
    if (player1.right) in0_ &= 0xef;
    if (player1.up) in0_ &= 0xdf;
    if (player1.down) in0_ &= 0xbf;
    if (player1.button1) in0_ &= 0x7f;

    if (player2.start) in1_ &= 0xfb;
    if (player2.left) in1_ &= 0xf7;
    if (player2.right) in1_ &= 0xef;
    if (player2.up) in1_ &= 0xdf;
    if (player2.down) in1_ &= 0xbf;
    if (player2.button1) in1_ &= 0x7f;
}

void Bagman::set_dip_switch(int bank, uint8_t value) {
    if (bank == 0) dsw_ = value;
}

void Bagman::drain_audio(std::vector<int16_t>& out) {
    out.insert(out.end(), audio_.begin(), audio_.end());
    audio_.clear();
}

}  // namespace dsp
