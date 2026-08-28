#include "drivers/actfancer.h"

#include <algorithm>
#include <cstring>

#include "core/rom_loader.h"

namespace dsp {
namespace {

const std::vector<RomEntry> kMainRom = {
    {"fe08-3.bin", 0x10000, 0x00000, 0x35f1999d},
    {"fe09-3.bin", 0x10000, 0x10000, 0xd21416ca},
    {"fe10-3.bin", 0x10000, 0x20000, 0x85535fcc},
};

const std::vector<RomEntry> kCharRom = {
    {"15", 0x10000, 0x00000, 0xa1baf21e},
    {"16", 0x10000, 0x10000, 0x22e64730},
};

const RomEntry kSoundRom = {"17-1", 0x8000, 0x8000, 0x289ad106};
const RomEntry kOkiRom = {"18", 0x10000, 0x00000, 0x5c55b242};

const std::vector<RomEntry> kTileRom = {
    {"14", 0x10000, 0x00000, 0xd6457420},
    {"12", 0x10000, 0x10000, 0x08787b7a},
    {"13", 0x10000, 0x20000, 0xc30c37dc},
    {"11", 0x10000, 0x30000, 0x1f006d9f},
};

const std::vector<RomEntry> kSpriteRom = {
    {"02", 0x10000, 0x00000, 0xb1db0efc},
    {"03", 0x08000, 0x10000, 0xf313e04f},
    {"06", 0x10000, 0x18000, 0x8cb6dd87},
    {"07", 0x08000, 0x28000, 0xdd345def},
    {"00", 0x10000, 0x30000, 0xd50a9550},
    {"01", 0x08000, 0x40000, 0x34935e93},
    {"04", 0x10000, 0x48000, 0xbcf41795},
    {"05", 0x08000, 0x58000, 0xd38b94aa},
};

// pt_x / pt_y from actfancer_hw.pas
const std::vector<int> kPtX = {16 * 8 + 0, 16 * 8 + 1, 16 * 8 + 2, 16 * 8 + 3,
                               16 * 8 + 4, 16 * 8 + 5, 16 * 8 + 6, 16 * 8 + 7,
                               0,          1,          2,          3,
                               4,          5,          6,          7};
const std::vector<int> kPtY = {0 * 8,  1 * 8,  2 * 8,  3 * 8,  4 * 8,  5 * 8,  6 * 8,  7 * 8,
                               8 * 8,  9 * 8,  10 * 8, 11 * 8, 12 * 8, 13 * 8, 14 * 8, 15 * 8};

uint8_t pal4bit(uint16_t value) {
    value &= 0x0f;
    return uint8_t(value | (value << 4));
}

std::vector<uint8_t> load_region(RomLoader& loader, const std::vector<RomEntry>& entries,
                                 uint32_t total_size, std::string* error) {
    std::vector<uint8_t> data(total_size, 0);
    if (!loader.load(entries, data, error)) return {};
    return data;
}

}  // namespace

ActFancer::ActFancer()
    : framebuffer_(size_t(kScreenWidth) * kScreenHeight, 0xff000000u) {}

bool ActFancer::init(const std::string& rom_path, std::string* error) {
    if (!load_roms(rom_path, error)) return false;

    main_cpu_.set_memory_handlers(
        [this](uint32_t address) { return main_read(address); },
        [this](uint32_t address, uint8_t value) { main_write(address, value); });
    sound_cpu_.set_memory_handlers(
        [this](uint16_t address) { return sound_read(address); },
        [this](uint16_t address, uint8_t value) { sound_write(address, value); });
    sound_cpu_.set_cycle_handler([this](int cycles) { on_sound_cycles(cycles); });
    ym3812_.set_irq_handler([this](bool state) {
        sound_cpu_.set_irq(state ? IrqLine::Hold : IrqLine::Clear);
    });

    reset();
    return true;
}

void ActFancer::reset() {
    main_cpu_.reset();
    sound_cpu_.reset();
    ym3812_.reset();
    ym2203_.reset();
    okim_.reset();
    bac06_.reset();
    ram_.fill(0);
    buffer_sprites_.fill(0);
    palette_ram_.fill(0);
    palette_.fill(0xff000000u);
    pens_.fill(0);
    sound_latch_ = 0;
    in0_ = 0xff;
    in1_ = 0x7f;
    in2_ = 0xff;
    frames_ = 0;
    audio_accumulator_ = 0;
    audio_.clear();
}

void ActFancer::run_frame() {
    const int main_cycles = int(double(kMainClock) / kFramesPerSecond / kScanlines + 0.5);
    const int sound_cycles = int(double(kSoundClock) / kFramesPerSecond / kScanlines + 0.5);

    for (int line = 0; line < kScanlines; line++) {
        if (line == 8) {
            in1_ = uint8_t(in1_ & 0x7f);  // clear VBLANK
        }
        if (line == 248) {
            main_cpu_.set_irq_line(0, IrqLine::Hold);
            update_video();
            in1_ = uint8_t(in1_ | 0x80);  // set VBLANK
        }
        main_cpu_.run(main_cycles);
        sound_cpu_.run(sound_cycles);
    }
    ++frames_;
}

void ActFancer::set_inputs(const MachineInputs& inputs) {
    // P1 -> in0, P2 -> in2, system (coin/vblank) -> in1
    in0_ = 0xff;
    in2_ = 0xff;
    in1_ = uint8_t(0x7f | (in1_ & 0x80));

    auto clear_bit = [](uint8_t& port, int bit, bool pressed) {
        if (pressed) port = uint8_t(port & ~(1u << bit));
    };

    const InputState& p1 = inputs.player1;
    const InputState& p2 = inputs.player2;
    clear_bit(in0_, 0, p1.up);
    clear_bit(in0_, 1, p1.down);
    clear_bit(in0_, 2, p1.left);
    clear_bit(in0_, 3, p1.right);
    clear_bit(in0_, 4, p1.button1);
    clear_bit(in0_, 5, p1.button2);
    clear_bit(in0_, 7, p1.start);

    clear_bit(in2_, 0, p2.up);
    clear_bit(in2_, 1, p2.down);
    clear_bit(in2_, 2, p2.left);
    clear_bit(in2_, 3, p2.right);
    clear_bit(in2_, 4, p2.button1);
    clear_bit(in2_, 5, p2.button2);
    clear_bit(in2_, 7, p2.start);

    clear_bit(in1_, 0, inputs.coin1);
    clear_bit(in1_, 1, inputs.coin2);
}

void ActFancer::set_dip_switch(int bank, uint8_t value) {
    if (bank == 0) dsw_a_ = value;
    if (bank == 1) dsw_b_ = value;
}

void ActFancer::drain_audio(std::vector<int16_t>& out) {
    out.insert(out.end(), audio_.begin(), audio_.end());
    audio_.clear();
}

void ActFancer::on_sound_cycles(int cycles) {
    audio_accumulator_ += int64_t(cycles) * int64_t(YM3812::kSampleRate);
    while (audio_accumulator_ >= int64_t(kSoundClock)) {
        audio_accumulator_ -= int64_t(kSoundClock);
        const int sample = ym3812_.update() + ym2203_.update() + okim_.update();
        audio_.push_back(int16_t(std::clamp(sample, -32768, 32767)));
    }
}

// ---------------------------------------------------------------------------
// BAC06 8-bit bus helpers (HuC6280)
// ---------------------------------------------------------------------------

void ActFancer::write_control0_8b(Bac06Layer& layer, uint32_t address, uint8_t value) {
    // Pascal change_control0_8b: even = low byte, odd = high byte.
    const int pos = int((address & 7) >> 1);
    uint16_t word = layer.control_0[size_t(pos)];
    if (address & 1) {
        word = uint16_t((word & 0x00ff) | (uint16_t(value) << 8));
    } else {
        word = uint16_t((word & 0xff00) | value);
    }
    layer.change_control0(pos, word);
}

void ActFancer::write_control1_8b_swap(Bac06Layer& layer, uint32_t address, uint8_t value) {
    // Pascal change_control1_8b_swap: even = low, odd = high; index from (addr & 7).
    const int pos = int((address & 7) >> 1);
    uint16_t word = layer.control_1[size_t(pos)];
    if (address & 1) {
        word = uint16_t((word & 0x00ff) | (uint16_t(value) << 8));
    } else {
        word = uint16_t((word & 0xff00) | value);
    }
    layer.change_control1(pos, word);
}

void ActFancer::write_tile_data_8b_swap(Bac06Layer& layer, uint32_t address, uint8_t value,
                                       uint32_t mask) {
    const size_t index = size_t((address & mask) >> 1);
    if (index >= layer.data.size()) return;
    uint16_t word = layer.data[index];
    if (address & 1) {
        word = uint16_t((word & 0x00ff) | (uint16_t(value) << 8));
    } else {
        word = uint16_t((word & 0xff00) | value);
    }
    layer.data[index] = word;
}

// ---------------------------------------------------------------------------
// Main CPU map
// ---------------------------------------------------------------------------

uint8_t ActFancer::main_read(uint32_t address) {
    address &= 0x1fffffu;
    if (address <= 0x2ffff) {
        return address < rom_.size() ? rom_[address] : 0;
    }
    if (address >= 0x62000 && address <= 0x63fff) {
        const uint16_t word = bac06_.tile_1.data[(address & 0x1fff) >> 1];
        return uint8_t(word >> (8 * (address & 1)));
    }
    if (address >= 0x72000 && address <= 0x727ff) {
        const uint16_t word = bac06_.tile_2.data[(address & 0x7ff) >> 1];
        return uint8_t(word >> (8 * (address & 1)));
    }
    if (address >= 0x100000 && address <= 0x1007ff) {
        return buffer_sprites_[address & 0x7ff];
    }
    if (address >= 0x120000 && address <= 0x1205ff) {
        return palette_ram_[address & 0x7ff];
    }
    if (address == 0x130000) return in0_;
    if (address == 0x130001) return in2_;
    if (address == 0x130002) return dsw_a_;
    if (address == 0x130003) return dsw_b_;
    if (address == 0x140000) return in1_;
    if (address >= 0x1f0000 && address <= 0x1f3fff) {
        return ram_[address & 0x3fff];
    }
    return 0;
}

void ActFancer::main_write(uint32_t address, uint8_t value) {
    address &= 0x1fffffu;
    if (address <= 0x2ffff) return;

    if (address >= 0x60000 && address <= 0x60007) {
        write_control0_8b(bac06_.tile_1, address, value);
        return;
    }
    if (address >= 0x60010 && address <= 0x6001f) {
        write_control1_8b_swap(bac06_.tile_1, address, value);
        return;
    }
    if (address >= 0x62000 && address <= 0x63fff) {
        write_tile_data_8b_swap(bac06_.tile_1, address, value, 0x1fff);
        return;
    }
    if (address >= 0x70000 && address <= 0x70007) {
        write_control0_8b(bac06_.tile_2, address, value);
        return;
    }
    if (address >= 0x70010 && address <= 0x7001f) {
        write_control1_8b_swap(bac06_.tile_2, address, value);
        return;
    }
    if (address >= 0x72000 && address <= 0x727ff) {
        write_tile_data_8b_swap(bac06_.tile_2, address, value, 0x7ff);
        return;
    }
    if (address >= 0x100000 && address <= 0x1007ff) {
        buffer_sprites_[address & 0x7ff] = value;
        return;
    }
    if (address == 0x110000) {
        // buffer_sprites is a byte view of little-endian words.
        uint16_t words[0x400];
        for (size_t i = 0; i < 0x400; i++) {
            words[i] = uint16_t(buffer_sprites_[i * 2] | (uint16_t(buffer_sprites_[i * 2 + 1]) << 8));
        }
        bac06_.update_sprite_data(words);
        return;
    }
    if (address >= 0x120000 && address <= 0x1205ff) {
        const size_t offset = address & 0x7ff;
        if (palette_ram_[offset] != value) {
            palette_ram_[offset] = value;
            update_palette_entry(int(offset & ~1u) >> 1);
        }
        return;
    }
    if (address == 0x150000) {
        sound_latch_ = value;
        sound_cpu_.set_nmi(IrqLine::Pulse);
        return;
    }
    if (address == 0x160000) return;  // nop
    if (address >= 0x1f0000 && address <= 0x1f3fff) {
        ram_[address & 0x3fff] = value;
        return;
    }
}

uint8_t ActFancer::sound_read(uint16_t address) {
    if (address == 0x3000) return sound_latch_;
    if (address == 0x3800) return okim_.read();
    if (address <= 0x7ff || address >= 0x4000) return mem_snd_[address];
    return 0;
}

void ActFancer::sound_write(uint16_t address, uint8_t value) {
    if (address <= 0x7ff) {
        mem_snd_[address] = value;
        return;
    }
    if (address == 0x800) {
        ym2203_.control(value);
        return;
    }
    if (address == 0x801) {
        ym2203_.write(value);
        return;
    }
    if (address == 0x1000) {
        ym3812_.control(value);
        return;
    }
    if (address == 0x1001) {
        ym3812_.write(value);
        return;
    }
    if (address == 0x3800) {
        okim_.write(value);
        return;
    }
}

void ActFancer::update_palette_entry(int index) {
    if (index < 0 || index >= 0x300) return;
    const size_t base = size_t(index * 2);
    const uint8_t rg = palette_ram_[base];
    const uint8_t b = palette_ram_[base + 1];
    const uint8_t red = pal4bit(rg);
    const uint8_t green = pal4bit(uint16_t(rg >> 4));
    const uint8_t blue = pal4bit(b);
    palette_[size_t(index)] =
        0xff000000u | (uint32_t(red) << 16) | (uint32_t(green) << 8) | blue;
}

void ActFancer::update_video() {
    // update_video_actfancer:
    //   tile_1 opaque, tile_2 transparent, sprites, then text on top.
    pens_.fill(0);
    const bool odd_frame = (frames_ & 1) != 0;

    bac06_.tile_1.draw(gfx_tiles_, false, pens_.data());
    bac06_.draw_sprites(gfx_sprites_, 0, 0, odd_frame, pens_.data());
    bac06_.tile_2.draw(gfx_char_, true, pens_.data());

    // actualiza_trozo_final(0,8,256,240,7)
    for (int y = 0; y < kScreenHeight; y++) {
        for (int x = 0; x < kScreenWidth; x++) {
            const uint16_t pen = pens_[size_t((y + 8) * Bac06Layer::kScreenWidth + x)];
            framebuffer_[size_t(y * kScreenWidth + x)] = palette_[size_t(pen & 0x3ff)];
        }
    }
}

bool ActFancer::load_roms(const std::string& rom_path, std::string* error) {
    RomLoader loader;
    if (!loader.open(rom_path, error)) return false;

    rom_ = load_region(loader, kMainRom, 0x30000, error);
    if (rom_.empty()) return false;

    std::vector<uint8_t> sound(0x10000, 0);
    if (!loader.load({kSoundRom}, sound, error)) return false;
    std::memcpy(mem_snd_.data(), sound.data(), std::min(sound.size(), mem_snd_.size()));

    std::vector<uint8_t> oki(0x10000, 0);
    if (!loader.load({kOkiRom}, oki, error)) return false;
    okim_.set_rom(std::move(oki));

    std::vector<uint8_t> char_rom = load_region(loader, kCharRom, 0x20000, error);
    if (char_rom.empty()) return false;
    std::vector<uint8_t> tile_rom = load_region(loader, kTileRom, 0x40000, error);
    if (tile_rom.empty()) return false;
    std::vector<uint8_t> sprite_rom = load_region(loader, kSpriteRom, 0x60000, error);
    if (sprite_rom.empty()) return false;

    decode_graphics(char_rom, tile_rom, sprite_rom);

    for (const std::string& warning : loader.warnings()) warnings_.push_back(warning);
    return true;
}

void ActFancer::decode_graphics(const std::vector<uint8_t>& char_rom,
                                const std::vector<uint8_t>& tile_rom,
                                const std::vector<uint8_t>& sprite_rom) {
    // Chars 8x8, $1000 tiles, planes at $8000*8, $18000*8, 0, $10000*8
    {
        GfxLayout layout;
        layout.width = 8;
        layout.height = 8;
        layout.total = 0x1000;
        layout.planes = 4;
        layout.char_increment = 8 * 8;
        layout.plane_offsets = {0x8000 * 8, 0x18000 * 8, 0, 0x10000 * 8};
        layout.x_offsets.assign(kPtX.begin() + 8, kPtX.end());
        layout.y_offsets.assign(kPtY.begin(), kPtY.begin() + 8);
        gfx_char_.decode(layout, char_rom);
    }
    // Tiles 16x16, $c00 tiles
    {
        GfxLayout layout;
        layout.width = 16;
        layout.height = 16;
        layout.total = 0xc00;
        layout.planes = 4;
        layout.char_increment = 32 * 8;
        layout.plane_offsets = {0, 0x10000 * 8, 0x20000 * 8, 0x30000 * 8};
        layout.x_offsets = kPtX;
        layout.y_offsets = kPtY;
        gfx_tiles_.decode(layout, tile_rom);
    }
    // Sprites 16x16, $c00 tiles
    {
        GfxLayout layout;
        layout.width = 16;
        layout.height = 16;
        layout.total = 0xc00;
        layout.planes = 4;
        layout.char_increment = 32 * 8;
        layout.plane_offsets = {0, 0x18000 * 8, 0x30000 * 8, 0x48000 * 8};
        layout.x_offsets = kPtX;
        layout.y_offsets = kPtY;
        gfx_sprites_.decode(layout, sprite_rom);
    }
}

}  // namespace dsp
