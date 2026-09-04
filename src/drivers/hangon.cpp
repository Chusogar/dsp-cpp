#include "drivers/hangon.h"

#include <algorithm>

#include "machine/fd1089.h"

namespace dsp {
namespace {

const std::vector<RomEntry> kHangOnMain = {
    {"epr-6918a.ic22", 0x8000, 0x00000, 0x20b1c2b0},
    {"epr-6916a.ic8", 0x8000, 0x00001, 0x7d9db1bf},
    {"epr-6917a.ic20", 0x8000, 0x10000, 0xfea12367},
    {"epr-6915a.ic6", 0x8000, 0x10001, 0xac883240},
};
const std::vector<RomEntry> kHangOnSub = {
    {"epr-6920.ic63", 0x8000, 0, 0x1c95013e},
    {"epr-6919.ic51", 0x8000, 1, 0x6ca30d69},
};
const std::vector<RomEntry> kHangOnSound = {{"epr-6833.ic73", 0x4000, 0, 0x3b942f5f}};
const std::vector<RomEntry> kHangOnTiles = {
    {"epr-6841.ic38", 0x8000, 0x0000, 0x54d295dc},
    {"epr-6842.ic23", 0x8000, 0x8000, 0xf677b568},
    {"epr-6843.ic7", 0x8000, 0x10000, 0xa257f0da},
};
const std::vector<RomEntry> kHangOnSprites = {
    {"epr-6819.ic27", 0x8000, 0x00000, 0x469dad07},
    {"epr-6820.ic34", 0x8000, 0x00001, 0x87cbc6de},
    {"epr-6821.ic28", 0x8000, 0x10000, 0x15792969},
    {"epr-6822.ic35", 0x8000, 0x10001, 0xe9718de5},
    {"epr-6823.ic29", 0x8000, 0x20000, 0x49422691},
    {"epr-6824.ic36", 0x8000, 0x20001, 0x701deaa4},
    {"epr-6825.ic30", 0x8000, 0x30000, 0x6e23c8b4},
    {"epr-6826.ic37", 0x8000, 0x30001, 0x77d0de2c},
    {"epr-6827.ic31", 0x8000, 0x40000, 0x7fa1bfb6},
    {"epr-6828.ic38", 0x8000, 0x40001, 0x8e880c93},
    {"epr-6829.ic32", 0x8000, 0x50000, 0x7ca0952d},
    {"epr-6830.ic39", 0x8000, 0x50001, 0xb1a63aef},
    {"epr-6845.ic18", 0x8000, 0x60000, 0xba08c9b8},
    {"epr-6846.ic25", 0x8000, 0x60001, 0xf21e57a3},
};
const std::vector<RomEntry> kHangOnRoad = {{"epr-6840.ic108", 0x8000, 0, 0x581230e3}};
const std::vector<RomEntry> kHangOnPcm = {
    {"epr-6831.ic5", 0x8000, 0x0000, 0xcfef5481},
    {"epr-6832.ic6", 0x8000, 0x8000, 0x4165aea5},
};
const std::vector<RomEntry> kZoomRom = {{"epr-6844.ic123", 0x2000, 0, 0xe3ec7bd6}};

const std::vector<RomEntry> kEnduroMain = {
    {"epr-7640a.ic97", 0x8000, 0x00000, 0x1d1dc5d4},
    {"epr-7636a.ic84", 0x8000, 0x00001, 0x84131639},
    {"epr-7641.ic98", 0x8000, 0x10000, 0x2503ae7c},
    {"epr-7637.ic85", 0x8000, 0x10001, 0x82a27a8c},
    {"epr-7642.ic99", 0x8000, 0x20000, 0x1c453bea},
    {"epr-7638.ic86", 0x8000, 0x20001, 0x70544779},
};
const std::vector<RomEntry> kEnduroSub = {
    {"epr-7634a.ic54", 0x8000, 0, 0xaec83731},
    {"epr-7635a.ic67", 0x8000, 1, 0xb2fce96f},
};
const std::vector<RomEntry> kEnduroSound = {{"epr-7682.ic58", 0x8000, 0, 0xc4efbf48}};
const std::vector<RomEntry> kEnduroTiles = {
    {"epr-7644.ic31", 0x8000, 0x0000, 0xe7a4ff90},
    {"epr-7645.ic46", 0x8000, 0x8000, 0x4caa0095},
    {"epr-7646.ic60", 0x8000, 0x10000, 0x7e432683},
};
const std::vector<RomEntry> kEnduroSprites = {
    {"epr-7678.ic36", 0x8000, 0x00000, 0x9fb5e656}, {"epr-7670.ic28", 0x8000, 0x00001, 0xdbbe2f6e},
    {"epr-7662.ic18", 0x8000, 0x00002, 0xcb0c13c5}, {"epr-7654.ic8", 0x8000, 0x00003, 0x2db6520d},
    {"epr-7677.ic35", 0x8000, 0x20000, 0x7764765b}, {"epr-7669.ic27", 0x8000, 0x20001, 0xf9525faa},
    {"epr-7661.ic17", 0x8000, 0x20002, 0xfe93a79b}, {"epr-7653.ic7", 0x8000, 0x20003, 0x46a52114},
    {"epr-7676.ic34", 0x8000, 0x40000, 0x2e42e0d4}, {"epr-7668.ic26", 0x8000, 0x40001, 0xe115ce33},
    {"epr-7660.ic16", 0x8000, 0x40002, 0x86dfbb68}, {"epr-7652.ic6", 0x8000, 0x40003, 0x2880cfdb},
    {"epr-7675.ic33", 0x8000, 0x60000, 0x05cd2d61}, {"epr-7667.ic25", 0x8000, 0x60001, 0x923bde9d},
    {"epr-7659.ic15", 0x8000, 0x60002, 0x629dc8ce}, {"epr-7651.ic5", 0x8000, 0x60003, 0xd7902bad},
    {"epr-7674.ic32", 0x8000, 0x80000, 0x1a129acf}, {"epr-7666.ic24", 0x8000, 0x80001, 0x23697257},
    {"epr-7658.ic14", 0x8000, 0x80002, 0x1677f24f}, {"epr-7650.ic4", 0x8000, 0x80003, 0x642635ec},
    {"epr-7673.ic31", 0x8000, 0xa0000, 0x82602394}, {"epr-7665.ic23", 0x8000, 0xa0001, 0x12d77607},
    {"epr-7657.ic13", 0x8000, 0xa0002, 0x8158839c}, {"epr-7649.ic3", 0x8000, 0xa0003, 0x4edba14c},
    {"epr-7672.ic30", 0x8000, 0xc0000, 0xd11452f7}, {"epr-7664.ic22", 0x8000, 0xc0001, 0x0df2cfad},
    {"epr-7656.ic12", 0x8000, 0xc0002, 0x6c741272}, {"epr-7648.ic2", 0x8000, 0xc0003, 0x983ea830},
    {"epr-7671.ic29", 0x8000, 0xe0000, 0xb0c7fdc6}, {"epr-7663.ic21", 0x8000, 0xe0001, 0x2b0b8f08},
    {"epr-7655.ic11", 0x8000, 0xe0002, 0x3433fe7b}, {"epr-7647.ic1", 0x8000, 0xe0003, 0x2e7fbec0},
};
const std::vector<RomEntry> kEnduroRoad = {{"epr-7633.ic1", 0x8000, 0, 0x6f146210}};
const std::vector<RomEntry> kEnduroKey = {{"317-0013a.key", 0x2000, 0, 0xa965b2da}};
const std::vector<RomEntry> kEnduroPcm = {
    {"epr-7681.ic8", 0x8000, 0x00000, 0xbc0c4d12},
    {"epr-7680.ic7", 0x8000, 0x10000, 0x627b3c8c},
};

const std::vector<RomEntry> kSharrierMain = {
    {"epr-7188a.ic97", 0x8000, 0x00000, 0x45e173c3},
    {"epr-7184a.ic84", 0x8000, 0x00001, 0xe1934a51},
    {"epr-7189.ic98", 0x8000, 0x10000, 0x40b1309f},
    {"epr-7185.ic85", 0x8000, 0x10001, 0xce78045c},
    {"epr-7190.ic99", 0x8000, 0x20000, 0xf6391091},
    {"epr-7186.ic86", 0x8000, 0x20001, 0x79b367d7},
    {"epr-7191.ic100", 0x8000, 0x30000, 0x6171e9d3},
    {"epr-7187.ic87", 0x8000, 0x30001, 0x70cb72ef},
};
const std::vector<RomEntry> kSharrierSub = {
    {"epr-7182.ic54", 0x8000, 0, 0xd7c535b6},
    {"epr-7183.ic67", 0x8000, 1, 0xa6153af8},
};
const std::vector<RomEntry> kSharrierSound = {
    {"epr-7234.ic73", 0x4000, 0x0000, 0xd6397933},
    {"epr-7233.ic72", 0x4000, 0x4000, 0x504e76d9},
};
const std::vector<RomEntry> kSharrierTiles = {
    {"epr-7196.ic31", 0x8000, 0x0000, 0x347fa325},
    {"epr-7197.ic46", 0x8000, 0x8000, 0x39d98bd1},
    {"epr-7198.ic60", 0x8000, 0x10000, 0x3da3ea6b},
};
const std::vector<RomEntry> kSharrierSprites = {
    {"epr-7230.ic36", 0x8000, 0x00000, 0x93e2d264}, {"epr-7222.ic28", 0x8000, 0x00001, 0xedbf5fc3},
    {"epr-7214.ic18", 0x8000, 0x00002, 0xe8c537d8}, {"epr-7206.ic8", 0x8000, 0x00003, 0x22844fa4},
    {"epr-7229.ic35", 0x8000, 0x20000, 0xcd6e7500}, {"epr-7221.ic27", 0x8000, 0x20001, 0x41f25a9c},
    {"epr-7213.ic17", 0x8000, 0x20002, 0x5bb09a67}, {"epr-7205.ic7", 0x8000, 0x20003, 0xdcaa2ebf},
    {"epr-7228.ic34", 0x8000, 0x40000, 0xd5e15e66}, {"epr-7220.ic26", 0x8000, 0x40001, 0xac62ae2e},
    {"epr-7212.ic16", 0x8000, 0x40002, 0x9c782295}, {"epr-7204.ic6", 0x8000, 0x40003, 0x3711105c},
    {"epr-7227.ic33", 0x8000, 0x60000, 0x60d7c1bb}, {"epr-7219.ic25", 0x8000, 0x60001, 0xf6330038},
    {"epr-7211.ic15", 0x8000, 0x60002, 0x60737b98}, {"epr-7203.ic5", 0x8000, 0x60003, 0x70fb5ebb},
    {"epr-7226.ic32", 0x8000, 0x80000, 0x6d7b5c97}, {"epr-7218.ic24", 0x8000, 0x80001, 0xcebf797c},
    {"epr-7210.ic14", 0x8000, 0x80002, 0x24596a8b}, {"epr-7202.ic4", 0x8000, 0x80003, 0xb537d082},
    {"epr-7225.ic31", 0x8000, 0xa0000, 0x5e784271}, {"epr-7217.ic23", 0x8000, 0xa0001, 0x510e5e10},
    {"epr-7209.ic13", 0x8000, 0xa0002, 0x7a2dad15}, {"epr-7201.ic3", 0x8000, 0xa0003, 0xf5ba4e08},
    {"epr-7224.ic30", 0x8000, 0xc0000, 0xec42c9ef}, {"epr-7216.ic22", 0x8000, 0xc0001, 0x6d4a7d7a},
    {"epr-7208.ic12", 0x8000, 0xc0002, 0x0f732717}, {"epr-7200.ic2", 0x8000, 0xc0003, 0xfc3bf8f3},
    {"epr-7223.ic29", 0x8000, 0xe0000, 0xed51fdc4}, {"epr-7215.ic21", 0x8000, 0xe0001, 0xdfe75f3d},
    {"epr-7207.ic11", 0x8000, 0xe0002, 0xa2c07741}, {"epr-7199.ic1", 0x8000, 0xe0003, 0xb191e22f},
};
const std::vector<RomEntry> kSharrierRoad = {{"epr-7181.ic2", 0x8000, 0, 0xb4740419}};
const std::vector<RomEntry> kSharrierMcu = {{"315-5163a.ic32", 0x1000, 0, 0x203dffeb}};
const std::vector<RomEntry> kSharrierPcm = {
    {"epr-7231.ic5", 0x8000, 0x0000, 0x871c6b14},
    {"epr-7232.ic6", 0x8000, 0x8000, 0x4b59340c},
};

}  // namespace

HangOn::HangOn(Game game)
    : game_(game),
      main_clock_(game == Game::HangOn ? 25174800u / 4 : 10000000u),
      sound_clock_(4000000),
      cpu_sync_(game == Game::Enduro ? 12 : (game == Game::Sharrier ? 16 : 8)),
      main_cpu_(game == Game::HangOn ? 25174800u / 4 : 10000000u),
      sub_cpu_(game == Game::HangOn ? 25174800u / 4 : 10000000u),
      sound_cpu_(4000000),
      ym2203_(4000000, 0.3f, 0.3f),
      ym2151_(4000000),
      pcm_(game == Game::Enduro ? 4000000u : 8000000u, game == Game::HangOn ? 1.3f : 1.0f),
      framebuffer_(kScreenWidth * kScreenHeight, 0) {
    use_fd1089_ = (game == Game::Enduro);
    use_ym2151_ = (game == Game::Enduro);
    sharrier_road_ = (game != Game::HangOn);
    main_cpu_.set_memory_handlers([this](uint32_t a) { return main_read(a); },
                                  [this](uint32_t a, uint16_t v) { main_write(a, v); });
    sub_cpu_.set_memory_handlers([this](uint32_t a) { return sub_read(a); },
                                 [this](uint32_t a, uint16_t v) { sub_write(a, v); });
    sound_cpu_.set_memory_handlers([this](uint16_t a) { return sound_read(a); },
                                   [this](uint16_t a, uint8_t v) { sound_write(a, v); });
    sound_cpu_.set_io_handlers([this](uint16_t p) { return sound_in(p); },
                               [this](uint16_t p, uint8_t v) { sound_out(p, v); });
    sound_cpu_.set_cycle_handler([this](int cycles) { on_sound_cycles(cycles); });
    ym2203_.set_irq_handler([this](bool state) {
        sound_cpu_.set_irq(state ? IrqLine::Assert : IrqLine::Clear);
    });
    pcm_.set_read_rom([this](uint32_t addr) -> uint8_t {
        if (pcm_rom_.empty()) return 0x80;
        return pcm_rom_[addr % pcm_rom_.size()];
    });
    pcm_.set_bank(SegaPcm::kBank512);
    ppi0_.set_port_handlers(nullptr, nullptr, nullptr,
                            [this](uint8_t value) {
                                sound_latch_ = value;
                                ++ppi_a_writes_;
                                // Pulse Z80 NMI on every latch write so Space
                                // Harrier still delivers commands if the 8255
                                // has not yet been programmed for mode 2 /OBF.
                                sound_cpu_.set_nmi(IrqLine::Assert);
                                sound_cpu_.set_nmi(IrqLine::Clear);
                            },
                            [this](uint8_t value) {
                                z80_reset_ = (value & 0x20) == 0;
                                if (z80_reset_) sound_cpu_.reset();
                                video_.screen_enabled = (value & 0x10) != 0;
                            },
                            [this](uint8_t value) {
                                sound_cpu_.set_nmi((value & 0x80) ? IrqLine::Clear
                                                                  : IrqLine::Assert);
                            });
    ppi1_.set_port_handlers(nullptr, nullptr, nullptr,
                            [this](uint8_t value) {
                                sub_cpu_.set_irq(4, (value & 0x40) ? IrqLine::Clear
                                                                   : IrqLine::Assert);
                                sub_cpu_.set_reset_line((value & 0x20) ? IrqLine::Assert
                                                                       : IrqLine::Clear);
                                adc_select_ = (value >> 2) & 3;
                            },
                            nullptr, nullptr);
    if (game == Game::Sharrier) {
        mcu_ = std::make_unique<Mcs51>(8000000);
        mcu_->set_port_write_handler(1, [this](uint8_t value) {
            i8751_addr_ = uint8_t(((value & 0x40) >> 2) | ((value & 0x38) >> 3));
            const uint8_t irq = uint8_t((~value) & 7);
            if (irq != 0) {
                ++mcu_irqs_;
                main_cpu_.set_irq(irq, IrqLine::Hold);
            }
        });
        mcu_->set_external_handlers(
            [this](uint16_t address) -> uint8_t {
                const uint32_t addr = (uint32_t(i8751_addr_) << 16) | (address ^ 1);
                const uint16_t word = main_read(addr);
                return (addr & 1) ? uint8_t(word) : uint8_t(word >> 8);
            },
            [this](uint16_t address, uint8_t value) {
                const uint32_t addr = (uint32_t(i8751_addr_) << 16) | (address ^ 1);
                if (addr == 0x40385) return;
                uint16_t word = main_read(addr);
                if (addr & 1) word = uint16_t((word & 0xff00) | value);
                else word = uint16_t((word & 0x00ff) | (uint16_t(value) << 8));
                main_write(addr, word);
            });
    }
}

const char* HangOn::title() const {
    switch (game_) {
        case Game::Enduro: return "Enduro Racer";
        case Game::Sharrier: return "Space Harrier";
        default: return "Hang-On";
    }
}

bool HangOn::init(const std::string& rom_path, std::string* error) {
    if (!load_roms(rom_path, error)) return false;
    video_.init_palette_luts();
    reset();
    return true;
}

bool HangOn::load_roms(const std::string& rom_path, std::string* error) {
    RomLoader loader;
    if (!loader.open(rom_path, error)) return false;
    if (game_ == Game::HangOn) {
        if (!load_roms16w(loader, kHangOnMain, rom_, error)) return false;
        if (!load_roms16w(loader, kHangOnSub, rom2_, error)) return false;
        std::vector<uint8_t> sound;
        if (!load_rom_bytes(loader, kHangOnSound, sound, error)) return false;
        std::fill(sound_mem_.begin(), sound_mem_.end(), 0);
        std::copy(sound.begin(), sound.end(), sound_mem_.begin());
        std::vector<uint8_t> tiles;
        if (!load_rom_bytes(loader, kHangOnTiles, tiles, error)) return false;
        decode_s16_tiles(video_.tiles, tiles, 1);
        if (!load_roms16b(loader, kHangOnSprites, sprite_rom_, error)) return false;
        if (!load_rom_bytes(loader, kZoomRom, zoom_, error)) return false;
        std::vector<uint8_t> road;
        if (!load_rom_bytes(loader, kHangOnRoad, road, error)) return false;
        decode_hangon_road(road_gfx_, road);
        if (!load_rom_bytes(loader, kHangOnPcm, pcm_rom_, error)) return false;
        sprite_banks_ = 7;
        dsw_b_ = 0xfffe;
        return true;
    }

    const bool enduro = game_ == Game::Enduro;
    if (enduro) {
        std::vector<uint16_t> encrypted;
        if (!load_roms16w(loader, kEnduroMain, encrypted, error)) return false;
        std::vector<uint8_t> key;
        if (!load_rom_bytes(loader, kEnduroKey, key, error)) return false;
        key.resize(0x2000, 0);
        rom_.assign(encrypted.size(), 0);
        rom_data_.assign(encrypted.size(), 0);
        fd1089_decrypt(encrypted.data(), rom_.data(), rom_data_.data(),
                       uint32_t(encrypted.size() * 2), key.data(), Fd1089Type::B);
        if (!load_roms16w(loader, kEnduroSub, rom2_, error)) return false;
        std::vector<uint8_t> sound;
        if (!load_rom_bytes(loader, kEnduroSound, sound, error)) return false;
        std::fill(sound_mem_.begin(), sound_mem_.end(), 0);
        std::copy(sound.begin(), sound.end(), sound_mem_.begin());
        std::vector<uint8_t> tiles;
        if (!load_rom_bytes(loader, kEnduroTiles, tiles, error)) return false;
        decode_s16_tiles(video_.tiles, tiles, 1);
        if (!load_roms32dw(loader, kEnduroSprites, sprite_rom32_, error)) return false;
        std::string zoom_error;
        if (!load_rom_bytes(loader, kZoomRom, zoom_, &zoom_error)) zoom_.assign(0x2000, 0);
        std::vector<uint8_t> road;
        if (!load_rom_bytes(loader, kEnduroRoad, road, error)) return false;
        decode_hangon_road(road_gfx_, road);
        if (!load_rom_bytes(loader, kEnduroPcm, pcm_rom_, error)) return false;
        pcm_rom_.resize(0x20000, 0);
        sprite_banks_ = 8;
        dsw_b_ = 0xff7e;
        return true;
    }

    if (!load_roms16w(loader, kSharrierMain, rom_, error)) return false;
    if (!load_roms16w(loader, kSharrierSub, rom2_, error)) return false;
    std::vector<uint8_t> sound;
    if (!load_rom_bytes(loader, kSharrierSound, sound, error)) return false;
    std::fill(sound_mem_.begin(), sound_mem_.end(), 0);
    std::copy(sound.begin(), sound.end(), sound_mem_.begin());
    std::vector<uint8_t> tiles;
    if (!load_rom_bytes(loader, kSharrierTiles, tiles, error)) return false;
    decode_s16_tiles(video_.tiles, tiles, 1);
    if (!load_roms32dw(loader, kSharrierSprites, sprite_rom32_, error)) return false;
    {
        std::string zoom_error;
        if (!load_rom_bytes(loader, kZoomRom, zoom_, &zoom_error)) zoom_.assign(0x2000, 0);
    }
    std::vector<uint8_t> road;
    if (!load_rom_bytes(loader, kSharrierRoad, road, error)) return false;
    decode_hangon_road(road_gfx_, road);
    if (!load_rom_bytes(loader, kSharrierPcm, pcm_rom_, error)) return false;
    std::vector<uint8_t> mcu;
    std::string mcu_error;
    if (mcu_ && load_rom_bytes(loader, kSharrierMcu, mcu, &mcu_error)) {
        std::fill(mcu_->rom(), mcu_->rom() + Mcs51::kRomSize, 0);
        std::copy(mcu.begin(), mcu.end(), mcu_->rom());
    } else {
        mcu_.reset();
    }
    sprite_banks_ = 8;
    dsw_b_ = 0xfffc;
    return true;
}

void HangOn::reset() {
    main_cpu_.reset();
    sub_cpu_.reset();
    sound_cpu_.reset();
    ym2203_.reset();
    ym2151_.reset();
    pcm_.reset();
    ppi0_.reset();
    ppi1_.reset();
    if (mcu_) {
        mcu_->reset();
        // Reach SETB EA ($022C) before the first vblank so INT0 is armed.
        mcu_->run(25000);
    }
    video_.reset();
    video_.screen_enabled = true;
    ram_.fill(0);
    ram2_.fill(0);
    road_ram_.fill(0);
    in0_ = 0xffff;
    adc_select_ = 0;
    sound_latch_ = 0;
    ppi_a_writes_ = 0;
    control_res_ = 0;
    // PB5 is Z80 /RESET (1 = run). The 8255 comes out of reset with
    // latches cleared, so the sound CPU stays held until the 68K writes
    // port B. Space Harrier's idle $80 command stops YM timers if the
    // Z80 runs that loop before the first real latch write.
    z80_reset_ = true;
    i8751_addr_ = 0;
    mcu_irqs_ = 0;
    analog_x_ = 0x80;
    analog_y_ = 0x80;
    analog_gas_ = 0;
    analog_brake_ = 0;
    analog_moto_ = 0;
    audio_acc_ = 0;
    pcm_acc_ = 0;
    audio_.clear();
    std::fill(framebuffer_.begin(), framebuffer_.end(), 0);
}

void HangOn::set_inputs(const MachineInputs& inputs) {
    in0_ = 0xffff;
    if (inputs.coin1) in0_ &= ~0x0001;
    if (inputs.coin2) in0_ &= ~0x0002;
    if (inputs.player1.start) {
        in0_ &= ~0x0010;
        in0_ &= ~0x0040;
    }
    if (game_ == Game::Sharrier) {
        if (inputs.player1.button1) in0_ &= ~0x0100;
        if (inputs.player1.button2) in0_ &= ~0x0200;
    }
    analog_x_ = 0x80;
    analog_y_ = 0x80;
    if (inputs.player1.left) analog_x_ = 0x20;
    if (inputs.player1.right) analog_x_ = 0xe0;
    if (inputs.player1.up) analog_y_ = 0x20;
    if (inputs.player1.down) analog_y_ = 0xe0;
    analog_gas_ = (inputs.player1.up || inputs.player1.button1) ? 0xff : 0;
    analog_brake_ = (inputs.player1.down || inputs.player1.button2) ? 0xff : 0;
    analog_moto_ = inputs.player1.button3 ? 0xff : 0;
}

void HangOn::set_dip_switch(int bank, uint8_t value) {
    if (bank == 0) dsw_a_ = uint16_t(0xff00 | value);
    if (bank == 1) dsw_b_ = uint16_t(0xff00 | value);
}

void HangOn::drain_audio(std::vector<int16_t>& out) {
    out.insert(out.end(), audio_.begin(), audio_.end());
    audio_.clear();
}

void HangOn::update_controls() {
    if (game_ == Game::Enduro) {
        switch (adc_select_) {
            case 0: control_res_ = analog_gas_; break;
            case 1: control_res_ = analog_brake_; break;
            case 2: control_res_ = analog_moto_; break;
            default: control_res_ = analog_x_; break;
        }
        return;
    }
    if (game_ == Game::Sharrier) {
        switch (adc_select_) {
            case 0: control_res_ = analog_x_; break;
            case 1: control_res_ = analog_y_; break;
            default: control_res_ = 0; break;
        }
        return;
    }
    switch (adc_select_) {
        case 0: control_res_ = analog_x_; break;
        case 1: control_res_ = analog_gas_; break;
        case 2: control_res_ = analog_brake_; break;
        default: control_res_ = 0; break;
    }
}

uint16_t HangOn::main_read(uint32_t address) {
    return is_sharrier_map() ? sharrier_read(address) : hangon_read(address);
}

void HangOn::main_write(uint32_t address, uint16_t value) {
    if (is_sharrier_map()) sharrier_write(address, value);
    else hangon_write(address, value);
}

uint16_t HangOn::hangon_read(uint32_t address) {
    address &= 0xffffff;
    if (address <= 0x3ffff) {
        if (rom_.empty()) return 0xffff;
        return rom_[(address >> 1) % rom_.size()];
    }
    if (address >= 0x20c000 && address <= 0x20ffff) return ram_[(address & 0x3fff) >> 1];
    if (address >= 0x400000 && address <= 0x403fff) return video_.tile_ram[(address & 0x3fff) >> 1];
    if (address >= 0x410000 && address <= 0x410fff) return video_.char_ram[(address & 0xfff) >> 1];
    if (address >= 0x600000 && address <= 0x6007ff) return video_.sprite_ram[(address & 0x7ff) >> 1];
    if (address >= 0xa00000 && address <= 0xa00fff) return video_.pal_ram[(address & 0xfff) >> 1];
    if (address >= 0xc00000 && address <= 0xc3ffff) {
        if (rom2_.empty()) return 0xffff;
        return rom2_[(address >> 1) % rom2_.size()];
    }
    if (address >= 0xc68000 && address <= 0xc68fff) return road_ram_[(address & 0xfff) >> 1];
    if (address >= 0xc7c000 && address <= 0xc7ffff) return ram2_[(address & 0x3fff) >> 1];
    if (address >= 0xe00000 && address <= 0xe00fff) return ppi0_.read((address & 7) >> 1);
    if (address >= 0xe01000 && address <= 0xe01fff) {
        switch ((address & 7) >> 1) {
            case 0: return in0_;
            case 1: return dsw_a_;
            case 2: return dsw_b_;
            default: return 0xffff;
        }
    }
    if (address >= 0xe03000 && address <= 0xe03fff) {
        if ((address & 0x3f) <= 0x1f) return uint16_t(0xff00 | ppi1_.read((address & 7) >> 1));
        return uint16_t(0xff00 | control_res_);
    }
    return 0xffff;
}

void HangOn::hangon_write(uint32_t address, uint16_t value) {
    address &= 0xffffff;
    if (address >= 0x20c000 && address <= 0x20ffff) {
        ram_[(address & 0x3fff) >> 1] = value;
        return;
    }
    if (address >= 0x400000 && address <= 0x403fff) {
        const uint16_t offset = uint16_t((address & 0x3fff) >> 1);
        if (video_.tile_ram[offset] != value) {
            video_.tile_ram[offset] = value;
            video_.mark_tile(offset);
        }
        return;
    }
    if (address >= 0x410000 && address <= 0x410fff) {
        const uint16_t offset = uint16_t((address & 0xfff) >> 1);
        if (video_.char_ram[offset] != value) {
            video_.char_ram[offset] = value;
            video_.text_dirty[offset] = true;
        }
        video_.apply_screen_select_hangon(offset);
        return;
    }
    if (address >= 0x600000 && address <= 0x6007ff) {
        video_.sprite_ram[(address & 0x7ff) >> 1] = value;
        return;
    }
    if (address >= 0xa00000 && address <= 0xa00fff) {
        video_.set_palette_entry(int((address & 0xfff) >> 1), value, false);
        return;
    }
    if (address >= 0xc68000 && address <= 0xc68fff) {
        road_ram_[(address & 0xfff) >> 1] = value;
        return;
    }
    if (address >= 0xc7c000 && address <= 0xc7ffff) {
        ram2_[(address & 0x3fff) >> 1] = value;
        return;
    }
    if (address >= 0xe00000 && address <= 0xe00fff) {
        ppi0_.write((address & 7) >> 1, uint8_t(value));
        return;
    }
    if (address >= 0xe03000 && address <= 0xe03fff) {
        if ((address & 0x3f) <= 0x1f) ppi1_.write((address & 7) >> 1, uint8_t(value));
        else update_controls();
    }
}

uint16_t HangOn::sharrier_read(uint32_t address) {
    address &= 0xffffff;
    if (address <= 0x3ffff) {
        if (use_fd1089_ && !rom_data_.empty()) {
            const size_t index = (address >> 1) % rom_.size();
            return main_cpu_.opcode() ? rom_[index] : rom_data_[index];
        }
        if (rom_.empty()) return 0xffff;
        return rom_[(address >> 1) % rom_.size()];
    }
    if (address >= 0x40000 && address <= 0x43fff) return ram_[(address & 0x3fff) >> 1];
    if (address >= 0x100000 && address <= 0x107fff) return video_.tile_ram[(address & 0x7fff) >> 1];
    if (address >= 0x108000 && address <= 0x108fff) return video_.char_ram[(address & 0xfff) >> 1];
    if (address >= 0x110000 && address <= 0x110fff) return video_.pal_ram[(address & 0xfff) >> 1];
    if (address >= 0x124000 && address <= 0x127fff) return ram2_[(address & 0x3fff) >> 1];
    if (address >= 0x130000 && address <= 0x130fff) return video_.sprite_ram[(address & 0xfff) >> 1];
    if (address >= 0x140000 && address <= 0x14ffff) {
        switch (address & 0x3f) {
            case 0x00 ... 0x0f: return ppi0_.read((address & 7) >> 1);
            case 0x10 ... 0x1f:
                switch ((address & 7) >> 1) {
                    case 0: return in0_;
                    case 1: return 0xffff;
                    case 2: return dsw_a_;
                    default: return dsw_b_;
                }
            case 0x20 ... 0x2f: return ppi1_.read((address & 7) >> 1);
            case 0x30 ... 0x3f: return uint16_t(0xff00 | control_res_);
            default: break;
        }
    }
    if (address >= 0xc68000 && address <= 0xc68fff) return road_ram_[(address & 0xfff) >> 1];
    return 0xffff;
}

void HangOn::sharrier_write(uint32_t address, uint16_t value) {
    address &= 0xffffff;
    if (address >= 0x40000 && address <= 0x43fff) {
        ram_[(address & 0x3fff) >> 1] = value;
        return;
    }
    if (address >= 0x100000 && address <= 0x107fff) {
        const uint16_t offset = uint16_t((address & 0x7fff) >> 1);
        if (video_.tile_ram[offset] != value) {
            video_.tile_ram[offset] = value;
            video_.mark_tile(offset);
        }
        return;
    }
    if (address >= 0x108000 && address <= 0x108fff) {
        const uint16_t offset = uint16_t((address & 0xfff) >> 1);
        if (video_.char_ram[offset] != value) {
            video_.char_ram[offset] = value;
            video_.text_dirty[offset] = true;
        }
        video_.apply_screen_select_hangon(offset);
        return;
    }
    if (address >= 0x110000 && address <= 0x110fff) {
        video_.set_palette_entry(int((address & 0xfff) >> 1), value, false);
        return;
    }
    if (address >= 0x124000 && address <= 0x127fff) {
        ram2_[(address & 0x3fff) >> 1] = value;
        return;
    }
    if (address >= 0x130000 && address <= 0x130fff) {
        video_.sprite_ram[(address & 0xfff) >> 1] = value;
        return;
    }
    if (address >= 0x140000 && address <= 0x14ffff) {
        switch (address & 0x3f) {
            case 0x00 ... 0x0f: ppi0_.write((address & 7) >> 1, uint8_t(value)); break;
            case 0x20 ... 0x2f: ppi1_.write((address & 7) >> 1, uint8_t(value)); break;
            case 0x30 ... 0x3f: update_controls(); break;
            default: break;
        }
        return;
    }
    if (address >= 0xc68000 && address <= 0xc68fff) {
        road_ram_[(address & 0xfff) >> 1] = value;
    }
}

uint16_t HangOn::sub_read(uint32_t address) {
    // Enduro Racer and Space Harrier reuse the Hang-On sub-CPU map.
    address &= 0x7ffff;
    if (address <= 0xffff) {
        if (rom2_.empty()) return 0xffff;
        return rom2_[(address >> 1) % rom2_.size()];
    }
    if (address >= 0x68000 && address <= 0x68fff) return road_ram_[(address & 0xfff) >> 1];
    if (address >= 0x7c000 && address <= 0x7ffff) return ram2_[(address & 0x3fff) >> 1];
    return 0xffff;
}

void HangOn::sub_write(uint32_t address, uint16_t value) {
    address &= 0x7ffff;
    if (address >= 0x68000 && address <= 0x68fff) road_ram_[(address & 0xfff) >> 1] = value;
    else if (address >= 0x7c000 && address <= 0x7ffff) ram2_[(address & 0x3fff) >> 1] = value;
}

uint8_t HangOn::sound_read(uint16_t address) {
    if (use_ym2151_) {
        if (address <= 0x7fff || address >= 0xf800) return sound_mem_[address];
        if (address >= 0xf000 && address <= 0xf7ff) return pcm_.read(address);
        return 0xff;
    }
    if (address <= 0x7fff) return sound_mem_[address];
    if (address >= 0xc000 && address <= 0xcfff) return sound_mem_[0xc000 + (address & 0x7ff)];
    if (address >= 0xd000 && address <= 0xdfff) {
        return (address & 1) ? ym2203_.read() : ym2203_.status();
    }
    if (address >= 0xe000 && address <= 0xefff) return pcm_.read(address);
    return 0xff;
}

void HangOn::sound_write(uint16_t address, uint8_t value) {
    if (use_ym2151_) {
        if (address >= 0xf000 && address <= 0xf7ff) pcm_.write(address, value);
        else if (address >= 0xf800) sound_mem_[address] = value;
        return;
    }
    if (address >= 0xc000 && address <= 0xcfff) {
        sound_mem_[0xc000 + (address & 0x7ff)] = value;
    } else if (address >= 0xd000 && address <= 0xdfff) {
        if ((address & 1) == 0) ym2203_.control(value);
        else ym2203_.write(value);
    } else if (address >= 0xe000 && address <= 0xefff) {
        pcm_.write(address, value);
    }
}

uint8_t HangOn::sound_in(uint16_t port) {
    const uint8_t p = uint8_t(port);
    if (use_ym2151_ && p <= 0x3f && (p & 1)) return ym2151_.status();
    if (p >= 0x40 && p <= 0x7f) {
        // PPI mode 2 ACK: the Z80 read strobes PC6 so /OBF (NMI) is released.
        ppi0_.pc6_w(false);
        ppi0_.pc6_w(true);
        return sound_latch_;
    }
    return 0xff;
}

void HangOn::sound_out(uint16_t port, uint8_t value) {
    if (!use_ym2151_) return;
    const uint8_t p = uint8_t(port);
    if (p <= 0x3f) {
        if ((p & 1) == 0) ym2151_.select_register(value);
        else ym2151_.write(value);
    }
}

void HangOn::on_sound_cycles(int cycles) {
    if (use_ym2151_) ym2151_.run_timers(cycles);
    else ym2203_.run_timers(cycles);
    pcm_acc_ += int64_t(cycles) * pcm_.tick_rate();
    while (pcm_acc_ >= sound_clock_) {
        pcm_acc_ -= sound_clock_;
        pcm_.clock();
    }
    audio_acc_ += int64_t(cycles) * YM2151::kSampleRate;
    while (audio_acc_ >= sound_clock_) {
        audio_acc_ -= sound_clock_;
        const int32_t fm = use_ym2151_ ? ym2151_.update() : ym2203_.update();
        const int32_t sample = fm + pcm_.last_sample();
        audio_.push_back(int16_t(std::clamp(sample, int32_t(-32768), int32_t(32767))));
    }
}

void HangOn::update_video() {
    if (!video_.screen_enabled) {
        std::fill(framebuffer_.begin(), framebuffer_.end(), video_.palette[0x2000]);
        return;
    }
    video_.render_tile_pages(bg_, fg_, 0, true, 5, 0xfff, 0, false, false);
    std::vector<uint32_t> unused;
    video_.render_tile_pages(fg_, unused, 4, true, 5, 0xfff, 0, false, false);
    video_.render_text(text_low_, text_high_, 8, 0xff, 0x800, false);
    const int scroll_x1 = (0xc8 - (video_.char_ram[0x7fd] & 0x1ff)) & 0x3ff;
    const int scroll_y1 = video_.char_ram[0x793] & 0xff;
    const int scroll_x2 = (0xc8 - (video_.char_ram[0x7fc] & 0x1ff)) & 0x3ff;
    const int scroll_y2 = video_.char_ram[0x792] & 0xff;
    draw_hangon_road(framebuffer_.data(), video_.palette.data(), road_ram_.data(), road_gfx_.data(),
                     0x38, 0x7c0, 0, sharrier_road_);
    video_.blit_scrolled(framebuffer_.data(), bg_, scroll_x1, scroll_y1, 1024, 512);
    video_.blit_scrolled(framebuffer_.data(), fg_, scroll_x2, scroll_y2, 1024, 512);
    draw_hangon_road(framebuffer_.data(), video_.palette.data(), road_ram_.data(), road_gfx_.data(),
                     0x38, 0x7c0, 1, sharrier_road_);
    if (sharrier_road_) {
        draw_sprites_sharrier(video_, framebuffer_.data(), sprite_rom32_, zoom_, sprite_banks_, 0);
        draw_sprites_sharrier(video_, framebuffer_.data(), sprite_rom32_, zoom_, sprite_banks_, 1);
        video_.blit_text(framebuffer_.data(), text_low_);
    } else {
        video_.blit_text(framebuffer_.data(), text_low_);
        for (int pri = 3; pri >= 0; pri--) {
            draw_sprites_hangon(video_, framebuffer_.data(), sprite_rom_, zoom_, sprite_banks_, pri);
        }
        video_.blit_text(framebuffer_.data(), text_high_);
    }
}

void HangOn::run_frame() {
    const int main_cycles =
        int(double(main_clock_) / kFramesPerSecond / (kScanlines * cpu_sync_) + 0.5);
    const int sound_cycles =
        int(double(sound_clock_) / kFramesPerSecond / (kScanlines * cpu_sync_) + 0.5);
    const int mcu_cycles =
        mcu_ ? int(double(mcu_->clock()) / kFramesPerSecond / (kScanlines * cpu_sync_) + 0.5) : 0;
    for (int line = 0; line < kScanlines; line++) {
        if (line == 0 && mcu_) mcu_->set_irq0_line(IrqLine::Clear);
        if (line == 224) {
            // Keep INT0 asserted for the whole vblank. A Hold pulse is
            // dropped if the 8751 has not yet set EX0 (MAME holds the line).
            if (mcu_) {
                mcu_->set_irq0_line(IrqLine::Assert);
                // Finish the vblank ISR (CLR P1.2 → 68K IRQ4) before the
                // 68000 samples IPL, matching MAME's 10 ms perfect quantum.
                mcu_->run(48);
            } else {
                main_cpu_.set_irq(4, IrqLine::Hold);
            }
            update_video();
        }
        for (int slice = 0; slice < cpu_sync_; slice++) {
            if (mcu_) mcu_->run(mcu_cycles);
            main_cpu_.run(main_cycles);
            sub_cpu_.run(main_cycles);
            if (!z80_reset_)
                sound_cpu_.run(sound_cycles);
            else
                on_sound_cycles(sound_cycles);
        }
    }
}

}  // namespace dsp
