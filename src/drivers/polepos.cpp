#include "drivers/polepos.h"

#include <algorithm>
#include <cstring>

#include "core/rom_loader.h"

namespace dsp {
namespace {

uint16_t be16(const uint8_t* p) { return uint16_t((uint16_t(p[0]) << 8) | p[1]); }

bool load_interleaved(RomLoader& loader, const std::vector<RomEntry>& even,
                      const std::vector<RomEntry>& odd, std::vector<uint8_t>& dest,
                      std::string* error) {
    std::vector<uint8_t> even_buf;
    std::vector<uint8_t> odd_buf;
    if (!loader.load(even, even_buf, error)) return false;
    if (!loader.load(odd, odd_buf, error)) return false;
    const size_t words = std::min(even_buf.size(), odd_buf.size());
    dest.assign(words * 2, 0);
    for (size_t i = 0; i < words; i++) {
        dest[i * 2] = even_buf[i];
        dest[i * 2 + 1] = odd_buf[i];
    }
    return true;
}

const std::vector<RomEntry> kPpZ80 = {
    {"pp3_9.6h", 0x2000, 0x0000, 0xc0511173},
    {"pp1_10b.5h", 0x1000, 0x2000, 0x7174bcb7},
};
const std::vector<RomEntry> kPpSub1Even = {{"pp3_2.8l", 0x2000, 0, 0xfafb9049}};
const std::vector<RomEntry> kPpSub1Odd = {{"pp3_1.8m", 0x2000, 0, 0x65c1c2c2}};
const std::vector<RomEntry> kPpSub2Even = {{"pp3_6.4l", 0x2000, 0, 0xacc1ebc3}};
const std::vector<RomEntry> kPpSub2Odd = {{"pp3_5.4m", 0x2000, 0, 0x46e5c99a}};
const std::vector<RomEntry> kPpChars = {{"pp3_28.1f", 0x1000, 0, 0x2e77187e}};
const std::vector<RomEntry> kPpTiles = {{"pp1_29.1e", 0x1000, 0, 0x706e888a}};
const std::vector<RomEntry> kPpSprites = {
    {"pp3_25.1n", 0x2000, 0x0000, 0xb52c086b},
    {"pp3_26.1m", 0x2000, 0x2000, 0xd24a5707},
};
const std::vector<RomEntry> kPpBigSprites = {
    {"pp1_17.5n", 0x2000, 0x0000, 0x2e134b46},
    {"pp1_19.4n", 0x2000, 0x2000, 0x43ff83e1},
    {"pp1_21.3n", 0x2000, 0x4000, 0x5f958eb4},
    {"pp1_18.5m", 0x2000, 0x8000, 0x6f9997d2},
    {"pp1_20.4m", 0x2000, 0xa000, 0xec18075b},
    {"pp1_22.3m", 0x2000, 0xc000, 0x1d2f30b1},
};
const std::vector<RomEntry> kPpRoad = {
    {"pp1_30.3a", 0x2000, 0x0000, 0xee6b3315},
    {"pp1_31.2a", 0x2000, 0x2000, 0x6d1e7042},
    {"pp1_32.1a", 0x1000, 0x4000, 0x4e97f101},
};
const std::vector<RomEntry> kPpScale = {{"pp1_27.1l", 0x1000, 0, 0xa61bff15}};
const std::vector<RomEntry> kPpProms = {
    {"pp1-7.8l", 0x0100, 0x0000, 0xf07ff2ad},
    {"pp1-8.9l", 0x0100, 0x0100, 0xadbde7d7},
    {"pp1-9.10l", 0x0100, 0x0200, 0xddac786a},
    {"pp2-10.2h|pp1-10.2h", 0x0100, 0x0300, 0x1e8d0491},
    {"pp1-11.4d", 0x0100, 0x0400, 0x0e4fe8a0},
    {"pp1-15.9a", 0x0100, 0x0500, 0x2d502464},
    {"pp1-16.10a", 0x0100, 0x0600, 0x027aa62c},
    {"pp1-17.11a", 0x0100, 0x0700, 0x1f8d0df3},
    {"pp1-12.3c", 0x0400, 0x0800, 0x7afc7cfc},
    {"pp3-6.6m", 0x0400, 0x0c00, 0x63fb6057},
};
const std::vector<RomEntry> kPpNamco = {{"pp1-5.3b", 0x0100, 0, 0x8568decc}};

const std::vector<RomEntry> kPp2Z80 = {
    {"pp4_9.6h", 0x2000, 0x0000, 0xbcf87004},
    {"pp4_10.5h", 0x1000, 0x2000, 0xa9d4c380},
};
const std::vector<RomEntry> kPp2Sub1Even = {
    {"pp4_2.8l", 0x2000, 0x0000, 0x51b9a669},
    {"pp4_8.3l", 0x1000, 0x2000, 0xef25a2ee},
};
const std::vector<RomEntry> kPp2Sub1Odd = {
    {"pp4_1.8m", 0x2000, 0x0000, 0x3f6ac294},
    {"pp4_7.3m", 0x1000, 0x2000, 0xad1c8994},
};
const std::vector<RomEntry> kPp2Sub2Even = {{"pp4_6.4l", 0x2000, 0, 0x38d04e0f}};
const std::vector<RomEntry> kPp2Sub2Odd = {{"pp4_5.4m", 0x2000, 0, 0xc3053cae}};
const std::vector<RomEntry> kPp2Chars = {{"pp4_28.1f", 0x2000, 0, 0x280dde7d}};
const std::vector<RomEntry> kPp2Tiles = {{"pp4_29.1e", 0x2000, 0, 0xec3ec6e6}};
const std::vector<RomEntry> kPp2Sprites = {
    {"pp4_25.1n", 0x2000, 0x0000, 0xfd098e65},
    {"pp4_26.1m", 0x2000, 0x2000, 0x35ac62b3},
};
const std::vector<RomEntry> kPp2BigSprites = {
    {"pp1_17.5n", 0x2000, 0x0000, 0x2e134b46},
    {"pp1_19.4n", 0x2000, 0x2000, 0x43ff83e1},
    {"pp1_21.3n", 0x2000, 0x4000, 0x5f958eb4},
    {"pp4_23.2n", 0x2000, 0x6000, 0x9e056fcd},
    {"pp1_18.5m", 0x2000, 0x8000, 0x6f9997d2},
    {"pp1_20.4m", 0x2000, 0xa000, 0xec18075b},
    {"pp1_22.3m", 0x2000, 0xc000, 0x1d2f30b1},
    {"pp4_24.2m", 0x2000, 0xe000, 0x795268cf},
};
const std::vector<RomEntry> kPp2Proms = {
    {"pp4-7.8l", 0x0100, 0x0000, 0x16d69c31},
    {"pp4-8.9l", 0x0100, 0x0100, 0x07340311},
    {"pp4-9.10l", 0x0100, 0x0200, 0x1efc84d7},
    {"pp4-10.2h", 0x0100, 0x0300, 0x064d51a0},
    {"pp4-11.4d", 0x0100, 0x0400, 0x7880c5af},
    {"pp1-15.9a", 0x0100, 0x0500, 0x2d502464},
    {"pp1-16.10a", 0x0100, 0x0600, 0x027aa62c},
    {"pp1-17.11a", 0x0100, 0x0700, 0x1f8d0df3},
    {"pp4-12.3c", 0x0400, 0x0800, 0x8b270902},
    {"pp4-6.6m", 0x0400, 0x0c00, 0x647212b5},
};

GfxLayout char_layout(int total) {
    GfxLayout layout;
    layout.width = 8;
    layout.height = 8;
    layout.total = total;
    layout.planes = 2;
    layout.char_increment = 8 * 8 * 2;
    layout.plane_offsets = {0, 4};
    layout.x_offsets = {0, 1, 2, 3, 8 * 8 + 0, 8 * 8 + 1, 8 * 8 + 2, 8 * 8 + 3};
    layout.y_offsets = {0 * 8, 1 * 8, 2 * 8, 3 * 8, 4 * 8, 5 * 8, 6 * 8, 7 * 8};
    return layout;
}

GfxLayout small_sprite_layout() {
    GfxLayout layout;
    layout.width = 16;
    layout.height = 16;
    layout.total = 128;
    layout.planes = 4;
    layout.char_increment = 16 * 32;
    layout.plane_offsets = {0, 4, 0x2000 * 8, 0x2000 * 8 + 4};
    layout.x_offsets = {0, 1, 2, 3, 8, 9, 10, 11, 16, 17, 18, 19, 24, 25, 26, 27};
    layout.y_offsets = {0 * 32,  1 * 32,  2 * 32,  3 * 32,  4 * 32,  5 * 32,  6 * 32,  7 * 32,
                        8 * 32,  9 * 32,  10 * 32, 11 * 32, 12 * 32, 13 * 32, 14 * 32, 15 * 32};
    return layout;
}

GfxLayout big_sprite_layout(int region_bytes) {
    GfxLayout layout;
    layout.width = 32;
    layout.height = 32;
    const int half = region_bytes / 2;
    layout.total = half / 256;
    layout.planes = 4;
    layout.char_increment = 32 * 64;
    layout.plane_offsets = {0, 4, half * 8, half * 8 + 4};
    layout.x_offsets.resize(32);
    for (int i = 0; i < 32; i++) layout.x_offsets[size_t(i)] = (i / 4) * 8 + (i % 4);
    layout.y_offsets.resize(32);
    for (int i = 0; i < 32; i++) layout.y_offsets[size_t(i)] = i * 64;
    return layout;
}

}  // namespace

PolePos::PolePos(Game game)
    : game_(game), z80_(kCpuClock), sub1_(kCpuClock), sub2_(kCpuClock) {
    framebuffer_.assign(size_t(kScreenWidth) * kScreenHeight, 0xff000000u);
    z80_.set_memory_handlers([this](uint16_t a) { return z80_read(a); },
                             [this](uint16_t a, uint8_t v) { z80_write(a, v); });
    z80_.set_io_handlers([this](uint16_t p) { return z80_in(p); },
                         [this](uint16_t p, uint8_t v) { z80_out(p, v); });
    z80_.set_cycle_handler([this](int cycles) { on_z80_cycles(cycles); });
    sub1_.set_memory_handlers([this](uint16_t a) { return z8002_read(0, a); },
                              [this](uint16_t a, uint8_t v) { z8002_write(0, a, v); });
    sub2_.set_memory_handlers([this](uint16_t a) { return z8002_read(1, a); },
                              [this](uint16_t a, uint8_t v) { z8002_write(1, a, v); });
}

bool PolePos::init(const std::string& rom_path, std::string* error) {
    RomLoader loader;
    if (!loader.open(rom_path, error)) return false;

    const bool pp2 = game_ == Game::PolePosition2;
    if (!loader.load(pp2 ? kPp2Z80 : kPpZ80, z80_rom_, error)) return false;
    if (!load_interleaved(loader, pp2 ? kPp2Sub1Even : kPpSub1Even, pp2 ? kPp2Sub1Odd : kPpSub1Odd,
                          sub1_rom_, error)) {
        return false;
    }
    if (!load_interleaved(loader, pp2 ? kPp2Sub2Even : kPpSub2Even, pp2 ? kPp2Sub2Odd : kPpSub2Odd,
                          sub2_rom_, error)) {
        return false;
    }
    if (sub1_rom_.size() < 0x8000) sub1_rom_.resize(0x8000, 0);
    if (sub2_rom_.size() < 0x8000) sub2_rom_.resize(0x8000, 0);

    if (!loader.load(pp2 ? kPp2Chars : kPpChars, char_rom_, error)) return false;
    if (!loader.load(pp2 ? kPp2Tiles : kPpTiles, tile_rom_, error)) return false;
    if (!loader.load(pp2 ? kPp2Sprites : kPpSprites, sprite_rom_, error)) return false;
    if (!loader.load(pp2 ? kPp2BigSprites : kPpBigSprites, bigsprite_rom_, error)) return false;
    if (!loader.load(kPpRoad, road_rom_, error)) return false;
    if (!loader.load(kPpScale, scalelut_, error)) return false;
    if (!loader.load(pp2 ? kPp2Proms : kPpProms, proms_, error)) return false;
    if (!loader.load(kPpNamco, namco_wavetable_, error)) return false;

    warnings_ = loader.warnings();
    decode_graphics();
    build_palette();
    reset();
    return true;
}

void PolePos::decode_graphics() {
    chars_.decode(char_layout(int(char_rom_.size() / 16)), char_rom_);
    tiles_.decode(char_layout(int(tile_rom_.size() / 16)), tile_rom_);
    sprites_.decode(small_sprite_layout(), sprite_rom_);
    if (bigsprite_rom_.size() < 0x10000) bigsprite_rom_.resize(0x10000, 0);
    bigsprites_.decode(big_sprite_layout(int(bigsprite_rom_.size())), bigsprite_rom_);
}

void PolePos::build_palette() {
    rgb_.fill(0xff000000u);
    pens_.fill(0);
    vpos_mod_.fill(0);
    if (proms_.size() < 0x1000) return;
    for (int i = 0; i < 128; i++) {
        auto chan = [&](int bank) {
            const uint8_t v = proms_[size_t(bank + i)];
            const int b0 = v & 1, b1 = (v >> 1) & 1, b2 = (v >> 2) & 1, b3 = (v >> 3) & 1;
            return 0x0e * b0 + 0x1f * b1 + 0x43 * b2 + 0x8f * b3;
        };
        rgb_[size_t(i)] = 0xff000000u | (uint32_t(chan(0)) << 16) | (uint32_t(chan(0x100)) << 8) |
                          uint32_t(chan(0x200));
    }
    for (int i = 0; i < 64 * 4; i++) {
        const int color = proms_[0x300 + size_t(i)] & 0x0f;
        pens_[size_t(0x000 + i)] = uint16_t((color != 15) ? (0x20 + color) : 0x2f);
        pens_[size_t(0x100 + i)] = uint16_t((color != 15) ? (0x60 + color) : 0x2f);
    }
    for (int i = 0; i < 64 * 4; i++) {
        pens_[size_t(0x200 + i)] = uint16_t(proms_[0x400 + size_t(i)] & 0x0f);
    }
    for (int i = 0; i < 64 * 16; i++) {
        const int color = proms_[0xc00 + size_t(i)] & 0x0f;
        pens_[size_t(0x300 + i)] = uint16_t((color != 15) ? (0x10 + color) : 0x1f);
        pens_[size_t(0x700 + i)] = uint16_t((color != 15) ? (0x50 + color) : 0x1f);
    }
    for (int i = 0; i < 64 * 16; i++) {
        pens_[size_t(0xb00 + i)] = uint16_t(0x40 + (proms_[0x800 + size_t(i)] & 0x0f));
    }
    for (int i = 0; i < 256; i++) {
        vpos_mod_[size_t(i)] = uint16_t(proms_[0x500 + size_t(i)] + (proms_[0x600 + size_t(i)] << 4) +
                                        (proms_[0x700 + size_t(i)] << 8));
    }
}

void PolePos::reset() {
    z80_.reset();
    sub1_.reset();
    sub2_.reset();
    nvram_.fill(0xff);
    sound_ram_.fill(0);
    sprite_ram_.fill(0);
    road_ram_.fill(0);
    alpha_ram_.fill(0);
    view_ram_.fill(0);
    view_hscroll_ = 0;
    road_vscroll_ = 0;
    ls259_ = 0;
    chacl_ = 0;
    sub_irq_mask_ = 0;
    sub1_reset_ = true;
    sub2_reset_ = true;
    irq_enable_ = false;
    adc_channel_ = false;
    adc_ready_ = true;
    adc_value_ = 0;
    scanline_ = 0;
    n06_control_ = 0;
    n06_timer_state_ = false;
    n06_read_stretch_ = false;
    n06_cycle_acc_ = 0;
    n06_period_cycles_ = 0;
    n51_mode_ = 1;
    n51_out_index_ = 0;
    n51_coinage_left_ = 0;
    n51_out_.fill(0x0f);
    n51_coinage_ = {1, 1, 1, 1};
    n51_cred_lo_ = 0;
    n51_cred_hi_ = 0;
    n51_coin1_partial_ = 0;
    n51_coin2_partial_ = 0;
    n51_in0_prev_ = 0xff;
    namco51_vblank();
    steer_last_ = steer_;
    steer_delta_ = 0;
    steer_accum_ = 0;
    ic25_result_ = 0;
    ic25_signed_ = 0;
    ic25_unsigned_ = 0;
    audio_accumulator_ = 0;
    audio_.clear();
    sub1_.set_reset_line(IrqLine::Assert);
    sub2_.set_reset_line(IrqLine::Assert);
}

uint8_t PolePos::in0() const {
    uint8_t value = 0xff;
    if (gear_hi_) value &= uint8_t(~0x02);
    // Bit 2 is auto-start (active low). Q6 high → start line active.
    if (ls259_ & 0x40) value &= uint8_t(~0x04);
    if ((in0_ & 0x10) == 0) value &= uint8_t(~0x10);
    if ((in0_ & 0x20) == 0) value &= uint8_t(~0x20);
    return value;
}

uint8_t PolePos::ready_r() const {
    uint8_t ret = 0xff;
    if (scanline_ >= 128) ret ^= 0x02;
    if (adc_ready_) ret ^= 0x08;
    return ret;
}

uint8_t PolePos::analog_r() const { return adc_channel_ ? accel_ : brake_; }

void PolePos::ls259_w(int bit, bool value) {
    const uint8_t mask = uint8_t(1u << bit);
    if (value)
        ls259_ |= mask;
    else
        ls259_ &= uint8_t(~mask);
    switch (bit) {
        case 0:
            irq_enable_ = value;
            if (value) z80_.set_irq(IrqLine::Clear);
            break;
        case 3:
            adc_channel_ = value;
            break;
        case 4:
            sub1_reset_ = !value;
            sub1_.set_reset_line(sub1_reset_ ? IrqLine::Assert : IrqLine::Clear);
            break;
        case 5:
            sub2_reset_ = !value;
            sub2_.set_reset_line(sub2_reset_ ? IrqLine::Assert : IrqLine::Clear);
            break;
        case 7:
            chacl_ = value ? 1 : 0;
            break;
        default:
            break;
    }
}

uint16_t PolePos::ic25_r(uint16_t offset) {
    offset = uint16_t(offset & 0x1ff);
    int result;
    if (offset < 0x100) {
        ic25_signed_ = int8_t(offset & 0xff);
        result = ic25_result_ & 0xff;
    } else {
        ic25_unsigned_ = uint8_t(offset & 0xff);
        result = (ic25_result_ >> 8) & 0xff;
        ic25_result_ = int16_t(int8_t(ic25_signed_) * int16_t(ic25_unsigned_));
    }
    return uint16_t(result | (result << 8));
}

void PolePos::namco06_ctrl_w(uint8_t data) {
    n06_control_ = data;
    if ((n06_control_ & 0xe0) == 0) {
        n06_period_cycles_ = 0;
        n06_timer_state_ = false;
        z80_.set_nmi(IrqLine::Clear);
    } else {
        const int divisor = 1 << ((n06_control_ >> 5) & 7);
        // One NMI per divided 06xx clock (64 Z80 cycles * divisor).
        n06_period_cycles_ = 64 * divisor;
        n06_cycle_acc_ = 0;
        if (n06_control_ & 0x10) {
            z80_.set_nmi(IrqLine::Clear);
            n06_read_stretch_ = true;
            n51_out_index_ = 0;
        } else {
            n06_read_stretch_ = false;
        }
    }
}

void PolePos::namco06_tick() {
    n06_timer_state_ = !n06_timer_state_;
    if (n06_timer_state_ && !n06_read_stretch_) {
        z80_.set_nmi(IrqLine::Pulse);
    } else {
        z80_.set_nmi(IrqLine::Clear);
    }
    n06_read_stretch_ = false;
}

uint8_t PolePos::namco51_read() {
    const uint8_t lo = n51_out_[size_t(n51_out_index_ & 7)];
    const uint8_t hi = n51_out_[size_t((n51_out_index_ + 1) & 7)];
    n51_out_index_ = (n51_out_index_ + 2) & 7;
    return uint8_t((hi << 4) | (lo & 0x0f));
}

void PolePos::namco51_write(uint8_t data) {
    data &= 0x0f;
    if (n51_coinage_left_ > 0) {
        n51_coinage_[size_t(n51_coinage_left_ - 1)] = data == 0 ? uint8_t(1) : data;
        n51_coinage_left_--;
        return;
    }
    switch (data) {
        case 1:
            n51_coinage_left_ = 4;
            break;
        case 2:
            n51_mode_ = 0;
            n51_out_index_ = 0;
            break;
        case 5:
            n51_mode_ = 1;
            n51_out_index_ = 0;
            break;
        default:
            break;
    }
}

void PolePos::namco51_vblank() {
    const uint8_t in = in0();
    n51_out_.fill(0x0f);
    if (n51_mode_ == 1) {
        n51_out_[0] = uint8_t(in & 0x0f);
        n51_out_[1] = uint8_t(in >> 4);
        n51_out_[2] = uint8_t(dswb_ & 0x0f);
        n51_out_[3] = uint8_t(dswb_ >> 4);
    } else {
        const uint8_t coins = uint8_t((~in) & (~n51_in0_prev_) & 0x30);
        if (coins & 0x10) {
            n51_coin1_partial_++;
            if (n51_coin1_partial_ >= n51_coinage_[3]) {
                n51_coin1_partial_ = 0;
                n51_cred_lo_ = uint8_t(n51_cred_lo_ + n51_coinage_[2]);
                while (n51_cred_lo_ > 9) {
                    n51_cred_lo_ = uint8_t(n51_cred_lo_ - 10);
                    if (n51_cred_hi_ < 9) n51_cred_hi_++;
                }
            }
        }
        if (coins & 0x20) {
            n51_coin2_partial_++;
            if (n51_coin2_partial_ >= n51_coinage_[1]) {
                n51_coin2_partial_ = 0;
                n51_cred_lo_ = uint8_t(n51_cred_lo_ + n51_coinage_[0]);
                while (n51_cred_lo_ > 9) {
                    n51_cred_lo_ = uint8_t(n51_cred_lo_ - 10);
                    if (n51_cred_hi_ < 9) n51_cred_hi_++;
                }
            }
        }
        n51_out_[0] = n51_cred_lo_;
        n51_out_[1] = n51_cred_hi_;
        if (n51_mode_ == 2) {
            n51_out_[2] = uint8_t(in & 0x0f);
            n51_out_[3] = 0;
            n51_out_[4] = uint8_t(in >> 4);
            n51_out_[5] = gear_hi_ ? 1 : 0;
        }
        const bool start = ((~in) & n51_in0_prev_ & 0x04) != 0;
        if (n51_mode_ == 0 && start && (n51_cred_lo_ || n51_cred_hi_)) {
            if (n51_cred_lo_ == 0) {
                n51_cred_hi_--;
                n51_cred_lo_ = 9;
            } else {
                n51_cred_lo_--;
            }
            n51_mode_ = 2;
            n51_out_[0] = n51_cred_lo_;
            n51_out_[1] = n51_cred_hi_;
        }
    }
    n51_in0_prev_ = in;
}

uint8_t PolePos::namco53_read() {
    const uint8_t steer_new = steer_;
    steer_accum_ = int16_t(steer_accum_ + int8_t(steer_new - steer_last_) * 2);
    steer_last_ = steer_new;
    if (steer_accum_ < 0) {
        steer_delta_ = 0;
        steer_accum_++;
    } else if (steer_accum_ > 0) {
        steer_delta_ = 1;
        steer_accum_--;
    }
    return uint8_t((steer_accum_ & 1) | (steer_delta_ << 1) | (dswa_ & 0xfc));
}

uint8_t PolePos::namco06_data_r() {
    if (!(n06_control_ & 0x10)) return 0;
    uint8_t result = 0xff;
    if (n06_control_ & 0x01) result &= namco51_read();
    if (n06_control_ & 0x02) result &= namco53_read();
    return result;
}

void PolePos::namco06_data_w(uint8_t data) {
    if (n06_control_ & 0x10) return;
    if (n06_control_ & 0x01) namco51_write(data);
}

void PolePos::on_z80_cycles(int cycles) {
    if (n06_period_cycles_ > 0) {
        n06_cycle_acc_ += cycles;
        while (n06_cycle_acc_ >= n06_period_cycles_) {
            n06_cycle_acc_ -= n06_period_cycles_;
            namco06_tick();
        }
    }
    audio_accumulator_ += int64_t(cycles) * kSampleRate;
    while (audio_accumulator_ >= int64_t(kCpuClock)) {
        audio_accumulator_ -= int64_t(kCpuClock);
        audio_.push_back(0);
    }
}

uint8_t PolePos::z80_read(uint16_t address) {
    if (address < 0x3000) {
        if (address < z80_rom_.size()) return z80_rom_[address];
        return 0xff;
    }
    if (address < 0x4000) return nvram_[address & 0x7ff];
    if (address < 0x4800) return sprite_ram_[(address - 0x4000) * 2 + 1];
    if (address < 0x4c00) return road_ram_[(address - 0x4800) * 2 + 1];
    if (address < 0x5000) return alpha_ram_[(address - 0x4c00) * 2 + 1];
    if (address < 0x5800) return view_ram_[(address - 0x5000) * 2 + 1];
    if (address >= 0x8000 && address < 0x9000) return sound_ram_[address & 0x3ff];
    if ((address & 0xf000) == 0x9000) {
        return (address & 0x0100) ? namco06_ctrl_r() : namco06_data_r();
    }
    if ((address & 0xf000) == 0xa000) {
        if ((address & 0x0300) == 0x0000) return ready_r();
        return 0xff;
    }
    return 0xff;
}

void PolePos::z80_write(uint16_t address, uint8_t value) {
    if (address >= 0x3000 && address < 0x4000) {
        nvram_[address & 0x7ff] = value;
        return;
    }
    auto poke_low = [](uint8_t* ram, uint16_t offset, uint8_t v) {
        ram[offset * 2 + 1] = v;
    };
    if (address >= 0x4000 && address < 0x4800) {
        poke_low(sprite_ram_.data(), uint16_t(address - 0x4000), value);
        return;
    }
    if (address >= 0x4800 && address < 0x4c00) {
        poke_low(road_ram_.data(), uint16_t(address - 0x4800), value);
        return;
    }
    if (address >= 0x4c00 && address < 0x5000) {
        poke_low(alpha_ram_.data(), uint16_t(address - 0x4c00), value);
        return;
    }
    if (address >= 0x5000 && address < 0x5800) {
        poke_low(view_ram_.data(), uint16_t(address - 0x5000), value);
        return;
    }
    if (address >= 0x8000 && address < 0x9000) {
        sound_ram_[address & 0x3ff] = value;
        return;
    }
    if ((address & 0xf000) == 0x9000) {
        if ((address & 0x0100) == 0)
            namco06_data_w(value);
        else
            namco06_ctrl_w(value);
        return;
    }
    if ((address & 0xf000) == 0xa000) {
        const int sel = address & 7;
        if ((address & 0x0300) == 0x0000) ls259_w(sel, (value & 1) != 0);
        return;
    }
}

uint8_t PolePos::z80_in(uint16_t port) {
    if ((port & 0xff) == 0x00) {
        adc_ready_ = true;
        return analog_r();
    }
    return 0xff;
}

void PolePos::z80_out(uint16_t port, uint8_t /*value*/) {
    if ((port & 0xff) == 0x00) {
        adc_value_ = analog_r();
        adc_ready_ = true;
    }
}

uint8_t PolePos::z8002_read(int which, uint16_t address) {
    if (address < 0x8000) {
        if (game_ == Game::PolePosition2 && which == 0 && address >= 0x4000 && address < 0x6000) {
            if ((address & 1) == 0) ic25_word_ = ic25_r(uint16_t((address - 0x4000) >> 1));
            return uint8_t(ic25_word_ >> ((address & 1) ? 0 : 8));
        }
        const auto& rom = (which == 0) ? sub1_rom_ : sub2_rom_;
        if (address < rom.size()) return rom[address];
        return 0xff;
    }
    if (address < 0x9000) return sprite_ram_[address & 0xfff];
    if (address < 0x9800) return road_ram_[address & 0x7ff];
    if (address < 0xa000) return alpha_ram_[address & 0x7ff];
    if (address < 0xb000) return view_ram_[address & 0xfff];
    return 0xff;
}

void PolePos::z8002_write(int which, uint16_t address, uint8_t value) {
    if (address < 0x8000) {
        // $6000 NVI enable, not shared.
        if ((address & 0xe000) == 0x6000) {
            // 16-bit BE write of 0x0001: low byte (odd address) holds the enable bit.
            if (address & 1) {
                sub_irq_mask_ = value & 1;
                if (!sub_irq_mask_) {
                    if (which == 0)
                        sub1_.set_nvi(IrqLine::Clear);
                    else
                        sub2_.set_nvi(IrqLine::Clear);
                }
            }
            return;
        }
        return;
    }
    if (address < 0x9000) {
        sprite_ram_[address & 0xfff] = value;
        return;
    }
    if (address < 0x9800) {
        road_ram_[address & 0x7ff] = value;
        return;
    }
    if (address < 0xa000) {
        alpha_ram_[address & 0x7ff] = value;
        return;
    }
    if (address < 0xb000) {
        view_ram_[address & 0xfff] = value;
        return;
    }
    if ((address & 0xc000) == 0xc000) {
        uint16_t* target = (address & 0x0100) ? &road_vscroll_ : &view_hscroll_;
        if ((address & 1) == 0)
            *target = uint16_t((*target & 0x00ff) | (uint16_t(value) << 8));
        else
            *target = uint16_t((*target & 0xff00) | value);
        return;
    }
    (void)which;
}

uint32_t PolePos::pen_rgb(int pen) const {
    if (pen < 0 || pen >= int(pens_.size())) return 0xff000000u;
    const uint16_t index = pens_[size_t(pen)];
    if (index >= rgb_.size()) return 0xff000000u;
    return rgb_[index];
}

void PolePos::draw_background() {
    for (int col = 0; col < 64; col++) {
        for (int row = 0; row < 16; row++) {
            const int tile_index = col * 16 + row;
            const uint16_t word = be16(&view_ram_[size_t(tile_index * 2)]);
            const int code = (word & 0xff) | ((word & 0x4000) >> 6);
            const int color = (word & 0x3f00) >> 8;
            const uint8_t* pix = tiles_.element(code);
            for (int y = 0; y < 8; y++) {
                const int sy = row * 8 + y;
                if (sy >= 128) continue;
                for (int x = 0; x < 8; x++) {
                    int sx = col * 8 + x - int(view_hscroll_ & 0x1ff);
                    sx %= 512;
                    if (sx < 0) sx += 512;
                    if (sx >= 256) continue;
                    const int pen = 0x200 + color * 4 + pix[y * 8 + x];
                    bitmap_[size_t(sy * kScreenWidth + sx)] = pen_rgb(pen);
                }
            }
        }
    }
}

void PolePos::draw_road() {
    if (road_rom_.size() < 0x5000) return;
    const uint8_t* bits1 = &road_rom_[0x2000];
    const uint8_t* bits2 = &road_rom_[0x4000];
    for (int y = 128; y < 256; y++) {
        const int yoffs = ((int(vpos_mod_[size_t(y)]) + int(road_vscroll_)) >> 3) & 0x1ff;
        const int roadpal = be16(&road_ram_[size_t(yoffs * 2)]) & 15;
        const int pen_base = 0x0b00 + (roadpal << 6);
        int xoffs = be16(&road_ram_[size_t((0x380 + (y & 0x7f)) * 2)]) & 0x3ff;
        const int xscroll = xoffs & 7;
        xoffs &= ~7;
        uint16_t line[256 + 8]{};
        uint16_t* dest = line;
        for (int x = 0; x < 256 / 8 + 1; x++, xoffs += 8) {
            if (xoffs & 0x200) {
                for (int i = 0; i < 8; i++) *dest++ = uint16_t(pen_base);
            } else {
                const int romoffs = ((y & 0x07f) << 6) + ((xoffs & 0x1f8) >> 3);
                const int control = road_rom_[size_t(romoffs)];
                const int b1 = bits1[romoffs];
                const int b2 = bits2[(romoffs & 0xfff) | ((romoffs & 0x1000) >> 1)];
                int roadval = control & 0x3f;
                const int carin = control >> 7;
                for (int i = 8; i > 0; i--) {
                    int bits = ((b1 >> i) & 1) + (((b2 >> i) & 1) << 1);
                    if (!carin && bits) bits++;
                    *dest++ = uint16_t(pen_base | (roadval & 0x3f));
                    roadval += bits;
                }
            }
        }
        for (int x = 0; x < 256; x++) {
            bitmap_[size_t(y * kScreenWidth + x)] = pen_rgb(line[x + xscroll]);
        }
    }
}

void PolePos::zoom_sprite(bool big, uint32_t code, uint32_t color, bool flipx, int sx, int sy,
                          int sizex, int sizey) {
    const GfxSet& gfx = big ? bigsprites_ : sprites_;
    const uint8_t* gfxdata = gfx.element(int(code));
    const int coloroffs = 0x300 + int(color) * 16;
    const int offsxor = flipx ? (big ? 0x1f : 0x0f) : 0;
    const int width = gfx.width();

    for (int y = 0; y <= sizey; y++) {
        const int yy = (sy + y) & 0x1ff;
        if (yy < 0x10 || yy >= 0xf0) continue;
        int dy = scalelut_.empty() ? 0 : (scalelut_[(size_t(y) << 6) + size_t(sizey)] & 0x1f);
        if (!big) dy >>= 1;
        int xx = sx & 0x3ff;
        int siz = 0;
        int offs = 0;
        const uint8_t* src = gfxdata + dy * width;
        for (int x = (big ? 0x40 : 0x20); x > 0; x--) {
            if (xx < 0x100) {
                const int pen = src[(offs / 2) ^ offsxor];
                if (pen != 0x0f) {
                    const int dest_pen = coloroffs + pen;
                    if ((pens_[size_t(dest_pen)] & 0xff) != 0x1f) {
                        bitmap_[size_t(yy * kScreenWidth + xx)] = pen_rgb(dest_pen);
                    }
                }
            }
            offs++;
            siz = siz + 1 + sizex;
            if (siz & 0x40) {
                siz &= 0x3f;
                xx = (xx + 1) & 0x3ff;
            }
        }
    }
}

void PolePos::draw_sprites() {
    for (int i = 0; i < 64; i++) {
        const uint8_t* pos = &sprite_ram_[size_t((0x380 + i * 2) * 2)];
        const uint8_t* siz = &sprite_ram_[size_t((0x780 + i * 2) * 2)];
        const uint16_t pos0 = be16(pos);
        const uint16_t pos1 = be16(pos + 2);
        const uint16_t siz0 = be16(siz);
        const uint16_t siz1 = be16(siz + 2);
        const int sx = int(pos1 & 0x3ff) - 0x40 + 4;
        const int sy = 512 - int(pos0 & 0x1ff) + 1;
        const int sizex = (siz1 & 0x3f00) >> 8;
        const int sizey = (siz0 & 0x3f00) >> 8;
        const int code = siz0 & 0x7f;
        const bool flipx = (siz0 & 0x80) != 0;
        int color = siz1 & 0x3f;
        if (sy >= 128) color |= 0x40;
        zoom_sprite((siz0 & 0x8000) != 0, uint32_t(code), uint32_t(color), flipx, sx, sy, sizex,
                    sizey);
    }
}

void PolePos::draw_text() {
    for (int row = 0; row < 32; row++) {
        for (int col = 0; col < 32; col++) {
            const int tile_index = row * 32 + col;
            uint16_t word = be16(&alpha_ram_[size_t(tile_index * 2)]);
            int code = (word & 0xff) | ((word & 0x4000) >> 6);
            int color = (word & 0x3f00) >> 8;
            if (chacl_ == 0) {
                code &= 0xff;
                color = 0;
            }
            if (tile_index >= 32 * 16) color |= 0x40;
            const uint8_t* pix = chars_.element(code);
            for (int y = 0; y < 8; y++) {
                const int sy = row * 8 + y;
                if (sy >= kRawHeight) continue;
                for (int x = 0; x < 8; x++) {
                    const int sx = col * 8 + x;
                    const uint8_t pixv = pix[y * 8 + x];
                    if (pixv == 0) continue;
                    const int pen = color * 4 + pixv;
                    const uint16_t rgb_index = pens_[size_t(pen)];
                    if (rgb_index == 0x2f) continue;
                    bitmap_[size_t(sy * kScreenWidth + sx)] = rgb_[rgb_index < 128 ? rgb_index : 0];
                }
            }
        }
    }
}

void PolePos::update_video() {
    bitmap_.fill(rgb_[0]);
    draw_background();
    draw_road();
    draw_sprites();
    draw_text();
    for (int y = 0; y < kScreenHeight; y++) {
        const uint32_t* src = &bitmap_[size_t((y + kVisTop) * kScreenWidth)];
        uint32_t* dst = &framebuffer_[size_t(y * kScreenWidth)];
        std::memcpy(dst, src, size_t(kScreenWidth) * sizeof(uint32_t));
    }
}

void PolePos::run_frame() {
    constexpr int kSlice = 16;
    for (int line = 0; line < kScanlines; line++) {
        scanline_ = line;
        if ((line == 64 || line == 192) && irq_enable_) {
            z80_.set_irq(IrqLine::Assert);
        }
        if (line == 240) {
            namco51_vblank();
            if (sub_irq_mask_) {
                if (!sub1_reset_) sub1_.set_nvi(IrqLine::Assert);
                if (!sub2_reset_) sub2_.set_nvi(IrqLine::Assert);
            }
        }
        int left = kCyclesPerLine;
        while (left > 0) {
            const int slice = left < kSlice ? left : kSlice;
            z80_.run(slice);
            if (!sub1_reset_) sub1_.run(slice);
            if (!sub2_reset_) sub2_.run(slice);
            left -= slice;
        }
    }
    update_video();
}

void PolePos::set_inputs(const MachineInputs& inputs) {
    in0_ = 0xff;
    if (inputs.coin1) in0_ &= uint8_t(~0x10);
    if (inputs.coin2) in0_ &= uint8_t(~0x20);
    if (inputs.player1.button3 && !gear_button_prev_) gear_hi_ = !gear_hi_;
    gear_button_prev_ = inputs.player1.button3;
    accel_ = (inputs.player1.button1 || inputs.player1.up) ? 0x90 : 0x00;
    brake_ = (inputs.player1.button2 || inputs.player1.down) ? 0x90 : 0x00;
    if (inputs.player1.left && steer_ > 8) steer_ = uint8_t(steer_ - 4);
    if (inputs.player1.right && steer_ < 0xf8) steer_ = uint8_t(steer_ + 4);
    if (!inputs.player1.left && !inputs.player1.right) {
        if (steer_ < 0x80) steer_ = uint8_t(steer_ + 1);
        if (steer_ > 0x80) steer_ = uint8_t(steer_ - 1);
    }
}

void PolePos::set_dip_switch(int bank, uint8_t value) {
    if (bank == 0) dswa_ = value;
    if (bank == 1) dswb_ = value;
}

void PolePos::drain_audio(std::vector<int16_t>& out) {
    out.swap(audio_);
    audio_.clear();
}

}  // namespace dsp
