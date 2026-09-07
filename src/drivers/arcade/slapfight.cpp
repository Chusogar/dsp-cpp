#include "drivers/arcade/slapfight.h"

#include <algorithm>
#include <cstring>

#include "core/rom_loader.h"
#include "cpu/irq_line.h"

namespace dsp {
namespace {

const std::vector<RomEntry> kSlapMain = {
    {"a77_00.8p", 0x8000, 0x0000, 0x674c0e0f},
    {"a77_01.8n", 0x8000, 0x8000, 0x3c42e4a7},
};
const std::vector<RomEntry> kSlapSnd = {{"a77_02.12d", 0x2000, 0, 0x87f4705a}};
const std::vector<RomEntry> kSlapMcu = {{"a77_13.6a", 0x800, 0, 0xa70c81d9}};
const std::vector<RomEntry> kSlapPal = {
    {"21_82s129.12q", 0x100, 0x000, 0xa0efaf99},
    {"20_82s129.12m", 0x100, 0x100, 0xa56d57e5},
    {"19_82s129.12n", 0x100, 0x200, 0x5cbf9fbf},
};
const std::vector<RomEntry> kSlapChar = {
    {"a77_04.6f", 0x2000, 0x0000, 0x2ac7b943},
    {"a77_03.6g", 0x2000, 0x2000, 0x33cadc93},
};
const std::vector<RomEntry> kSlapTiles = {
    {"a77_08.6k", 0x8000, 0x00000, 0xb6358305},
    {"a77_07.6m", 0x8000, 0x08000, 0xe92d9d60},
    {"a77_06.6n", 0x8000, 0x10000, 0x5faeeea3},
    {"a77_05.6p", 0x8000, 0x18000, 0x974e2ea9},
};
const std::vector<RomEntry> kSlapSprites = {
    {"a77_12.8j", 0x8000, 0x00000, 0x8545d397},
    {"a77_11.7j", 0x8000, 0x08000, 0xb1b7b925},
    {"a77_10.8h", 0x8000, 0x10000, 0x422d946b},
    {"a77_09.7h", 0x8000, 0x18000, 0x587113ae},
};

void decode_chars(GfxSet& out, const std::vector<uint8_t>& rom, int total) {
    GfxLayout layout;
    layout.width = 8;
    layout.height = 8;
    layout.total = total;
    layout.planes = 2;
    layout.char_increment = 8 * 8;
    layout.rotate_ccw = true;  // convert_gfx(..., false, true) → flip Y / rot
    layout.plane_offsets = {0 * 8 * 8, total * 8 * 8};
    layout.x_offsets = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
    layout.x_offsets.resize(8);
    layout.y_offsets = {0 * 8, 1 * 8, 2 * 8, 3 * 8, 4 * 8, 5 * 8, 6 * 8, 7 * 8};
    out.decode(layout, rom);
}

void decode_tiles(GfxSet& out, const std::vector<uint8_t>& rom, int total) {
    GfxLayout layout;
    layout.width = 8;
    layout.height = 8;
    layout.total = total;
    layout.planes = 4;
    layout.char_increment = 8 * 8;
    layout.rotate_ccw = true;
    layout.plane_offsets = {total * 8 * 8 * 0, total * 8 * 8 * 1, total * 8 * 8 * 2,
                            total * 8 * 8 * 3};
    layout.x_offsets = {0, 1, 2, 3, 4, 5, 6, 7};
    layout.y_offsets = {0 * 8, 1 * 8, 2 * 8, 3 * 8, 4 * 8, 5 * 8, 6 * 8, 7 * 8};
    out.decode(layout, rom);
}

void decode_sprites(GfxSet& out, const std::vector<uint8_t>& rom, int total) {
    GfxLayout layout;
    layout.width = 16;
    layout.height = 16;
    layout.total = total;
    layout.planes = 4;
    layout.char_increment = 32 * 8;
    layout.rotate_ccw = true;
    layout.plane_offsets = {total * 32 * 8 * 0, total * 32 * 8 * 1, total * 32 * 8 * 2,
                            total * 32 * 8 * 3};
    layout.x_offsets = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
    layout.y_offsets = {0 * 16, 1 * 16, 2 * 16, 3 * 16, 4 * 16, 5 * 16, 6 * 16, 7 * 16,
                        8 * 16, 9 * 16, 10 * 16, 11 * 16, 12 * 16, 13 * 16, 14 * 16, 15 * 16};
    out.decode(layout, rom);
}

void blit(uint32_t* dest, int dw, int dh, int dx, int dy, const uint8_t* src, int sw, int sh,
          const uint32_t* pal, int color_base, bool transparent) {
    for (int y = 0; y < sh; y++) {
        const int py = dy + y;
        if (py < 0 || py >= dh) continue;
        for (int x = 0; x < sw; x++) {
            const int px = dx + x;
            if (px < 0 || px >= dw) continue;
            const uint8_t pix = src[y * sw + x];
            if (transparent && pix == 0) continue;
            dest[py * dw + px] = pal[(color_base + pix) & 0xff];
        }
    }
}

}  // namespace

SlapFight::SlapFight(Variant v)
    : variant_(v),
      main_cpu_(kMainClock),
      sound_cpu_(kSoundClock),
      mcu_(kMcuClock, v == Variant::TigerHeli ? Taito68705::Type::TigerHeli
                                              : Taito68705::Type::Standard),
      ay0_(kAyClock, 1.2f),
      ay1_(kAyClock, 1.2f) {
    fg_layer_.assign(256 * 512, 0);
    bg_layer_.assign(256 * 512, 0);
    sprite_layer_.assign(256 * 512, 0);
    framebuffer_.assign(size_t(kScreenWidth) * kScreenHeight, 0xff000000u);

    main_cpu_.set_memory_handlers([this](uint16_t a) { return main_read(a); },
                                  [this](uint16_t a, uint8_t v) { main_write(a, v); });
    main_cpu_.set_io_handlers([this](uint16_t p) { return main_in(p); },
                              [this](uint16_t p, uint8_t v) { main_out(p, v); });
    sound_cpu_.set_memory_handlers([this](uint16_t a) { return sound_read(a); },
                                   [this](uint16_t a, uint8_t v) { sound_write(a, v); });
    mcu_.set_misc_callback([this](int pos, uint8_t value) { mcu_scroll_y(pos, value); });
    ay0_.set_port_handlers([this]() { return in0_; }, [this]() { return in1_; }, nullptr,
                           nullptr);
    ay1_.set_port_handlers([this]() { return dswa_; }, [this]() { return dswb_; }, nullptr,
                           nullptr);
}

bool SlapFight::init(const std::string& rom_path, std::string* error) {
    RomLoader loader;
    if (!loader.open(rom_path, error)) return false;

    std::vector<uint8_t> main(0x10000, 0);
    if (!loader.load(kSlapMain, main, error)) return false;
    std::memcpy(memory_.data(), main.data(), 0x8000);
    std::memcpy(rom_bank_[0].data(), main.data() + 0x8000, 0x4000);
    std::memcpy(rom_bank_[1].data(), main.data() + 0xc000, 0x4000);

    std::vector<uint8_t> sound(0x2000, 0);
    if (!loader.load(kSlapSnd, sound, error)) return false;
    std::memset(sound_mem_.data(), 0, sound_mem_.size());
    std::memcpy(sound_mem_.data(), sound.data(), 0x2000);

    std::vector<uint8_t> mcu(0x800, 0);
    if (!loader.load(kSlapMcu, mcu, error)) return false;
    std::memcpy(mcu_.rom_data(), mcu.data(), 0x800);

    std::vector<uint8_t> chars(0x4000, 0);
    if (!loader.load(kSlapChar, chars, error)) return false;
    decode_chars(chars_, chars, 0x400);

    std::vector<uint8_t> tiles(0x20000, 0);
    if (!loader.load(kSlapTiles, tiles, error)) return false;
    decode_tiles(tiles_, tiles, 0x1000);

    std::vector<uint8_t> sprites(0x20000, 0);
    if (!loader.load(kSlapSprites, sprites, error)) return false;
    decode_sprites(sprites_, sprites, 0x400);

    std::vector<uint8_t> prom(0x300, 0);
    if (!loader.load(kSlapPal, prom, error)) return false;
    for (int f = 0; f < 0x100; f++) {
        auto bitw = [&](int base, int shift) {
            const int b0 = (prom[size_t(base + f)] >> 0) & 1;
            const int b1 = (prom[size_t(base + f)] >> 1) & 1;
            const int b2 = (prom[size_t(base + f)] >> 2) & 1;
            const int b3 = (prom[size_t(base + f)] >> 3) & 1;
            return uint8_t(0x0e * b0 + 0x1f * b1 + 0x43 * b2 + 0x8f * b3);
        };
        const uint8_t r = bitw(0, 0);
        const uint8_t g = bitw(0x100, 0);
        const uint8_t b = bitw(0x200, 0);
        palette_[size_t(f)] = 0xff000000u | (uint32_t(r) << 16) | (uint32_t(g) << 8) | b;
    }

    warnings_ = loader.warnings();
    reset();
    return true;
}

void SlapFight::reset() {
    main_cpu_.reset();
    sound_cpu_.reset();
    mcu_.reset();
    ay0_.reset();
    ay1_.reset();
    rom_bank_sel_ = 0;
    scroll_x_ = 0;
    scroll_y_ = 0;
    ena_irq_ = false;
    sound_nmi_ = false;
    sound_reset_ = true;
    status_state_ = 0;
    sound_nmi_counter_ = 0;
    in0_ = in1_ = 0xff;
    audio_accum_ = 0;
    audio_.clear();
    std::fill(framebuffer_.begin(), framebuffer_.end(), 0xff000000u);
}

void SlapFight::mcu_scroll_y(int pos, uint8_t value) {
    if (pos == 0) scroll_y_ = uint16_t((scroll_y_ & 0xff00) | value);
    if (pos == 1) scroll_y_ = uint16_t((scroll_y_ & 0x00ff) | (value << 8));
}

uint8_t SlapFight::main_read(uint16_t address) {
    // Pascal sf_getbyte — $E803 MCU must be reachable (hardware / MAME);
    // listed after the RAM range in the .pas case, but must win in practice.
    if (address == 0xe803) return mcu_.read();
    if (address <= 0x7fff || (address >= 0xc000 && address <= 0xe7ff) || address >= 0xf000) {
        return memory_[address];
    }
    if (address >= 0x8000 && address <= 0xbfff) {
        return rom_bank_[rom_bank_sel_ & 1][address & 0x3fff];
    }
    return 0xff;
}

void SlapFight::main_write(uint16_t address, uint8_t value) {
    // Pascal sf_putbyte (specials for scroll/MCU; RAM otherwise)
    if (address <= 0xbfff) return;
    if (address == 0xe800) {
        scroll_y_ = uint16_t((scroll_y_ & 0xff00) | value);
        return;
    }
    if (address == 0xe801) {
        scroll_y_ = uint16_t((scroll_y_ & 0x00ff) | (uint16_t(value) << 8));
        return;
    }
    if (address == 0xe802) {
        scroll_x_ = value;
        return;
    }
    if (address == 0xe803) {
        mcu_.write(value);
        return;
    }
    if ((address >= 0xc000 && address <= 0xcfff) || (address >= 0xe000 && address <= 0xe7ff)) {
        memory_[address] = value;
        return;
    }
    if (address >= 0xd000 && address <= 0xdfff) {
        memory_[address] = value;
        return;
    }
    if (address >= 0xf000) {
        memory_[address] = value;
        return;
    }
}

uint8_t SlapFight::main_in(uint16_t port) {
    if ((port & 0xff) == 0) {
        static const uint8_t states[3] = {0xc7, 0x55, 0x00};
        const int idx = status_state_ % 3;
        status_state_ = (status_state_ + 1) & 3;
        return uint8_t((states[idx] & 0xf9) | ((!mcu_.main_sent()) << 1) |
                       ((!mcu_.mcu_sent()) << 2));
    }
    return 0xff;
}

void SlapFight::main_out(uint16_t port, uint8_t value) {
    (void)value;
    switch (port & 0xff) {
        case 0:
            sound_reset_ = true;
            break;
        case 1:
            sound_reset_ = false;
            sound_nmi_ = false;
            break;
        case 6:
            ena_irq_ = false;
            main_cpu_.set_irq(IrqLine::Clear);
            break;
        case 7:
            ena_irq_ = true;
            break;
        case 8:
            rom_bank_sel_ = 0;
            break;
        case 9:
            rom_bank_sel_ = 1;
            break;
        default:
            break;
    }
}

uint8_t SlapFight::sound_read(uint16_t address) {
    if (address <= 0x1fff || address >= 0xd000) return sound_mem_[address];
    if (address == 0xa081) return ay0_.read();
    if (address == 0xa091) return ay1_.read();
    if (address >= 0xc800 && address <= 0xcfff) return memory_[address];
    return 0xff;
}

void SlapFight::sound_write(uint16_t address, uint8_t value) {
    if (address <= 0x1fff) return;
    if (address == 0xa080) {
        ay0_.control(value);
        return;
    }
    if (address == 0xa082) {
        ay0_.write(value);
        return;
    }
    if (address == 0xa090) {
        ay1_.control(value);
        return;
    }
    if (address == 0xa092) {
        ay1_.write(value);
        return;
    }
    if (address >= 0xa0e0 && address <= 0xa0ef) {
        sound_nmi_ = (address & 1) == 0;
        return;
    }
    if (address >= 0xc800 && address <= 0xcfff) {
        memory_[address] = value;
        return;
    }
    if (address >= 0xd000) {
        sound_mem_[address] = value;
        return;
    }
}

void SlapFight::update_video() {
    // Pascal update_video_sf_hw
    // Screens: FG=1, BG=2, final=3 — all 256×512 in the .pas engine.
    // Sprite Y uses 797-(...), so we compose on a 256×1024 buffer and
    // present the (0,0)-(239,280) window like actualiza_trozo_final.
    constexpr int kCompH = 512;
    std::vector<uint32_t> screen3(256 * kCompH, 0xff000000u);

    std::fill(fg_layer_.begin(), fg_layer_.end(), 0);
    std::fill(bg_layer_.begin(), bg_layer_.end(), 0);

    for (int f = 0; f < 0x800; f++) {
        const int x = f / 64;
        const int y = 63 - (f % 64);
        // Foreground (gfx 0, transparent)
        {
            const uint8_t atrib = memory_[0xf800 + f];
            const int color = atrib & 0xfc;
            const int nchar = memory_[0xf000 + f] + ((atrib & 3) << 8);
            blit(fg_layer_.data(), 256, 512, x * 8, y * 8, chars_.element(nchar & 0x3ff), 8, 8,
                 palette_.data(), color, true);
        }
        // Background (gfx 1, opaque)
        {
            const uint8_t atrib = memory_[0xd800 + f];
            const int color = atrib & 0xf0;
            const int nchar = memory_[0xd000 + f] + ((atrib & 0x0f) << 8);
            blit(bg_layer_.data(), 256, 512, x * 8, y * 8, tiles_.element(nchar & 0xfff), 8, 8,
                 palette_.data(), color, false);
        }
    }

    // scroll_x_y(2, 3, scroll_x+17, 736-scroll_y) — BG → screen3
    const int sx = int(scroll_x_) + 17;
    const int sy = 736 - int(scroll_y_);
    for (int y = 0; y < kCompH; y++) {
        for (int x = 0; x < 256; x++) {
            const int bx = (x + sx) & 0xff;
            const int by = (y + sy) & 0x1ff;
            screen3[y * 256 + x] = bg_layer_[by * 256 + bx];
        }
    }

    // Sprites — exact Pascal positions (no Y mask)
    for (int f = 0; f < 0x200; f++) {
        const uint8_t atrib = memory_[0xe002 + f * 4];
        const int nchar = memory_[0xe000 + f * 4] + ((atrib & 0xc0) << 2);
        const int color = (atrib & 0x1e) << 3;
        const int x = int(memory_[0xe003 + f * 4]) - 16;
        // Pascal screen 3 is 256×512; y=797-... wraps into that space
        // (680 → 168, etc.), matching gfx engine clipping on a 512-tall buffer.
        const int y = (797 - (int(memory_[0xe001 + f * 4]) + ((atrib & 1) << 8))) & 0x1ff;
        blit(screen3.data(), 256, 512, x, y, sprites_.element(nchar & 0x3ff), 16, 16,
             palette_.data(), color, true);
    }

    // actualiza_trozo(16,224,239,280,1, 0,0,240,239,3) — FG overlay
    for (int y = 0; y < kScreenHeight; y++) {
        for (int x = 0; x < kScreenWidth; x++) {
            const int fx = x + 16;
            const int fy = y + 224;
            if (fx < 256 && fy < 512) {
                const uint32_t fg = fg_layer_[fy * 256 + fx];
                if (fg) screen3[y * 256 + x] = fg;
            }
        }
    }

    // actualiza_trozo_final(0,0,239,280,3)
    for (int y = 0; y < kScreenHeight; y++) {
        for (int x = 0; x < kScreenWidth; x++) {
            const uint32_t pix = screen3[y * 256 + x];
            framebuffer_[size_t(y * kScreenWidth + x)] = pix ? pix : 0xff000000u;
        }
    }
}

void SlapFight::on_sound_cycles(int cycles) {
    if (sound_reset_) return;
    // NMI at 180 Hz when enabled
    sound_nmi_counter_ += cycles;
    const int period = int(kSoundClock / 180);
    while (sound_nmi_counter_ >= period) {
        sound_nmi_counter_ -= period;
        if (sound_nmi_) sound_cpu_.set_nmi(IrqLine::Pulse);
    }
    audio_accum_ += int64_t(cycles) * AY8910::kSampleRate;
    while (audio_accum_ >= kSoundClock) {
        audio_accum_ -= kSoundClock;
        int32_t s = ay0_.update() + ay1_.update();
        audio_.push_back(int16_t(std::clamp(s, int32_t(-32768), int32_t(32767))));
    }
}

void SlapFight::run_frame() {
    const int main_slice = std::max(1, int(kMainClock / kFramesPerSecond) / kScanlines);
    const int snd_slice = std::max(1, int(kSoundClock / kFramesPerSecond) / kScanlines);
    const int mcu_slice = std::max(1, int(kMcuClock / kFramesPerSecond) / kScanlines);

    for (int line = 0; line < kScanlines; line++) {
        if (line == 240) {
            if (ena_irq_) main_cpu_.set_irq(IrqLine::Assert);
            update_video();
        }
        main_cpu_.run(main_slice);
        if (!sound_reset_) {
            sound_cpu_.run(snd_slice);
            on_sound_cycles(snd_slice);
        }
        mcu_.run(mcu_slice);
    }
}


void SlapFight::set_inputs(const MachineInputs& inputs) {
    in0_ = 0xff;
    in1_ = 0xff;
    const auto& p1 = inputs.player1;
    const auto& p2 = inputs.player2;
    if (p1.up) in0_ &= ~0x01;
    if (p1.down) in0_ &= ~0x02;
    if (p1.right) in0_ &= ~0x04;
    if (p1.left) in0_ &= ~0x08;
    if (p2.up) in0_ &= ~0x10;
    if (p2.down) in0_ &= ~0x20;
    if (p2.right) in0_ &= ~0x40;
    if (p2.left) in0_ &= ~0x80;

    if (p1.button1) in1_ &= ~0x01;
    if (p1.button2) in1_ &= ~0x02;
    if (p2.button1) in1_ &= ~0x04;
    if (p2.button2) in1_ &= ~0x08;
    if (p1.start) in1_ &= ~0x10;
    if (p2.start) in1_ &= ~0x20;
    if (inputs.coin1) in1_ &= ~0x40;
    if (inputs.coin2) in1_ &= ~0x80;
}

void SlapFight::set_dip_switch(int bank, uint8_t value) {
    if (bank == 0) dswa_ = value;
    if (bank == 1) dswb_ = value;
}

void SlapFight::drain_audio(std::vector<int16_t>& out) {
    out.insert(out.end(), audio_.begin(), audio_.end());
    audio_.clear();
}

}  // namespace dsp
