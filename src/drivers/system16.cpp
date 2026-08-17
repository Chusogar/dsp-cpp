#include "drivers/system16.h"

#include <algorithm>
#include <cstring>

namespace dsp {
namespace {

const std::vector<RomEntry> kFantzoneMain = {
    {"epr-7385a.43", 0x8000, 0x00000, 0x4091af42},
    {"epr-7382a.26", 0x8000, 0x00001, 0x77d67bfd},
    {"epr-7386a.42", 0x8000, 0x10000, 0xb0a67cd0},
    {"epr-7383a.25", 0x8000, 0x10001, 0x5f79b2a9},
    {"epr-7387.41", 0x8000, 0x20000, 0x0acd335d},
    {"epr-7384.24", 0x8000, 0x20001, 0xfd909341},
};
const std::vector<RomEntry> kFantzoneSound = {{"epr-7535a.12", 0x8000, 0, 0xbc1374fa}};
const std::vector<RomEntry> kFantzoneTiles = {
    {"epr-7388.95", 0x8000, 0x0000, 0x8eb02f6b},
    {"epr-7389.94", 0x8000, 0x8000, 0x2f4f71b8},
    {"epr-7390.93", 0x8000, 0x10000, 0xd90609c6},
};
const std::vector<RomEntry> kFantzoneSprites = {
    {"epr-7392.10", 0x8000, 0x00000, 0x5bb7c8b6},
    {"epr-7396.11", 0x8000, 0x00001, 0x74ae4b57},
    {"epr-7393.17", 0x8000, 0x10000, 0x14fc7e82},
    {"epr-7397.18", 0x8000, 0x10001, 0xe05a1e25},
    {"epr-7394.23", 0x8000, 0x20000, 0x531ca13f},
    {"epr-7398.24", 0x8000, 0x20001, 0x68807b49},
};

const std::vector<RomEntry> kShinobiMain = {
    {"epr-12010.43", 0x10000, 0x00000, 0x7df7f4a2},
    {"epr-12008.26", 0x10000, 0x00001, 0xf5ae64cd},
    {"epr-12011.42", 0x10000, 0x20000, 0x9d46e707},
    {"epr-12009.25", 0x10000, 0x20001, 0x7961d07e},
};
const std::vector<RomEntry> kShinobiSound = {{"epr-11267.12", 0x8000, 0, 0xdd50b745}};
const std::vector<RomEntry> kShinobiTiles = {
    {"epr-11264.95", 0x10000, 0x00000, 0x46627e7d},
    {"epr-11265.94", 0x10000, 0x10000, 0x87d0f321},
    {"epr-11266.93", 0x10000, 0x20000, 0xefb4af87},
};
const std::vector<RomEntry> kShinobiSprites = {
    {"epr-11290.10", 0x10000, 0x00000, 0x611f413a},
    {"epr-11294.11", 0x10000, 0x00001, 0x5eb00fc1},
    {"epr-11291.17", 0x10000, 0x20000, 0x3c0797c0},
    {"epr-11295.18", 0x10000, 0x20001, 0x25307ef8},
    {"epr-11292.23", 0x10000, 0x40000, 0xc29ac34e},
    {"epr-11296.24", 0x10000, 0x40001, 0x04a437f8},
    {"epr-11293.29", 0x10000, 0x60000, 0x41f41063},
    {"epr-11297.30", 0x10000, 0x60001, 0xb6e1fd72},
};

const std::vector<RomEntry> kTetrisMain = {
    {"xepr12201.rom", 0x8000, 1, 0x343c0670},
    {"xepr12200.rom", 0x8000, 0, 0x0b694740},
};
const std::vector<RomEntry> kTetrisSound = {{"epr-12205.rom", 0x8000, 0, 0x6695dc99}};
const std::vector<RomEntry> kTetrisTiles = {
    {"epr-12202.rom", 0x10000, 0x00000, 0x2f7da741},
    {"epr-12203.rom", 0x10000, 0x10000, 0xa6e58ec5},
    {"epr-12204.rom", 0x10000, 0x20000, 0x0ae98e23},
};
const std::vector<RomEntry> kTetrisSprites = {
    {"epr-12169.b1", 0x8000, 0, 0xdacc6165},
    {"epr-12170.b5", 0x8000, 1, 0x87354e42},
};

const std::vector<RomEntry> kAltbeastMain = {
    {"epr-11907.a7", 0x20000, 0, 0x29e0c3ad},
    {"epr-11906.a5", 0x20000, 1, 0x4c9e9cd8},
};
const std::vector<RomEntry> kAltbeastSound = {
    {"epr-11671.a10", 0x8000, 0x0000, 0x2b71343b},
    {"opr-11672.a11", 0x20000, 0x8000, 0xbbd7f460},
    {"opr-11673.a12", 0x20000, 0x28000, 0x400c4a36},
};
const std::vector<RomEntry> kAltbeastMcu = {{"317-0078.c2", 0x1000, 0, 0x8101925f}};
const std::vector<RomEntry> kAltbeastTiles = {
    {"opr-11674.a14", 0x20000, 0x00000, 0xa57a66d5},
    {"opr-11675.a15", 0x20000, 0x20000, 0x2ef2f144},
    {"opr-11676.a16", 0x20000, 0x40000, 0x0c04acac},
};
const std::vector<RomEntry> kAltbeastSprites = {
    {"epr-11677.b1", 0x20000, 1, 0xa01425cd},
    {"epr-11681.b5", 0x20000, 0, 0xd9e03363},
    {"epr-11678.b2", 0x20000, 0x40001, 0x17a9fc53},
    {"epr-11682.b6", 0x20000, 0x40000, 0xe3f77c5e},
    {"epr-11679.b3", 0x20000, 0x80001, 0x14dcc245},
    {"epr-11683.b7", 0x20000, 0x80000, 0xf9a60f06},
    {"epr-11680.b4", 0x20000, 0xc0001, 0xf43dcdec},
    {"epr-11684.b8", 0x20000, 0xc0000, 0xb20c0edb},
};

void scramble_s16a_sprites(std::vector<uint16_t>& dest, const std::vector<uint8_t>& raw) {
    std::vector<uint8_t> out(0x80000, 0);
    auto copy64k = [&](uint32_t dst, uint32_t src) {
        if (src + 0x10000 > raw.size()) return;
        std::copy(raw.begin() + src, raw.begin() + src + 0x10000, out.begin() + dst);
    };
    copy64k(0x00000, 0x00000);
    copy64k(0x40000, 0x10000);
    copy64k(0x10000, 0x20000);
    copy64k(0x50000, 0x30000);
    copy64k(0x20000, 0x40000);
    copy64k(0x60000, 0x50000);
    copy64k(0x30000, 0x60000);
    copy64k(0x70000, 0x70000);
    dest.resize(out.size() / 2);
    for (size_t i = 0; i < dest.size(); i++) {
        dest[i] = uint16_t((out[i * 2] << 8) | out[i * 2 + 1]);
    }
}

bool load_sprites_16b_bytes(RomLoader& loader, const std::vector<RomEntry>& entries,
                            std::vector<uint8_t>& raw, std::string* error) {
    uint32_t bytes = 0;
    for (const RomEntry& entry : entries) {
        bytes = std::max(bytes, entry.offset + entry.length * 2);
    }
    raw.assign(bytes, 0);
    for (const RomEntry& entry : entries) {
        std::vector<uint8_t> data(entry.length, 0);
        RomEntry single{entry.name, entry.length, 0, entry.crc};
        if (!loader.load({single}, data, error)) return false;
        for (uint32_t i = 0; i < entry.length; i++) raw[entry.offset + i * 2] = data[i];
    }
    return true;
}

}  // namespace

System16::System16(Game game)
    : game_(game),
      main_cpu_(kMainClock),
      sound_cpu_(game == Game::Altbeast ? 5000000u : 4000000u),
      ym_(4000000),
      framebuffer_(kScreenWidth * kScreenHeight, 0) {
    fps_ = is_16b() ? 60.05439 : 60.0;
    sound_clock_ = is_16b() ? 5000000u : 4000000u;
    use_mcu_ = (game == Game::Altbeast);
    main_cpu_.set_memory_handlers([this](uint32_t a) { return main_read(a); },
                                  [this](uint32_t a, uint16_t v) { main_write(a, v); });
    sound_cpu_.set_memory_handlers([this](uint16_t a) { return sound_read(a); },
                                   [this](uint16_t a, uint8_t v) { sound_write(a, v); });
    sound_cpu_.set_io_handlers([this](uint16_t p) { return sound_in(p); },
                               [this](uint16_t p, uint8_t v) { sound_out(p, v); });
    sound_cpu_.set_cycle_handler([this](int cycles) { on_sound_cycles(cycles); });
    if (!is_16b()) {
        ppi_.set_port_handlers(nullptr, nullptr, nullptr,
                               [this](uint8_t value) { sound_latch_ = value; },
                               [this](uint8_t value) { video_.screen_enabled = (value & 0x10) != 0; },
                               [this](uint8_t value) {
                                   sound_cpu_.set_nmi((value & 0x80) ? IrqLine::Clear
                                                                     : IrqLine::Assert);
                               });
    } else {
        mapper_.set_sound_latch([this](uint8_t value) {
            sound_latch_ = value;
            sound_cpu_.set_irq(IrqLine::Assert);
        });
        mapper_.set_open_bus([this]() -> uint8_t {
            if (rom_.empty()) return 0xff;
            return uint8_t(rom_[(main_cpu_.pc() >> 1) % rom_.size()]);
        });
        mapper_.set_bus_handlers([this](uint32_t a) { return read_16b(a); },
                                 [this](uint32_t a, uint16_t v) { write_16b(a, v, false); });
        mapper_.set_reset_handler([this](IrqLine state) { main_cpu_.set_reset_line(state); });
        mapper_.set_irq_handler(
            [this](int level, IrqLine state) { main_cpu_.set_irq(level, state); });
        mcu_ = std::make_unique<Mcs51>(8000000);
        mcu_->set_external_handlers(
            [this](uint16_t address) { return mapper_.read_reg(uint8_t(address & 0x1f)); },
            [this](uint16_t address, uint8_t value) {
                const uint32_t old = mapper_.dirs_start(5);
                mapper_.write_reg(uint8_t(address & 0x1f), value);
                if (old != mapper_.dirs_start(5)) {
                    for (auto& page : video_.tile_dirty) page.fill(false);
                }
            });
        mcu_->set_port_read_handler(1, [this]() { return uint8_t(in0_); });
    }
}

const char* System16::title() const {
    switch (game_) {
        case Game::Fantzone: return "Fantasy Zone";
        case Game::Shinobi: return "Shinobi";
        case Game::Tetris: return "Tetris";
        case Game::Altbeast: return "Altered Beast";
    }
    return "System 16";
}

bool System16::init(const std::string& rom_path, std::string* error) {
    if (!load_roms(rom_path, error)) return false;
    video_.init_palette_luts();
    reset();
    return true;
}

bool System16::load_roms(const std::string& rom_path, std::string* error) {
    RomLoader loader;
    if (!loader.open(rom_path, error)) return false;
    const std::vector<RomEntry>* main = nullptr;
    const std::vector<RomEntry>* sound = nullptr;
    const std::vector<RomEntry>* tiles = nullptr;
    const std::vector<RomEntry>* sprites = nullptr;
    bool scramble = false;
    switch (game_) {
        case Game::Fantzone:
            main = &kFantzoneMain;
            sound = &kFantzoneSound;
            tiles = &kFantzoneTiles;
            sprites = &kFantzoneSprites;
            tile_n_ = 1;
            sprite_banks_ = 4;
            dsw_b_ = 0xfc;
            break;
        case Game::Shinobi:
            main = &kShinobiMain;
            sound = &kShinobiSound;
            tiles = &kShinobiTiles;
            sprites = &kShinobiSprites;
            tile_n_ = 2;
            sprite_banks_ = 8;
            dsw_b_ = 0xfc;
            scramble = true;
            break;
        case Game::Tetris:
            main = &kTetrisMain;
            sound = &kTetrisSound;
            tiles = &kTetrisTiles;
            sprites = &kTetrisSprites;
            tile_n_ = 2;
            sprite_banks_ = 1;
            dsw_b_ = 0x30;
            break;
        case Game::Altbeast:
            main = &kAltbeastMain;
            sound = &kAltbeastSound;
            tiles = &kAltbeastTiles;
            sprites = &kAltbeastSprites;
            tile_n_ = 4;
            sprite_banks_ = 8;
            dsw_b_ = 0xfd;
            video_.tile_banks = 3;
            break;
    }
    if (!load_roms16w(loader, *main, rom_, error)) return false;
    std::vector<uint8_t> sound_bytes;
    if (!load_rom_bytes(loader, *sound, sound_bytes, error)) return false;
    std::fill(sound_mem_.begin(), sound_mem_.end(), 0);
    const size_t copy_n = std::min(sound_bytes.size(), size_t(0x8000));
    std::copy(sound_bytes.begin(), sound_bytes.begin() + int(copy_n), sound_mem_.begin());
    if (is_16b()) {
        for (int bank = 0; bank < 16; bank++) {
            const size_t src = 0x8000 + size_t(bank) * 0x4000;
            sound_bank_[size_t(bank)].fill(0);
            if (src < sound_bytes.size()) {
                const size_t n = std::min(size_t(0x4000), sound_bytes.size() - src);
                std::copy(sound_bytes.begin() + int(src), sound_bytes.begin() + int(src + n),
                          sound_bank_[size_t(bank)].begin());
            }
        }
        std::vector<uint8_t> mcu_rom;
        if (!load_rom_bytes(loader, kAltbeastMcu, mcu_rom, error)) return false;
        std::fill(mcu_->rom(), mcu_->rom() + Mcs51::kRomSize, 0);
        std::copy(mcu_rom.begin(), mcu_rom.end(), mcu_->rom());
        if (!load_roms16w(loader, *sprites, sprite_rom_, error)) return false;
    } else if (scramble) {
        std::vector<uint8_t> raw;
        if (!load_sprites_16b_bytes(loader, *sprites, raw, error)) return false;
        scramble_s16a_sprites(sprite_rom_, raw);
    } else {
        if (!load_roms16b(loader, *sprites, sprite_rom_, error)) return false;
    }
    std::vector<uint8_t> tile_bytes;
    if (!load_rom_bytes(loader, *tiles, tile_bytes, error)) return false;
    decode_s16_tiles(video_.tiles, tile_bytes, tile_n_);
    return true;
}

void System16::reset() {
    if (is_16b()) mapper_.reset();
    main_cpu_.reset();
    sound_cpu_.reset();
    ym_.reset();
    if (!is_16b()) ppi_.reset();
    if (mcu_) mcu_->reset();
    video_.reset();
    video_.screen_enabled = !is_16b();
    video_.tile_bank[0] = 0;
    video_.tile_bank[1] = 1;
    ram_.fill(0);
    in0_ = 0xffff;
    in1_ = 0xffff;
    in2_ = 0xffff;
    sound_latch_ = 0;
    sound_bank_num_ = 0;
    audio_acc_ = 0;
    audio_.clear();
    std::fill(framebuffer_.begin(), framebuffer_.end(), 0);
}

void System16::set_inputs(const MachineInputs& inputs) {
    auto apply_player = [](uint16_t& port, const InputState& p, bool system16b) {
        if (system16b) {
            port = 0xffff;
            if (p.button3) port &= ~0x0001;
            if (p.button1) port &= ~0x0002;
            if (p.button2) port &= ~0x0004;
            if (p.down) port &= ~0x0010;
            if (p.up) port &= ~0x0020;
            if (p.right) port &= ~0x0040;
            if (p.left) port &= ~0x0080;
        } else {
            if (p.up) port &= ~0x0020;
            else port |= 0x0020;
            if (p.down) port &= ~0x0010;
            else port |= 0x0010;
            if (p.left) port &= ~0x0080;
            else port |= 0x0080;
            if (p.right) port &= ~0x0040;
            else port |= 0x0040;
            if (p.button2) port &= ~0x0004;
            else port |= 0x0004;
            if (p.button1) port &= ~0x0002;
            else port |= 0x0002;
            if (p.button3) port &= ~0x0001;
            else port |= 0x0001;
        }
    };
    if (is_16b()) {
        in0_ = 0xffff;
        apply_player(in1_, inputs.player1, true);
        apply_player(in2_, inputs.player2, true);
        if (inputs.coin1) in0_ &= ~0x0001;
        if (inputs.coin2) in0_ &= ~0x0002;
        if (inputs.player1.start) in0_ &= ~0x0010;
        if (inputs.player2.start) in0_ &= ~0x0020;
    } else {
        apply_player(in1_, inputs.player1, false);
        apply_player(in2_, inputs.player2, false);
        if (inputs.player1.start) in0_ &= ~0x0010;
        else in0_ |= 0x0010;
        if (inputs.player2.start) in0_ &= ~0x0020;
        else in0_ |= 0x0020;
        if (inputs.coin1) in0_ &= ~0x0001;
        else in0_ |= 0x0001;
        if (inputs.coin2) in0_ &= ~0x0002;
        else in0_ |= 0x0002;
    }
}

void System16::set_dip_switch(int bank, uint8_t value) {
    if (bank == 0) dsw_a_ = value;
    if (bank == 1) dsw_b_ = value;
}

void System16::drain_audio(std::vector<int16_t>& out) {
    out.swap(audio_);
    audio_.clear();
}

uint16_t System16::io_16a(uint16_t address) {
    switch (address & 0x3000) {
        case 0x0000:
            return ppi_.read((address >> 1) & 3);
        case 0x1000:
            switch (address & 7) {
                case 0:
                case 1: return in0_;
                case 2:
                case 3: return in1_;
                case 6:
                case 7: return in2_;
                default: return 0xff;
            }
        case 0x2000:
            return (address & 2) ? dsw_b_ : dsw_a_;
        default:
            return 0xffff;
    }
}

uint16_t System16::io_16b(uint16_t address) {
    switch (address & 0x1800) {
        case 0x0800:
            switch (address & 3) {
                case 0: return in0_;
                case 1: return in1_;
                case 2: return 0xffff;
                case 3: return in2_;
            }
            break;
        case 0x1000:
            return (address & 1) ? dsw_a_ : dsw_b_;
        default:
            break;
    }
    return 0xffff;
}

uint16_t System16::read_16a(uint32_t address) {
    address &= 0xffffff;
    if (address <= 0x3fffff) return rom_[(address & 0x3ffff) >> 1];
    if (address >= 0x400000 && address <= 0x7fffff) {
        switch (address & 0x7ffff) {
            case 0x00000 ... 0x0ffff: return video_.tile_ram[(address & 0x7fff) >> 1];
            case 0x10000 ... 0x1ffff: return video_.char_ram[(address & 0xfff) >> 1];
            case 0x40000 ... 0x7ffff: return video_.sprite_ram[(address & 0x7ff) >> 1];
            default: return 0xffff;
        }
    }
    if (address >= 0x800000 && address <= 0xbfffff) {
        if ((address & 0xfffff) >= 0x40000 && (address & 0xfffff) <= 0x7ffff) {
            return video_.pal_ram[(address & 0xfff) >> 1];
        }
        return 0xffff;
    }
    if (address >= 0xc00000) {
        switch (address & 0x7ffff) {
            case 0x00000 ... 0x0ffff: return video_.tile_ram[(address & 0x7fff) >> 1];
            case 0x10000 ... 0x1ffff: return video_.char_ram[(address & 0xfff) >> 1];
            case 0x40000 ... 0x5ffff: return io_16a(address & 0x3fff);
            case 0x70000 ... 0x7ffff: return ram_[(address & 0x3fff) >> 1];
            default: return 0xffff;
        }
    }
    return 0xffff;
}

void System16::write_16a(uint32_t address, uint16_t value) {
    address &= 0xffffff;
    if (address >= 0x400000 && address <= 0x7fffff) {
        switch (address & 0x7ffff) {
            case 0x00000 ... 0x0ffff: {
                const uint16_t offset = uint16_t((address & 0x7fff) >> 1);
                if (video_.tile_ram[offset] != value) {
                    video_.tile_ram[offset] = value;
                    video_.mark_tile(offset);
                }
                break;
            }
            case 0x10000 ... 0x1ffff: {
                const uint16_t offset = uint16_t((address & 0xfff) >> 1);
                if (video_.char_ram[offset] != value) {
                    video_.char_ram[offset] = value;
                    video_.text_dirty[offset] = true;
                }
                video_.apply_screen_select_16a(offset);
                break;
            }
            case 0x40000 ... 0x7ffff:
                video_.sprite_ram[(address & 0x7ff) >> 1] = value;
                break;
            default:
                break;
        }
        return;
    }
    if (address >= 0x800000 && address <= 0xbfffff) {
        if ((address & 0xfffff) >= 0x40000 && (address & 0xfffff) <= 0x7ffff) {
            video_.set_palette_entry(int((address & 0xfff) >> 1), value, false);
        }
        return;
    }
    if (address >= 0xc00000) {
        switch (address & 0x7ffff) {
            case 0x00000 ... 0x0ffff: {
                const uint16_t offset = uint16_t((address & 0x7fff) >> 1);
                if (video_.tile_ram[offset] != value) {
                    video_.tile_ram[offset] = value;
                    video_.mark_tile(offset);
                }
                break;
            }
            case 0x10000 ... 0x1ffff: {
                const uint16_t offset = uint16_t((address & 0xfff) >> 1);
                if (video_.char_ram[offset] != value) {
                    video_.char_ram[offset] = value;
                    video_.text_dirty[offset] = true;
                }
                video_.apply_screen_select_16a(offset);
                break;
            }
            case 0x40000 ... 0x5ffff:
                if ((address & 0x3000) == 0) ppi_.write((address >> 1) & 3, uint8_t(value));
                break;
            case 0x70000 ... 0x7ffff: {
                const uint16_t offset = uint16_t((address & 0x3fff) >> 1);
                ram_[offset] = value;
                if (offset == 0x38) sound_latch_ = uint8_t(value);
                break;
            }
            default:
                break;
        }
    }
}

void System16::region2_write(uint32_t address, uint16_t value) {
    const int index = int((address & 3) >> 1);
    const uint8_t bank = uint8_t((value & 7) & video_.tile_banks);
    if (video_.tile_bank[size_t(index)] != bank) {
        video_.tile_bank[size_t(index)] = bank;
        for (auto& page : video_.tile_dirty) page.fill(true);
    }
}

uint16_t System16::read_16b(uint32_t address) {
    address &= 0xffffff;
    if (mapper_.contains(0, address)) {
        return rom_[(address >> 1) % std::max<size_t>(rom_.size(), 1)];
    }
    bool mapped = false;
    uint16_t result = 0xffff;
    if (mapper_.contains(3, address)) {
        result = ram_[(address & 0xffff) >> 1];
        mapped = true;
    }
    if (mapper_.contains(4, address)) {
        result = video_.sprite_ram[(address & 0x7ff) >> 1];
        mapped = true;
    }
    if (mapper_.contains(5, address)) {
        if ((address & 0x1ffff) <= 0xffff) result = video_.tile_ram[(address & 0xffff) >> 1];
        else result = video_.char_ram[(address & 0xfff) >> 1];
        mapped = true;
    }
    if (mapper_.contains(6, address)) {
        result = video_.pal_ram[(address & 0xfff) >> 1];
        mapped = true;
    }
    if (mapper_.contains(7, address)) {
        result = io_16b(uint16_t((address >> 1) & 0x1fff));
        mapped = true;
    }
    if (!mapped) result = uint16_t(0xff00 | mapper_.read_reg(uint8_t((address >> 1) & 0x1f)));
    return result;
}

void System16::write_16b(uint32_t address, uint16_t value, bool allow_mapper) {
    address &= 0xffffff;
    bool mapped = false;
    if (mapper_.contains(0, address)) mapped = true;
    if (mapper_.contains(2, address)) {
        region2_write(address, value);
        mapped = true;
    }
    if (mapper_.contains(3, address)) {
        ram_[(address & 0xffff) >> 1] = value;
        mapped = true;
    }
    if (mapper_.contains(4, address)) {
        video_.sprite_ram[(address & 0x7ff) >> 1] = value;
        mapped = true;
    }
    if (mapper_.contains(5, address)) {
        if ((address & 0x1ffff) <= 0xffff) {
            const uint16_t offset = uint16_t((address & 0xffff) >> 1);
            if (video_.tile_ram[offset] != value) {
                video_.tile_ram[offset] = value;
                video_.mark_tile(offset);
            }
        } else {
            const uint16_t offset = uint16_t((address & 0xfff) >> 1);
            if (video_.char_ram[offset] != value) {
                video_.char_ram[offset] = value;
                video_.text_dirty[offset] = true;
            }
            video_.apply_screen_select_16b(offset);
        }
        mapped = true;
    }
    if (mapper_.contains(6, address)) {
        video_.set_palette_entry(int((address & 0xfff) >> 1), value, true);
        mapped = true;
    }
    if (mapper_.contains(7, address)) {
        if (((address & 0x1fff) >> 1) == 0) video_.screen_enabled = (value & 0x20) != 0;
        mapped = true;
    }
    if (!mapped && allow_mapper) {
        const uint32_t old = mapper_.dirs_start(5);
        mapper_.write_reg(uint8_t((address >> 1) & 0x1f), uint8_t(value));
        if (old != mapper_.dirs_start(5)) {
            for (auto& page : video_.tile_dirty) page.fill(false);
        }
    }
}

uint16_t System16::main_read(uint32_t address) {
    return is_16b() ? read_16b(address) : read_16a(address);
}

void System16::main_write(uint32_t address, uint16_t value) {
    if (is_16b()) write_16b(address, value, !use_mcu_);
    else write_16a(address, value);
}

uint8_t System16::sound_read(uint16_t address) {
    if (is_16b()) {
        if (address <= 0x7fff) return sound_mem_[address];
        if (address >= 0x8000 && address <= 0xdfff) {
            return sound_bank_[sound_bank_num_ & 0xf][address & 0x3fff];
        }
        if (address == 0xe800) {
            sound_cpu_.set_irq(IrqLine::Clear);
            return sound_latch_;
        }
        if (address >= 0xf800) return sound_mem_[address];
        return 0xff;
    }
    if (address <= 0x7fff || address >= 0xf800) return sound_mem_[address];
    if (address == 0xe800) return sound_latch_;
    return 0xff;
}

void System16::sound_write(uint16_t address, uint8_t value) {
    if (address >= 0xf800) sound_mem_[address] = value;
}

uint8_t System16::sound_in(uint16_t port) {
    const uint8_t p = uint8_t(port);
    if (p <= 0x3f && (p & 1)) return ym_.status();
    if (is_16b()) {
        if (p >= 0x80 && p <= 0xbf) return 0x80;  // UPD7759 busy (stub, idle)
        if (p >= 0xc0) {
            sound_cpu_.set_irq(IrqLine::Clear);
            return sound_latch_;
        }
    } else if (p >= 0xc0) {
        return sound_latch_;
    }
    return 0xff;
}

void System16::sound_out(uint16_t port, uint8_t value) {
    const uint8_t p = uint8_t(port);
    if (p <= 0x3f) {
        if ((p & 1) == 0) ym_.select_register(value);
        else ym_.write(value);
        return;
    }
    if (is_16b() && p >= 0x40 && p <= 0x7f) {
        sound_bank_num_ = uint8_t(value & 0xf);
    }
}

void System16::on_sound_cycles(int cycles) {
    const int ym_cycles = is_16b() ? int((int64_t(cycles) * 4000000) / sound_clock_) : cycles;
    ym_.run_timers(ym_cycles);
    audio_acc_ += int64_t(cycles) * YM2151::kSampleRate;
    while (audio_acc_ >= sound_clock_) {
        audio_acc_ -= sound_clock_;
        audio_.push_back(int16_t(std::clamp(ym_.update(), int32_t(-32768), int32_t(32767))));
    }
}

void System16::update_video() {
    const uint32_t blank = is_16b() ? video_.palette[0x1000] : video_.palette[0x1fff];
    if (!video_.screen_enabled) {
        std::fill(framebuffer_.begin(), framebuffer_.end(), blank);
        return;
    }
    if (is_16b()) {
        video_.render_tile_pages(bg_low_, bg_high_, 0, false, 6, 0x1fff, 0x8000, true, false);
        video_.render_tile_pages(fg_low_, fg_high_, 4, true, 6, 0x1fff, 0x8000, true, false);
        video_.render_text(text_low_, text_high_, 9, 0x1ff, 0x8000, true);
        int scroll_x1 = 0, scroll_y1 = video_.char_ram[0x749] & 0x1ff;
        int scroll_x2 = 0, scroll_y2 = video_.char_ram[0x748] & 0x1ff;
        bool row_back = (video_.char_ram[0x74d] & 0x8000) != 0;
        bool row_fore = (video_.char_ram[0x74c] & 0x8000) != 0;
        if (!row_back) scroll_x1 = (704 - (video_.char_ram[0x74d] & 0x3ff)) & 0x3ff;
        if (!row_fore) scroll_x2 = (704 - (video_.char_ram[0x74c] & 0x3ff)) & 0x3ff;
        auto blit_rows = [&](const std::vector<uint32_t>& src, bool row, int sx, int sy,
                             uint16_t table_base) {
            if (!row) {
                video_.blit_scrolled(framebuffer_.data(), src, sx, sy, 1024, 512);
                return;
            }
            for (int y = 0; y < kScreenHeight; y++) {
                const int line = (y + sy) & 0x1ff;
                const int row_i = (line >> 3) & 0x3f;
                const int rx = (704 - (video_.char_ram[table_base + row_i] & 0x3ff)) & 0x3ff;
                for (int x = 0; x < kScreenWidth; x++) {
                    const uint32_t pixel = src[size_t(line * 1024 + ((x + rx) & 0x3ff))];
                    if (pixel) framebuffer_[size_t(y * kScreenWidth + x)] = pixel;
                }
            }
        };
        blit_rows(bg_low_, row_back, scroll_x1, scroll_y1, 0x7e0);
        draw_sprites_16b(video_, framebuffer_.data(), sprite_rom_, sprite_banks_, 0, 0x800);
        blit_rows(bg_high_, row_back, scroll_x1, scroll_y1, 0x7e0);
        draw_sprites_16b(video_, framebuffer_.data(), sprite_rom_, sprite_banks_, 1, 0x800);
        blit_rows(fg_low_, row_fore, scroll_x2, scroll_y2, 0x7c0);
        draw_sprites_16b(video_, framebuffer_.data(), sprite_rom_, sprite_banks_, 2, 0x800);
        blit_rows(fg_high_, row_fore, scroll_x2, scroll_y2, 0x7c0);
        video_.blit_text(framebuffer_.data(), text_low_);
        draw_sprites_16b(video_, framebuffer_.data(), sprite_rom_, sprite_banks_, 3, 0x800);
        video_.blit_text(framebuffer_.data(), text_high_);
        return;
    }
    video_.render_tile_pages(bg_low_, bg_high_, 0, false, 5, 0x1fff, 0x1000, false, true);
    video_.render_tile_pages(fg_low_, fg_high_, 4, true, 5, 0x1fff, 0x1000, false, true);
    video_.render_text(text_low_, text_high_, 8, 0xff, 0x800, false);
    const int scroll_x1 = (0xc8 - (video_.char_ram[0x7fd] & 0x1ff)) & 0x3ff;
    const int scroll_y1 = video_.char_ram[0x793] & 0xff;
    const int scroll_x2 = (0xc8 - (video_.char_ram[0x7fc] & 0x1ff)) & 0x3ff;
    const int scroll_y2 = video_.char_ram[0x792] & 0xff;
    video_.blit_scrolled(framebuffer_.data(), bg_low_, scroll_x1, scroll_y1, 1024, 512);
    draw_sprites_16a(video_, framebuffer_.data(), sprite_rom_, sprite_banks_, 0, 0x400, 0x1000);
    video_.blit_scrolled(framebuffer_.data(), bg_high_, scroll_x1, scroll_y1, 1024, 512);
    draw_sprites_16a(video_, framebuffer_.data(), sprite_rom_, sprite_banks_, 1, 0x400, 0x1000);
    video_.blit_scrolled(framebuffer_.data(), fg_low_, scroll_x2, scroll_y2, 1024, 512);
    draw_sprites_16a(video_, framebuffer_.data(), sprite_rom_, sprite_banks_, 2, 0x400, 0x1000);
    video_.blit_scrolled(framebuffer_.data(), fg_high_, scroll_x2, scroll_y2, 1024, 512);
    video_.blit_text(framebuffer_.data(), text_low_);
    draw_sprites_16a(video_, framebuffer_.data(), sprite_rom_, sprite_banks_, 3, 0x400, 0x1000);
    video_.blit_text(framebuffer_.data(), text_high_);
}

void System16::run_frame() {
    const int main_cycles =
        int(double(kMainClock) / fps_ / (kScanlines * kCpuSync) + 0.5);
    const int sound_cycles =
        int(double(sound_clock_) / fps_ / (kScanlines * kCpuSync) + 0.5);
    const int mcu_cycles =
        mcu_ ? int(double(mcu_->clock()) / fps_ / (kScanlines * kCpuSync) + 0.5) : 0;
    for (int line = 0; line < kScanlines; line++) {
        if (line == 224) {
            if (use_mcu_ && mcu_) mcu_->set_irq0_line(IrqLine::Hold);
            else main_cpu_.set_irq(4, IrqLine::Hold);
            update_video();
        }
        for (int slice = 0; slice < kCpuSync; slice++) {
            main_cpu_.run(main_cycles);
            sound_cpu_.run(sound_cycles);
            if (use_mcu_ && mcu_) mcu_->run(mcu_cycles);
        }
    }
}

}  // namespace dsp
