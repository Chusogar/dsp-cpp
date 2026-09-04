#include "drivers/arcade/opwolf.h"

#include <algorithm>
#include <filesystem>

#include "core/rom_loader.h"

namespace dsp {
namespace {

// MAME `ROM_START(opwolf)` in src/mame/taito/opwolf.cpp (World, rev 2, set 1).
// Names accept the `.icXX` labels used in some dumps; CRC is the MAME value.
const std::vector<RomEntry> kMainRoms = {
    {"b20-05-02.40|b20-05-02.ic40|b20-05-2.40", 0x10000, 0x00000, 0x3ffbfe3a},
    {"b20-03-02.30|b20-03-02.ic30|b20-03-2.30", 0x10000, 0x00001, 0xfdabd8a5},
    {"b20-04.39|b20-04.ic39", 0x10000, 0x20000, 0x216b4838},
    {"b20-20.29|b20-20.ic29", 0x10000, 0x20001, 0xd244431a},
};
const std::vector<RomEntry> kSoundRoms = {{"b20-07.10|b20-07.ic10", 0x10000, 0, 0x45c7ace3}};
const RomEntry kCchipEprom{"b20-18.73|b20-18.ic73", 0x2000, 0, 0x5987b4e9};
const RomEntry kCchipMcu{"cchip_upd78c11.bin", 0x1000, 0, 0x43021521};
const std::vector<RomEntry> kTileRoms = {{"b20-13.13|b20-13.ic13", 0x80000, 0, 0xf6acdab1}};
const std::vector<RomEntry> kSpriteRoms = {{"b20-14.72|b20-14.ic72", 0x80000, 0, 0x89f889e5}};
const std::vector<RomEntry> kAdpcm = {{"b20-08.21|b20-08.ic21", 0x80000, 0, 0xf3e19c64}};

// Load by MAME filename, then by CRC so a renamed dump of the same set still works.
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

bool load_from_zip(const std::string& path, const RomEntry& entry, std::vector<uint8_t>& dest) {
    RomLoader extra;
    std::string extra_error;
    return extra.open(path, &extra_error) && load_bytes(extra, entry, dest, &extra_error);
}

bool load_cchip_mcu(const std::string& rom_path, RomLoader& game, std::vector<uint8_t>& dest,
                    std::string* error) {
    std::string game_error;
    if (load_bytes(game, kCchipMcu, dest, &game_error)) return true;

    namespace fs = std::filesystem;
    const fs::path parent = fs::path(rom_path).parent_path();
    const char* extras[] = {"taito_cchip.zip", "cchip.zip"};
    for (const char* zip : extras) {
        const fs::path sibling = parent.empty() ? fs::path(zip) : parent / zip;
        if (load_from_zip(sibling.string(), kCchipMcu, dest)) return true;
        if (load_from_zip((fs::path("/tmp/roms") / zip).string(), kCchipMcu, dest)) return true;
    }
    if (error) {
        *error = game_error.empty()
                     ? "missing C-Chip MCU ROM cchip_upd78c11.bin (MAME taito_cchip device)"
                     : game_error;
    }
    return false;
}

const std::vector<int> kPixelX = {8,  12, 0,  4,  24, 28, 16, 20,
                                  40, 44, 32, 36, 56, 60, 48, 52};

GfxLayout tile_layout() {
    GfxLayout layout;
    layout.width = 8;
    layout.height = 8;
    layout.total = 0x4000;
    layout.planes = 4;
    layout.char_increment = 32 * 8;
    layout.plane_offsets = {0, 1, 2, 3};
    layout.x_offsets = std::vector<int>(kPixelX.begin(), kPixelX.begin() + 8);
    layout.y_offsets = {0 * 32, 1 * 32, 2 * 32, 3 * 32, 4 * 32, 5 * 32, 6 * 32, 7 * 32};
    return layout;
}

GfxLayout sprite_layout() {
    GfxLayout layout;
    layout.width = 16;
    layout.height = 16;
    layout.total = 0x1000;
    layout.planes = 4;
    layout.char_increment = 128 * 8;
    layout.plane_offsets = {0, 1, 2, 3};
    layout.x_offsets = kPixelX;
    layout.y_offsets = {0 * 64,  1 * 64,  2 * 64,  3 * 64,  4 * 64,  5 * 64,  6 * 64,  7 * 64,
                        8 * 64,  9 * 64,  10 * 64, 11 * 64, 12 * 64, 13 * 64, 14 * 64, 15 * 64};
    return layout;
}

inline uint8_t pal4bit(uint16_t value) { return uint8_t((value & 0x0f) * 0x11); }

}  // namespace

OpWolf::OpWolf()
    : main_cpu_(kMainClock),
      sound_cpu_(kSoundClock),
      ym_(kYmClock),
      msm0_(kMsmClock, 48, 4),
      msm1_(kMsmClock, 48, 4) {
    background_.assign(size_t(kWorkWidth) * kWorkHeight, 0xff000000u);
    foreground_.assign(size_t(kWorkWidth) * kWorkHeight, 0);
    composite_.assign(size_t(kWorkWidth) * kWorkHeight, 0xff000000u);
    framebuffer_.assign(size_t(kScreenWidth) * kScreenHeight, 0xff000000u);

    main_cpu_.set_memory_handlers([this](uint32_t address) { return main_read(address); },
                                  [this](uint32_t address, uint16_t value) {
                                      main_write(address, value);
                                  });
    // Byte accesses to the TC0140SYT must not read-modify-write the word: a
    // dummy read of $3E0002 consumes a mailbox nibble and kills the handshake
    // (Pascal only returns comm_r on read_8bits_hi_dir).
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
}

bool OpWolf::init(const std::string& rom_path, std::string* error) {
    if (!load_roms(rom_path, error)) return false;
    cchip_.set_rom(rom_.data(), rom_.size());
    reset();
    return true;
}

bool OpWolf::load_roms(const std::string& rom_path, std::string* error) {
    RomLoader loader;
    if (!loader.open(rom_path, error)) return false;

    // ROM_LOAD16_BYTE: even file = high byte of each 68000 word.
    rom_.assign(0x20000, 0);
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
    if (!load_raw(loader, kSoundRoms, sound_rom, error)) return false;
    std::copy(sound_rom.begin(), sound_rom.begin() + 0x4000, sound_rom_.begin());
    for (int bank = 0; bank < 4; bank++) {
        std::copy(sound_rom.begin() + bank * 0x4000, sound_rom.begin() + (bank + 1) * 0x4000,
                  sound_bank_[size_t(bank)].begin());
    }

    // MAME maps these as cchip:cchip_eprom and the TAITO_CCHIP device MCU ROM.
    // The driver still uses the software C-Chip, but the set is rejected unless
    // both dumps are present so a MAME `opwolf` zip is used as-is.
    std::vector<uint8_t> cchip_eprom;
    if (!load_bytes(loader, kCchipEprom, cchip_eprom, error)) return false;
    std::vector<uint8_t> cchip_mcu;
    if (!load_cchip_mcu(rom_path, loader, cchip_mcu, error)) return false;
    if (cchip_eprom.size() != kCchipEprom.length || cchip_mcu.size() != kCchipMcu.length) {
        if (error) *error = "C-Chip ROMs have the wrong size";
        return false;
    }

    std::vector<uint8_t> tile_rom(0x80000, 0);
    if (!load_raw(loader, kTileRoms, tile_rom, error)) return false;
    std::vector<uint8_t> sprite_rom(0x80000, 0);
    if (!load_raw(loader, kSpriteRoms, sprite_rom, error)) return false;
    decode_graphics(tile_rom, sprite_rom);

    // Both MSM5205 chips share the single 512 KiB ADPCM region (`b20-08.21`).
    std::vector<uint8_t> adpcm(0x80000, 0);
    if (!load_raw(loader, kAdpcm, adpcm, error)) return false;
    msm0_.set_rom(adpcm);
    msm1_.set_rom(adpcm);

    for (const std::string& warning : loader.warnings()) warnings_.push_back(warning);
    return true;
}

void OpWolf::decode_graphics(const std::vector<uint8_t>& tile_rom,
                             const std::vector<uint8_t>& sprite_rom) {
    tiles_.decode(tile_layout(), tile_rom);
    sprites_.decode(sprite_layout(), sprite_rom);
}

void OpWolf::reset_sound() {
    sound_cpu_.reset();
    ym_.reset();
    msm0_.reset();
    msm1_.reset();
    sound_bank_index_ = 0;
    adpcm_b_.fill(0);
    adpcm_c_.fill(0);
    audio_accumulator_ = 0;
    msm_accumulator_ = 0;
}

void OpWolf::reset() {
    main_cpu_.reset();
    syt_.reset();
    reset_sound();
    cchip_.reset();
    in0_ = 0xfc;
    in1_ = 0xff;
    scroll_x1_ = scroll_y1_ = 0;
    scroll_x2_ = scroll_y2_ = 0;
    sprite_bank_ = 0;
    gun_x_ = 175;
    gun_y_ = 120;
    audio_.clear();
}

uint16_t OpWolf::main_read(uint32_t address) {
    address &= 0xffffffu;
    if (address <= 0x3ffff) {
        const uint32_t word = address >> 1;
        return word < rom_.size() ? rom_[word] : 0;
    }
    if (address >= 0x0f0000 && address <= 0x0fffff) {
        const uint32_t sub = address & 0xfff;
        if (sub <= 0x7ff) return cchip_.data_r(uint16_t(sub));
        if ((sub & ~1u) == 0x802) return cchip_.status_r();
        return 0;
    }
    if (address >= 0x100000 && address <= 0x107fff) {
        return ram1_[(address & 0x7fff) >> 1];
    }
    if (address >= 0x200000 && address <= 0x200fff) {
        return palette_ram_[(address & 0xfff) >> 1];
    }
    if ((address & ~1u) == 0x380000) return dsw_a_;
    if ((address & ~1u) == 0x380002) return dsw_b_;
    if ((address & ~1u) == 0x3a0000) return gun_x_;
    if ((address & ~1u) == 0x3a0002) return gun_y_;
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

void OpWolf::main_write(uint32_t address, uint16_t value) {
    address &= 0xffffffu;
    if (address <= 0x3ffff) return;
    if (address >= 0x0ff000 && address <= 0x0ff7ff) {
        cchip_.data_w(uint16_t(address & 0x7ff), value);
        return;
    }
    if ((address & ~1u) == 0x0ff802) {
        cchip_.status_w(value);
        return;
    }
    if ((address & ~1u) == 0x0ffc00) {
        cchip_.bank_w(value);
        return;
    }
    if (address >= 0x100000 && address <= 0x107fff) {
        ram1_[(address & 0x7fff) >> 1] = value;
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
    if (address >= 0xd00000 && address <= 0xd03fff) {
        ram3_[(address & 0x3fff) >> 1] = value;
        return;
    }
}

uint8_t OpWolf::main_read_byte(uint32_t address) {
    address &= 0xffffffu;
    if ((address & ~1u) == 0x3e0002) {
        return (address & 1u) == 0 ? syt_.comm_r() : 0;
    }
    const uint16_t word = main_read(address);
    return (address & 1u) ? uint8_t(word) : uint8_t(word >> 8);
}

void OpWolf::main_write_byte(uint32_t address, uint8_t value) {
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

uint8_t OpWolf::sound_read(uint16_t address) {
    if (address <= 0x3fff) return sound_rom_[address];
    if (address <= 0x7fff) return sound_bank_[sound_bank_index_][address & 0x3fff];
    if (address <= 0x8fff) return sound_ram_[address & 0xfff];
    if (address == 0x9001) return ym_.status();
    if (address == 0xa001) return syt_.slave_comm_r();
    return 0xff;
}

void OpWolf::start_adpcm(MSM5205& chip, const uint8_t* regs) {
    const uint32_t start = (uint32_t(regs[0]) + (uint32_t(regs[1]) << 8)) * 16;
    const uint32_t end = (uint32_t(regs[2]) + (uint32_t(regs[3]) << 8)) * 16;
    chip.set_start(start);
    chip.set_end(end);
    chip.set_reset(false);
}

void OpWolf::sound_write(uint16_t address, uint8_t value) {
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
    if (address >= 0xb000 && address <= 0xb006) {
        adpcm_b_[address & 7] = value;
        if ((address & 7) == 4) start_adpcm(msm0_, adpcm_b_.data());
        return;
    }
    if (address >= 0xc000 && address <= 0xc006) {
        adpcm_c_[address & 7] = value;
        if ((address & 7) == 4) start_adpcm(msm1_, adpcm_c_.data());
        return;
    }
}

void OpWolf::on_sound_cycles(int cycles) {
    ym_.run_timers(cycles);

    msm_accumulator_ += int64_t(cycles) * msm0_.sample_frequency();
    while (msm_accumulator_ >= int64_t(kSoundClock)) {
        msm_accumulator_ -= int64_t(kSoundClock);
        msm0_.vclk();
        msm1_.vclk();
    }

    audio_accumulator_ += int64_t(cycles) * YM2151::kSampleRate;
    while (audio_accumulator_ >= int64_t(kSoundClock)) {
        audio_accumulator_ -= int64_t(kSoundClock);
        int32_t sample = ym_.update() + (msm0_.output() + msm1_.output()) / 4;
        audio_.push_back(int16_t(std::clamp(sample, -32768, 32767)));
    }
}

void OpWolf::set_palette(int index, uint16_t value) {
    const uint8_t red = pal4bit(uint16_t(value >> 8));
    const uint8_t green = pal4bit(uint16_t(value >> 4));
    const uint8_t blue = pal4bit(value);
    palette_[size_t(index)] =
        0xff000000u | (uint32_t(red) << 16) | (uint32_t(green) << 8) | blue;
}

void OpWolf::draw_tilemap(bool foreground) {
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

void OpWolf::blit_layer(const std::vector<uint32_t>& layer, uint16_t scroll_x, uint16_t scroll_y,
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

void OpWolf::draw_sprites() {
    for (int index = 255; index >= 0; index--) {
        const uint16_t* entry = &ram3_[size_t(index * 4)];
        const int code = entry[2] & 0xfff;
        if (code == 0) continue;
        const uint16_t attrib = entry[0];
        const int color = ((attrib & 0x0f) | ((sprite_bank_ & 0x0f) << 4)) << 4;
        const bool flip_x = (attrib & 0x4000) != 0;
        const bool flip_y = (attrib & 0x8000) != 0;
        const int pos_x = int(entry[3]) + 16;
        const int pos_y = int(entry[1]);
        const uint8_t* pixels = sprites_.element(code);
        for (int row = 0; row < 16; row++) {
            const int y = pos_y + row;
            if (y < 0 || y >= kWorkHeight) continue;
            const int source_y = flip_y ? 15 - row : row;
            for (int column = 0; column < 16; column++) {
                const int x = pos_x + column;
                if (x < 0 || x >= kWorkWidth) continue;
                const int source_x = flip_x ? 15 - column : column;
                const uint8_t pen = pixels[source_y * 16 + source_x];
                if (pen == 0) continue;
                composite_[size_t(y * kWorkWidth + x)] = palette_[size_t(color + pen)];
            }
        }
    }
}

void OpWolf::draw_sight() {
    // Host overlay matching dsp-emulator's show_mouse_cursor / MAME's light-gun
    // crosshair. The 68000 gun ports are offset by +15 in X (visible crop).
    const int cx = int(gun_x_) - 15;
    const int cy = int(gun_y_);
    auto plot = [&](int x, int y, uint32_t color) {
        if (x < 0 || y < 0 || x >= kScreenWidth || y >= kScreenHeight) return;
        framebuffer_[size_t(y * kScreenWidth + x)] = color;
    };
    const uint32_t outline = 0xff000000u;
    const uint32_t fill = 0xffffffffu;
    for (int d = -6; d <= 6; d++) {
        if (d >= -1 && d <= 1) continue;
        plot(cx + d, cy - 1, outline);
        plot(cx + d, cy, fill);
        plot(cx + d, cy + 1, outline);
        plot(cx - 1, cy + d, outline);
        plot(cx, cy + d, fill);
        plot(cx + 1, cy + d, outline);
    }
    for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
            plot(cx + dx, cy + dy, (dx == 0 && dy == 0) ? fill : outline);
        }
    }
}

void OpWolf::update_video() {
    draw_tilemap(false);
    draw_tilemap(true);
    blit_layer(background_, scroll_x1_, scroll_y1_, false);
    draw_sprites();
    blit_layer(foreground_, scroll_x2_, scroll_y2_, true);
    for (int y = 0; y < kScreenHeight; y++) {
        const int source_y = y + 8;
        for (int x = 0; x < kScreenWidth; x++) {
            framebuffer_[size_t(y * kScreenWidth + x)] =
                composite_[size_t(source_y * kWorkWidth + x + 16)];
        }
    }
    draw_sight();
}

void OpWolf::run_frame() {
    const int main_cycles = int(double(kMainClock) / kFramesPerSecond / kScanlines + 0.5);
    const int sound_cycles = int(double(kSoundClock) / kFramesPerSecond / kScanlines + 0.5);

    for (int line = 0; line < kScanlines; line++) {
        if (line == 248) {
            update_video();
            main_cpu_.set_irq(5, IrqLine::Hold);
            // Match dsp-emulator: the C-Chip 60 Hz timer is tied to the 68000
            // and fires around vblank, after the 68k has already run the frame.
            cchip_.set_inputs(in0_, in1_);
            cchip_.update();
        }
        main_cpu_.run(main_cycles);
        cchip_.run_cycles(main_cycles);
        sound_cpu_.run(sound_cycles);
    }
}

void OpWolf::set_inputs(const MachineInputs& inputs) {
    in0_ = 0xfc;
    in1_ = 0xff;
    if (inputs.coin1) in0_ = uint8_t(in0_ | 0x01);
    if (inputs.coin2) in0_ = uint8_t(in0_ | 0x02);

    const bool fire = inputs.player1.button1 || inputs.pointer_button1;
    const bool grenade = inputs.player1.button2 || inputs.pointer_button2;
    if (fire) in1_ = uint8_t(in1_ & 0xfe);
    if (grenade) in1_ = uint8_t(in1_ & 0xfd);
    if (inputs.player1.start) in1_ = uint8_t(in1_ & 0xef);

    if (inputs.has_pointer) {
        gun_x_ = uint16_t(std::clamp(inputs.pointer_x, 0, kScreenWidth - 1) + 15);
        gun_y_ = uint16_t(std::clamp(inputs.pointer_y, 0, kScreenHeight - 1));
    } else {
        int x = int(gun_x_);
        int y = int(gun_y_);
        if (inputs.player1.left) x -= 4;
        if (inputs.player1.right) x += 4;
        if (inputs.player1.up) y -= 4;
        if (inputs.player1.down) y += 4;
        gun_x_ = uint16_t(std::clamp(x, 15, 15 + kScreenWidth - 1));
        gun_y_ = uint16_t(std::clamp(y, 0, kScreenHeight - 1));
    }
}

void OpWolf::set_dip_switch(int bank, uint8_t value) {
    if (bank == 0) dsw_a_ = value;
    if (bank == 1) dsw_b_ = value;
}

void OpWolf::drain_audio(std::vector<int16_t>& out) {
    out.insert(out.end(), audio_.begin(), audio_.end());
    audio_.clear();
}

}  // namespace dsp
