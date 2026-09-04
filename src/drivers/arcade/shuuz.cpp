#include "drivers/arcade/shuuz.h"

#include <algorithm>
#include <set>

#include "core/rom_loader.h"
#include "cpu/irq_line.h"

namespace dsp {
namespace {

const std::vector<RomEntry> kMainRoms = {
    {"136083-4010.23p", 0x20000, 0x000000, 0x1c2459f8},
    {"136083-4011.13p", 0x20000, 0x000001, 0x6db53a85},
};

const std::vector<RomEntry> kPlayfieldRoms = {
    {"136083-2030.43x", 0x20000, 0x000000, 0x8ecf1ed8},
    {"136083-2032.20x", 0x20000, 0x020000, 0x5af184e6},
    {"136083-2031.87x", 0x20000, 0x040000, 0x72e9db63},
    {"136083-2033.65x", 0x20000, 0x060000, 0x8f552498},
};

const std::vector<RomEntry> kSpriteRoms = {
    {"136083-1020.43u", 0x20000, 0x000000, 0xd21ad039},
    {"136083-1022.20u", 0x20000, 0x020000, 0x0c10bc90},
    {"136083-1024.43m", 0x20000, 0x040000, 0xadb09347},
    {"136083-1026.20m", 0x20000, 0x060000, 0x9b20e13d},
    {"136083-1021.87u", 0x20000, 0x080000, 0x8388910c},
    {"136083-1023.65u", 0x20000, 0x0a0000, 0x71353112},
    {"136083-1025.87m", 0x20000, 0x0c0000, 0xf7b20a64},
    {"136083-1027.65m", 0x20000, 0x0e0000, 0x55d54952},
};

const std::vector<RomEntry> kOkiRoms = {
    {"136083-1040.75b", 0x20000, 0x00000, 0x0896702b},
    {"136083-1041.65b", 0x20000, 0x20000, 0xb3b07ce9},
};

constexpr int kPlayfieldRegion = 0x80000;
constexpr int kSpriteRegion = 0x100000;

// MAME pfmolayout: 8x8, RGN_FRAC(1,2), 4 planes, ROMREGION_INVERT.
GfxLayout pfm_layout(uint32_t region_size) {
    const int half_bits = int(region_size / 2) * 8;
    GfxLayout layout;
    layout.width = 8;
    layout.height = 8;
    layout.total = int(region_size / 2) / 16;
    layout.planes = 4;
    layout.char_increment = 16 * 8;
    // Plane 0 is the MSB, like the other Atari ports in this tree. MAME lists
    // these offsets LSB-first; treating them that way bit-reverses every pen
    // (title horse becomes candy red, SHUUZ letters go grey).
    layout.plane_offsets = {0, 4, half_bits, half_bits + 4};
    layout.x_offsets = {0, 1, 2, 3, 8, 9, 10, 11};
    layout.y_offsets = {0 * 8, 2 * 8, 4 * 8, 6 * 8, 8 * 8, 10 * 8, 12 * 8, 14 * 8};
    return layout;
}

AtariMotionObjects::Config motion_object_config() {
    AtariMotionObjects::Config config;
    config.tile_width = 8;
    config.tile_height = 8;
    config.bankcount = 1;
    config.linked = true;
    config.split = false;
    config.slipheight = 8;
    config.maxperline = 0;
    config.palettebase = 0x000;
    config.link_entry = {0x00ff, 0, 0, 0};
    config.code_entry = {{0, 0x7fff, 0, 0}, {0, 0, 0, 0}};
    config.color_entry = {{0, 0, 0x000f, 0}, {0, 0, 0, 0}};
    config.xpos_entry = {0, 0, 0xff80, 0};
    config.ypos_entry = {0, 0, 0, 0xff80};
    config.width_entry = {0, 0, 0, 0x0070};
    config.height_entry = {0, 0, 0, 0x0007};
    config.hflip_entry = {0, 0x8000, 0, 0};
    return config;
}

uint8_t pal6bit(uint8_t bits) {
    bits = uint8_t(bits & 0x3f);
    return uint8_t((bits << 2) | (bits >> 4));
}

void invert_region(std::vector<uint8_t>& data) {
    for (uint8_t& byte : data) byte = uint8_t(~byte);
}

int wrap_coord(int value, int screen, int plane) {
    if (value >= screen) value -= plane;
    return value;
}

}  // namespace

Shuuz::Shuuz()
    : main_cpu_(kMainClock), oki_(kOkiClock, true), rom_(0x20000, 0) {
    framebuffer_.assign(size_t(kScreenWidth) * kScreenHeight, 0xff000000u);
    mo_index_.assign(size_t(kScreenWidth) * kScreenHeight, kMoTransparent);

    main_cpu_.set_memory_handlers(
        [this](uint32_t address) { return main_read(address); },
        [this](uint32_t address, uint16_t value) { main_write(address, value); });
    main_cpu_.set_cycle_handler([this](int cycles) { on_cycles(cycles); });

    motion_objects_ = std::make_unique<AtariMotionObjects>(
        motion_object_config(), slip_.data(), &ram_[(0x3fd000 - 0x3f8000) >> 1],
        kScreenWidth + 8, kScreenHeight + 8);
}

bool Shuuz::init(const std::string& rom_path, std::string* error) {
    if (!load_roms(rom_path, error)) return false;
    eeprom_.fill(0xff);
    reset();
    return true;
}

bool Shuuz::load_roms(const std::string& rom_path, std::string* error) {
    RomLoader loader;
    if (!loader.open(rom_path, error)) return false;

    std::vector<uint8_t> program(0x40000, 0);
    for (const RomEntry& entry : kMainRoms) {
        std::vector<uint8_t> data(entry.length, 0);
        RomEntry single{entry.name, entry.length, 0, entry.crc};
        if (!loader.load({single}, data, error)) return false;
        for (uint32_t byte = 0; byte < entry.length; byte++) {
            program[(entry.offset & ~1u) + byte * 2 + (entry.offset & 1u)] = data[byte];
        }
    }
    for (uint32_t index = 0; index < 0x40000; index += 2) {
        rom_[index >> 1] = uint16_t((program[index] << 8) | program[index + 1]);
    }

    std::vector<uint8_t> playfield(kPlayfieldRegion, 0);
    if (!loader.load(kPlayfieldRoms, playfield, error)) return false;
    invert_region(playfield);
    playfield_gfx_.decode(pfm_layout(kPlayfieldRegion), playfield);

    std::vector<uint8_t> sprites(kSpriteRegion, 0);
    if (!loader.load(kSpriteRoms, sprites, error)) return false;
    invert_region(sprites);
    sprite_gfx_.decode(pfm_layout(kSpriteRegion), sprites);

    std::vector<uint8_t> samples(0x40000, 0);
    if (!loader.load(kOkiRoms, samples, error)) return false;
    oki_.set_rom(std::move(samples));

    warnings_ = loader.warnings();
    return true;
}

void Shuuz::reset() {
    main_cpu_.reset();
    oki_.reset();
    oki_.set_pin7(true);

    playfield_.fill(0);
    playfield_ext_.fill(0);
    eof_.fill(0);
    slip_.fill(0);
    ram_.fill(0);
    vad_control_.fill(0);
    palette_ram_.fill(0);
    palette_.fill(0xff000000u);
    std::fill(mo_index_.begin(), mo_index_.end(), kMoTransparent);

    scanline_ = 0;
    in_hblank_ = false;
    eeprom_unlocked_ = false;
    pf0_xscroll_raw_ = pf1_xscroll_raw_ = 0;
    pf0_yscroll_ = mo_xscroll_ = mo_yscroll_ = 0;
    irq_scanline_ = 0;
    irq_armed_ = false;
    motion_objects_->set_bank(0);

    system_port_ = 0xffff;
    buttons_port_ = 0xffff;
    leta_cur_[0] = leta_cur_[1] = 0;
    track_dx_ = track_dy_ = 0;
    pointer_seen_ = false;

    audio_accumulator_ = oki_accumulator_ = 0;
    last_oki_ = 0;
    audio_.clear();
}

int Shuuz::debug_palette_used() const {
    std::set<uint16_t> used(palette_ram_.begin(), palette_ram_.end());
    return int(used.size());
}

int Shuuz::debug_motion_object_pixels() const {
    return int(std::count_if(mo_index_.begin(), mo_index_.end(),
                             [](uint16_t value) { return value != kMoTransparent; }));
}

uint16_t Shuuz::main_read(uint32_t address) {
    if (address < 0x40000) return rom_[address >> 1];
    if (address >= 0x100000 && address <= 0x100fff) {
        return uint16_t(0xff00 | eeprom_[(address & 0xfff) >> 1]);
    }
    if (address >= 0x103000 && address <= 0x103003) return uint16_t(leta_r((address >> 1) & 1));
    if (address >= 0x105000 && address <= 0x105001) return special_port0_r();
    if (address >= 0x105002 && address <= 0x105003) return buttons_port_;
    if (address >= 0x106000 && address <= 0x106001) return uint16_t(0xff00 | oki_.read());
    if (address >= 0x107000 && address <= 0x107007) return 0xffff;
    if (address >= 0x3e0000 && address <= 0x3e07ff) return palette_ram_[(address & 0x7ff) >> 1];
    if (address >= 0x3effc0 && address <= 0x3effff) {
        return vad_control_read((address - 0x3effc0) >> 1);
    }
    if (address >= 0x3f4000 && address <= 0x3f5eff) {
        return playfield_[(address - 0x3f4000) >> 1];
    }
    if (address >= 0x3f5f00 && address <= 0x3f5f7f) return eof_[(address - 0x3f5f00) >> 1];
    if (address >= 0x3f5f80 && address <= 0x3f5fff) return slip_[(address - 0x3f5f80) >> 1];
    if (address >= 0x3f6000 && address <= 0x3f7fff) {
        return playfield_ext_[(address - 0x3f6000) >> 1];
    }
    if (address >= 0x3f8000 && address <= 0x3fffff) {
        return ram_[(address - 0x3f8000) >> 1];
    }
    return 0xffff;
}

void Shuuz::main_write(uint32_t address, uint16_t value) {
    if (address < 0x40000) return;
    if (address >= 0x100000 && address <= 0x100fff) {
        if (eeprom_unlocked_) {
            eeprom_[(address & 0xfff) >> 1] = uint8_t(value);
            eeprom_unlocked_ = false;
        }
        return;
    }
    if (address >= 0x101000 && address <= 0x101fff) {
        eeprom_unlocked_ = true;
        return;
    }
    if (address >= 0x102000 && address <= 0x102001) return;  // watchdog
    if (address >= 0x105000 && address <= 0x105001) return;  // latch_w (unused)
    if (address >= 0x106000 && address <= 0x106001) {
        oki_.write(uint8_t(value));
        return;
    }
    if (address >= 0x107000 && address <= 0x107007) return;
    if (address >= 0x3e0000 && address <= 0x3e07ff) {
        set_palette(int((address & 0x7ff) >> 1), value);
        return;
    }
    if (address >= 0x3effc0 && address <= 0x3effff) {
        vad_control_write(int((address - 0x3effc0) >> 1), value);
        return;
    }
    if (address >= 0x3f4000 && address <= 0x3f5eff) {
        const size_t offset = (address - 0x3f4000) >> 1;
        playfield_[offset] = value;
        if ((vad_control_[0x0a] & 0x80) != 0) {
            playfield_ext_[offset] =
                uint16_t((playfield_ext_[offset] & 0x00ff) | (vad_control_[0x1c] & 0xff00));
        }
        return;
    }
    if (address >= 0x3f5f00 && address <= 0x3f5f7f) {
        eof_[(address - 0x3f5f00) >> 1] = value;
        return;
    }
    if (address >= 0x3f5f80 && address <= 0x3f5fff) {
        slip_[(address - 0x3f5f80) >> 1] = value;
        return;
    }
    if (address >= 0x3f6000 && address <= 0x3f7fff) {
        playfield_ext_[(address - 0x3f6000) >> 1] = value;
        return;
    }
    if (address >= 0x3f8000 && address <= 0x3fffff) {
        ram_[(address - 0x3f8000) >> 1] = value;
    }
}

void Shuuz::set_palette(int index, uint16_t value) {
    index &= 0x3ff;
    palette_ram_[size_t(index)] = value;
    const uint8_t intensity = uint8_t((value >> 15) & 1);
    const uint8_t red = pal6bit(uint8_t(((value >> 9) & 0x3e) | intensity));
    const uint8_t green = pal6bit(uint8_t(((value >> 4) & 0x3e) | intensity));
    const uint8_t blue = pal6bit(uint8_t(((value << 1) & 0x3e) | intensity));
    palette_[size_t(index)] =
        0xff000000u | (uint32_t(red) << 16) | (uint32_t(green) << 8) | uint32_t(blue);
}

uint16_t Shuuz::vad_control_read(int offset) {
    if (offset == 0) {
        int vpos = scanline_;
        if (vpos > 255) vpos = 255;
        uint16_t result = uint16_t(vpos);
        if (scanline_ >= kScreenHeight) result = uint16_t(result | 0x4000);
        return result;
    }
    return vad_control_[size_t(offset) & 0x1f];
}

void Shuuz::vad_control_write(int offset, uint16_t value) {
    offset &= 0x1f;
    vad_control_[size_t(offset)] = value;
    switch (offset) {
        case 0x03:
            irq_scanline_ = int(value & 0x1ff);
            irq_armed_ = true;
            break;
        case 0x10:
        case 0x11:
        case 0x12:
        case 0x13:
        case 0x14:
        case 0x15:
        case 0x16:
        case 0x17:
        case 0x18:
        case 0x19:
        case 0x1a:
        case 0x1b:
            vad_update_parameter(value);
            break;
        case 0x1e:
            main_cpu_.set_irq(4, IrqLine::Clear);
            break;
        default:
            break;
    }
}

void Shuuz::vad_update_parameter(uint16_t word) {
    const uint32_t scrolled = (uint32_t(word) >> 7) & 0x1ff;
    switch (word & 15) {
        case 9: mo_xscroll_ = scrolled; break;
        case 10: pf1_xscroll_raw_ = scrolled; break;
        case 11: pf0_xscroll_raw_ = scrolled; break;
        case 13: mo_yscroll_ = scrolled; break;
        case 15: pf0_yscroll_ = scrolled; break;
        default: break;
    }
}

void Shuuz::apply_eof() {
    for (int i = 0; i < 0x1c; i++) {
        if (eof_[size_t(i)] != 0) vad_control_write(i, eof_[size_t(i)]);
    }
}

uint16_t Shuuz::leta_r(int offset) {
    const int which = offset & 1;
    if (which == 0) {
        leta_cur_[0] = int16_t(int(track_dx_) + int(track_dy_));
        leta_cur_[1] = int16_t(int(track_dx_) - int(track_dy_));
    }
    return uint16_t(leta_cur_[which]);
}

uint16_t Shuuz::special_port0_r() const {
    uint16_t result = system_port_;
    if (scanline_ >= kScreenHeight) result = uint16_t(result & ~0x0800);
    else result = uint16_t(result | 0x0800);
    if ((result & 0x0800) != 0 && in_hblank_) result = uint16_t(result & ~0x0800);
    return result;
}

void Shuuz::draw_motion_object_band(int line) {
    const int clip_bottom = std::min(line + 7, kScreenHeight - 1);
    for (int y = line; y <= clip_bottom; y++) {
        const size_t base = size_t(y) * kScreenWidth;
        std::fill(mo_index_.begin() + base, mo_index_.begin() + base + kScreenWidth,
                  kMoTransparent);
    }

    const int band = motion_objects_->band_for_line(line, int(mo_yscroll_));
    motion_objects_->draw_band(
        band, int(mo_xscroll_), int(mo_yscroll_), -1,
        [&](int code, int color, bool hflip, bool vflip, int x, int y, int, int) {
            const int sx = wrap_coord(x, kScreenWidth, kMoPlaneWidth);
            const int sy = wrap_coord(y, kScreenHeight, kMoPlaneHeight);
            const uint8_t* pixels = sprite_gfx_.element(code);
            for (int row = 0; row < 8; row++) {
                const int target_y = sy + row;
                if (target_y < line || target_y > clip_bottom) continue;
                const int source_row = vflip ? (7 - row) : row;
                for (int column = 0; column < 8; column++) {
                    const int target_x = sx + column;
                    if (target_x < 0 || target_x >= kScreenWidth) continue;
                    const int source_column = hflip ? (7 - column) : column;
                    const uint8_t pen = pixels[source_row * 8 + source_column];
                    if (pen == 0) continue;
                    mo_index_[size_t(target_y) * kScreenWidth + size_t(target_x)] =
                        uint16_t(color + pen);
                }
            }
        });
}

void Shuuz::render_line(int line) {
    const int pf_scrollx = int(pf0_xscroll_raw_ + (pf1_xscroll_raw_ & 7));
    const int source_y = (line + int(pf0_yscroll_)) & 0x1ff;
    const int playfield_row = source_y >> 3;
    const int playfield_pixel_row = source_y & 7;

    uint32_t* target = &framebuffer_[size_t(line) * kScreenWidth];
    for (int x = 0; x < kScreenWidth; x++) {
        const int source_x = (x + pf_scrollx) & 0x1ff;
        const size_t tile = size_t((source_x >> 3) * 64 + playfield_row);
        const uint16_t data = playfield_[tile];
        const int code = data & 0x3fff;
        const bool flipx = (data & 0x8000) != 0;
        const int color = (playfield_ext_[tile] >> 8) & 0x0f;
        int column = source_x & 7;
        if (flipx) column = 7 - column;
        const uint8_t pen = playfield_gfx_.element(code)[playfield_pixel_row * 8 + column];
        int index = 0x100 + color * 16 + pen;

        const uint16_t object = mo_index_[size_t(line) * kScreenWidth + size_t(x)];
        if (object != kMoTransparent) {
            const bool o13 = (index & 0xf0) == 0xf0;
            const bool mopf =
                ((index & 0x80) != 0 ? ((object & 0xc0) == 0xc0) : ((object & 0xc0) != 0xc0)) &&
                !o13;
            if (mopf) {
                if ((object & 0x0e) != 0) index = object;
                else if ((object & 0x01) != 0) index |= 0x200;
            }
        }

        target[x] = palette_[size_t(index) & 0x3ff];
    }
}

void Shuuz::run_frame() {
    const int main_cycles =
        int(double(kMainClock) / kFramesPerSecond / (kScanlines * kCpuSync) + 0.5);

    apply_eof();

    for (int line = 0; line < kScanlines; line++) {
        scanline_ = line;
        if (irq_armed_ && line == irq_scanline_) {
            main_cpu_.set_irq(4, IrqLine::Assert);
            irq_armed_ = false;
        }

        for (int step = 0; step < kCpuSync; step++) {
            in_hblank_ = step == kCpuSync - 1;
            main_cpu_.run(main_cycles);
        }
        in_hblank_ = false;

        if (line < kScreenHeight) {
            if ((line & 7) == 0) draw_motion_object_band(line);
            render_line(line);
        }
    }
}

void Shuuz::on_cycles(int cycles) {
    oki_accumulator_ += int64_t(cycles) * oki_.sample_frequency();
    while (oki_accumulator_ >= int64_t(kMainClock)) {
        oki_accumulator_ -= int64_t(kMainClock);
        last_oki_ = oki_.update();
    }

    audio_accumulator_ += int64_t(cycles) * kSampleRate;
    while (audio_accumulator_ >= int64_t(kMainClock)) {
        audio_accumulator_ -= int64_t(kMainClock);
        audio_.push_back(int16_t(std::clamp(last_oki_, int32_t(-32768), int32_t(32767))));
    }
}

void Shuuz::set_inputs(const MachineInputs& inputs) {
    system_port_ = 0xffff;
    if (inputs.coin1) system_port_ = uint16_t(system_port_ & ~0x0001);
    if (inputs.coin2) system_port_ = uint16_t(system_port_ & ~0x0002);

    buttons_port_ = 0xffff;
    if (inputs.player1.button1 || inputs.pointer_button1) {
        buttons_port_ = uint16_t(buttons_port_ & ~0x0001);
    }
    if (inputs.player1.button2 || inputs.pointer_button2) {
        buttons_port_ = uint16_t(buttons_port_ & ~0x0002);
    }
    if (service_) buttons_port_ = uint16_t(buttons_port_ & ~0x0800);

    int dx = 0;
    int dy = 0;
    if (inputs.has_pointer) {
        if (!pointer_seen_) {
            last_pointer_x_ = inputs.pointer_x;
            last_pointer_y_ = inputs.pointer_y;
            pointer_seen_ = true;
        } else {
            dx += inputs.pointer_x - last_pointer_x_;
            dy += last_pointer_y_ - inputs.pointer_y;  // TRACKY is reversed
            last_pointer_x_ = inputs.pointer_x;
            last_pointer_y_ = inputs.pointer_y;
        }
    }
    if (inputs.player1.right) dx += 8;
    if (inputs.player1.left) dx -= 8;
    if (inputs.player1.up) dy += 8;
    if (inputs.player1.down) dy -= 8;
    track_dx_ = int8_t(std::clamp(dx, -127, 127));
    track_dy_ = int8_t(std::clamp(dy, -127, 127));
}

void Shuuz::set_dip_switch(int bank, uint8_t value) {
    if (bank == 0) service_ = (value & 1) != 0;
}

void Shuuz::drain_audio(std::vector<int16_t>& out) {
    out.insert(out.end(), audio_.begin(), audio_.end());
    audio_.clear();
}

}  // namespace dsp
