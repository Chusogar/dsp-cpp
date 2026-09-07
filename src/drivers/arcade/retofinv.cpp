#include "drivers/arcade/retofinv.h"

#include <algorithm>
#include <cstring>

#include "core/rom_loader.h"

namespace dsp {
namespace {

const std::vector<RomEntry> kMainRoms = {
    {"a37__03.ic70|a37-03.70", 0x2000, 0x0000, 0xeae7459d},
    {"a37__02.ic71|a37-02.71", 0x2000, 0x2000, 0x72895e37},
    {"a37__01.ic72|a37-01.72", 0x2000, 0x4000, 0x505dd20b},
};
const std::vector<RomEntry> kSubRoms = {{"a37__04.ic62|a37-04.62", 0x2000, 0x0000, 0xd2899cc1}};
const std::vector<RomEntry> kSoundRoms = {{"a37__05.ic17|a37-05.17", 0x2000, 0x0000, 0x9025abea}};
const std::vector<RomEntry> kMcuRoms = {{"a37__09.ic37|a37-09.37", 0x0800, 0x0000, 0x6a6d008d}};
const std::vector<RomEntry> kCharRoms = {{"a37__16.gfxboard.ic61|a37-16.61", 0x2000, 0x0000, 0x4e3f501c}};
const std::vector<RomEntry> kTileRoms = {
    {"a37__14.gfxboard.ic55|a37-14.55", 0x2000, 0x0000, 0xef7f8651},
    {"a37__15.gfxboard.ic56|a37-15.56", 0x2000, 0x2000, 0x03b40905},
};
const std::vector<RomEntry> kSpriteRoms = {
    {"a37__10.gfxboard.ic8|a37-10.8", 0x2000, 0x0000, 0x6afdeec8},
    {"a37__11.gfxboard.ic9|a37-11.9", 0x2000, 0x2000, 0xd3dc9da3},
    {"a37__12.gfxboard.ic10|a37-12.10", 0x2000, 0x4000, 0xd10b2eed},
    {"a37__13.gfxboard.ic11|a37-13.11", 0x2000, 0x6000, 0x00ca6b3d},
};
const std::vector<RomEntry> kPromRoms = {
    {"a37-06.ic13|a37-06.13", 0x100, 0x000, 0xe9643b8b},
    {"a37-07.ic4|a37-07.4", 0x100, 0x100, 0xe8f34e11},
    {"a37-08.ic3|a37-08.3", 0x100, 0x200, 0x50030af0},
};
// Pascal order: 17@0, 18@$800, 19@$400, 20@$c00
const std::vector<RomEntry> kClutRoms = {
    {"a37-17.gfxboard.ic36|a37-17.36", 0x400, 0x000, 0xc63cf10e},
    {"a37-18.gfxboard.ic37|a37-18.37", 0x400, 0x800, 0x6db07bd1},
    {"a37-19.gfxboard.ic83|a37-19.83", 0x400, 0x400, 0xa92aea27},
    {"a37-20.gfxboard.ic84|a37-20.84", 0x400, 0xc00, 0x77a7aaf6},
};

GfxLayout char_layout() {
    GfxLayout l;
    l.width = 8; l.height = 8; l.total = 0x200; l.planes = 1;
    l.char_increment = 64; l.rotate_cw = true;
    l.plane_offsets = {0};
    l.x_offsets = {7, 6, 5, 4, 3, 2, 1, 0};
    l.y_offsets = {0, 8, 16, 24, 32, 40, 48, 56};
    return l;
}
GfxLayout tile_layout() {
    GfxLayout l;
    l.width = 8; l.height = 8; l.total = 0x200; l.planes = 4;
    l.char_increment = 128; l.rotate_cw = true;
    l.plane_offsets = {0, 0x200 * 128 + 4, 0x200 * 128, 4};
    l.x_offsets = {0, 1, 2, 3, 64, 65, 66, 67};
    l.y_offsets = {0, 8, 16, 24, 32, 40, 48, 56};
    return l;
}
GfxLayout sprite_layout() {
    GfxLayout l;
    l.width = 16; l.height = 16; l.total = 0x100; l.planes = 4;
    l.char_increment = 512; l.rotate_cw = true;
    l.plane_offsets = {0, 0x100 * 512 + 4, 0x100 * 512, 4};
    l.x_offsets = {0, 1, 2, 3, 64, 65, 66, 67, 128, 129, 130, 131, 192, 193, 194, 195};
    l.y_offsets = {0, 8, 16, 24, 32, 40, 48, 56, 256, 264, 272, 280, 288, 296, 304, 312};
    return l;
}
uint16_t bitswap_clut(uint16_t v) {
    return uint16_t((v & 0xfff8) | ((v & 1) << 2) | (v & 2) | ((v & 4) >> 2));
}

}  // namespace

Retofinv::Retofinv()
    : main_cpu_(kCpuClock), sub_cpu_(kCpuClock), sound_cpu_(kCpuClock),
      mcu_(kCpuClock, Taito68705::Type::Standard),
      sn0_(kCpuClock), sn1_(kCpuClock) {
    framebuffer_.assign(size_t(kScreenWidth) * kScreenHeight, 0xff000000u);
    work_.assign(size_t(kScreenWidth) * kScreenHeight, 0xff000000u);
    main_cpu_.set_memory_handlers(
        [this](uint16_t a) { return main_read(a); },
        [this](uint16_t a, uint8_t v) { main_write(a, v); });
    sub_cpu_.set_memory_handlers(
        [this](uint16_t a) { return sub_read(a); },
        [this](uint16_t a, uint8_t v) { sub_write(a, v); });
    sound_cpu_.set_memory_handlers(
        [this](uint16_t a) { return sound_read(a); },
        [this](uint16_t a, uint8_t v) { sound_write(a, v); });
    sound_cpu_.set_cycle_handler([this](int c) { on_sound_cycles(c); });
}

bool Retofinv::init(const std::string& rom_path, std::string* error) {
    RomLoader loader;
    if (!loader.open(rom_path, error)) return false;
    std::vector<uint8_t> buf(0x6000, 0);
    if (!loader.load(kMainRoms, buf, error)) return false;
    std::copy(buf.begin(), buf.end(), memory_.begin());
    buf.assign(0x2000, 0);
    if (!loader.load(kSubRoms, buf, error)) return false;
    std::copy(buf.begin(), buf.end(), sub_rom_.begin());
    buf.assign(0x2000, 0);
    if (!loader.load(kSoundRoms, buf, error)) return false;
    std::copy(buf.begin(), buf.end(), sound_mem_.begin());
    std::copy(buf.begin(), buf.end(), sound_mem_.begin() + 0xe000);
    buf.assign(Taito68705::kRomSize, 0);
    if (!loader.load(kMcuRoms, buf, error)) return false;
    std::copy(buf.begin(), buf.end(), mcu_.rom_data());

    std::vector<uint8_t> chars(0x2000), tiles(0x4000), sprites(0x8000), prom(0x300), clut(0x1000);
    if (!loader.load(kCharRoms, chars, error)) return false;
    if (!loader.load(kTileRoms, tiles, error)) return false;
    if (!loader.load(kSpriteRoms, sprites, error)) return false;
    if (!loader.load(kPromRoms, prom, error)) return false;
    if (!loader.load(kClutRoms, clut, error)) return false;
    decode_graphics(chars, tiles, sprites);
    build_palette(prom, clut);
    warnings_ = loader.warnings();
    reset();
    return true;
}

void Retofinv::decode_graphics(const std::vector<uint8_t>& c, const std::vector<uint8_t>& t,
                               const std::vector<uint8_t>& s) {
    chars_.decode(char_layout(), c);
    tiles_.decode(tile_layout(), t);
    sprites_.decode(sprite_layout(), s);
}

void Retofinv::build_palette(const std::vector<uint8_t>& prom, const std::vector<uint8_t>& clut) {
    for (size_t i = 0; i < 0x100; i++) {
        int r = prom[i] & 0x0f, g = prom[i + 0x100] & 0x0f, b = prom[i + 0x200] & 0x0f;
        r |= r << 4; g |= g << 4; b |= b << 4;
        palette_[i] = 0xff000000u | (uint32_t(r) << 16) | (uint32_t(g) << 8) | uint32_t(b);
    }
    // FG 1bpp: odd pens map to (index>>1), even -> transparent 0
    for (int f = 0; f < 0x200; f++) fg_lut_[size_t(f)] = (f & 1) ? uint8_t(f >> 1) : 0;
    // Combine CLUT ROMs like Pascal:
    // memoria_temp[$1000+f] = (mem[f] & $f)<<4 | (mem[$800+f] & $f)  for f=0..$7ff
    // ROM layout: 17@0, 19@$400, 18@$800, 20@$c00
    std::array<uint8_t, 0x800> comb{};
    // Try: low from 17, high from 18 (swap if logo hues inverted)
    for (int f = 0; f < 0x800; f++) {
        comb[size_t(f)] = uint8_t(((clut[size_t(f)] & 0x0f) << 4) | (clut[size_t(f) + 0x800] & 0x0f));
    }
    for (int f = 0; f < 0x800; f++) {
        const int sw = bitswap_clut(uint16_t(f)) & 0x7ff;
        bg_lut_[size_t(f)] = comb[size_t(sw)];
        sprite_lut_[size_t(f)] = comb[size_t(sw)];
    }
}

void Retofinv::reset() {
    main_cpu_.reset(); sub_cpu_.reset(); sound_cpu_.reset();
    mcu_.reset();
    sn0_.reset(); sn1_.reset();
    fg_bank_ = bg_bank_ = 0;
    flip_screen_ = main_irq_enable_ = sub_irq_enable_ = false;
    sub_enabled_ = sound_enabled_ = mcu_enabled_ = false;
    sound_latch_ = sound_return_ = 0;
    in0_ = in1_ = 0xff; in2_ = 0xcf;
    audio_accumulator_ = 0; audio_.clear();
}

uint8_t Retofinv::main_read(uint16_t address) {
    if (address <= 0xa7ff) return memory_[address];
    switch (address) {
        case 0xc000: return in0_;
        case 0xc001: return in1_;
        case 0xc002: return 0x00;
        case 0xc003: {
            // Pascal: (not main_sent)<<4 | mcu_sent<<5
            uint8_t res = 0;
            if (!mcu_.main_sent()) res |= 0x10;
            if (mcu_.mcu_sent()) res |= 0x20;
            return res;
        }
        case 0xc004: return in2_;
        case 0xc005: return dsw_a_;
        case 0xc006: return dsw_b_;
        case 0xc007: return dsw_c_;
        case 0xe000: return mcu_.read();
        case 0xf800: return sound_return_;
        default: return 0xff;
    }
}

void Retofinv::main_write(uint16_t address, uint8_t value) {
    if (address <= 0x7fff) return;
    if (address <= 0xa7ff) { memory_[address] = value; return; }
    switch (address) {
        case 0xb800: flip_screen_ = (value & 1) != 0; break;
        case 0xb801: fg_bank_ = uint8_t(value & 1); break;
        case 0xb802: bg_bank_ = uint8_t(value & 1); break;
        case 0xc800:
            main_irq_enable_ = (value & 1) != 0;
            if (!main_irq_enable_) main_cpu_.set_irq(IrqLine::Clear);
            break;
        case 0xc802:
            sound_enabled_ = (value & 1) != 0;
            if (!sound_enabled_) sound_cpu_.reset();
            break;
        case 0xc803:
            mcu_enabled_ = (value & 1) != 0;
            mcu_.set_reset(!mcu_enabled_);
            break;
        case 0xc804:
            sub_irq_enable_ = (value & 1) != 0;
            if (!sub_irq_enable_) sub_cpu_.set_irq(IrqLine::Clear);
            break;
        case 0xc805:
            sub_enabled_ = (value & 1) != 0;
            if (!sub_enabled_) sub_cpu_.reset();
            break;
        case 0xd800:
            sound_latch_ = value;
            sound_cpu_.set_irq(IrqLine::Hold);
            break;
        case 0xe800:
            mcu_.write(value);
            break;
        default: break;
    }
}

uint8_t Retofinv::sub_read(uint16_t address) {
    if (address <= 0x1fff) return sub_rom_[address];
    if (address >= 0x8000 && address <= 0xa7ff) return memory_[address];
    return 0xff;
}
void Retofinv::sub_write(uint16_t address, uint8_t value) {
    if (address >= 0x8000 && address <= 0xa7ff) main_write(address, value);
    else if (address == 0xc804) {
        sub_irq_enable_ = (value & 1) != 0;
        if (!sub_irq_enable_) sub_cpu_.set_irq(IrqLine::Clear);
    }
}

uint8_t Retofinv::sound_read(uint16_t address) {
    if (address <= 0x27ff || address >= 0xe000) return sound_mem_[address];
    if (address == 0x4000) { sound_cpu_.set_irq(IrqLine::Clear); return sound_latch_; }
    return 0xff;
}
void Retofinv::sound_write(uint16_t address, uint8_t value) {
    if (address >= 0x2000 && address <= 0x27ff) { sound_mem_[address] = value; return; }
    if (address == 0x6000) sound_return_ = value;
    else if (address == 0x8000) sn0_.write(value);
    else if (address == 0xa000) sn1_.write(value);
}

void Retofinv::on_sound_cycles(int cycles) {
    audio_accumulator_ += int64_t(cycles) * SN76496::kSampleRate;
    while (audio_accumulator_ >= kCpuClock) {
        audio_accumulator_ -= kCpuClock;
        int32_t s = (sn0_.update() + sn1_.update()) / 2;
        if (s > 32767) s = 32767;
        if (s < -32768) s = -32768;
        audio_.push_back(int16_t(s));
    }
}

void Retofinv::draw_bg() {
    for (int screen_x = 0; screen_x < 28; screen_x++) {
        for (int screen_y = 0; screen_y < 36; screen_y++) {
            int sx = 29 - screen_x, sy = screen_y - 2;
            int offs = (sy & 0x20) ? (sx + ((sy & 0x1f) << 5)) : (sy + (sx << 5));
            int code = memory_[0xa000 + offs] + (bg_bank_ << 8);
            // Pascal: put_gfx(..., $400 + ((attr&$3f)<<4), ...)
            int color = 0x400 + ((memory_[0xa400 + offs] & 0x3f) << 4);
            const uint8_t* tile = tiles_.element(code);
            int dx = screen_x * 8, dy = screen_y * 8;
            for (int y = 0; y < 8; y++) {
                int py = dy + y;
                if (py < 0 || py >= kScreenHeight) continue;
                uint32_t* line = work_.data() + size_t(py * kScreenWidth + dx);
                for (int x = 0; x < 8; x++) {
                    if (dx + x >= kScreenWidth) continue;
                    uint8_t pen = tile[y * 8 + x];
                    uint8_t entry = bg_lut_[size_t((color + pen) & 0x7ff)];
                    if (entry == 0xff) continue;
                    line[x] = palette_[entry];
                }
            }
        }
    }
}

void Retofinv::draw_fg() {
    for (int screen_x = 0; screen_x < 28; screen_x++) {
        for (int screen_y = 0; screen_y < 36; screen_y++) {
            int sx = 29 - screen_x, sy = screen_y - 2;
            int offs = (sy & 0x20) ? (sx + ((sy & 0x1f) << 5)) : (sy + (sx << 5));
            int code = memory_[0x8000 + offs] + (fg_bank_ << 8);
            int color = memory_[0x8400 + offs] << 1;
            const uint8_t* tile = chars_.element(code);
            int dx = screen_x * 8, dy = screen_y * 8;
            for (int y = 0; y < 8; y++) {
                int py = dy + y;
                if (py < 0 || py >= kScreenHeight) continue;
                uint32_t* line = work_.data() + size_t(py * kScreenWidth + dx);
                for (int x = 0; x < 8; x++) {
                    if (dx + x >= kScreenWidth) continue;
                    uint8_t pen = tile[y * 8 + x];
                    if (!pen) continue;
                    uint8_t entry = fg_lut_[size_t((color + pen) & 0x1ff)];
                    if (!entry) continue;
                    line[x] = palette_[entry];
                }
            }
        }
    }
}

void Retofinv::draw_sprites() {
    for (int f = 0; f < 0x40; f++) {
        int nchar = memory_[0x8f80 + f * 2];
        int color = (memory_[0x8f81 + f * 2] & 0x3f) << 4;
        uint8_t attr = memory_[0x9f80 + f * 2];
        int size_sprite = (attr & 0x0c) >> 2;
        int y, x;
        if (!flip_screen_) {
            y = ((memory_[0x9781 + f * 2] << 1) + ((memory_[0x9f81 + f * 2] & 0x80) >> 7)) - 39;
            x = ((memory_[0x9780 + f * 2] << 1) + ((attr & 0x80) >> 7)) - 17;
        } else {
            y = 255 - ((memory_[0x9781 + f * 2] << 1) + ((memory_[0x9f81 + f * 2] & 0x80) >> 7)) + 56;
            x = 255 - ((memory_[0x9780 + f * 2] << 1) + ((attr & 0x80) >> 7)) - 31;
            if (size_sprite) { x -= 16; if (size_sprite != 1) y -= 16; }
        }
        bool flip_x = (attr & 2) != 0, flip_y = (attr & 1) != 0;
        nchar &= ~size_sprite;
        auto blit = [&](int code, int ox, int oy) {
            const uint8_t* pix = sprites_.element(code);
            for (int py = 0; py < 16; py++) {
                int sy = flip_y ? 15 - py : py;
                int dy = y + oy + py;
                if (dy < 0 || dy >= kScreenHeight) continue;
                uint32_t* line = work_.data() + size_t(dy * kScreenWidth);
                for (int px = 0; px < 16; px++) {
                    int sx = flip_x ? 15 - px : px;
                    int dx = x + ox + px;
                    if (dx < 0 || dx >= kScreenWidth) continue;
                    uint8_t pen = pix[sy * 16 + sx];
                    if (!pen) continue;
                    uint8_t entry = sprite_lut_[size_t((color + pen) & 0x7ff)];
                    if (entry == 0xff) continue;
                    line[dx] = palette_[entry];
                }
            }
        };
        switch (size_sprite) {
            case 0: blit(nchar, 0, 0); break;
            case 1: blit(nchar + 2, 0, 0); blit(nchar, 16, 0); break;
            case 2: blit(nchar, 0, 0); blit(nchar + 1, 0, 16); break;
            case 3:
                blit(nchar + 2, 0, 0); blit(nchar, 16, 0);
                blit(nchar + 3, 0, 16); blit(nchar + 1, 16, 16);
                break;
        }
    }
}

void Retofinv::update_video() {
    std::fill(work_.begin(), work_.end(), 0xff000000u);
    draw_bg();
    draw_sprites();
    draw_fg();
    for (int y = 0; y < kScreenHeight; y++) {
        for (int x = 0; x < kScreenWidth; x++) {
            size_t src = size_t(y * kScreenWidth + x);
            size_t dst = flip_screen_
                ? size_t((kScreenHeight - 1 - y) * kScreenWidth + (kScreenWidth - 1 - x))
                : src;
            framebuffer_[dst] = work_[src];
        }
    }
}

void Retofinv::run_frame() {
    const int total = int(kCpuClock / kFramesPerSecond);
    const int slice = total / kScanlines;
    for (int line = 0; line < kScanlines; line++) {
        main_cpu_.run(slice);
        if (sub_enabled_) sub_cpu_.run(slice);
        if (sound_enabled_) {
            sound_cpu_.run(slice);
            if (line == kScanlines / 2 || line == kScanlines - 1)
                sound_cpu_.set_nmi(IrqLine::Pulse);
        }
        if (mcu_enabled_) mcu_.run(slice);
    }
    if (main_irq_enable_) main_cpu_.set_irq(IrqLine::Assert);
    if (sub_enabled_ && sub_irq_enable_) sub_cpu_.set_irq(IrqLine::Assert);
    update_video();
}

void Retofinv::set_inputs(const MachineInputs& inputs) {
    const auto& p1 = inputs.player1;
    const auto& p2 = inputs.player2;
    in0_ = in1_ = 0xff; in2_ = 0xcf;
    if (p1.left) in0_ &= 0xfd;
    if (p1.right) in0_ &= 0xf7;
    if (p1.button1) in0_ &= 0x7f;
    if (p2.left) in1_ &= 0xfd;
    if (p2.right) in1_ &= 0xf7;
    if (p2.button1) in1_ &= 0x7f;
    if (p1.start) in2_ &= 0xfe;
    if (p2.start) in2_ &= 0xfd;
    if (inputs.coin1) in2_ |= 0x10;
    if (inputs.coin2) in2_ |= 0x20;
}

void Retofinv::set_dip_switch(int bank, uint8_t value) {
    if (bank == 0) dsw_a_ = value;
    else if (bank == 1) dsw_b_ = value;
    else if (bank == 2) dsw_c_ = value;
}

void Retofinv::drain_audio(std::vector<int16_t>& out) {
    out.insert(out.end(), audio_.begin(), audio_.end());
    audio_.clear();
}

}  // namespace dsp
