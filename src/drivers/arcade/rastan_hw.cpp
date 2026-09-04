#include "drivers/arcade/rastan_hw.h"

#include <algorithm>
#include <cstring>

#include "core/rom_loader.h"

namespace dsp {
namespace {

// MAME `ROM_START(rastan)` in src/mame/taito/rastan.cpp (World rev 1).
const std::vector<RomEntry> kMainRoms = {
    {"b04-38.19", 0x10000, 0x00000, 0x1c91dbb1},
    {"b04-37.7", 0x10000, 0x00001, 0xecf20bdd},
    {"b04-40.20", 0x10000, 0x20000, 0x0930d4b3},
    {"b04-39.8", 0x10000, 0x20001, 0xd95ade5e},
    {"b04-42.21", 0x10000, 0x40000, 0x1857a7cb},
    {"b04-43-1.9", 0x10000, 0x40001, 0xca4702ff},
};
const std::vector<RomEntry> kCharRoms = {
    {"b04-01.40", 0x20000, 0x00000, 0xcd30de19},
    {"b04-03.39", 0x20000, 0x20000, 0xab67e064},
    {"b04-02.67", 0x20000, 0x40000, 0x54040fec},
    {"b04-04.66", 0x20000, 0x60000, 0x94737e93},
};
const std::vector<RomEntry> kSpriteRoms = {
    {"b04-05.15", 0x20000, 0x00000, 0xc22d94ac},
    {"b04-07.14", 0x20000, 0x20000, 0xb5632a51},
    {"b04-06.28", 0x20000, 0x40000, 0x002ccf39},
    {"b04-08.27", 0x20000, 0x60000, 0xfeafca05},
};
const RomEntry kSoundRom{"b04-19.49", 0x10000, 0, 0xee81fdd8};
const RomEntry kAdpcmRom{"b04-20.76", 0x10000, 0, 0xfd1a34cc};

bool load_bytes(RomLoader& loader, const RomEntry& entry, std::vector<uint8_t>& data,
                std::string* error) {
    data.assign(entry.length, 0);
    RomEntry single{entry.name, entry.length, 0, entry.crc};
    std::string name_error;
    if (loader.load({single}, data, &name_error)) return true;
    for (const std::string& name : loader.filenames()) {
        std::vector<uint8_t> candidate;
        if (!loader.try_read(name, candidate)) continue;
        if (candidate.size() != entry.length) continue;
        if (entry.crc != 0 && crc32_of(candidate.data(), candidate.size()) == entry.crc) {
            data = std::move(candidate);
            return true;
        }
    }
    if (error) {
        *error = name_error.empty() ? std::string("missing ROM file: ") + entry.name : name_error;
    }
    return false;
}

bool load_raw(RomLoader& loader, const std::vector<RomEntry>& entries, std::vector<uint8_t>& dest,
              std::string* error) {
    for (const RomEntry& entry : entries) {
        std::vector<uint8_t> data;
        if (!load_bytes(loader, entry, data, error)) return false;
        if (entry.offset + entry.length > dest.size()) dest.resize(entry.offset + entry.length);
        std::copy(data.begin(), data.end(), dest.begin() + std::ptrdiff_t(entry.offset));
    }
    return true;
}

// ps_x from rastan_hw.pas: pixel-column bit offsets into the packed 4bpp ROM.
// $40000*8 = 0x200000 bits = byte 0x40000 (the third gfx ROM per region).
const std::vector<int> kPixelX = {0,        4,        0x200000, 0x200004, 8,   12,   0x200008,
                                  0x20000c, 16,       20,       0x200010, 0x200014, 24, 28,
                                  0x200018, 0x20001c};

GfxLayout tile_layout() {
    GfxLayout layout;
    layout.width = 8;
    layout.height = 8;
    layout.total = 0x4000;
    layout.planes = 4;
    layout.char_increment = 16 * 8;  // 8 pixels � 4 planes over 16-bit steps
    layout.plane_offsets = {0, 1, 2, 3};
    layout.x_offsets = std::vector<int>(kPixelX.begin(), kPixelX.begin() + 8);
    layout.y_offsets = {0 * 16, 1 * 16, 2 * 16, 3 * 16, 4 * 16, 5 * 16, 6 * 16, 7 * 16};
    return layout;
}

GfxLayout sprite_layout() {
    GfxLayout layout;
    layout.width = 16;
    layout.height = 16;
    layout.total = 0x1000;
    layout.planes = 4;
    layout.char_increment = 64 * 8;  // 16 pixels � 4 planes
    layout.plane_offsets = {0, 1, 2, 3};
    layout.x_offsets = kPixelX;
    layout.y_offsets = {0 * 32,  1 * 32,  2 * 32,  3 * 32,  4 * 32,  5 * 32,  6 * 32,  7 * 32,
                        8 * 32,  9 * 32,  10 * 32, 11 * 32, 12 * 32, 13 * 32, 14 * 32, 15 * 32};
    return layout;
}

inline uint8_t pal5bit(uint16_t value) {
    value &= 0x1f;
    return uint8_t((value << 3) | (value >> 2));
}

}  // namespace

Rastan::Rastan()
    : main_cpu_(kMainClock), sound_cpu_(kSoundClock), ym_(kYmClock), msm_(kMsmClock, 48, 4) {
    background_.assign(size_t(kWorkWidth) * kWorkHeight, 0xff000000u);
    foreground_.assign(size_t(kWorkWidth) * kWorkHeight, 0);
    composite_.assign(size_t(kWorkWidth) * kWorkHeight, 0xff000000u);
    framebuffer_.assign(size_t(kScreenWidth) * kScreenHeight, 0xff000000u);

    main_cpu_.set_memory_handlers([this](uint32_t address) { return main_read(address); },
                                  [this](uint32_t address, uint16_t value) {
                                      main_write(address, value);
                                  });
    main_cpu_.set_byte_handlers([this](uint32_t address) { return main_read_byte(address); },
                                [this](uint32_t address, uint8_t value) {
                                    main_write_byte(address, value);
                                });
    sound_cpu_.set_memory_handlers(
        [this](uint16_t address) { return sound_read(address); },
        [this](uint16_t address, uint8_t value) { sound_write(address, value); });
    sound_cpu_.set_cycle_handler([this](int cycles) { on_sound_cycles(cycles); });
    ym_.set_irq_handler([this](bool state) {
        sound_cpu_.set_irq(state ? IrqLine::Assert : IrqLine::Clear);
    });
    ym_.set_port_handler([this](uint8_t value) { sound_bank_index_ = uint8_t(value & 3); });
    syt_.set_nmi_handler([this]() { sound_cpu_.set_nmi(IrqLine::Pulse); });
    syt_.set_reset_handler([this]() { reset_sound(); });

    // RaSTAN streams ADPCM straight from the CPU port (no on-chip ROM): feed a
    // nibble per sample clock and let vclk() decode it.
    msm_.set_vclk_handler([this]() { advance_adpcm(); });
}

bool Rastan::init(const std::string& rom_path, std::string* error) {
    if (!load_roms(rom_path, error)) return false;
    reset();
    return true;
}

bool Rastan::load_roms(const std::string& rom_path, std::string* error) {
    RomLoader loader;
    if (!loader.open(rom_path, error)) return false;

    // ROM_LOAD16_BYTE: even file = high byte of each 68000 word.
    rom_.assign(0x30000, 0);
    for (const RomEntry& entry : kMainRoms) {
        std::vector<uint8_t> data;
        if (!load_bytes(loader, entry, data, error)) return false;
        const bool high = (entry.offset & 1u) == 0;
        const uint32_t word_base = entry.offset >> 1;
        for (uint32_t index = 0; index < entry.length; index++) {
            uint16_t& word = rom_[word_base + index];
            if (high) word = uint16_t((uint16_t(data[index]) << 8) | (word & 0x00ff));
            else word = uint16_t((word & 0xff00) | data[index]);
        }
    }

    std::vector<uint8_t> sound_rom(0x10000, 0);
    if (!load_bytes(loader, kSoundRom, sound_rom, error)) return false;
    std::copy(sound_rom.begin(), sound_rom.begin() + 0x4000, sound_rom_.begin());
    for (int bank = 0; bank < 4; bank++) {
        std::copy(sound_rom.begin() + bank * 0x4000, sound_rom.begin() + (bank + 1) * 0x4000,
                  sound_bank_[size_t(bank)].begin());
    }

    std::vector<uint8_t> adpcm(0x10000, 0);
    if (!load_bytes(loader, kAdpcmRom, adpcm, error)) return false;
    std::copy(adpcm.begin(), adpcm.end(), adpcm_rom_.begin());

    std::vector<uint8_t> tile_rom(0x80000, 0);
    if (!load_raw(loader, kCharRoms, tile_rom, error)) return false;
    std::vector<uint8_t> sprite_rom(0x80000, 0);
    if (!load_raw(loader, kSpriteRoms, sprite_rom, error)) return false;
    decode_graphics(tile_rom, sprite_rom);

    for (const std::string& warning : loader.warnings()) warnings_.push_back(warning);
    return true;
}

void Rastan::decode_graphics(const std::vector<uint8_t>& tile_rom,
                             const std::vector<uint8_t>& sprite_rom) {
    tiles_.decode(tile_layout(), tile_rom);
    sprites_.decode(sprite_layout(), sprite_rom);
}

void Rastan::reset_sound() {
    sound_cpu_.reset();
    ym_.reset();
    msm_.reset();
    sound_bank_index_ = 0;
    adpcm_pos_ = 0;
    adpcm_val_ = 0;
    adpcm_have_val_ = false;
    audio_accumulator_ = 0;
    msm_accumulator_ = 0;
}

void Rastan::reset() {
    main_cpu_.reset();
    syt_.reset();
    reset_sound();
    in0_ = 0x1f;
    in1_ = 0xff;
    in2_ = 0xff;
    scroll_x1_ = scroll_y1_ = 0;
    scroll_x2_ = scroll_y2_ = 0;
    sprite_bank_ = 0;
    audio_.clear();
}

uint16_t Rastan::main_read(uint32_t address) {
    address &= 0xffffffu;
    if (address <= 0x5ffff) {
        const uint32_t word = address >> 1;
        return word < rom_.size() ? rom_[word] : 0;
    }
    if (address >= 0x10c000 && address <= 0x10ffff) {
        return ram1_[(address & 0x3fff) >> 1];
    }
    if (address >= 0x200000 && address <= 0x200fff) {
        return palette_ram_[(address & 0xfff) >> 1];
    }
    if ((address & ~1u) == 0x390000) return in1_;
    if ((address & ~1u) == 0x390002) return in2_;
    if ((address & ~1u) == 0x390004) return 0x8f;
    if ((address & ~1u) == 0x390006) return in0_;
    if ((address & ~1u) == 0x390008) return dsw_a_;
    if ((address & ~1u) == 0x39000a) return dsw_b_;
    if ((address & ~1u) == 0x3e0002) {
        // Word reads do not touch the mailbox; MOVE.B of the high byte does.
        return 0;
    }
    if (address >= 0xc00000 && address <= 0xc0ffff) {
        return ram2_[(address & 0xffff) >> 1];
    }
    if (address >= 0xd00000 && address <= 0xd03fff) {
        return ram3_[(address & 0x3fff) >> 1];
    }
    return 0;
}

void Rastan::main_write(uint32_t address, uint16_t value) {
    address &= 0xffffffu;
    if (address <= 0x5ffff) return;
    if (address >= 0x10c000 && address <= 0x10ffff) {
        ram1_[(address & 0x3fff) >> 1] = value;
        return;
    }
    if (address >= 0x200000 && address <= 0x200fff) {
        const int index = int((address & 0xfff) >> 1);
        if (palette_ram_[size_t(index)] != value) {
            palette_ram_[size_t(index)] = value;
            set_palette(index, value);
        }
        return;
    }
    if ((address & ~1u) == 0x350008 || (address & ~1u) == 0x3c0000) return;
    if ((address & ~1u) == 0x380000) {
        sprite_bank_ = uint8_t((value & 0xe0) >> 5);
        return;
    }
    if ((address & ~1u) == 0x3e0000) {
        syt_.port_w(uint8_t(value >> 8));
        return;
    }
    if ((address & ~1u) == 0x3e0002) {
        syt_.comm_w(uint8_t(value >> 8));
        return;
    }
    if (address >= 0xc00000 && address <= 0xc0ffff) {
        ram2_[(address & 0xffff) >> 1] = value;
        return;
    }
    if ((address & ~1u) == 0xc20000) {
        scroll_y1_ = uint16_t((512 - value) & 0x1ff);
        return;
    }
    if ((address & ~1u) == 0xc20002) {
        scroll_y2_ = uint16_t((512 - value) & 0x1ff);
        return;
    }
    if ((address & ~1u) == 0xc40000) {
        scroll_x1_ = uint16_t((512 - value) & 0x1ff);
        return;
    }
    if ((address & ~1u) == 0xc40002) {
        scroll_x2_ = uint16_t((512 - value) & 0x1ff);
        return;
    }
    if (address >= 0xc50000 && address <= 0xc50003) return;
    if (address >= 0xd00000 && address <= 0xd03fff) {
        ram3_[(address & 0x3fff) >> 1] = value;
        return;
    }
}

uint8_t Rastan::main_read_byte(uint32_t address) {
    address &= 0xffffffu;
    if ((address & ~1u) == 0x3e0002) {
        return (address & 1u) == 0 ? syt_.comm_r() : 0;
    }
    const uint16_t word = main_read(address);
    return (address & 1u) ? uint8_t(word) : uint8_t(word >> 8);
}

void Rastan::main_write_byte(uint32_t address, uint8_t value) {
    address &= 0xffffffu;
    if ((address & ~1u) == 0x3e0000) {
        if ((address & 1u) == 0) syt_.port_w(value);
        return;
    }
    if ((address & ~1u) == 0x3e0002) {
        if ((address & 1u) == 0) syt_.comm_w(value);
        return;
    }
    uint16_t word = main_read(address);
    if (address & 1u) word = uint16_t((word & 0xff00) | value);
    else word = uint16_t((word & 0x00ff) | (uint16_t(value) << 8));
    main_write(address, word);
}

uint8_t Rastan::sound_read(uint16_t address) {
    if (address <= 0x3fff) return sound_rom_[address];
    if (address <= 0x7fff) return sound_bank_[sound_bank_index_][address & 0x3fff];
    if (address <= 0x8fff) return sound_ram_[address & 0xfff];
    if (address == 0x9001) return ym_.status();
    if (address == 0xa001) return syt_.slave_comm_r();
    return 0xff;
}

void Rastan::sound_write(uint16_t address, uint8_t value) {
    if (address <= 0x7fff) return;
    if (address <= 0x8fff) {
        sound_ram_[address & 0xfff] = value;
        return;
    }
    if (address == 0x9000) {
        ym_.select_register(value);
        return;
    }
    if (address == 0x9001) {
        ym_.write(value);
        return;
    }
    if (address == 0xa000) {
        syt_.slave_port_w(value);
        return;
    }
    if (address == 0xa001) {
        syt_.slave_comm_w(value);
        return;
    }
    if (address == 0xb000) {
        adpcm_pos_ = uint16_t((adpcm_pos_ & 0xff) | (uint16_t(value) << 8));
        return;
    }
    if (address == 0xc000) {
        msm_.set_reset(false);
        return;
    }
    if (address == 0xd000) {
        msm_.set_reset(true);
        adpcm_pos_ &= 0xff00;
        return;
    }
}

void Rastan::advance_adpcm() {
    // rastan_snd_adpcm in dsp-emulator: alternate high/low nibble of each byte.
    const size_t index = adpcm_pos_ & 0xffff;
    if (adpcm_have_val_) {
        msm_.data_w(adpcm_val_ & 0x0f);
        adpcm_have_val_ = false;
        adpcm_pos_ = uint16_t((adpcm_pos_ + 1) & 0xffff);
    } else {
        adpcm_val_ = index < adpcm_rom_.size() ? adpcm_rom_[index] : 0;
        msm_.data_w(adpcm_val_ >> 4);
        adpcm_have_val_ = true;
    }
}

void Rastan::on_sound_cycles(int cycles) {
    ym_.run_timers(cycles);

    msm_accumulator_ += int64_t(cycles) * msm_.sample_frequency();
    while (msm_accumulator_ >= int64_t(kSoundClock)) {
        msm_accumulator_ -= int64_t(kSoundClock);
        msm_.vclk();
    }

    audio_accumulator_ += int64_t(cycles) * YM2151::kSampleRate;
    while (audio_accumulator_ >= int64_t(kSoundClock)) {
        audio_accumulator_ -= int64_t(kSoundClock);
        int32_t sample = ym_.update() + msm_.output() / 4;
        audio_.push_back(int16_t(std::clamp(sample, -32768, 32767)));
    }
}

void Rastan::set_palette(int index, uint16_t value) {
    // BGR 555 (MAME xBGR_555): red in bits 0-4, green 5-9, blue 10-14.
    const uint8_t red = pal5bit(value);
    const uint8_t green = pal5bit(uint16_t(value >> 5));
    const uint8_t blue = pal5bit(uint16_t(value >> 10));
    palette_[size_t(index)] =
        0xff000000u | (uint32_t(red) << 16) | (uint32_t(green) << 8) | blue;
}

void Rastan::draw_tilemap(bool foreground) {
    std::vector<uint32_t>& target = foreground ? foreground_ : background_;
    const int base = foreground ? 0x4000 : 0;
    for (int tile = 0; tile < 0x1000; tile++) {
        const uint16_t attrib = ram2_[size_t(base + tile * 2)];
        const int code = ram2_[size_t(base + tile * 2 + 1)] & 0x3fff;
        const int color = (attrib & 0x7f) << 4;
        const bool flip_x = (attrib & 0x4000) != 0;
        const bool flip_y = (attrib & 0x8000) != 0;
        const int x = tile % 64;
        const int y = tile / 64;
        const uint8_t* pixels = tiles_.element(code);
        for (int row = 0; row < 8; row++) {
            const int source_y = flip_y ? 7 - row : row;
            for (int column = 0; column < 8; column++) {
                const int source_x = flip_x ? 7 - column : column;
                const uint8_t pen = pixels[source_y * 8 + source_x];
                const size_t dest = size_t((y * 8 + row) * kWorkWidth + x * 8 + column);
                if (foreground && pen == 0) {
                    target[dest] = 0;
                } else {
                    target[dest] = palette_[size_t(color + pen)];
                }
            }
        }
    }
}

void Rastan::blit_layer(const std::vector<uint32_t>& layer, uint16_t scroll_x, uint16_t scroll_y,
                        bool transparent) {
    for (int y = 0; y < kWorkHeight; y++) {
        const int source_y = (y + int(scroll_y)) & 0x1ff;
        for (int x = 0; x < kWorkWidth; x++) {
            const int source_x = (x + int(scroll_x)) & 0x1ff;
            const uint32_t pixel = layer[size_t(source_y * kWorkWidth + source_x)];
            if (transparent && pixel == 0) continue;
            composite_[size_t(y * kWorkWidth + x)] = pixel;
        }
    }
}

void Rastan::draw_sprites() {
    for (int index = 255; index >= 0; index--) {
        const uint16_t* entry = &ram3_[size_t(index * 4)];
        const int code = entry[2] & 0xfff;
        if (code == 0) continue;
        const uint16_t attrib = entry[0];
        const int color = ((attrib & 0x0f) | ((sprite_bank_ & 0x0f) << 4)) << 4;
        const bool flip_x = (attrib & 0x4000) != 0;
        const bool flip_y = (attrib & 0x8000) != 0;
        const int pos_x = (int(entry[3]) + 16) & 0x1ff;
        const int pos_y = int(entry[1]) & 0x1ff;
        const uint8_t* pixels = sprites_.element(code);
        for (int row = 0; row < 16; row++) {
            const int y = (pos_y + row) & 0x1ff;
            if (y < 0 || y >= kWorkHeight) continue;
            const int source_y = flip_y ? 15 - row : row;
            for (int column = 0; column < 16; column++) {
                const int x = (pos_x + column) & 0x1ff;
                if (x < 0 || x >= kWorkWidth) continue;
                const int source_x = flip_x ? 15 - column : column;
                const uint8_t pen = pixels[source_y * 16 + source_x];
                if (pen == 0) continue;
                composite_[size_t(y * kWorkWidth + x)] = palette_[size_t(color + pen)];
            }
        }
    }
}

void Rastan::update_video() {
    draw_tilemap(false);
    draw_tilemap(true);
    blit_layer(background_, scroll_x1_, scroll_y1_, false);
    blit_layer(foreground_, scroll_x2_, scroll_y2_, true);
    draw_sprites();
    // actualiza_trozo_final(16, 8, 320, 240): crop the visible window.
    for (int y = 0; y < kScreenHeight; y++) {
        const int source_y = y + 8;
        for (int x = 0; x < kScreenWidth; x++) {
            framebuffer_[size_t(y * kScreenWidth + x)] =
                composite_[size_t(source_y * kWorkWidth + x + 16)];
        }
    }
}

void Rastan::run_frame() {
    const int main_cycles = int(double(kMainClock) / kFramesPerSecond / kScanlines + 0.5);
    const int sound_cycles = int(double(kSoundClock) / kFramesPerSecond / kScanlines + 0.5);

    for (int line = 0; line < kScanlines; line++) {
        if (line == 248) {
            update_video();
            main_cpu_.set_irq(5, IrqLine::Hold);
        }
        main_cpu_.run(main_cycles);
        sound_cpu_.run(sound_cycles);
    }
}

void Rastan::set_inputs(const MachineInputs& inputs) {
    in0_ = 0x1f;
    in1_ = 0xff;
    in2_ = 0xff;
    if (inputs.coin1) in0_ = uint8_t(in0_ | 0x20);
    if (inputs.coin2) in0_ = uint8_t(in0_ | 0x40);

    auto apply = [](const InputState& state, uint8_t& reg) {
        if (state.up) reg = uint8_t(reg & 0xfe);
        if (state.down) reg = uint8_t(reg & 0xfd);
        if (state.left) reg = uint8_t(reg & 0xfb);
        if (state.right) reg = uint8_t(reg & 0xf7);
        if (state.button1) reg = uint8_t(reg & 0xef);
        if (state.button2) reg = uint8_t(reg & 0xdf);
    };
    apply(inputs.player1, in1_);
    apply(inputs.player2, in2_);
    if (inputs.player1.start) in0_ = uint8_t(in0_ & 0xf7);
    if (inputs.player2.start) in0_ = uint8_t(in0_ & 0xef);
}

void Rastan::set_dip_switch(int bank, uint8_t value) {
    if (bank == 0) dsw_a_ = value;
    if (bank == 1) dsw_b_ = value;
}

void Rastan::drain_audio(std::vector<int16_t>& out) {
    out.insert(out.end(), audio_.begin(), audio_.end());
    audio_.clear();
}

}  // namespace dsp