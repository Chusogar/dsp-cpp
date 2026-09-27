#include "drivers/arcade/badlands.h"

#include <algorithm>

#include "core/rom_loader.h"
#include "cpu/irq_line.h"

namespace dsp {
namespace {

const std::vector<RomEntry> kMainRoms = {
    {"136074-1008.20f", 0x10000, 0x00000, 0xa3da5774},
    {"136074-1006.27f", 0x10000, 0x00001, 0xaa03b4f3},
    {"136074-1009.17f", 0x10000, 0x20000, 0x0e2e807f},
    {"136074-1007.24f", 0x10000, 0x20001, 0x99a20c2c},
};
const std::vector<RomEntry> kSoundRoms = {
    {"136074-1018.9c", 0x10000, 0, 0xa05fd146},
};
const std::vector<RomEntry> kPlayfieldRoms = {
    {"136074-1012.4n", 0x10000, 0x00000, 0x5d124c6c},
    {"136074-1013.2n", 0x10000, 0x10000, 0xb1ec90d6},
    {"136074-1014.4s", 0x10000, 0x20000, 0x248a6845},
    {"136074-1015.2s", 0x10000, 0x30000, 0x792296d8},
    {"136074-1016.4u", 0x10000, 0x40000, 0x878f7c66},
    {"136074-1017.2u", 0x10000, 0x50000, 0xad0071a3},
};
const std::vector<RomEntry> kSpriteRoms = {
    {"136074-1010.14r", 0x10000, 0x00000, 0xc15f629e},
    {"136074-1011.10r", 0x10000, 0x10000, 0xfb0b6717},
    {"136074-1019.14t", 0x10000, 0x20000, 0x0e26bff6},
};

GfxLayout playfield_layout() {
    GfxLayout layout;
    layout.width = 8; layout.height = 8; layout.total = 0x3000; layout.planes = 4;
    layout.char_increment = 32 * 8;
    layout.plane_offsets = {0, 1, 2, 3};
    layout.x_offsets = {0, 4, 8, 12, 16, 20, 24, 28};
    layout.y_offsets = {0, 32, 64, 96, 128, 160, 192, 224};
    return layout;
}

GfxLayout sprite_layout() {
    GfxLayout layout;
    layout.width = 16; layout.height = 8; layout.total = 0xc00; layout.planes = 4;
    layout.char_increment = 64 * 8;
    layout.plane_offsets = {0, 1, 2, 3};
    layout.x_offsets = {0, 4, 8, 12, 16, 20, 24, 28, 32, 36, 40, 44, 48, 52, 56, 60};
    layout.y_offsets = {0, 64, 128, 192, 256, 320, 384, 448};
    return layout;
}

AtariMotionObjects::Config motion_object_config() {
    AtariMotionObjects::Config c;
    c.tile_width = 16; c.tile_height = 8; c.bankcount = 1;
    c.linked = false; c.split = true; c.reverse = false; c.slipheight = 0;
    c.palettebase = 0x80;
    c.link_entry = {0, 0, 0, 0x003f};
    c.code_entry = {{0x0fff, 0, 0, 0}, {0, 0, 0, 0}};
    c.color_entry = {{0, 0, 0, 0x0007}, {0, 0, 0, 0}};
    c.xpos_entry = {0, 0, 0, 0xff80};
    c.ypos_entry = {0, 0xff80, 0, 0};
    c.height_entry = {0, 0x000f, 0, 0};
    c.priority_entry = {0, 0, 0, 0x0008};
    return c;
}

uint8_t pal6bit(uint8_t v) { return uint8_t((v << 2) | (v >> 4)); }
void invert(std::vector<uint8_t>& v) { for (uint8_t& b : v) b = uint8_t(~b); }

}  // namespace

BadLands::BadLands()
    : main_cpu_(kMainClock), sound_cpu_(kSoundClock), ym_(kYmClock), main_rom_(0x20000, 0) {
    framebuffer_.assign(size_t(kScreenWidth) * kScreenHeight, 0xff000000u);
    playfield_pixels_.assign(size_t(512) * 256, 0);
    main_cpu_.set_memory_handlers([this](uint32_t a) { return main_read(a); },
                                  [this](uint32_t a, uint16_t v) { main_write(a, v); });
    sound_cpu_.set_memory_handlers([this](uint16_t a) { return sound_read(a); },
                                   [this](uint16_t a, uint8_t v) { sound_write(a, v); });
    sound_cpu_.set_cycle_handler([this](int cycles) { on_sound_cycles(cycles); });
    ym_.set_irq_handler([this](bool state) {
        ym_irq_ = state;
        update_sound_irq();
    });
    motion_objects_ = std::make_unique<AtariMotionObjects>(
        motion_object_config(), nullptr, &ram_[0x800], kScreenWidth + 8, kScreenHeight + 8);
}

bool BadLands::init(const std::string& path, std::string* error) {
    if (!load_roms(path, error)) return false;
    eeprom_.fill(0xff);
    reset();
    return true;
}

bool BadLands::load_roms(const std::string& path, std::string* error) {
    RomLoader loader;
    if (!loader.open(path, error)) return false;
    std::vector<uint8_t> program(0x40000, 0);
    for (const RomEntry& entry : kMainRoms) {
        std::vector<uint8_t> data(entry.length);
        if (!loader.load({{entry.name, entry.length, 0, entry.crc}}, data, error)) return false;
        for (uint32_t i = 0; i < entry.length; ++i)
            program[(entry.offset & ~1u) + i * 2 + (entry.offset & 1u)] = data[i];
    }
    for (size_t i = 0; i < program.size(); i += 2)
        main_rom_[i >> 1] = uint16_t((program[i] << 8) | program[i + 1]);

    std::vector<uint8_t> sound(0x10000);
    if (!loader.load(kSoundRoms, sound, error)) return false;
    std::copy(sound.begin() + 0x4000, sound.end(), sound_memory_.begin() + 0x4000);
    for (int bank = 0; bank < 4; ++bank)
        std::copy_n(sound.begin() + bank * 0x1000, 0x1000, sound_banks_[size_t(bank)].begin());

    std::vector<uint8_t> playfield(0x60000);
    if (!loader.load(kPlayfieldRoms, playfield, error)) return false;
    invert(playfield);
    playfield_gfx_.decode(playfield_layout(), playfield);

    std::vector<uint8_t> sprites(0x30000);
    if (!loader.load(kSpriteRoms, sprites, error)) return false;
    invert(sprites);
    sprite_gfx_.decode(sprite_layout(), sprites);
    warnings_ = loader.warnings();
    return true;
}

void BadLands::reset() {
    main_cpu_.reset(); sound_cpu_.reset(); ym_.reset();
    ram_.fill(0); palette_ram_.fill(0); palette_.fill(0xff000000u);
    std::fill(playfield_pixels_.begin(), playfield_pixels_.end(), 0);
    std::fill(framebuffer_.begin(), framebuffer_.end(), 0xff000000u);
    sound_bank_ = playfield_bank_ = sound_latch_ = main_latch_ = 0;
    eeprom_unlocked_ = main_pending_ = sound_pending_ = false;
    sound_halted_ = ym_irq_ = timed_irq_ = false;
    main_inputs_ = 0xffbf; sound_inputs_ = 0;
    pedal1_ = pedal2_ = steering1_ = steering2_ = 0x80;
    audio_accumulator_ = 0; audio_.clear();
}

uint16_t BadLands::main_read(uint32_t a) {
    if (a <= 0x3ffff) return main_rom_[a >> 1];
    if (a >= 0xfc0000 && a <= 0xfc1fff) return uint16_t(0xfeff | (sound_pending_ ? 0x0100 : 0));
    if (a >= 0xfd0000 && a <= 0xfd1fff) return uint16_t(0xff00 | eeprom_[(a & 0x1fff) >> 1]);
    if (a == 0xfe4000) return main_inputs_;
    if (a == 0xfe6000) return uint16_t(0xff00 | steering1_);
    if (a == 0xfe6002) return uint16_t(0xff00 | steering2_);
    if (a == 0xfe6004) return pedal1_;
    if (a == 0xfe6006) return pedal2_;
    if (a >= 0xfea000 && a <= 0xfebfff) {
        main_pending_ = false; main_cpu_.set_irq(2, IrqLine::Clear);
        return uint16_t(main_latch_ << 8);
    }
    if (a >= 0xffc000 && a <= 0xffc3ff) return palette_ram_[(a & 0x3ff) >> 1];
    if (a >= 0xffe000) return ram_[(a & 0x1fff) >> 1];
    return 0xffff;
}

void BadLands::main_write(uint32_t a, uint16_t v) {
    if (a <= 0x3ffff) return;
    if (a >= 0xfc0000 && a <= 0xfc1fff) {
        sound_cpu_.reset(); sound_bank_ = 0; ym_.reset(); sound_halted_ = false; return;
    }
    if (a >= 0xfd0000 && a <= 0xfd1fff) {
        if (eeprom_unlocked_) { eeprom_[(a & 0x1fff) >> 1] = uint8_t(v); eeprom_unlocked_ = false; }
        return;
    }
    if (a >= 0xfe0000 && a <= 0xfe1fff) return;
    if (a >= 0xfe2000 && a <= 0xfe3fff) { main_cpu_.set_irq(1, IrqLine::Clear); return; }
    if (a >= 0xfe8000 && a <= 0xfe9fff) {
        sound_latch_ = uint8_t(v >> 8); sound_pending_ = true; sound_cpu_.set_nmi(IrqLine::Assert); return;
    }
    if (a >= 0xfec000 && a <= 0xfedfff) { playfield_bank_ = uint8_t(v & 1); return; }
    if (a >= 0xfee000 && a <= 0xfeffff) { eeprom_unlocked_ = true; return; }
    if (a >= 0xffc000 && a <= 0xffc3ff) { set_palette(int((a & 0x3ff) >> 1), v); return; }
    if (a >= 0xffe000) ram_[(a & 0x1fff) >> 1] = v;
}

uint8_t BadLands::sound_read(uint16_t a) {
    if (a <= 0x1fff || a >= 0x4000) return sound_memory_[a];
    if (a >= 0x2000 && a <= 0x27ff && (a & 1)) return ym_.status();
    if (a >= 0x2800 && a <= 0x29ff) {
        switch (a & 6) {
            case 2: sound_pending_ = false; sound_cpu_.set_nmi(IrqLine::Clear); return sound_latch_;
            case 4: return uint8_t(sound_inputs_ | 0x10 | (main_pending_ ? 0x20 : 0) | (!sound_pending_ ? 0x40 : 0));
            case 6: timed_irq_ = false; update_sound_irq(); return 0xff;
            default: break;
        }
    }
    if (a >= 0x3000 && a <= 0x3fff) return sound_banks_[sound_bank_][a & 0x0fff];
    return 0xff;
}

void BadLands::sound_write(uint16_t a, uint8_t v) {
    if (a <= 0x1fff) { sound_memory_[a] = v; return; }
    if (a >= 0x2000 && a <= 0x27ff) {
        if (a & 1) ym_.write(v); else ym_.select_register(v);
        return;
    }
    if (a >= 0x2800 && a <= 0x29ff && (a & 6) == 6) {
        timed_irq_ = false; update_sound_irq(); return;
    }
    if (a >= 0x2a00 && a <= 0x2bff) {
        if ((a & 6) == 2) { main_latch_ = v; main_pending_ = true; main_cpu_.set_irq(2, IrqLine::Assert); }
        if ((a & 6) == 4) { sound_bank_ = uint8_t((v >> 6) & 3); if ((v & 1) == 0) ym_.reset(); }
    }
}

void BadLands::set_palette(int index, uint16_t v) {
    index &= 0x1ff;
    palette_ram_[size_t(index)] = v;

    // The board stores one IRRRRRGG GGGBBBBB colour across the upper byte of
    // two adjacent 68000 words. Rebuild the colour after either half changes.
    const int color_index = index >> 1;
    const uint16_t packed = uint16_t((palette_ram_[size_t(color_index * 2)] & 0xff00) |
                                     (palette_ram_[size_t(color_index * 2 + 1)] >> 8));
    const uint8_t intensity = uint8_t((packed >> 15) & 1);
    const uint8_t red = pal6bit(uint8_t(((packed >> 9) & 0x3e) | intensity));
    const uint8_t green = pal6bit(uint8_t(((packed >> 4) & 0x3e) | intensity));
    const uint8_t blue = pal6bit(uint8_t(((packed << 1) & 0x3e) | intensity));
    palette_[size_t(color_index)] =
        0xff000000u | (uint32_t(red) << 16) | (uint32_t(green) << 8) | blue;
}

void BadLands::draw_motion_objects(int priority) {
    motion_objects_->draw(0, 0, priority, [this](int code, int color, bool hf, bool vf, int x, int y, int, int) {
        const uint8_t* pixels = sprite_gfx_.element(code);
        for (int row = 0; row < 8; ++row) {
            const int dy = y + row; if (dy < 0 || dy >= kScreenHeight) continue;
            const int sy = vf ? 7 - row : row;
            for (int col = 0; col < 16; ++col) {
                const int dx = x + col; if (dx < 0 || dx >= kScreenWidth) continue;
                const int sx = hf ? 15 - col : col;
                const uint8_t pen = pixels[sy * 16 + sx]; if (pen == 0) continue;
                framebuffer_[size_t(dy) * kScreenWidth + dx] = palette_[size_t((color + pen) & 0xff)];
            }
        }
    });
}

void BadLands::update_video() {
    for (int tile = 0; tile < 0x800; ++tile) {
        const int tx = tile & 63, ty = tile >> 6;
        const uint16_t attr = ram_[size_t(tile)];
        int code = attr & 0x1fff;
        if (attr & 0x1000) code += int(playfield_bank_) << 12;
        const int base = ((attr >> 13) & 7) << 4;
        const uint8_t* pixels = playfield_gfx_.element(code);
        for (int row = 0; row < 8; ++row)
            for (int col = 0; col < 8; ++col)
                playfield_pixels_[size_t(ty * 8 + row) * 512 + tx * 8 + col] = uint16_t(base + pixels[row * 8 + col]);
    }
    for (int y = 0; y < kScreenHeight; ++y)
        for (int x = 0; x < kScreenWidth; ++x) {
            const uint16_t p = playfield_pixels_[size_t(y) * 512 + x];
            framebuffer_[size_t(y) * kScreenWidth + x] = palette_[size_t((p & 8) ? 0 : p)];
        }
    draw_motion_objects(0);
    for (int y = 0; y < kScreenHeight; ++y)
        for (int x = 0; x < kScreenWidth; ++x) {
            const uint16_t p = playfield_pixels_[size_t(y) * 512 + x];
            if (p & 8) framebuffer_[size_t(y) * kScreenWidth + x] = palette_[size_t(p)];
        }
    draw_motion_objects(1);
}

void BadLands::on_sound_cycles(int cycles) {
    ym_.run_timers(cycles * 2);
    audio_accumulator_ += int64_t(cycles) * YM2151::kSampleRate;
    while (audio_accumulator_ >= kSoundClock) {
        audio_accumulator_ -= kSoundClock;
        audio_.push_back(int16_t(std::clamp(ym_.update(), int32_t(-32768), int32_t(32767))));
    }
}

void BadLands::update_sound_irq() {
    sound_cpu_.set_irq((ym_irq_ || timed_irq_) ? IrqLine::Assert : IrqLine::Clear);
}

void BadLands::run_frame() {
    const int mc = int(double(kMainClock) / kFramesPerSecond / (kScanlines * kCpuSync) + 0.5);
    const int sc = int(double(kSoundClock) / kFramesPerSecond / (kScanlines * kCpuSync) + 0.5);
    for (int line = 0; line < kScanlines; ++line) {
        if (line == 0) { main_inputs_ &= ~0x40; timed_irq_ = true; update_sound_irq(); }
        if (line == 64 || line == 128 || line == 192) { timed_irq_ = true; update_sound_irq(); }
        if (line == 240) { update_video(); main_cpu_.set_irq(1, IrqLine::Assert); main_inputs_ |= 0x40; }
        for (int step = 0; step < kCpuSync; ++step) { main_cpu_.run(mc); if (!sound_halted_) sound_cpu_.run(sc); }
    }
    if (!pedal1_pressed_) --pedal1_;
    if (!pedal2_pressed_) --pedal2_;
}

void BadLands::set_inputs(const MachineInputs& in) {
    main_inputs_ = service_ ? 0xff3f : 0xffbf;
    if (in.player1.button1) main_inputs_ &= ~0x10;
    if (in.player2.button1) main_inputs_ &= ~0x20;
    sound_inputs_ = uint8_t((in.coin1 ? 1 : 0) | (in.coin2 ? 2 : 0));
    pedal1_pressed_ = in.player1.button2;
    pedal2_pressed_ = in.player2.button2;
    if (in.player1.left) steering1_ = uint8_t(steering1_ - 4);
    if (in.player1.right) steering1_ = uint8_t(steering1_ + 4);
    if (in.player2.left) steering2_ = uint8_t(steering2_ - 4);
    if (in.player2.right) steering2_ = uint8_t(steering2_ + 4);
}

void BadLands::set_dip_switch(int bank, uint8_t value) { if (bank == 0) service_ = (value & 1) != 0; }
void BadLands::drain_audio(std::vector<int16_t>& out) { out.insert(out.end(), audio_.begin(), audio_.end()); audio_.clear(); }

}  // namespace dsp
