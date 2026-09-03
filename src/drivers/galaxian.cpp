#include "drivers/galaxian.h"

#include "core/rom_loader.h"

#include <algorithm>

namespace dsp {
namespace {

const std::vector<RomEntry> kGalaxianCpu = {
    {"galmidw.u", 0x0800, 0x0000, 0x745e2d61},
    {"galmidw.v", 0x0800, 0x0800, 0x9c999a40},
    {"galmidw.w", 0x0800, 0x1000, 0xb5894925},
    {"galmidw.y", 0x0800, 0x1800, 0x6b3ca10b},
    {"7l", 0x0800, 0x2000, 0x1b933207},
};
const std::vector<RomEntry> kGalaxianGfx = {
    {"1h.bin", 0x0800, 0x0000, 0x39fb43a4},
    {"1k.bin", 0x0800, 0x0800, 0x7e3f56a2},
};
const std::vector<RomEntry> kGalaxianPal = {{"6l.bpr", 0x0020, 0x0000, 0xc3ac9467}};

const std::vector<RomEntry> kMooncrstCpu = {
    {"mc1", 0x0800, 0x0000, 0x7d954a7a},
    {"mc2", 0x0800, 0x0800, 0x44bb7cfa},
    {"mc3", 0x0800, 0x1000, 0x9c412104},
    {"mc4", 0x0800, 0x1800, 0x7e9b1ab5},
    {"mc5.7r", 0x0800, 0x2000, 0x16c759af},
    {"mc6.8d", 0x0800, 0x2800, 0x69bcafdb},
    {"mc7.8e", 0x0800, 0x3000, 0xb50dbc46},
    {"mc8", 0x0800, 0x3800, 0x18ca312b},
};
const std::vector<RomEntry> kMooncrstGfx = {
    {"mcs_b", 0x0800, 0x0000, 0xfb0f1f81},
    {"mcs_d", 0x0800, 0x0800, 0x13932a15},
    {"mcs_a", 0x0800, 0x1000, 0x631ebb5a},
    {"mcs_c", 0x0800, 0x1800, 0x24cfd145},
};
const std::vector<RomEntry> kMooncrstPal = {{"mmi6331.6l", 0x0020, 0x0000, 0x6a0c7d87}};

const std::vector<RomEntry> kScrambleCpu = {
    {"s1.2d", 0x0800, 0x0000, 0xea35ccaa},
    {"s2.2e", 0x0800, 0x0800, 0xe7bba1b3},
    {"s3.2f", 0x0800, 0x1000, 0x12d7fc3e},
    {"s4.2h", 0x0800, 0x1800, 0xb59360eb},
    {"s5.2j", 0x0800, 0x2000, 0x4919a91c},
    {"s6.2l", 0x0800, 0x2800, 0x26a4547b},
    {"s7.2m", 0x0800, 0x3000, 0x0bb49470},
    {"s8.2p", 0x0800, 0x3800, 0x6a5740e5},
};
const std::vector<RomEntry> kScrambleGfx = {
    {"c2.5f", 0x0800, 0x0000, 0x4708845b},
    {"c1.5h", 0x0800, 0x0800, 0x11fd2887},
};
const std::vector<RomEntry> kScramblePal = {{"c01s.6e", 0x0020, 0x0000, 0x4e3caeab}};
const std::vector<RomEntry> kScrambleSound = {
    {"ot1.5c", 0x0800, 0x0000, 0xbcd297f0},
    {"ot2.5d", 0x0800, 0x0800, 0xde7912da},
    {"ot3.5e", 0x0800, 0x1000, 0xba2fa933},
};

const std::vector<RomEntry> kFroggerCpu = {
    {"frogger.26", 0x1000, 0x0000, 0x597696d6},
    {"frogger.27", 0x1000, 0x1000, 0xb6e6fcc3},
    {"frsm3.7", 0x1000, 0x2000, 0xaca22ae0},
};
const std::vector<RomEntry> kFroggerGfx = {
    {"frogger.607", 0x0800, 0x0000, 0x05f7d883},
    {"frogger.606", 0x0800, 0x0800, 0xf524ee30},
};
const std::vector<RomEntry> kFroggerPal = {{"pr-91.6l", 0x0020, 0x0000, 0x413703bf}};
const std::vector<RomEntry> kFroggerSound = {
    {"frogger.608", 0x0800, 0x0000, 0xe8ab0256},
    {"frogger.609", 0x0800, 0x0800, 0x7380a48f},
    {"frogger.610", 0x0800, 0x1000, 0x31d7eb27},
};

// Frogger has D0/D1 swapped on the sound ROM and on the second GFX ROM.
uint8_t bitswap8_d0d1(uint8_t v) {
    return uint8_t((v & 0xfc) | ((v & 0x01) << 1) | ((v & 0x02) >> 1));
}

// Frogger scrambles the low/high nibble of the horizontal coordinate.
inline int frogger_x(uint8_t v) { return ((v & 0x0f) << 4) | (v >> 4); }

// Frogger rotates the colour bits of the attribute/sprite colour field.
inline int frogger_color(uint8_t v) {
    return ((((v >> 1) & 3) + ((v << 2) & 4)) & 7) << 2;
}

GfxLayout char_layout(int total) {
    GfxLayout layout;
    layout.width = 8;
    layout.height = 8;
    layout.total = total;
    layout.planes = 2;
    layout.char_increment = 8 * 8;
    layout.rotate_cw = true;
    layout.plane_offsets = {0, total * 8 * 8};
    layout.x_offsets = {0, 1, 2, 3, 4, 5, 6, 7};
    layout.y_offsets = {0 * 8, 1 * 8, 2 * 8, 3 * 8, 4 * 8, 5 * 8, 6 * 8, 7 * 8};
    return layout;
}

GfxLayout sprite_layout(int total) {
    GfxLayout layout;
    layout.width = 16;
    layout.height = 16;
    layout.total = total;
    layout.planes = 2;
    layout.char_increment = 16 * 16;
    layout.rotate_cw = true;
    layout.plane_offsets = {0, total * 16 * 16};
    layout.x_offsets = {0, 1, 2, 3, 4, 5, 6, 7, 8 * 8 + 0, 8 * 8 + 1, 8 * 8 + 2, 8 * 8 + 3,
                        8 * 8 + 4, 8 * 8 + 5, 8 * 8 + 6, 8 * 8 + 7};
    layout.y_offsets = {0 * 8,  1 * 8,  2 * 8,  3 * 8,  4 * 8,  5 * 8,  6 * 8,  7 * 8,
                        16 * 8, 17 * 8, 18 * 8, 19 * 8, 20 * 8, 21 * 8, 22 * 8, 23 * 8};
    return layout;
}

uint8_t bitswap8_moon(uint8_t v) {
    return uint8_t(((v >> 7) & 1) << 7 | ((v >> 2) & 1) << 6 | ((v >> 5) & 1) << 5 |
                   ((v >> 4) & 1) << 4 | ((v >> 3) & 1) << 3 | ((v >> 6) & 1) << 2 |
                   ((v >> 1) & 1) << 1 | ((v >> 0) & 1) << 0);
}

}  // namespace

Galaxian::Galaxian(Game game)
    : game_(game),
      cpu_(kCpuClock),
      sound_cpu_(kSoundClock),
      ay0_(kSoundClock),
      ay1_(kSoundClock),
      framebuffer_(size_t(kScreenWidth * kScreenHeight), 0xff000000u) {
    cpu_.set_memory_handlers(
        [this](uint16_t a) { return read_byte(a); },
        [this](uint16_t a, uint8_t v) { write_byte(a, v); });
    cpu_.set_cycle_handler([this](int cycles) { on_cycles(cycles); });

    // Konami sound board (MAME konami_sound_map / konami_sound_portmap)
    sound_cpu_.set_memory_handlers(
        [this](uint16_t a) { return sound_read(a); },
        [this](uint16_t a, uint8_t v) { sound_write(a, v); });
    sound_cpu_.set_io_handlers(
        [this](uint16_t p) { return sound_in(p); },
        [this](uint16_t p, uint8_t v) { sound_out(p, v); });
    ay0_.set_port_handlers(
        [this]() { return sound_latch_; },
        [this]() { return konami_sound_timer_r(); },
        {}, {});
}

const char* Galaxian::title() const {
    switch (game_) {
        case Game::MoonCresta: return "Moon Cresta";
        case Game::Scramble: return "Scramble";
        case Game::Frogger: return "Frogger";
        default: return "Galaxian";
    }
}

void Galaxian::decrypt_mooncrst(std::vector<uint8_t>& rom) {
    // MAME galaxian_state::decode_mooncrst: XOR both parities, bitswap evens.
    for (size_t f = 0; f < rom.size() && f < 0x4000; ++f) {
        uint8_t res = rom[f];
        if (res & 0x02) res = uint8_t(res ^ 0x40);
        if (res & 0x20) res = uint8_t(res ^ 0x04);
        if ((f & 1) == 0) res = bitswap8_moon(res);
        rom[f] = res;
    }
}

void Galaxian::setup_scramble_ppi() {
    // PPI0: inputs (active-low style as eventos_scramble builds)
    ppi0_.set_port_a([this]() { return in0_; });
    ppi0_.set_port_b([this]() { return uint8_t(in1_ | dsw_a_); });
    ppi0_.set_port_c([this]() {
        return uint8_t(in2_ | dsw_b_ | (scramble_prot_ & 0x80) |
                       ((scramble_prot_ & 0x80) >> 2));
    });

    // PPI1: sound latch + protection + IRQ to sound CPU
    ppi1_.set_port_a({}, [this](uint8_t v) { sound_latch_ = v; });
    ppi1_.set_port_b({}, [this](uint8_t v) {
        // konami_sound_control_w: bit3 falling edge → INT; bit4 = mute
        const uint8_t old = port_b_latch_;
        port_b_latch_ = v;
        if ((old & 0x08) && !(v & 0x08) && sound_present_) {
            sound_cpu_.set_irq(IrqLine::Hold);
        }
        sound_mute_ = (v & 0x10) != 0;
    });
    ppi1_.set_port_c(
        [this]() { return scramble_prot_; },
        [this](uint8_t v) {
            scramble_prot_state_ =
                uint16_t((scramble_prot_state_ << 4) | (v & 0x0f));
            const int num1 = (scramble_prot_state_ >> 8) & 0x0f;
            const int num2 = (scramble_prot_state_ >> 4) & 0x0f;
            const int op = scramble_prot_state_ & 0x0f;
            switch (op) {
                case 0x6:
                    scramble_prot_ = uint8_t(scramble_prot_ ^ 0x80);
                    break;
                case 0x9: {
                    int p = num1 + 1;
                    scramble_prot_ = p > 0x0f ? 0xf0 : uint8_t(p << 4);
                    break;
                }
                case 0xa:
                    scramble_prot_ = 0;
                    break;
                case 0xb: {
                    const int res = num2 - num1;
                    scramble_prot_ = res < 0 ? 0 : uint8_t(res << 4);
                    break;
                }
                case 0xf: {
                    const int res = num1 - num2;
                    scramble_prot_ = res < 0 ? 0 : uint8_t(res << 4);
                    break;
                }
                default:
                    break;
            }
        });
}

bool Galaxian::load_roms(const std::string& rom_path, std::string* error) {
    RomLoader loader;
    if (!loader.open(rom_path, error)) return false;

    if (game_ == Game::Scramble) {
        std::vector<uint8_t> cpu_rom(0x4000, 0);
        if (!loader.load(kScrambleCpu, cpu_rom, error)) return false;
        std::copy(cpu_rom.begin(), cpu_rom.end(), memory_.begin());

        std::vector<uint8_t> gfx_rom(0x1000, 0);
        if (!loader.load(kScrambleGfx, gfx_rom, error)) return false;
        decode_graphics(gfx_rom, 0x100, 0x40);

        std::vector<uint8_t> pal(0x20, 0);
        if (!loader.load(kScramblePal, pal, error)) return false;
        build_palette(pal);

        std::vector<uint8_t> snd(0x1800, 0);
        if (loader.load(kScrambleSound, snd, nullptr)) {
            std::copy(snd.begin(), snd.end(), sound_memory_.begin());
            sound_present_ = true;
        } else {
            sound_present_ = false;
            warnings_.push_back("Scramble sound ROMs missing; audio disabled");
        }
        setup_scramble_ppi();
    } else if (game_ == Game::Frogger) {
        std::vector<uint8_t> cpu_rom(0x3000, 0);
        if (!loader.load(kFroggerCpu, cpu_rom, error)) return false;
        std::copy(cpu_rom.begin(), cpu_rom.end(), memory_.begin());

        std::vector<uint8_t> gfx_rom(0x1000, 0);
        if (!loader.load(kFroggerGfx, gfx_rom, error)) return false;
        for (size_t i = 0x800; i < gfx_rom.size(); ++i)
            gfx_rom[i] = bitswap8_d0d1(gfx_rom[i]);
        decode_graphics(gfx_rom, 0x100, 0x40);

        std::vector<uint8_t> pal(0x20, 0);
        if (!loader.load(kFroggerPal, pal, error)) return false;
        build_palette(pal);

        std::vector<uint8_t> snd(0x1800, 0);
        if (loader.load(kFroggerSound, snd, nullptr)) {
            for (size_t i = 0; i < 0x800; ++i) snd[i] = bitswap8_d0d1(snd[i]);
            std::copy(snd.begin(), snd.end(), sound_memory_.begin());
            sound_present_ = true;
        } else {
            sound_present_ = false;
            warnings_.push_back("Frogger sound ROMs missing; audio disabled");
        }
        setup_frogger_ppi();
    } else if (game_ == Game::MoonCresta) {
        std::vector<uint8_t> cpu_rom(0x4000, 0);
        if (!loader.load(kMooncrstCpu, cpu_rom, error)) return false;
        decrypt_mooncrst(cpu_rom);
        std::copy(cpu_rom.begin(), cpu_rom.end(), memory_.begin());

        std::vector<uint8_t> gfx_rom(0x2000, 0);
        if (!loader.load(kMooncrstGfx, gfx_rom, error)) return false;
        decode_graphics(gfx_rom, 0x200, 0x80);

        std::vector<uint8_t> pal(0x20, 0);
        if (!loader.load(kMooncrstPal, pal, error)) return false;
        build_palette(pal);
        dsw_b_ = 0x80;  // Moon Cresta IN1: English
        dsw_c_ = 0x00;  // Moon Cresta IN2: 1C_1C / 1C_1C
    } else {
        std::vector<uint8_t> cpu_rom(0x4000, 0);
        if (!loader.load(kGalaxianCpu, cpu_rom, error)) return false;
        std::copy(cpu_rom.begin(), cpu_rom.begin() + 0x2800, memory_.begin());

        std::vector<uint8_t> gfx_rom(0x1000, 0);
        if (!loader.load(kGalaxianGfx, gfx_rom, error)) return false;
        decode_graphics(gfx_rom, 0x100, 0x40);

        std::vector<uint8_t> pal(0x20, 0);
        if (!loader.load(kGalaxianPal, pal, error)) return false;
        build_palette(pal);
    }

    warnings_.insert(warnings_.end(), loader.warnings().begin(), loader.warnings().end());
    return true;
}

bool Galaxian::init(const std::string& rom_path, std::string* error) {
    if (!load_roms(rom_path, error)) return false;
    reset();
    return true;
}

void Galaxian::decode_graphics(const std::vector<uint8_t>& gfx_rom, int char_total,
                               int sprite_total) {
    chars_.decode(char_layout(char_total), gfx_rom);
    sprites_gfx_.decode(sprite_layout(sprite_total), gfx_rom);
}

void Galaxian::build_palette(const std::vector<uint8_t>& prom) {
    const std::vector<int> resistances_rg = {1000, 470, 220};
    const std::vector<int> resistances_b = {470, 220};
    auto weights = compute_resistor_weights(
        0, 255, -1.0,
        {{resistances_rg, 470, 0}, {resistances_rg, 470, 0}, {resistances_b, 470, 0}});

    palette_.fill(0xff000000u);
    for (size_t i = 0; i < 32 && i < prom.size(); ++i) {
        const uint8_t d = prom[i];
        const int r = combine_weights(weights[0], {(d >> 0) & 1, (d >> 1) & 1, (d >> 2) & 1});
        const int g = combine_weights(weights[1], {(d >> 3) & 1, (d >> 4) & 1, (d >> 5) & 1});
        const int b = combine_weights(weights[2], {(d >> 6) & 1, (d >> 7) & 1});
        palette_[i] = 0xff000000u | (uint32_t(r) << 16) | (uint32_t(g) << 8) | uint32_t(b);
    }
    palette_[32] = 0xffffffffu;
    palette_[33] = 0xffffff00u;
    // Scramble sky blue (BACK_COLOR index used in Pascal)
    palette_[35] = 0xff0040c0u;
}

void Galaxian::reset() {
    cpu_.reset();
    sound_cpu_.reset();
    ay0_.reset();
    ay1_.reset();
    ppi0_.reset();
    ppi1_.reset();
    nmi_enable_ = false;
    stars_enable_ = false;
    scramble_background_ = false;
    stars_scroll_ = 0;
    if (game_ == Game::Scramble || game_ == Game::Frogger) {
        in0_ = in1_ = in2_ = 0xff;  // active-low
    } else {
        in0_ = in1_ = 0;
        in2_ = 0;
    }
    gfx_bank_.fill(0);
    videoram_.fill(0);
    attributes_.fill(0);
    sprites_.fill(0);
    bullets_.fill(0);
    dirty_.fill(true);
    tilemap_.fill(0xff000000u);
    composite_.fill(0xff000000u);
    sound_latch_ = 0;
    port_b_latch_ = 0;
    sound_cycles_ = 0;
    sound_mute_ = false;
    discrete_.reset();
    pitch_writes_ = 0;
    sound_bit_writes_ = 0;
    scramble_prot_state_ = 0;
    scramble_prot_ = 0;
    audio_accumulator_ = 0;
    audio_.clear();
    cpu_.set_nmi(IrqLine::Clear);
    if (game_ == Game::Scramble) setup_scramble_ppi();
    if (game_ == Game::Frogger) setup_frogger_ppi();
}

void Galaxian::set_inputs(const MachineInputs& inputs) {
    if (game_ == Game::Frogger) {
        // eventos_frogger — active low
        in0_ = in1_ = in2_ = 0xff;
        if (inputs.player2.up) in0_ &= ~0x01;
        if (inputs.player1.right) in0_ &= ~0x10;
        if (inputs.player1.left) in0_ &= ~0x20;
        if (inputs.coin2) in0_ &= ~0x40;
        if (inputs.coin1) in0_ &= ~0x80;

        if (inputs.player2.right) in1_ &= ~0x10;
        if (inputs.player2.left) in1_ &= ~0x20;
        if (inputs.player2.start) in1_ &= ~0x40;
        if (inputs.player1.start) in1_ &= ~0x80;

        if (inputs.player2.down) in2_ &= ~0x01;
        if (inputs.player1.up) in2_ &= ~0x10;
        if (inputs.player1.down) in2_ &= ~0x40;
        return;
    }
    if (game_ == Game::Scramble) {
        // eventos_scramble — active low (clear bit when pressed)
        in0_ = 0xff;
        in1_ = 0xff;
        in2_ = 0xff;
        if (inputs.player2.up) in0_ &= ~0x01;
        if (inputs.player1.button2) in0_ &= ~0x02;
        if (inputs.player1.button1) in0_ &= ~0x08;
        if (inputs.player1.right) in0_ &= ~0x10;
        if (inputs.player1.left) in0_ &= ~0x20;
        if (inputs.coin2) in0_ &= ~0x40;
        if (inputs.coin1) in0_ &= ~0x80;

        if (inputs.player2.button2) in1_ &= ~0x04;
        if (inputs.player2.button1) in1_ &= ~0x08;
        if (inputs.player2.right) in1_ &= ~0x10;
        if (inputs.player2.left) in1_ &= ~0x20;
        if (inputs.player2.start) in1_ &= ~0x40;
        if (inputs.player1.start) in1_ &= ~0x80;

        if (inputs.player2.down) in2_ &= ~0x01;
        if (inputs.player1.up) in2_ &= ~0x10;
        if (inputs.player1.down) in2_ &= ~0x40;
        return;
    }
    in0_ = 0;
    in1_ = 0;
    if (inputs.coin1) in0_ |= 0x01;
    if (inputs.coin2) in0_ |= 0x02;
    if (inputs.player1.left) in0_ |= 0x04;
    if (inputs.player1.right) in0_ |= 0x08;
    if (inputs.player1.button1) in0_ |= 0x10;
    if (inputs.player1.start) in1_ |= 0x01;
    if (inputs.player2.start) in1_ |= 0x02;
}

void Galaxian::set_dip_switch(int bank, uint8_t value) {
    if (bank == 0) dsw_a_ = value;
    else if (bank == 1) dsw_b_ = value;
    else if (bank == 2) dsw_c_ = value;
}

void Galaxian::setup_frogger_ppi() {
    // PPI0: inputs (frogger_port_0_[abc]_read)
    ppi0_.set_port_a([this]() { return in0_; });
    ppi0_.set_port_b([this]() { return uint8_t(in1_ | dsw_a_); });
    ppi0_.set_port_c([this]() { return uint8_t(in2_ | dsw_b_); });

    // PPI1: sound latch + IRQ towards the Konami sound board
    ppi1_.set_port_a({}, [this](uint8_t v) { sound_latch_ = v; });
    ppi1_.set_port_b({}, [this](uint8_t v) {
        if (port_b_latch_ == 0 && v != 0 && sound_present_)
            sound_cpu_.set_irq(IrqLine::Hold);
        port_b_latch_ = v;
    });
    ppi1_.set_port_c({}, {});
}

// ---- Frogger map ----
// $0000-$2fff ROM, $8000-$87ff RAM, $a800-$afff videoram,
// $b000-$b7ff attributes/sprites/bullets, $b800-$bfff latches,
// $c000-$ffff PPI 8255 (A1/A2 select the port, A12/A13 the chip).
uint8_t Galaxian::read_frogger(uint16_t address) {
    if (address <= 0x3fff) return memory_[address];
    if (address >= 0x8000 && address <= 0x87ff) return memory_[address];
    if (address >= 0xa800 && address <= 0xafff) return videoram_[address & 0x3ff];
    if (address >= 0xb000 && address <= 0xb7ff) {
        const uint8_t off = uint8_t(address & 0xff);
        if (off <= 0x3f) return attributes_[off];
        if (off <= 0x5f) return sprites_[off & 0x1f];
        if (off <= 0x7f) return bullets_[off & 0x1f];
        return memory_[0xb000 + off];
    }
    if (address >= 0xc000) {
        uint8_t res = 0xff;
        if (address & 0x1000) res = uint8_t(res & ppi1_.read((address >> 1) & 3));
        if (address & 0x2000) res = uint8_t(res & ppi0_.read((address >> 1) & 3));
        return res;
    }
    return 0xff;
}

void Galaxian::write_frogger(uint16_t address, uint8_t value) {
    if (address <= 0x3fff) return;
    if (address >= 0x8000 && address <= 0x87ff) {
        memory_[address] = value;
        return;
    }
    if (address >= 0xa800 && address <= 0xafff) {
        const int off = address & 0x3ff;
        if (videoram_[size_t(off)] != value) {
            videoram_[size_t(off)] = value;
            dirty_[size_t(off)] = true;
        }
        return;
    }
    if (address >= 0xb000 && address <= 0xb7ff) {
        const uint8_t off = uint8_t(address & 0xff);
        if (off <= 0x3f) {
            if (attributes_[off] != value) {
                attributes_[off] = value;
                const int col = off >> 1;
                for (int row = 0; row < 32; ++row)
                    dirty_[size_t(col + (row << 5))] = true;
            }
            return;
        }
        if (off <= 0x5f) {
            sprites_[off & 0x1f] = value;
            return;
        }
        if (off <= 0x7f) {
            bullets_[off & 0x1f] = value;
            return;
        }
        memory_[0xb000 + off] = value;
        return;
    }
    if (address >= 0xb800 && address <= 0xbfff) {
        if ((address & 0x1f) == 8) {
            nmi_enable_ = (value & 1) != 0;
            if (!nmi_enable_) cpu_.set_nmi(IrqLine::Clear);
        }
        return;
    }
    if (address >= 0xc000) {
        if (address & 0x1000) ppi1_.write((address >> 1) & 3, value);
        if (address & 0x2000) ppi0_.write((address >> 1) & 3, value);
    }
}

// ---- Galaxian map ----
uint8_t Galaxian::read_galaxian(uint16_t address) {
    if (address <= 0x3fff) return memory_[address];
    if (address >= 0x4000 && address <= 0x47ff)
        return memory_[0x4000 + (address & 0x3ff)];
    if (address >= 0x5000 && address <= 0x57ff) return videoram_[address & 0x3ff];
    if (address >= 0x5800 && address <= 0x5fff) {
        const uint8_t off = uint8_t(address & 0xff);
        if (off <= 0x3f) return attributes_[off];
        if (off <= 0x5f) return sprites_[off & 0x1f];
        if (off <= 0x7f) return bullets_[off & 0x1f];
        return memory_[0x5800 + off];
    }
    if (address >= 0x6000 && address <= 0x67ff) return uint8_t(in0_ | dsw_a_);
    if (address >= 0x6800 && address <= 0x6fff) return uint8_t(in1_ | dsw_b_);
    if (address >= 0x7000 && address <= 0x77ff) return dsw_c_;
    return 0xff;
}

void Galaxian::write_galaxian(uint16_t address, uint8_t value) {
    if (address <= 0x3fff) return;
    if (address >= 0x4000 && address <= 0x47ff) {
        memory_[0x4000 + (address & 0x3ff)] = value;
        return;
    }
    if (address >= 0x5000 && address <= 0x57ff) {
        const int off = address & 0x3ff;
        if (videoram_[size_t(off)] != value) {
            videoram_[size_t(off)] = value;
            dirty_[size_t(off)] = true;
        }
        return;
    }
    if (address >= 0x5800 && address <= 0x5fff) {
        const uint8_t off = uint8_t(address & 0xff);
        if (off <= 0x3f) {
            if (attributes_[off] != value) {
                attributes_[off] = value;
                const int col = off >> 1;
                for (int row = 0; row < 32; ++row)
                    dirty_[size_t(col + (row << 5))] = true;
            }
            return;
        }
        if (off <= 0x5f) {
            sprites_[off & 0x1f] = value;
            return;
        }
        if (off <= 0x7f) {
            bullets_[off & 0x1f] = value;
            return;
        }
        memory_[0x5800 + off] = value;
        return;
    }
    if (address >= 0x6000 && address <= 0x67ff) {
        write_discrete(address, value, 0x6004, 0x0000, 0x0000);
        return;
    }
    if (address >= 0x6800 && address <= 0x6fff) {
        write_discrete(address, value, 0x0000, 0x6800, 0x0000);
        return;
    }
    if (address >= 0x7000 && address <= 0x77ff) {
        switch (address & 7) {
            case 1:
                nmi_enable_ = (value & 1) != 0;
                if (!nmi_enable_) cpu_.set_nmi(IrqLine::Clear);
                break;
            case 4:
                stars_enable_ = (value & 1) != 0;
                break;
            default:
                break;
        }
        return;
    }
    if (address >= 0x7800 && address <= 0x7fff) {
        write_discrete(address, value, 0x0000, 0x0000, 0x7800);
    }
}

// ---- Moon Cresta map ----
uint8_t Galaxian::read_mooncrst(uint16_t address) {
    if (address <= 0x3fff) return memory_[address];
    if (address >= 0x8000 && address <= 0x87ff)
        return memory_[0x8000 + (address & 0x3ff)];
    if (address >= 0x9000 && address <= 0x97ff) return videoram_[address & 0x3ff];
    if (address >= 0x9800 && address <= 0x9fff) {
        const uint8_t off = uint8_t(address & 0xff);
        if (off <= 0x3f) return attributes_[off];
        if (off <= 0x5f) return sprites_[off & 0x1f];
        if (off <= 0x7f) return bullets_[off & 0x1f];
        return memory_[0x9800 + off];
    }
    if (address >= 0xa000 && address <= 0xa7ff) return uint8_t(in0_ | dsw_a_);
    if (address >= 0xa800 && address <= 0xafff) return uint8_t(in1_ | dsw_b_);
    if (address >= 0xb000 && address <= 0xb7ff) return dsw_c_;
    return 0xff;
}

void Galaxian::write_mooncrst(uint16_t address, uint8_t value) {
    if (address <= 0x3fff) return;
    if (address >= 0x8000 && address <= 0x87ff) {
        memory_[0x8000 + (address & 0x3ff)] = value;
        return;
    }
    if (address >= 0x9000 && address <= 0x97ff) {
        const int off = address & 0x3ff;
        if (videoram_[size_t(off)] != value) {
            videoram_[size_t(off)] = value;
            dirty_[size_t(off)] = true;
        }
        return;
    }
    if (address >= 0x9800 && address <= 0x9fff) {
        const uint8_t off = uint8_t(address & 0xff);
        if (off <= 0x3f) {
            if (attributes_[off] != value) {
                attributes_[off] = value;
                const int col = off >> 1;
                for (int row = 0; row < 32; ++row)
                    dirty_[size_t(col + (row << 5))] = true;
            }
            return;
        }
        if (off <= 0x5f) {
            sprites_[off & 0x1f] = value;
            return;
        }
        if (off <= 0x7f) {
            bullets_[off & 0x1f] = value;
            return;
        }
        memory_[0x9800 + off] = value;
        return;
    }
    if (address >= 0xa000 && address <= 0xa7ff) {
        const int bank = address & 7;
        if (bank <= 2 && gfx_bank_[size_t(bank)] != value) {
            gfx_bank_[size_t(bank)] = value;
            dirty_.fill(true);
        }
        write_discrete(address, value, 0xa004, 0x0000, 0x0000);
        return;
    }
    if (address >= 0xa800 && address <= 0xafff) {
        write_discrete(address, value, 0x0000, 0xa800, 0x0000);
        return;
    }
    if (address >= 0xb000 && address <= 0xb7ff) {
        switch (address & 7) {
            case 0:
                nmi_enable_ = (value & 1) != 0;
                if (!nmi_enable_) cpu_.set_nmi(IrqLine::Clear);
                break;
            case 4:
                stars_enable_ = (value & 1) != 0;
                break;
            default:
                break;
        }
        return;
    }
    if (address >= 0xb800 && address <= 0xbfff) {
        write_discrete(address, value, 0x0000, 0x0000, 0xb800);
    }
}

// ---- Scramble map ----
uint8_t Galaxian::read_scramble(uint16_t address) {
    if (address <= 0x47ff) return memory_[address];
    if (address >= 0x4800 && address <= 0x4fff) return videoram_[address & 0x3ff];
    if (address >= 0x5000 && address <= 0x5fff) {
        const uint8_t off = uint8_t(address & 0xff);
        if (off <= 0x3f) return attributes_[off];
        if (off <= 0x5f) return sprites_[off & 0x1f];
        if (off <= 0x7f) return bullets_[off & 0x1f];
        return memory_[0x5000 + off];
    }
    if (address >= 0x8000) {
        uint8_t res = 0xff;
        if (address & 0x100) res = uint8_t(res & ppi0_.read(address & 3));
        if (address & 0x200) res = uint8_t(res & ppi1_.read(address & 3));
        return res;
    }
    return 0xff;
}

void Galaxian::write_scramble(uint16_t address, uint8_t value) {
    if (address <= 0x3fff) return;
    if (address >= 0x4000 && address <= 0x47ff) {
        memory_[address] = value;
        return;
    }
    if (address >= 0x4800 && address <= 0x4fff) {
        const int off = address & 0x3ff;
        if (videoram_[size_t(off)] != value) {
            videoram_[size_t(off)] = value;
            dirty_[size_t(off)] = true;
        }
        return;
    }
    if (address >= 0x5000 && address <= 0x5fff) {
        const uint8_t off = uint8_t(address & 0xff);
        if (off <= 0x3f) {
            if (attributes_[off] != value) {
                attributes_[off] = value;
                const int col = off >> 1;
                for (int row = 0; row < 32; ++row)
                    dirty_[size_t(col + (row << 5))] = true;
            }
            return;
        }
        if (off <= 0x5f) {
            sprites_[off & 0x1f] = value;
            return;
        }
        if (off <= 0x7f) {
            bullets_[off & 0x1f] = value;
            return;
        }
        memory_[0x5000 + off] = value;
        return;
    }
    if (address >= 0x6800 && address <= 0x6fff) {
        switch (address & 7) {
            case 1:
                nmi_enable_ = (value & 1) != 0;
                if (!nmi_enable_) cpu_.set_nmi(IrqLine::Clear);
                break;
            case 3:
                scramble_background_ = (value & 1) != 0;
                break;
            case 4:
                stars_enable_ = (value & 1) != 0;
                break;
            default:
                break;
        }
        return;
    }
    if (address >= 0x8000) {
        if (address & 0x100) ppi0_.write(address & 3, value);
        if (address & 0x200) ppi1_.write(address & 3, value);
    }
}

uint8_t Galaxian::read_byte(uint16_t address) {
    switch (game_) {
        case Game::Scramble: return read_scramble(address);
        case Game::Frogger: return read_frogger(address);
        case Game::MoonCresta: return read_mooncrst(address);
        default: return read_galaxian(address);
    }
}

void Galaxian::write_byte(uint16_t address, uint8_t value) {
    switch (game_) {
        case Game::Scramble: write_scramble(address, value); break;
        case Game::Frogger: write_frogger(address, value); break;
        case Game::MoonCresta: write_mooncrst(address, value); break;
        default: write_galaxian(address, value); break;
    }
}

void Galaxian::write_discrete(uint16_t address, uint8_t value, uint16_t lfo_base,
                              uint16_t sound_base, uint16_t pitch_addr) {
    if (lfo_base && address >= lfo_base && address < uint16_t(lfo_base + 4)) {
        discrete_.lfo_freq_w(int(address - lfo_base), value);
        return;
    }
    if (sound_base && (address & 0xf800) == (sound_base & 0xf800)) {
        discrete_.sound_w(int(address & 7), value);
        ++sound_bit_writes_;
        return;
    }
    if (pitch_addr && (address & 0xf800) == (pitch_addr & 0xf800)) {
        discrete_.pitch_w(value);
        ++pitch_writes_;
    }
}

// ---- Konami sound board (MAME konami_sound_map / konami_ay8910_*) ----
// Memory:
//   $0000-$1fff ROM
//   $8000-$83ff RAM (mirrors $6c00)
//   $9000-$9fff filter (stub)
// I/O ports $00-$ff:
//   bit4 AY1 addr, bit5 AY1 data, bit6 AY0 addr, bit7 AY0 data
// AY0 port A = sound latch, port B = konami_sound_timer_r

uint8_t Galaxian::sound_read(uint16_t address) {
    if (game_ == Game::Frogger) {
        const uint16_t a = uint16_t(address & 0x7fff);
        if (a <= 0x1fff) return sound_memory_[a];
        if (a >= 0x4000 && a <= 0x5fff) return sound_memory_[0x4000 + (a & 0x3ff)];
        return 0xff;
    }
    if (address <= 0x1fff) return sound_memory_[address];
    // RAM $8000-$83ff, mirror 0x6c00 (MAME konami_sound_map)
    const uint16_t ram_base = uint16_t(address & ~uint16_t(0x6c00));
    if (ram_base >= 0x8000 && ram_base <= 0x83ff)
        return sound_memory_[0x8000 + (address & 0x3ff)];
    return 0xff;
}

void Galaxian::sound_write(uint16_t address, uint8_t value) {
    if (game_ == Game::Frogger) {
        const uint16_t a = uint16_t(address & 0x7fff);
        if (a >= 0x4000 && a <= 0x5fff) sound_memory_[0x4000 + (a & 0x3ff)] = value;
        // $6000-$7fff: discrete RC filters (no netlist -> nop)
        return;
    }
    const uint16_t ram_base = uint16_t(address & ~uint16_t(0x6c00));
    if (ram_base >= 0x8000 && ram_base <= 0x83ff) {
        sound_memory_[0x8000 + (address & 0x3ff)] = value;
        return;
    }
    // $9000-$9fff mirror 0x6000: discrete RC filter select (no netlist → nop)
    (void)value;
}

uint8_t Galaxian::konami_sound_timer_r() const {
    // Frogger wires the timer bits to port B in a different order (see below).
    // MAME: period = 16*16*2*8*5*2 = 40960 master clocks;
    // sound CPU clock is master/8, so cycles*8 mod 40960.
    constexpr uint32_t kPeriod = 16u * 16u * 2u * 8u * 5u * 2u;  // 40960
    uint32_t cycles = uint32_t((sound_cycles_ * 8u) % kPeriod);
    uint8_t hibit = 0;
    if (cycles >= 16u * 16u * 2u * 8u * 5u) {
        hibit = 1;
        cycles -= 16u * 16u * 2u * 8u * 5u;
    }
    const uint8_t value = uint8_t((hibit << 7) |
                                  (((cycles >> 14) & 1) << 6) |
                                  (((cycles >> 13) & 1) << 5) |
                                  (((cycles >> 11) & 1) << 4) |
                                  0x0e);
    if (game_ == Game::Frogger) {
        // BITSWAP8(timer,7,6,3,4,5,2,1,0)
        return uint8_t((value & 0xc7) | ((value & 0x08) << 3) | (value & 0x10) |
                       ((value & 0x20) >> 3));
    }
    return value;
}

uint8_t Galaxian::sound_in(uint16_t port) {
    if (game_ == Game::Frogger)
        return (port & 0xff) == 0x40 ? ay0_.read() : uint8_t(0xff);
    // konami_ay8910_r — both chips can be selected at once
    const uint8_t offset = uint8_t(port);
    uint8_t result = 0xff;
    if (offset & 0x20) result = uint8_t(result & ay1_.read());
    if (offset & 0x80) result = uint8_t(result & ay0_.read());
    return result;
}

void Galaxian::sound_out(uint16_t port, uint8_t value) {
    if (game_ == Game::Frogger) {
        switch (port & 0xff) {
            case 0x40: ay0_.write(value); break;
            case 0x80: ay0_.control(value); break;
            default: break;
        }
        return;
    }
    // konami_ay8910_w
    const uint8_t offset = uint8_t(port);
    // AY #1 (bits 4/5)
    if (offset & 0x10)
        ay1_.control(value);
    else if (offset & 0x20)
        ay1_.write(value);
    // AY #0 (bits 6/7)
    if (offset & 0x40)
        ay0_.control(value);
    else if (offset & 0x80)
        ay0_.write(value);
}

void Galaxian::run_sound(int main_cycles) {
    if (!sound_present_) return;
    const int sound_cycles =
        int((int64_t(main_cycles) * int64_t(kSoundClock) + (kCpuClock / 2)) / kCpuClock);
    if (sound_cycles > 0) {
        const int ran = sound_cpu_.run(sound_cycles);
        sound_cycles_ += uint64_t(ran > 0 ? ran : sound_cycles);
    }
}

void Galaxian::on_cycles(int cycles) {
    run_sound(cycles);
    audio_accumulator_ += int64_t(cycles) * AY8910::kSampleRate;
    while (audio_accumulator_ >= kCpuClock) {
        audio_accumulator_ -= kCpuClock;
        int32_t sample = 0;
        if (uses_discrete_sound()) {
            sample = discrete_.update();
        } else if (sound_present_ && !sound_mute_) {
            sample = game_ == Game::Frogger ? ay0_.update() : (ay0_.update() + ay1_.update());
        }
        audio_.push_back(int16_t(std::clamp(sample, int32_t(-32768), int32_t(32767))));
    }
}

int Galaxian::calc_nchar(int offset) const {
    int code = videoram_[size_t(offset & 0x3ff)];
    if (game_ == Game::MoonCresta) {
        if (gfx_bank_[2] != 0 && (code & 0xc0) == 0x80) {
            code = (code & 0x3f) | (int(gfx_bank_[0]) << 6) | (int(gfx_bank_[1]) << 7) | 0x100;
        }
        return code & 0x1ff;
    }
    return code & 0xff;
}

void Galaxian::calc_sprite(int index, int& code, bool& flipx, bool& flipy) const {
    const uint8_t attr = sprites_[1 + index * 4];
    flipx = (attr & 0x80) != 0;
    flipy = (attr & 0x40) != 0;
    code = attr & 0x3f;
    if (game_ == Game::MoonCresta) {
        if (gfx_bank_[2] != 0 && (code & 0x30) == 0x20) {
            code = (code & 0x0f) | (int(gfx_bank_[0]) << 4) | (int(gfx_bank_[1]) << 5) | 0x40;
        }
        code &= 0x7f;
    }
}

void Galaxian::draw_tile(int offset) {
    const int tile_x = 31 - (offset / 32);
    const int tile_y = offset % 32;
    const int color = (attributes_[1 + (tile_y << 1)] & 7) << 2;
    const int scroll = (tile_x * 8) + attributes_[tile_y << 1];
    const int code = calc_nchar(offset);
    const uint8_t* pixels = chars_.element(code);
    for (int y = 0; y < 8; ++y) {
        const int sy = tile_y * 8 + y;
        if (sy < 0 || sy >= 256) continue;
        for (int x = 0; x < 8; ++x) {
            const int sx = (scroll + x) & 0xff;
            const uint8_t pix = pixels[y * 8 + x];
            tilemap_[size_t(sy * 256 + sx)] =
                pix == 0 ? 0u : palette_[size_t(pix + color)];
        }
    }
}

void Galaxian::draw_tile_frogger(int offset) {
    const int tile_x = 31 - (offset / 32);
    const int tile_y = offset % 32;
    const uint8_t attr = attributes_[size_t(tile_y * 2)];
    const int color = frogger_color(attributes_[size_t(1 + tile_y * 2)]);
    const int scroll = (tile_x * 8) + ((attr & 0x0f) << 4) + (attr >> 4);
    const uint8_t* pixels = chars_.element(videoram_[size_t(offset & 0x3ff)]);
    for (int y = 0; y < 8; ++y) {
        const int sy = tile_y * 8 + y;
        for (int x = 0; x < 8; ++x) {
            const int sx = (scroll + x) & 0xff;
            const uint8_t pix = pixels[y * 8 + x];
            if (pix == 0) continue;
            tilemap_[size_t(sy * 256 + sx)] = palette_[size_t(pix + color)];
        }
    }
}

void Galaxian::draw_sprite_frogger(int index) {
    const uint8_t* e = &sprites_[index * 4];
    const int y = int(e[3]) + 1;
    if (y < 16) return;
    const uint8_t attr = e[1];
    const int code = attr & 0x3f;
    const bool flipx = (attr & 0x80) != 0;
    const bool flipy = (attr & 0x40) != 0;
    const int color = frogger_color(e[2]);
    const int x = frogger_x(e[0]);
    const uint8_t* pixels = sprites_gfx_.element(code);
    for (int py = 0; py < 16; ++py) {
        const int sy = y + py;
        if (sy < 0 || sy >= 256) continue;
        const int src_y = flipy ? (15 - py) : py;
        for (int px = 0; px < 16; ++px) {
            const int src_x = flipx ? (15 - px) : px;
            const uint8_t pix = pixels[src_y * 16 + src_x];
            if (pix == 0) continue;
            composite_[size_t(sy * 256 + ((x + px) & 0xff))] = palette_[size_t(pix + color)];
        }
    }
}

void Galaxian::draw_sprite(int index) {
    const uint8_t* e = &sprites_[index * 4];
    const int y = int(e[3]) + 1;
    if (y < 16) return;
    int code = 0;
    bool flipx = false, flipy = false;
    calc_sprite(index, code, flipx, flipy);
    const int color = (e[2] & 7) << 2;
    const int x = e[0];
    const uint8_t* pixels = sprites_gfx_.element(code);
    for (int py = 0; py < 16; ++py) {
        const int sy = y + py;
        if (sy < 0 || sy >= 256) continue;
        const int src_y = flipy ? (15 - py) : py;
        for (int px = 0; px < 16; ++px) {
            const int src_x = flipx ? (15 - px) : px;
            const uint8_t pix = pixels[src_y * 16 + src_x];
            if (pix == 0) continue;
            const int sx = (x + px) & 0xff;
            composite_[size_t(sy * 256 + sx)] = palette_[size_t(pix + color)];
        }
    }
}

void Galaxian::draw_bullets() {
    for (int f = 0; f < 8; ++f) {
        int y = (game_ == Game::Scramble ? 249 : 250) - int(bullets_[3 + f * 4]);
        if (f > 2) y += 1;
        const uint32_t color = palette_[game_ == Game::Scramble ? 32 : (f == 7 ? 33 : 32)];
        const int x = bullets_[1 + f * 4];
        const int h = game_ == Game::Scramble ? 2 : 4;
        for (int dy = 0; dy < h; ++dy) {
            const int sy = y + dy;
            if (sy < 0 || sy >= 256) continue;
            composite_[size_t(sy * 256 + (x & 0xff))] = color;
        }
    }
}

void Galaxian::draw_stars() {
    if (!stars_enable_) return;
    uint32_t rng = 0x12345678u ^ stars_scroll_;
    for (int n = 0; n < 64; ++n) {
        rng = rng * 1664525u + 1013904223u;
        const int x = int((rng >> 8) + stars_scroll_) & 0xff;
        const int y = int(rng >> 16) & 0xff;
        const uint32_t cur = composite_[size_t(y * 256 + x)];
        if (cur != 0 && (cur & 0x00ffffffu) != 0) continue;
        const int bright = (rng >> 24) & 3;
        const uint8_t c = uint8_t(0x40 + bright * 0x3f);
        composite_[size_t(y * 256 + x)] =
            0xff000000u | (uint32_t(c) << 16) | (uint32_t(c) << 8) | uint32_t(c);
    }
}

void Galaxian::update_video() {
    if (game_ == Game::Frogger) {
        // Half of the (rotated) screen is the blue river background.
        for (int y = 0; y < 128; ++y)
            std::fill(tilemap_.begin() + y * 256, tilemap_.begin() + y * 256 + 256,
                      palette_[35]);
        std::fill(tilemap_.begin() + 128 * 256, tilemap_.end(), palette_[0]);

        for (int offset = 0; offset < 0x400; ++offset) draw_tile_frogger(offset);
        composite_ = tilemap_;
        for (int i = 7; i >= 0; --i) draw_sprite_frogger(i);

        for (int y = 0; y < kScreenHeight; ++y) {
            const uint32_t* src = &composite_[size_t(y * 256 + 16)];
            std::copy(src, src + kScreenWidth, &framebuffer_[size_t(y * kScreenWidth)]);
        }
        return;
    }
    // Background
    if (game_ == Game::Scramble && scramble_background_) {
        tilemap_.fill(palette_[35]);
    } else {
        tilemap_.fill(0xff000000u);
    }

    for (int offset = 0; offset < 0x400; ++offset) draw_tile(offset);

    composite_ = tilemap_;
    draw_stars();
    draw_bullets();
    for (int i = 7; i >= 0; --i) draw_sprite(i);

    for (int y = 0; y < kScreenHeight; ++y) {
        const uint32_t* src = &composite_[size_t(y * 256 + 16)];
        uint32_t* dst = &framebuffer_[size_t(y * kScreenWidth)];
        std::copy(src, src + kScreenWidth, dst);
    }
    ++stars_scroll_;
}

void Galaxian::run_frame() {
    const int cycles_per_line =
        int(double(kCpuClock) / (kFramesPerSecond * kScanlines) + 0.5);
    for (int line = 0; line < kScanlines; ++line) {
        if (line == 248 && nmi_enable_) cpu_.set_nmi(IrqLine::Assert);
        cpu_.run(cycles_per_line);
        if (line == 248) cpu_.set_nmi(IrqLine::Clear);
    }
    update_video();
    if (uses_discrete_sound()) {
        const int target = AY8910::kSampleRate / 60;
        while (int(audio_.size()) < target) audio_.push_back(discrete_.update());
    }
}

void Galaxian::drain_audio(std::vector<int16_t>& out) {
    out.insert(out.end(), audio_.begin(), audio_.end());
    audio_.clear();
}

}  // namespace dsp
