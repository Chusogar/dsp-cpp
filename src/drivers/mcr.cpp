#include "drivers/mcr.h"

#include <algorithm>
#include <cstring>

#include "core/rom_loader.h"

namespace dsp {
namespace {

uint32_t pal3bit(uint16_t v) {
    const int c = int(v & 7) * 36;
    return uint32_t(std::min(255, c));
}

const std::vector<RomEntry> kTapperRom = {
    {"tapper_c.p.u._pg_0_1c_1-27-84.1c", 0x4000, 0x0000, 0xbb060bb0},
    {"tapper_c.p.u._pg_1_2c_1-27-84.2c", 0x4000, 0x4000, 0xfd9acc22},
    {"tapper_c.p.u._pg_2_3c_1-27-84.3c", 0x4000, 0x8000, 0xb3755d41},
    {"tapper_c.p.u._pg_3_4c_1-27-84.4c", 0x2000, 0xc000, 0x77273096},
};
const std::vector<RomEntry> kTapperSnd = {
    {"tapper_sound_snd_0_a7_12-7-83.a7", 0x1000, 0x0000, 0x0e8bb9d5},
    {"tapper_sound_snd_1_a8_12-7-83.a8", 0x1000, 0x1000, 0x0cf0e29b},
    {"tapper_sound_snd_2_a9_12-7-83.a9", 0x1000, 0x2000, 0x31eb6dc6},
    {"tapper_sound_snd_3_a10_12-7-83.a10", 0x1000, 0x3000, 0x01a9be6a},
};
const std::vector<RomEntry> kTapperChar = {
    {"tapper_c.p.u._bg_1_6f_12-7-83.6f", 0x4000, 0x0000, 0x2a30238c},
    {"tapper_c.p.u._bg_0_5f_12-7-83.5f", 0x4000, 0x4000, 0x394ab576},
};
const std::vector<RomEntry> kTapperSprites = {
    {"tapper_video_fg_1_a7_12-7-83.a7", 0x4000, 0x00000, 0x32509011},
    {"tapper_video_fg_0_a8_12-7-83.a8", 0x4000, 0x04000, 0x8412c808},
    {"tapper_video_fg_3_a5_12-7-83.a5", 0x4000, 0x08000, 0x818fffd4},
    {"tapper_video_fg_2_a6_12-7-83.a6", 0x4000, 0x0c000, 0x67e37690},
    {"tapper_video_fg_5_a3_12-7-83.a3", 0x4000, 0x10000, 0x800f7c8a},
    {"tapper_video_fg_4_a4_12-7-83.a4", 0x4000, 0x14000, 0x32674ee6},
    {"tapper_video_fg_7_a1_12-7-83.a1", 0x4000, 0x18000, 0x070b4c81},
    {"tapper_video_fg_6_a2_12-7-83.a2", 0x4000, 0x1c000, 0xa37aef36},
};

const std::vector<RomEntry> kDotronRom = {
    {"disc_tron_uprt_pg0_10-4-83.1c", 0x4000, 0x0000, 0x40d00195},
    {"disc_tron_uprt_pg1_10-4-83.2c", 0x4000, 0x4000, 0x5a7d1300},
    {"disc_tron_uprt_pg2_10-4-83.3c", 0x4000, 0x8000, 0xcb89c9be},
    {"disc_tron_uprt_pg3_10-4-83.4c", 0x2000, 0xc000, 0x5098faf4},
};
const std::vector<RomEntry> kDotronSnd = {
    {"disc_tron_uprt_snd0_10-4-83.a7", 0x1000, 0x0000, 0x7fb54293},
    {"disc_tron_uprt_snd1_10-4-83.a8", 0x1000, 0x1000, 0xedef7326},
    {"disc_tron_uprt_snd2_9-22-83.a9", 0x1000, 0x2000, 0xe8ef6519},
    {"disc_tron_uprt_snd3_9-22-83.a10", 0x1000, 0x3000, 0x6b5aeb02},
};
const std::vector<RomEntry> kDotronChar = {
    {"loc-bg2.6f", 0x2000, 0x0000, 0x40167124},
    {"loc-bg1.5f", 0x2000, 0x2000, 0xbb2d7a5d},
};
const std::vector<RomEntry> kDotronSprites = {
    {"loc-g.cp4", 0x2000, 0x0000, 0x57a2b1ff},
    {"loc-h.cp3", 0x2000, 0x2000, 0x3bb4d475},
    {"loc-e.cp6", 0x2000, 0x4000, 0xce957f1a},
    {"loc-f.cp5", 0x2000, 0x6000, 0xd26053ce},
    {"loc-c.cp8", 0x2000, 0x8000, 0xef45d146},
    {"loc-d.cp7", 0x2000, 0xa000, 0x5e8a3ef3},
    {"loc-a.cp0", 0x2000, 0xc000, 0xb35f5374},
    {"loc-b.cp9", 0x2000, 0xe000, 0x565a5c48},
};

const std::vector<RomEntry> kTronRom = {
    {"scpu-pga_lctn-c2_tron_aug_9.c2", 0x2000, 0x0000, 0x0de0471a},
    {"scpu-pgb_lctn-c3_tron_aug_9.c3", 0x2000, 0x2000, 0x8ddf8717},
    {"scpu-pgc_lctn-c4_tron_aug_9.c4", 0x2000, 0x4000, 0x4241e3a0},
    {"scpu-pgd_lctn-c5_tron_aug_9.c5", 0x2000, 0x6000, 0x035d2fe7},
    {"scpu-pge_lctn-c6_tron_aug_9.c6", 0x2000, 0x8000, 0x24c185d8},
    {"scpu-pgf_lctn-c7_tron_aug_9.c7", 0x2000, 0xa000, 0x38c4bbaf},
};
const std::vector<RomEntry> kTronSnd = {
    {"ssi-0a_lctn-a7_tron.a7", 0x1000, 0x0000, 0x765e6eba},
    {"ssi-0b_lctn-a8_tron.a8", 0x1000, 0x1000, 0x1b90ccdd},
    {"ssi-0c_lctn-a9_tron.a9", 0x1000, 0x2000, 0x3a4bc629},
};
const std::vector<RomEntry> kTronChar = {
    {"scpu-bgg_lctn-g3_tron.g3", 0x2000, 0x0000, 0x1a9ed2f5},
    {"lscpu-bgh_lctn-g4_tron.g4", 0x2000, 0x2000, 0x3220f974},
};
const std::vector<RomEntry> kTronSprites = {
    {"vga_lctn-e1_tron.e1", 0x2000, 0x0000, 0xbc036d1d},
    {"vgb_lctn-dc1_tron.dc1", 0x2000, 0x2000, 0x58ee14d3},
    {"vgc_lctn-cb1_tron.cb1", 0x2000, 0x4000, 0x3329f9d4},
    {"vgd_lctn-a1_tron.a1", 0x2000, 0x6000, 0x9743f873},
};

const std::vector<RomEntry> kTimberRom = {
    {"timpg0.bin", 0x4000, 0x0000, 0x377032ab},
    {"timpg1.bin", 0x4000, 0x4000, 0xfd772836},
    {"timpg2.bin", 0x4000, 0x8000, 0x632989f9},
    {"timpg3.bin", 0x2000, 0xc000, 0xdae8a0dc},
};
const std::vector<RomEntry> kTimberSnd = {
    {"tima7.bin", 0x1000, 0x0000, 0xc615dc3e},
    {"tima8.bin", 0x1000, 0x1000, 0x83841c87},
    {"tima9.bin", 0x1000, 0x2000, 0x22bcdcd3},
};
const std::vector<RomEntry> kTimberChar = {
    {"timbg1.bin", 0x4000, 0x0000, 0xb1cb2651},
    {"timbg0.bin", 0x4000, 0x4000, 0x2ae352c4},
};
const std::vector<RomEntry> kTimberSprites = {
    {"timfg1.bin", 0x4000, 0x00000, 0x81de4a73},
    {"timfg0.bin", 0x4000, 0x04000, 0x7f3a4f59},
    {"timfg3.bin", 0x4000, 0x08000, 0x37c03272},
    {"timfg2.bin", 0x4000, 0x0c000, 0xe2c2885c},
    {"timfg5.bin", 0x4000, 0x10000, 0xeb636216},
    {"timfg4.bin", 0x4000, 0x14000, 0xb7105eb7},
    {"timfg7.bin", 0x4000, 0x18000, 0xd9c27475},
    {"timfg6.bin", 0x4000, 0x1c000, 0x244778e8},
};

const std::vector<RomEntry> kShollowRom = {
    {"sh-pro.00", 0x2000, 0x0000, 0x95e2b800},
    {"sh-pro.01", 0x2000, 0x2000, 0xb99f6ff8},
    {"sh-pro.02", 0x2000, 0x4000, 0x1202c7b2},
    {"sh-pro.03", 0x2000, 0x6000, 0x0a64afb9},
    {"sh-pro.04", 0x2000, 0x8000, 0x22fa9175},
    {"sh-pro.05", 0x2000, 0xa000, 0x1716e2bb},
};
const std::vector<RomEntry> kShollowSnd = {
    {"sh-snd.01", 0x1000, 0x0000, 0x55a297cc},
    {"sh-snd.02", 0x1000, 0x1000, 0x46fc31f6},
    {"sh-snd.03", 0x1000, 0x2000, 0xb1f4a6a8},
};
const std::vector<RomEntry> kShollowChar = {
    {"sh-bg.00", 0x2000, 0x0000, 0x3e2b333c},
    {"sh-bg.01", 0x2000, 0x2000, 0xd1d70cc4},
};
const std::vector<RomEntry> kShollowSprites = {
    {"sh-fg.00", 0x2000, 0x0000, 0x33f4554e},
    {"sh-fg.01", 0x2000, 0x2000, 0xba1a38b4},
    {"sh-fg.02", 0x2000, 0x4000, 0x6b57f6da},
    {"sh-fg.03", 0x2000, 0x6000, 0x37ea9d07},
};

const std::vector<RomEntry> kDominoRom = {
    {"dmanpg0.bin", 0x2000, 0x0000, 0x3bf3bb1c},
    {"dmanpg1.bin", 0x2000, 0x2000, 0x85cf1d69},
    {"dmanpg2.bin", 0x2000, 0x4000, 0x7dd2177a},
    {"dmanpg3.bin", 0x2000, 0x6000, 0xf2e0aa44},
};
const std::vector<RomEntry> kDominoSnd = {
    {"dm-a7.snd", 0x1000, 0x0000, 0xfa982dcc},
    {"dm-a8.snd", 0x1000, 0x1000, 0x72839019},
    {"dm-a9.snd", 0x1000, 0x2000, 0xad760da7},
    {"dm-a10.snd", 0x1000, 0x3000, 0x958c7287},
};
const std::vector<RomEntry> kDominoChar = {
    {"dmanbg0.bin", 0x2000, 0x0000, 0x9163007f},
    {"dmanbg1.bin", 0x2000, 0x2000, 0x28615c56},
};
const std::vector<RomEntry> kDominoSprites = {
    {"dmanfg0.bin", 0x2000, 0x0000, 0x0b1f9f9e},
    {"dmanfg1.bin", 0x2000, 0x2000, 0x16aa4b9b},
    {"dmanfg2.bin", 0x2000, 0x4000, 0x4a8e76b8},
    {"dmanfg3.bin", 0x2000, 0x6000, 0x1f39257e},
};

const std::vector<RomEntry> kWackoRom = {
    {"wackocpu.2d", 0x2000, 0x0000, 0xc98e29b6},
    {"wackocpu.3d", 0x2000, 0x2000, 0x90b89774},
    {"wackocpu.4d", 0x2000, 0x4000, 0x515edff7},
    {"wackocpu.5d", 0x2000, 0x6000, 0x9b01bf32},
};
const std::vector<RomEntry> kWackoSnd = {
    {"wackosnd.7a", 0x1000, 0x0000, 0x1a58763f},
    {"wackosnd.8a", 0x1000, 0x1000, 0xa4e3c771},
    {"wackosnd.9a", 0x1000, 0x2000, 0x155ba3dd},
};
const std::vector<RomEntry> kWackoChar = {
    {"wackocpu.3g", 0x2000, 0x0000, 0x33160eb1},
    {"wackocpu.4g", 0x2000, 0x2000, 0xdaf37d7c},
};
const std::vector<RomEntry> kWackoSprites = {
    {"wackovid.1e", 0x2000, 0x0000, 0xdca59be7},
    {"wackovid.1d", 0x2000, 0x2000, 0xa02f1672},
    {"wackovid.1b", 0x2000, 0x4000, 0x7d899790},
    {"wackovid.1a", 0x2000, 0x6000, 0x080be3ad},
};

uint8_t analog_axis(bool neg, bool pos) {
    if (neg && !pos) return 0x00;
    if (pos && !neg) return 0xff;
    return 0x80;
}

}  // namespace

Mcr::Mcr(Game game)
    : game_(game),
      main_cpu_(kMainClock),
      sound_cpu_(kSoundClock),
      ay0_(kSoundClock),
      ay1_(kSoundClock) {}

const char* Mcr::title() const {
    switch (game_) {
        case Game::Tapper: return "Tapper";
        case Game::Tron: return "Tron";
        case Game::Shollow: return "Satan's Hollow";
        case Game::Domino: return "Domino Man";
        case Game::Wacko: return "Wacko";
        case Game::Dotron: return "Discs of Tron";
        case Game::Timber: return "Timber";
    }
    return "MCR";
}

void Mcr::decode_chars(const std::vector<uint8_t>& rom, int count) {
    GfxLayout layout;
    layout.width = 16;
    layout.height = 16;
    layout.total = count;
    layout.planes = 4;
    layout.char_increment = 16 * 8;
    const int plane = count * 16 * 8;
    layout.plane_offsets = {plane, plane + 1, 0, 1};
    layout.x_offsets = {0, 0, 2, 2, 4, 4, 6, 6, 8, 8, 10, 10, 12, 12, 14, 14};
    layout.y_offsets = {0, 0, 16, 16, 32, 32, 48, 48, 64, 64, 80, 80, 96, 96, 112, 112};
    chars_.decode(layout, rom);
}

void Mcr::decode_sprites(const std::vector<uint8_t>& rom, int count) {
    GfxLayout layout;
    layout.width = 32;
    layout.height = 32;
    layout.total = count;
    layout.planes = 4;
    layout.char_increment = 32 * 32;
    layout.plane_offsets = {0, 1, 2, 3};
    const int bank = count * 32 * 32;
    layout.x_offsets.resize(32);
    for (int i = 0; i < 4; i++) {
        const int bit = i * 8;
        layout.x_offsets[size_t(i * 8 + 0)] = bit;
        layout.x_offsets[size_t(i * 8 + 1)] = bit + 4;
        layout.x_offsets[size_t(i * 8 + 2)] = bank + bit;
        layout.x_offsets[size_t(i * 8 + 3)] = bank + bit + 4;
        layout.x_offsets[size_t(i * 8 + 4)] = 2 * bank + bit;
        layout.x_offsets[size_t(i * 8 + 5)] = 2 * bank + bit + 4;
        layout.x_offsets[size_t(i * 8 + 6)] = 3 * bank + bit;
        layout.x_offsets[size_t(i * 8 + 7)] = 3 * bank + bit + 4;
    }
    layout.y_offsets.resize(32);
    for (int y = 0; y < 32; y++) layout.y_offsets[size_t(y)] = y * 32;
    sprites_.decode(layout, rom);
}

bool Mcr::load_roms(const std::string& path, std::string* error) {
    RomLoader loader;
    if (!loader.open(path, error)) return false;

    const std::vector<RomEntry>* main = nullptr;
    const std::vector<RomEntry>* snd = nullptr;
    const std::vector<RomEntry>* chars = nullptr;
    const std::vector<RomEntry>* spr = nullptr;
    int char_count = 0, sprite_count = 0;
    size_t gfx_bytes = 0, spr_bytes = 0;

    switch (game_) {
        case Game::Tapper:
            main = &kTapperRom; snd = &kTapperSnd; chars = &kTapperChar; spr = &kTapperSprites;
            char_count = 0x400; sprite_count = 0x100; gfx_bytes = 0x8000; spr_bytes = 0x20000;
            dsw_ = 0xc0;
            break;
        case Game::Dotron:
            main = &kDotronRom; snd = &kDotronSnd; chars = &kDotronChar; spr = &kDotronSprites;
            char_count = 0x200; sprite_count = 0x80; gfx_bytes = 0x4000; spr_bytes = 0x10000;
            dsw_ = 0xff;
            break;
        case Game::Tron:
            main = &kTronRom; snd = &kTronSnd; chars = &kTronChar; spr = &kTronSprites;
            char_count = 0x200; sprite_count = 0x40; gfx_bytes = 0x4000; spr_bytes = 0x8000;
            dsw_ = 0x00;
            break;
        case Game::Timber:
            main = &kTimberRom; snd = &kTimberSnd; chars = &kTimberChar; spr = &kTimberSprites;
            char_count = 0x400; sprite_count = 0x100; gfx_bytes = 0x8000; spr_bytes = 0x20000;
            dsw_ = 0xc0;
            break;
        case Game::Shollow:
            main = &kShollowRom; snd = &kShollowSnd; chars = &kShollowChar; spr = &kShollowSprites;
            char_count = 0x200; sprite_count = 0x40; gfx_bytes = 0x4000; spr_bytes = 0x8000;
            dsw_ = 0xfd;
            break;
        case Game::Domino:
            main = &kDominoRom; snd = &kDominoSnd; chars = &kDominoChar; spr = &kDominoSprites;
            char_count = 0x200; sprite_count = 0x40; gfx_bytes = 0x4000; spr_bytes = 0x8000;
            dsw_ = 0x3e;
            break;
        case Game::Wacko:
            main = &kWackoRom; snd = &kWackoSnd; chars = &kWackoChar; spr = &kWackoSprites;
            char_count = 0x200; sprite_count = 0x40; gfx_bytes = 0x4000; spr_bytes = 0x8000;
            dsw_ = 0x3e;
            break;
    }

    std::vector<uint8_t> main_rom(0x10000, 0xff);
    if (!loader.load(*main, main_rom, error)) return false;
    std::copy(main_rom.begin(), main_rom.end(), mem_.begin());

    std::vector<uint8_t> snd_rom(0x4000, 0xff);
    if (!loader.load(*snd, snd_rom, error)) return false;
    std::copy(snd_rom.begin(), snd_rom.end(), sound_mem_.begin());

    std::vector<uint8_t> char_rom(gfx_bytes, 0);
    if (!loader.load(*chars, char_rom, error)) return false;
    decode_chars(char_rom, char_count);

    std::vector<uint8_t> spr_rom(spr_bytes, 0);
    if (!loader.load(*spr, spr_rom, error)) return false;
    decode_sprites(spr_rom, sprite_count);

    warnings_ = loader.warnings();
    return true;
}

bool Mcr::init(const std::string& rom_path, std::string* error) {
    if (!load_roms(rom_path, error)) return false;

    main_cpu_.set_memory_handlers(
        [this](uint16_t a) { return main_read(a); },
        [this](uint16_t a, uint8_t v) { main_write(a, v); });
    main_cpu_.set_io_handlers(
        [this](uint16_t p) { return main_in(p); },
        [this](uint16_t p, uint8_t v) { main_out(p, v); });
    main_cpu_.set_cycle_handler([this](int c) { on_main_cycles(c); });

    sound_cpu_.set_memory_handlers(
        [this](uint16_t a) { return sound_read(a); },
        [this](uint16_t a, uint8_t v) { sound_write(a, v); });

    ctc_.set_irq_callback([this](IrqLine s, uint8_t v) {
        if (s != IrqLine::Clear) ctc_irqs_++;
        main_cpu_.set_irq(s, v);
    });
    ctc_.set_zc_callback([this](int ch) {
        if (ch == 0) ctc_.pulse_trigger(1);
    });

    reset();
    return true;
}

void Mcr::reset() {
    main_cpu_.reset();
    sound_cpu_.reset();
    // Tapper's boot CALL $01AC is a block-copy that only runs when BC == IX
    // (IX then points at a 6-byte DE/BC/HL descriptor). A hard reset leaves
    // both at 0, which accidentally copies ~62K and clobbers the return
    // address. Real Z80 index registers power on as $FFFF, so the CALL is a
    // no-op until the game sets IX on purpose.
    main_cpu_.ix = 0xffff;
    main_cpu_.iy = 0xffff;
    sound_cpu_.ix = 0xffff;
    sound_cpu_.iy = 0xffff;
    ctc_.reset();
    ay0_.reset();
    ay1_.reset();
    nvram_.fill(0);
    in0_ = in1_ = in2_ = in3_ = 0xff;
    analog_x_ = analog_y_ = 0x80;
    ssio_status_ = 0;
    ssio_data_.fill(0);
    ssio_14024_ = 0;
    ssio_14024_acc_ = 0;
    ctc_irqs_ = 0;
    audio_.clear();
    audio_acc_ = 0;
    palette_.fill(0xff000000);
    std::fill(framebuffer_.begin(), framebuffer_.end(), 0xff000000);
}

void Mcr::set_dip_switch(int bank, uint8_t value) {
    if (bank == 0) dsw_ = value;
}

void Mcr::apply_inputs_tapper(const MachineInputs& in) {
    uint8_t i0 = 0xff;
    if (in.coin1) i0 &= 0xfe;
    if (in.coin2) i0 &= 0xfd;
    if (in.player1.start) i0 &= 0xfb;
    if (in.player2.start) i0 &= 0xf7;
    in0_ = i0;

    auto stick = [](const InputState& p) {
        uint8_t v = 0xff;
        if (p.right) v &= 0xfe;
        if (p.left) v &= 0xfd;
        if (p.down) v &= 0xfb;
        if (p.up) v &= 0xf7;
        if (p.button2) v &= 0xef;
        if (p.button1) v &= 0xdf;
        return v;
    };
    in1_ = stick(in.player1);
    in2_ = stick(in.player2);
}

void Mcr::apply_inputs_dotron(const MachineInputs& in) {
    analog_x_ = analog_axis(in.player1.left, in.player1.right);
    in1_ = uint8_t(analog_x_ | 0x80);
    uint8_t i0 = 0xff;
    if (in.coin1) i0 &= 0xfe;
    if (in.coin2) i0 &= 0xfd;
    if (in.player1.start) i0 &= 0xfb;
    if (in.player2.start) i0 &= 0xf7;
    if (in.player1.button1) i0 &= 0xef;
    in0_ = i0;
    uint8_t i2 = 0xff;
    if (in.player1.left) i2 &= 0xfe;
    if (in.player1.right) i2 &= 0xfd;
    if (in.player1.up) i2 &= 0xfb;
    if (in.player1.down) i2 &= 0xf7;
    if (in.player1.button3) i2 &= 0xef;
    if (in.player2.button1) i2 &= 0xdf;
    if (in.player1.button2) i2 &= 0xbf;
    in2_ = i2;
}

void Mcr::apply_inputs_tron(const MachineInputs& in) {
    analog_x_ = analog_axis(in.player1.left, in.player1.right);
    in1_ = analog_x_;
    uint8_t i0 = 0xff;
    if (in.coin1) i0 &= 0xfe;
    if (in.coin2) i0 &= 0xfd;
    if (in.player1.start) i0 &= 0xfb;
    if (in.player2.start) i0 &= 0xf7;
    if (in.player1.button1) i0 &= 0xef;
    in0_ = i0;
    uint8_t i2 = 0xff;
    if (in.player1.left) i2 &= 0xfe;
    if (in.player1.right) i2 &= 0xfd;
    if (in.player1.up) i2 &= 0xfb;
    if (in.player1.down) i2 &= 0xf7;
    if (in.player2.left) i2 &= 0xef;
    if (in.player2.right) i2 &= 0xdf;
    if (in.player2.up) i2 &= 0xbf;
    if (in.player2.down) i2 &= 0x7f;
    in2_ = i2;
}

void Mcr::apply_inputs_shollow(const MachineInputs& in) {
    uint8_t i0 = 0xff;
    if (in.coin1) i0 &= 0xfe;
    if (in.coin2) i0 &= 0xfd;
    if (in.player1.start) i0 &= 0xfb;
    if (in.player2.start) i0 &= 0xf7;
    in0_ = i0;
    uint8_t i1 = 0xff;
    if (in.player1.left) i1 &= 0xfe;
    if (in.player1.right) i1 &= 0xfd;
    if (in.player1.button2) i1 &= 0xfb;
    if (in.player1.button1) i1 &= 0xf7;
    if (in.player2.left) i1 &= 0xef;
    if (in.player2.right) i1 &= 0xdf;
    if (in.player2.button2) i1 &= 0xbf;
    if (in.player2.button1) i1 &= 0x7f;
    in1_ = i1;
}

void Mcr::apply_inputs_domino(const MachineInputs& in) {
    uint8_t i0 = 0xff;
    if (in.coin1) i0 &= 0xfe;
    if (in.coin2) i0 &= 0xfd;
    if (in.player1.start) i0 &= 0xfb;
    if (in.player2.start) i0 &= 0xf7;
    if (in.player1.button1) i0 &= 0xef;
    in0_ = i0;
    uint8_t i1 = 0xff;
    if (in.player1.left) i1 &= 0xfe;
    if (in.player1.right) i1 &= 0xfd;
    if (in.player1.up) i1 &= 0xfb;
    if (in.player1.down) i1 &= 0xf7;
    in1_ = i1;
    uint8_t i2 = 0xff;
    if (in.player2.left) i2 &= 0xfe;
    if (in.player2.right) i2 &= 0xfd;
    if (in.player2.up) i2 &= 0xfb;
    if (in.player2.down) i2 &= 0xf7;
    if (in.player2.button1) i2 &= 0xef;
    in2_ = i2;
}

void Mcr::apply_inputs_wacko(const MachineInputs& in) {
    analog_x_ = analog_axis(in.player1.left, in.player1.right);
    analog_y_ = analog_axis(in.player1.up, in.player1.down);
    in1_ = analog_x_;
    in2_ = analog_y_;
    uint8_t i0 = 0xff;
    if (in.coin1) i0 &= 0xfe;
    if (in.coin2) i0 &= 0xfd;
    if (in.player1.start) i0 &= 0xfb;
    if (in.player2.start) i0 &= 0xf7;
    if (in.player1.button1) i0 &= 0xef;
    in0_ = i0;
    uint8_t i3 = 0xff;
    if (in.player1.right) i3 &= 0xfe;
    if (in.player1.left) i3 &= 0xfd;
    if (in.player1.down) i3 &= 0xfb;
    if (in.player1.up) i3 &= 0xf7;
    if (in.player2.right) i3 &= 0xef;
    if (in.player2.left) i3 &= 0xdf;
    if (in.player2.down) i3 &= 0xbf;
    if (in.player2.up) i3 &= 0x7f;
    in3_ = i3;
}

void Mcr::set_inputs(const MachineInputs& in) {
    switch (game_) {
        case Game::Tapper:
        case Game::Timber: apply_inputs_tapper(in); break;
        case Game::Dotron: apply_inputs_dotron(in); break;
        case Game::Tron: apply_inputs_tron(in); break;
        case Game::Shollow: apply_inputs_shollow(in); break;
        case Game::Domino: apply_inputs_domino(in); break;
        case Game::Wacko: apply_inputs_wacko(in); break;
    }
}

void Mcr::set_color(int index, uint16_t value) {
    const uint32_t r = pal3bit(value >> 6);
    const uint32_t g = pal3bit(value);
    const uint32_t b = pal3bit(value >> 3);
    palette_[index & 0xff] = 0xff000000u | (r << 16) | (g << 8) | b;
}

uint8_t Mcr::tapper_read(uint16_t addr) {
    if (addr <= 0xdfff || (addr >= 0xf000 && addr <= 0xf7ff)) return mem_[addr];
    if (addr >= 0xe000 && addr <= 0xe7ff) return nvram_[addr & 0x7ff];
    if (addr >= 0xe800 && addr <= 0xebff) return mem_[0xe800 + (addr & 0x1ff)];
    return 0xff;
}

void Mcr::tapper_write(uint16_t addr, uint8_t value) {
    if (addr <= 0xdfff) return;
    if (addr >= 0xe000 && addr <= 0xe7ff) {
        nvram_[addr & 0x7ff] = value;
        return;
    }
    if (addr >= 0xe800 && addr <= 0xebff) {
        mem_[0xe800 + (addr & 0x1ff)] = value;
        return;
    }
    if (addr >= 0xf000 && addr <= 0xf7ff) {
        mem_[addr] = value;
        return;
    }
    if (addr >= 0xf800) set_color((addr & 0x7f) >> 1, uint16_t(value | ((addr & 1) << 8)));
}

uint8_t Mcr::tron_read(uint16_t addr) {
    if (addr <= 0xbfff) return mem_[addr];
    if (addr >= 0xc000 && addr <= 0xdfff) return nvram_[addr & 0x7ff];
    if ((addr & 0xfff) <= 0x7ff) return mem_[0xe000 + (addr & 0x1ff)];
    return mem_[0xe800 + (addr & 0x7ff)];
}

void Mcr::tron_write(uint16_t addr, uint8_t value) {
    if (addr <= 0xbfff) return;
    if (addr >= 0xc000 && addr <= 0xdfff) {
        nvram_[addr & 0x7ff] = value;
        return;
    }
    if ((addr & 0xfff) <= 0x7ff) {
        mem_[0xe000 + (addr & 0x1ff)] = value;
        return;
    }
    mem_[0xe800 + (addr & 0x7ff)] = value;
    if ((addr & 0x780) == 0x780) {
        set_color((addr & 0x7f) >> 1, uint16_t(value | ((addr & 1) << 8)));
    }
}

uint8_t Mcr::main_read(uint16_t addr) {
    return is_tron_map() ? tron_read(addr) : tapper_read(addr);
}

void Mcr::main_write(uint16_t addr, uint8_t value) {
    if (is_tron_map()) tron_write(addr, value);
    else tapper_write(addr, value);
}

uint8_t Mcr::main_in(uint16_t port) {
    const uint8_t p = uint8_t(port & 0xff);
    if (p <= 0x1f) {
        switch (p & 7) {
            case 0: return in0_;
            case 1: return in1_;
            case 2: return in2_;
            case 3: return dsw_;
            case 4: return in3_;
            case 7: return ssio_status_;
            default: return 0xff;
        }
    }
    if (p >= 0xf0 && p <= 0xf3) return ctc_.read(uint8_t(p & 3));
    return 0xff;
}

void Mcr::main_out(uint16_t port, uint8_t value) {
    const uint8_t p = uint8_t(port & 0xff);
    if (p >= 0x1c && p <= 0x1f) {
        ssio_data_[p & 3] = value;
        return;
    }
    if (p >= 0xf0 && p <= 0xf3) ctc_.write(uint8_t(p & 3), value);
}

uint8_t Mcr::sound_read(uint16_t addr) {
    if (addr <= 0x3fff) return sound_mem_[addr];
    if (addr >= 0x8000 && addr <= 0x8fff) return sound_mem_[0x8000 | (addr & 0x3ff)];
    if (addr >= 0x9000 && addr <= 0x9fff) return ssio_data_[addr & 3];
    if (addr >= 0xa000 && addr <= 0xafff && (addr & 3) == 1) return ay0_.read();
    if (addr >= 0xb000 && addr <= 0xbfff && (addr & 3) == 1) return ay1_.read();
    if (addr >= 0xe000 && addr <= 0xefff) {
        ssio_14024_ = 0;
        sound_cpu_.set_irq(IrqLine::Clear);
        return 0xff;
    }
    if (addr >= 0xf000) return 0xff;
    return 0xff;
}

void Mcr::sound_write(uint16_t addr, uint8_t value) {
    if (addr <= 0x3fff) return;
    if (addr >= 0x8000 && addr <= 0x8fff) {
        sound_mem_[0x8000 | (addr & 0x3ff)] = value;
        return;
    }
    if (addr >= 0xa000 && addr <= 0xafff) {
        if ((addr & 3) == 0) ay0_.control(value);
        if ((addr & 3) == 2) ay0_.write(value);
        return;
    }
    if (addr >= 0xb000 && addr <= 0xbfff) {
        if ((addr & 3) == 0) ay1_.control(value);
        if ((addr & 3) == 2) ay1_.write(value);
        return;
    }
    if (addr >= 0xc000 && addr <= 0xcfff) ssio_status_ = value;
}

void Mcr::ssio_14024_tick() {
    ssio_14024_ = uint8_t((ssio_14024_ + 1) & 0x7f);
    if ((ssio_14024_ & 0x3f) == 0) {
        sound_cpu_.set_irq((ssio_14024_ & 0x40) != 0 ? IrqLine::Assert : IrqLine::Clear);
    }
}

void Mcr::on_main_cycles(int cycles) {
    ctc_.tick(cycles);
    audio_acc_ += int64_t(cycles) * kSampleRate;
    while (audio_acc_ >= int64_t(kMainClock)) {
        audio_acc_ -= int64_t(kMainClock);
        const int32_t mix = ay0_.update() + ay1_.update();
        audio_.push_back(int16_t(std::clamp(mix, int32_t(-32768), int32_t(32767))));
    }
}

void Mcr::update_video_tapper() {
    std::fill(framebuffer_.begin(), framebuffer_.end(), 0xff000000u);
    for (int f = 0; f < 0x3c0; f++) {
        const uint16_t atrib = uint16_t(mem_[0xf000 + f * 2] | (mem_[0xf001 + f * 2] << 8));
        const int color = (atrib >> 12) & 3;
        const int tx = f & 0x1f;
        const int ty = f >> 5;
        const int nchar = atrib & 0x3ff;
        const bool flipx = (atrib & 0x400) != 0;
        const bool flipy = (atrib & 0x800) != 0;
        const uint8_t* src = chars_.element(nchar);
        const int pal = color << 4;
        for (int y = 0; y < 16; y++) {
            const int sy = flipy ? (15 - y) : y;
            for (int x = 0; x < 16; x++) {
                const int sx = flipx ? (15 - x) : x;
                framebuffer_[(ty * 16 + y) * kScreenW + tx * 16 + x] =
                    palette_[(pal + src[sy * 16 + sx]) & 0xff];
            }
        }
    }
    std::vector<uint8_t> prio(512 * 512, 1);
    for (int f = 0x7f; f >= 0; --f) {
        const int base = 0xe800 + f * 4;
        const int sx = ((mem_[base + 3] - 3) * 2) & 0x1ff;
        const int sy = ((241 - mem_[base]) * 2) & 0x1ff;
        const uint8_t attr = mem_[base + 1];
        const int code = (mem_[base + 2] + ((attr & 8) << 5)) % std::max(1, sprites_.total());
        const int color = ((~attr) & 3) << 4;
        const bool flipx = (attr & 0x10) != 0;
        const bool flipy = (attr & 0x20) != 0;
        const uint8_t* src = sprites_.element(code);
        for (int y = 0; y < 32; y++) {
            const int yy = flipy ? (31 - y) : y;
            for (int x = 0; x < 32; x++) {
                const int xx = flipx ? (31 - x) : x;
                const uint8_t pen = src[yy * 32 + xx];
                const int dx = (sx + x) & 0x1ff;
                const int dy = (sy + y) & 0x1ff;
                uint8_t& slot = prio[size_t(dx) * 512 + size_t(dy)];
                if (!slot) continue;
                if ((pen & 0x0f) == 0) continue;
                slot = 0;
                if ((pen & 7) == 0) continue;
                if (dx < kScreenW && dy < kScreenH)
                    framebuffer_[size_t(dy) * kScreenW + size_t(dx)] = palette_[(color + pen) & 0xff];
            }
        }
    }
}

void Mcr::update_video_tron() {
    std::array<uint8_t, 32 * 32> prio{};
    std::fill(framebuffer_.begin(), framebuffer_.end(), 0xff000000u);
    for (int f = 0; f < 0x3c0; f++) {
        const uint16_t atrib = uint16_t(mem_[0xe800 + f * 2] | (mem_[0xe801 + f * 2] << 8));
        const int color = (atrib >> 11) & 3;
        const int tx = f & 0x1f;
        const int ty = f >> 5;
        prio[size_t(ty * 32 + tx)] = uint8_t(atrib >> 14);
        const int nchar = atrib & 0x3ff;
        const bool flipx = (atrib & 0x200) != 0;
        const bool flipy = (atrib & 0x400) != 0;
        const uint8_t* src = chars_.element(nchar);
        const int pal = color << 4;
        for (int y = 0; y < 16; y++) {
            const int sy = flipy ? (15 - y) : y;
            for (int x = 0; x < 16; x++) {
                const int sx = flipx ? (15 - x) : x;
                framebuffer_[(ty * 16 + y) * kScreenW + tx * 16 + x] =
                    palette_[(pal + src[sy * 16 + sx]) & 0xff];
            }
        }
    }
    for (int f = 0; f < 0x80; f++) {
        const int base = 0xe000 + f * 4;
        const int sx = ((mem_[base + 2] - 4) * 2) & 0x1ff;
        const int sy = ((240 - mem_[base]) * 2) & 0x1ff;
        const uint8_t attr = mem_[base + 1];
        const int code = attr % std::max(1, sprites_.total());
        const bool flipy = (attr & 0x80) != 0;
        const bool flipx = (attr & 0x40) != 0;
        const uint8_t* src = sprites_.element(code);
        for (int y = 0; y < 32; y++) {
            const int yy = flipy ? (31 - y) : y;
            for (int x = 0; x < 32; x++) {
                const int xx = flipx ? (31 - x) : x;
                const int dx = (sx + x) & 0x1ff;
                const int dy = (sy + y) & 0x1ff;
                const uint8_t tile_pri = prio[size_t(((dy / 16) & 0x1f) * 32 + ((dx / 16) & 0x1f))];
                const uint8_t pen = uint8_t((tile_pri << 4) | src[yy * 32 + xx]);
                if ((pen & 7) == 0) continue;
                if (dx < kScreenW && dy < kScreenH)
                    framebuffer_[size_t(dy) * kScreenW + size_t(dx)] = palette_[pen & 0xff];
            }
        }
    }
}

void Mcr::update_video() {
    if (is_tron_map()) update_video_tron();
    else update_video_tapper();
}

void Mcr::run_frame() {
    const int main_line = int(kMainClock / kFps / kScanlines);
    const int sound_line = int(kSoundClock / kFps / kScanlines);
    constexpr int kSsioPeriod = 160 * 2 * 16 * 10;  // mcr_hw.pas 2000000/(160*2*16*10)
    for (int line = 0; line < kScanlines; line++) {
        if (line == 0) {
            ctc_.pulse_trigger(2);
            update_video();
            ctc_.pulse_trigger(3);
        } else if (line == 240) {
            ctc_.pulse_trigger(2);
        }
        int left = main_line;
        while (left > 0) {
            const int ran = main_cpu_.run(left);
            if (ran <= 0) break;
            left -= ran;
        }
        int sleft = sound_line;
        while (sleft > 0) {
            const int ran = sound_cpu_.run(sleft);
            if (ran <= 0) break;
            sleft -= ran;
        }
        ssio_14024_acc_ += sound_line;
        while (ssio_14024_acc_ >= kSsioPeriod) {
            ssio_14024_acc_ -= kSsioPeriod;
            ssio_14024_tick();
        }
    }
}

void Mcr::drain_audio(std::vector<int16_t>& out) {
    out.swap(audio_);
    audio_.clear();
}

}  // namespace dsp
