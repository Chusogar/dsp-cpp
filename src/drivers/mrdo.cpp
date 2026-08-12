#include "drivers/mrdo.h"

#include <algorithm>
#include <cmath>

#include "core/rom_loader.h"

namespace dsp {
namespace {

const std::vector<RomEntry> kMainRoms = {
    {"a4-01.bin", 0x2000, 0x0000, 0x03dcfba2},
    {"c4-02.bin", 0x2000, 0x2000, 0x0ecdd39c},
    {"e4-03.bin", 0x2000, 0x4000, 0x358f5dc2},
    {"f4-04.bin", 0x2000, 0x6000, 0xf4190cfc},
};

const std::vector<RomEntry> kPaletteRoms = {
    {"u02--2.bin", 0x20, 0x00, 0x238a65d7},
    {"t02--3.bin", 0x20, 0x20, 0xae263dc0},
    {"f10--1.bin", 0x20, 0x40, 0x16ee4ca2},
};

const std::vector<RomEntry> kChar1Roms = {
    {"s8-09.bin", 0x1000, 0x0000, 0xaa80c5b6},
    {"u8-10.bin", 0x1000, 0x1000, 0xd20ec85b},
};

const std::vector<RomEntry> kChar2Roms = {
    {"r8-08.bin", 0x1000, 0x0000, 0xdbdc9ffa},
    {"n8-07.bin", 0x1000, 0x1000, 0x4b9973db},
};

const std::vector<RomEntry> kSpriteRoms = {
    {"h5-05.bin", 0x1000, 0x0000, 0xe1218cc5},
    {"k5-06.bin", 0x1000, 0x1000, 0xb1f68b04},
};

// convert_gfx(..., flipx=false, flipy=true): reverse the Y offsets.
GfxLayout char_layout() {
    GfxLayout layout;
    layout.width = 8;
    layout.height = 8;
    layout.total = 512;
    layout.planes = 2;
    layout.char_increment = 8 * 8;
    layout.plane_offsets = {0, 512 * 8 * 8};
    layout.x_offsets = {7, 6, 5, 4, 3, 2, 1, 0};
    layout.y_offsets = {7 * 8, 6 * 8, 5 * 8, 4 * 8, 3 * 8, 2 * 8, 1 * 8, 0 * 8};
    return layout;
}

GfxLayout sprite_layout() {
    GfxLayout layout;
    layout.width = 16;
    layout.height = 16;
    layout.total = 128;
    layout.planes = 2;
    layout.char_increment = 64 * 8;
    layout.plane_offsets = {4, 0};
    layout.x_offsets = {3,     2,     1,     0,     8 + 3, 8 + 2, 8 + 1, 8 + 0,
                        16 + 3, 16 + 2, 16 + 1, 16 + 0, 24 + 3, 24 + 2, 24 + 1, 24 + 0};
    layout.y_offsets = {30 * 16, 28 * 16, 26 * 16, 24 * 16, 22 * 16, 20 * 16, 18 * 16, 16 * 16,
                        14 * 16, 12 * 16, 10 * 16, 8 * 16,  6 * 16,  4 * 16,  2 * 16,  0 * 16};
    return layout;
}

constexpr uint32_t kTransparent = 0;

}  // namespace

MrDo::MrDo() : cpu_(kCpuClock), sn0_(kCpuClock), sn1_(kCpuClock) {
    framebuffer_.assign(size_t(kScreenWidth) * kScreenHeight, 0xff000000u);

    cpu_.set_memory_handlers([this](uint16_t address) { return read_byte(address); },
                             [this](uint16_t address, uint8_t value) { write_byte(address, value); });
    cpu_.set_cycle_handler([this](int cycles) { on_cycles(cycles); });
}

bool MrDo::init(const std::string& rom_path, std::string* error) {
    RomLoader loader;
    if (!loader.open(rom_path, error)) return false;

    std::vector<uint8_t> main_rom(0x8000, 0);
    if (!loader.load(kMainRoms, main_rom, error)) return false;
    std::copy(main_rom.begin(), main_rom.end(), memory_.begin());

    std::vector<uint8_t> char1_rom(0x2000, 0);
    if (!loader.load(kChar1Roms, char1_rom, error)) return false;

    std::vector<uint8_t> char2_rom(0x2000, 0);
    if (!loader.load(kChar2Roms, char2_rom, error)) return false;

    std::vector<uint8_t> sprite_rom(0x2000, 0);
    if (!loader.load(kSpriteRoms, sprite_rom, error)) return false;

    std::vector<uint8_t> prom(0x60, 0);
    if (!loader.load(kPaletteRoms, prom, error)) return false;

    decode_graphics(char1_rom, char2_rom, sprite_rom);
    build_palette(prom);
    warnings_ = loader.warnings();

    reset();
    return true;
}

void MrDo::decode_graphics(const std::vector<uint8_t>& char1_rom,
                           const std::vector<uint8_t>& char2_rom,
                           const std::vector<uint8_t>& sprite_rom) {
    chars_fg_.decode(char_layout(), char1_rom);
    chars_bg_.decode(char_layout(), char2_rom);
    sprites_.decode(sprite_layout(), sprite_rom);
}

void MrDo::build_palette(const std::vector<uint8_t>& prom) {
    // Port of calc_paleta from mrdo_hw.pas: diode / resistor DAC.
    constexpr double kR1 = 150.0;
    constexpr double kR2 = 120.0;
    constexpr double kR3 = 100.0;
    constexpr double kR4 = 75.0;
    constexpr double kPull = 220.0;
    constexpr double kPotAdjust = 0.7;

    std::array<double, 16> weight{};
    std::array<double, 16> pot{};
    for (int f = 15; f >= 0; f--) {
        double par = 0.0;
        if ((f & 1) != 0) par += 1.0 / kR1;
        if ((f & 2) != 0) par += 1.0 / kR2;
        if ((f & 4) != 0) par += 1.0 / kR3;
        if ((f & 8) != 0) par += 1.0 / kR4;
        if (par != 0.0) {
            par = 1.0 / par;
            pot[size_t(f)] = kPull / (kPull + par) - kPotAdjust;
        } else {
            pot[size_t(f)] = 0.0;
        }
        weight[size_t(f)] = 255.0 * pot[size_t(f)] / pot[15];
        if (weight[size_t(f)] < 0.0) weight[size_t(f)] = 0.0;
    }

    for (int f = 0; f < 0x100; f++) {
        const int a1 = ((f >> 3) & 0x1c) + (f & 3) + 0x20;
        const int a2 = ((f >> 0) & 0x1c) + (f & 3);
        int bits0 = (prom[size_t(a1)] >> 0) & 3;
        int bits1 = (prom[size_t(a2)] >> 0) & 3;
        const int red = int(std::trunc(weight[size_t(bits0 + (bits1 << 2))]));
        bits0 = (prom[size_t(a1)] >> 2) & 3;
        bits1 = (prom[size_t(a2)] >> 2) & 3;
        const int green = int(std::trunc(weight[size_t(bits0 + (bits1 << 2))]));
        bits0 = (prom[size_t(a1)] >> 4) & 3;
        bits1 = (prom[size_t(a2)] >> 4) & 3;
        const int blue = int(std::trunc(weight[size_t(bits0 + (bits1 << 2))]));
        palette_[size_t(f)] =
            0xff000000u | (uint32_t(red) << 16) | (uint32_t(green) << 8) | uint32_t(blue);
    }

    // Sprite CLUT from the third PROM (offset 0x40).
    for (int f = 0; f < 0x40; f++) {
        uint8_t bits0 = prom[size_t(0x40 + (f & 0x1f))];
        if ((f & 0x20) != 0) bits0 = uint8_t(bits0 >> 4);
        else bits0 = uint8_t(bits0 & 0x0f);
        sprite_lut_[size_t(f)] = uint8_t(bits0 + ((bits0 & 0x0c) << 3));
    }
}

void MrDo::reset() {
    cpu_.reset();
    sn0_.reset();
    sn1_.reset();
    scroll_x_ = 0;
    scroll_y_ = 0;
    flip_screen_ = false;
    in0_ = 0xff;
    in1_ = 0xff;
    dirty_bg_.fill(true);
    dirty_fg_.fill(true);
    bg_opaque_.fill(0xff000000u);
    fg_opaque_.fill(kTransparent);
    bg_trans_.fill(kTransparent);
    fg_trans_.fill(kTransparent);
    composite_.fill(0xff000000u);
    audio_accumulator_ = 0;
    audio_.clear();
}

uint8_t MrDo::read_byte(uint16_t address) {
    if (address <= 0x8fff || (address >= 0xe000 && address <= 0xefff)) {
        return memory_[address];
    }
    if (address >= 0x9000 && address <= 0x90ff) return memory_[address];
    if (address == 0x9803) {
        // Protection: return the byte pointed by HL (same hack as mrdo_hw.pas).
        const uint16_t hl = uint16_t((cpu_.h << 8) | cpu_.l);
        return memory_[hl];
    }
    if (address == 0xa000) return in0_;
    if (address == 0xa001) return in1_;
    if (address == 0xa002) return dsw_a_;
    if (address == 0xa003) return dsw_b_;
    return 0xff;
}

void MrDo::write_byte(uint16_t address, uint8_t value) {
    if (address <= 0x7fff) return;  // ROM

    if (address >= 0x8000 && address <= 0x87ff) {
        if (memory_[address] != value) {
            dirty_bg_[address & 0x3ff] = true;
            memory_[address] = value;
        }
        return;
    }
    if (address >= 0x8800 && address <= 0x8fff) {
        if (memory_[address] != value) {
            dirty_fg_[address & 0x3ff] = true;
            memory_[address] = value;
        }
        return;
    }
    if ((address >= 0x9000 && address <= 0x90ff) || (address >= 0xe000 && address <= 0xefff)) {
        memory_[address] = value;
        return;
    }
    if (address == 0x9800) {
        flip_screen_ = (value & 1) != 0;
        return;
    }
    if (address == 0x9801) {
        sn0_.write(value);
        return;
    }
    if (address == 0x9802) {
        sn1_.write(value);
        return;
    }
    if (address >= 0xf000 && address <= 0xf7ff) {
        scroll_y_ = value;
        return;
    }
    if (address >= 0xf800) {
        scroll_x_ = value;
        return;
    }
}

void MrDo::on_cycles(int cycles) {
    audio_accumulator_ += int64_t(cycles) * SN76496::kSampleRate;
    while (audio_accumulator_ >= int64_t(kCpuClock)) {
        audio_accumulator_ -= int64_t(kCpuClock);
        const int32_t sample = (sn0_.update() + sn1_.update()) / 2;
        audio_.push_back(int16_t(std::clamp(sample, -32768, 32767)));
    }
}

void MrDo::draw_bg_tile(int offset) {
    const int tile_x = offset / 32;
    const int tile_y = 31 - (offset % 32);
    const uint8_t attrib = memory_[0x8000 + offset];
    const int code = memory_[0x8400 + offset] + ((attrib & 0x80) << 1);
    const int color = (attrib & 0x3f) << 2;
    const bool priority = (attrib & 0x40) != 0;
    const uint8_t* pixels = chars_bg_.element(code);

    for (int y = 0; y < 8; y++) {
        const size_t row = size_t((tile_y * 8 + y) * 256 + tile_x * 8);
        for (int x = 0; x < 8; x++) {
            const uint8_t pen = pixels[y * 8 + x];
            const uint32_t rgb = palette_[size_t(pen + color)];
            if (priority) {
                bg_opaque_[row + size_t(x)] = rgb;
                bg_trans_[row + size_t(x)] = kTransparent;
            } else {
                bg_opaque_[row + size_t(x)] = 0xff000000u;
                bg_trans_[row + size_t(x)] = (pen == 0) ? kTransparent : rgb;
            }
        }
    }
}

void MrDo::draw_fg_tile(int offset) {
    const int tile_x = offset / 32;
    const int tile_y = 31 - (offset % 32);
    const uint8_t attrib = memory_[0x8800 + offset];
    const int code = memory_[0x8c00 + offset] + ((attrib & 0x80) << 1);
    const int color = (attrib & 0x3f) << 2;
    const bool priority = (attrib & 0x40) != 0;
    const uint8_t* pixels = chars_fg_.element(code);

    for (int y = 0; y < 8; y++) {
        const size_t row = size_t((tile_y * 8 + y) * 256 + tile_x * 8);
        for (int x = 0; x < 8; x++) {
            const uint8_t pen = pixels[y * 8 + x];
            const uint32_t rgb = palette_[size_t(pen + color)];
            if (priority) {
                fg_opaque_[row + size_t(x)] = rgb;
                fg_trans_[row + size_t(x)] = kTransparent;
            } else {
                fg_opaque_[row + size_t(x)] = kTransparent;
                fg_trans_[row + size_t(x)] = (pen == 0) ? kTransparent : rgb;
            }
        }
    }
}

void MrDo::draw_sprite(int index) {
    const int base = index * 4 + 0x9000;
    const uint8_t pos_x_raw = memory_[base + 1];
    if (pos_x_raw == 0) return;

    const int code = memory_[base] & 0x7f;
    const uint8_t attrib = memory_[base + 2];
    const int color = (attrib & 0x0f) << 2;
    const int pos_y = 240 - memory_[base + 3];
    const int pos_x = 256 - pos_x_raw;
    const bool flip_x = (attrib & 0x20) != 0;
    const bool flip_y = (attrib & 0x10) != 0;
    const uint8_t* pixels = sprites_.element(code);

    for (int y = 0; y < 16; y++) {
        const int screen_y = pos_y + y;
        if (screen_y < 0 || screen_y >= 256) continue;
        const int source_y = flip_y ? (15 - y) : y;
        for (int x = 0; x < 16; x++) {
            const int source_x = flip_x ? (15 - x) : x;
            const uint8_t pen = pixels[source_y * 16 + source_x];
            if (pen == 0) continue;
            const uint8_t mapped = sprite_lut_[size_t((pen + color) & 0x3f)];
            const int screen_x = (pos_x + x) & 0xff;
            composite_[size_t(screen_y * 256 + screen_x)] = palette_[mapped];
        }
    }
}

void MrDo::blit_scrolled(const std::array<uint32_t, 256 * 256>& source, bool transparent) {
    for (int y = 0; y < 256; y++) {
        const int source_y = (y + scroll_y_) & 0xff;
        for (int x = 0; x < 256; x++) {
            const int source_x = (x + scroll_x_) & 0xff;
            const uint32_t pixel = source[size_t(source_y * 256 + source_x)];
            if (transparent && pixel == kTransparent) continue;
            composite_[size_t(y * 256 + x)] = pixel;
        }
    }
}

void MrDo::blit_layer(const std::array<uint32_t, 256 * 256>& source, bool transparent) {
    for (size_t index = 0; index < composite_.size(); index++) {
        const uint32_t pixel = source[index];
        if (transparent && pixel == kTransparent) continue;
        composite_[index] = pixel;
    }
}

void MrDo::update_video() {
    for (int offset = 0; offset < 0x400; offset++) {
        if (dirty_bg_[size_t(offset)]) {
            draw_bg_tile(offset);
            dirty_bg_[size_t(offset)] = false;
        }
        const uint8_t fg_attrib = memory_[0x8800 + offset];
        // Priority tiles (bit 6) are always redrawn, matching mrdo_hw.pas.
        if (dirty_fg_[size_t(offset)] || (fg_attrib & 0x40) != 0) {
            draw_fg_tile(offset);
            dirty_fg_[size_t(offset)] = false;
        }
    }

    // Layer order from mrdo_hw.pas: scrolled BG opaque, FG opaque, scrolled BG
    // transparent, FG transparent, then sprites.
    blit_scrolled(bg_opaque_, false);
    blit_layer(fg_opaque_, true);
    blit_scrolled(bg_trans_, true);
    blit_layer(fg_trans_, true);

    for (int index = 0x3f; index >= 0; index--) draw_sprite(index);

    // Visible area: actualiza_trozo_final(32, 8, 192, 240, 3).
    for (int y = 0; y < kScreenHeight; y++) {
        for (int x = 0; x < kScreenWidth; x++) {
            const uint32_t pixel = composite_[size_t((y + 8) * 256 + (x + 32))];
            const size_t target =
                flip_screen_ ? size_t((kScreenHeight - 1 - y) * kScreenWidth +
                                      (kScreenWidth - 1 - x))
                             : size_t(y * kScreenWidth + x);
            framebuffer_[target] = pixel;
        }
    }
}

void MrDo::run_frame() {
    const int cycles_per_line = int(kCpuClock / kFramesPerSecond / kScanlines);
    for (int line = 0; line < kScanlines; line++) {
        if (line == 224) {
            cpu_.set_irq(IrqLine::Hold);
            update_video();
        }
        cpu_.run(cycles_per_line);
    }
}

void MrDo::set_inputs(const MachineInputs& inputs) {
    const InputState& player1 = inputs.player1;
    const InputState& player2 = inputs.player2;
    in0_ = 0xff;
    in1_ = 0xff;

    if (player1.left) in0_ &= 0xfe;
    if (player1.down) in0_ &= 0xfd;
    if (player1.right) in0_ &= 0xfb;
    if (player1.up) in0_ &= 0xf7;
    if (player1.button1) in0_ &= 0xef;
    if (player1.start) in0_ &= 0xdf;
    if (player2.start) in0_ &= 0xbf;

    if (player2.left) in1_ &= 0xfe;
    if (player2.down) in1_ &= 0xfd;
    if (player2.right) in1_ &= 0xfb;
    if (player2.up) in1_ &= 0xf7;
    if (player2.button1) in1_ &= 0xef;
    if (inputs.coin1) in1_ &= 0xbf;
    if (inputs.coin2) in1_ &= 0x7f;
}

void MrDo::set_dip_switch(int bank, uint8_t value) {
    if (bank == 0) dsw_a_ = value;
    if (bank == 1) dsw_b_ = value;
}

void MrDo::drain_audio(std::vector<int16_t>& out) {
    out.insert(out.end(), audio_.begin(), audio_.end());
    audio_.clear();
}

}  // namespace dsp
