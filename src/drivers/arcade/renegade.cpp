#include "drivers/arcade/renegade.h"

#include <algorithm>
#include <cstring>

#include "core/rom_loader.h"
#include "cpu/irq_line.h"

namespace dsp {
namespace {

const std::vector<RomEntry> kMainRoms = {
    {"na-5.ic52", 0x8000, 0x0000, 0xde7e7df4},
    {"nb-5.ic51", 0x8000, 0x8000, 0xba683ddf},
};
const std::vector<RomEntry> kCharRom = {
    {"nc-5.bin", 0x8000, 0, 0x9adfaa5d},
};
const std::vector<RomEntry> kSoundRom = {
    {"n0-5.ic13", 0x8000, 0x8000, 0x3587de3b},
};
const std::vector<RomEntry> kMcuRom = {
    {"nz-5.ic97", 0x800, 0, 0x32e47560},
};
const std::vector<RomEntry> kTileRoms = {
    {"n1-5.ic1", 0x8000, 0x00000, 0x4a9f47f3},
    {"n6-5.ic28", 0x8000, 0x08000, 0xd62a0aa8},
    {"n7-5.ic27", 0x8000, 0x10000, 0x7ca5a532},
    {"n2-5.ic14", 0x8000, 0x18000, 0x8d2e7982},
    {"n8-5.ic26", 0x8000, 0x20000, 0x0dba31d3},
    {"n9-5.ic25", 0x8000, 0x28000, 0x5b621b6a},
};
const std::vector<RomEntry> kSpriteRoms = {
    {"nh-5.bin", 0x8000, 0x00000, 0xdcd7857c},
    {"nd-5.bin", 0x8000, 0x08000, 0x2de1717c},
    {"nj-5.bin", 0x8000, 0x10000, 0x0f96a18e},
    {"nn-5.bin", 0x8000, 0x18000, 0x1bf15787},
    {"ne-5.bin", 0x8000, 0x20000, 0x924c7388},
    {"nk-5.bin", 0x8000, 0x28000, 0x69499a94},
    {"ni-5.bin", 0x8000, 0x30000, 0x6f597ed2},
    {"nf-5.bin", 0x8000, 0x38000, 0x0efc8d45},
    {"nl-5.bin", 0x8000, 0x40000, 0x14778336},
    {"no-5.bin", 0x8000, 0x48000, 0x147dd23b},
    {"ng-5.bin", 0x8000, 0x50000, 0xa8ee3720},
    {"nm-5.bin", 0x8000, 0x58000, 0xc100258e},
};
const std::vector<RomEntry> kAdpcmRoms = {
    {"n3-5.ic33", 0x8000, 0x0000, 0x78fd6190},
    {"n4-5.ic32", 0x8000, 0x8000, 0x6557564c},
    {"n5-5.ic31", 0x8000, 0x10000, 0x7ee43a3c},
};

constexpr uint8_t pal4bit(uint8_t n) {
    n &= 0x0f;
    return uint8_t(n | (n << 4));
}

void decode_chars(GfxSet& out, const std::vector<uint8_t>& rom) {
    GfxLayout layout;
    layout.width = 8;
    layout.height = 8;
    layout.total = 0x400;
    layout.planes = 3;
    layout.char_increment = 32 * 8;
    layout.plane_offsets = {2, 4, 6};
    layout.x_offsets = {1, 0, 65, 64, 129, 128, 193, 192};
    layout.y_offsets = {0 * 8, 1 * 8, 2 * 8, 3 * 8, 4 * 8, 5 * 8, 6 * 8, 7 * 8};
    out.decode(layout, rom);
}

// Pascal converts 4 chunks of 0x400 tiles per 0x18000 ROM half (2 halves).
void decode_tiles_or_sprites(GfxSet& out, const std::vector<uint8_t>& rom, int total,
                             bool /*sprites*/) {
    out.create(16, 16, total);
    const int halves = total / 0x400;  // 2 for tiles (0x800), 4 for sprites (0x1000)
    static const int kPtX[16] = {
        3, 2, 1, 0, 16 * 8 + 3, 16 * 8 + 2, 16 * 8 + 1, 16 * 8 + 0,
        32 * 8 + 3, 32 * 8 + 2, 32 * 8 + 1, 32 * 8 + 0,
        48 * 8 + 3, 48 * 8 + 2, 48 * 8 + 1, 48 * 8 + 0};
    static const int kPtY[16] = {
        0 * 8, 1 * 8, 2 * 8, 3 * 8, 4 * 8, 5 * 8, 6 * 8, 7 * 8,
        8 * 8, 9 * 8, 10 * 8, 11 * 8, 12 * 8, 13 * 8, 14 * 8, 15 * 8};

    auto make_layout = [&](int p0, int p1, int p2) {
        GfxLayout layout;
        layout.width = 16;
        layout.height = 16;
        layout.total = 0x100;  // one sub-batch
        layout.planes = 3;
        layout.char_increment = 64 * 8;
        layout.plane_offsets = {p0, p1, p2};
        layout.x_offsets.assign(kPtX, kPtX + 16);
        layout.y_offsets.assign(kPtY, kPtY + 16);
        return layout;
    };

    // Per half: 4 plane configurations into consecutive 0x100 blocks → 0x400 tiles
    for (int f = 0; f < halves; f++) {
        std::vector<uint8_t> chunk(rom.begin() + f * 0x18000,
                                   rom.begin() + f * 0x18000 + 0x18000);
        const int base = f * 0x400;
        out.decode_elements(make_layout(4, 0x8000 * 8 + 0, 0x8000 * 8 + 4), chunk, base + 0x000);
        out.decode_elements(make_layout(0, 0xc000 * 8 + 0, 0xc000 * 8 + 4), chunk, base + 0x100);
        out.decode_elements(make_layout(0x4000 * 8 + 4, 0x10000 * 8 + 0, 0x10000 * 8 + 4), chunk,
                            base + 0x200);
        out.decode_elements(make_layout(0x4000 * 8 + 0, 0x14000 * 8 + 0, 0x14000 * 8 + 4), chunk,
                            base + 0x300);
    }
}

void blit_trans(uint32_t* dest, int dest_w, int dest_h, int dx, int dy, const uint8_t* src,
                int sw, int sh, const uint32_t* pal, int color_base, bool flip_x, bool flip_y) {
    for (int y = 0; y < sh; y++) {
        const int sy = flip_y ? (sh - 1 - y) : y;
        const int py = dy + y;
        if (py < 0 || py >= dest_h) continue;
        for (int x = 0; x < sw; x++) {
            const int sx = flip_x ? (sw - 1 - x) : x;
            const uint8_t pix = src[sy * sw + sx];
            if (pix == 0) continue;
            const int px = dx + x;
            if (px < 0 || px >= dest_w) continue;
            dest[py * dest_w + px] = pal[(color_base + pix) & 0xff];
        }
    }
}

}  // namespace

Renegade::Renegade()
    : main_cpu_(kMainClock, M6502::Type::Nmos),
      sound_cpu_(kSoundClock),
      mcu_(kMcuClock, Taito68705::Type::Standard),
      ym_(kYmClock, YM3812::kYM3526),
      msm_(kMsmClock, 48, 4) {
    bg_layer_.assign(1024 * 256, 0);
    fg_layer_.assign(256 * 256, 0);
    sprite_layer_.assign(256 * 256, 0);
    framebuffer_.assign(size_t(kScreenWidth) * kScreenHeight, 0);

    main_cpu_.set_memory_handlers([this](uint16_t a) { return main_read(a); },
                                  [this](uint16_t a, uint8_t v) { main_write(a, v); });
    sound_cpu_.set_memory_handlers([this](uint16_t a) { return sound_read(a); },
                                   [this](uint16_t a, uint8_t v) { sound_write(a, v); });
    ym_.set_irq_handler([this](bool state) {
        sound_cpu_.set_firq(state ? IrqLine::Assert : IrqLine::Clear);
    });
    msm_.set_vclk_handler([this]() { msm_vclk(); });
}

bool Renegade::init(const std::string& rom_path, std::string* error) {
    RomLoader loader;
    if (!loader.open(rom_path, error)) return false;

    std::vector<uint8_t> main(0x10000, 0);
    if (!loader.load(kMainRoms, main, error)) return false;
    std::memcpy(memory_.data() + 0x8000, main.data() + 0x8000, 0x8000);
    std::memcpy(rom_bank_[0].data(), main.data() + 0x0000, 0x4000);
    std::memcpy(rom_bank_[1].data(), main.data() + 0x4000, 0x4000);

    std::vector<uint8_t> sound(0x10000, 0);
    if (!loader.load(kSoundRom, sound, error)) return false;
    std::copy(sound.begin(), sound.end(), sound_mem_.begin());

    std::vector<uint8_t> mcu(0x800, 0);
    if (!loader.load(kMcuRom, mcu, error)) return false;
    std::memcpy(mcu_.rom_data(), mcu.data(), 0x800);

    std::vector<uint8_t> chars(0x8000, 0);
    if (!loader.load(kCharRom, chars, error)) return false;
    decode_chars(chars_, chars);

    std::vector<uint8_t> tiles(0x30000, 0);
    if (!loader.load(kTileRoms, tiles, error)) return false;
    decode_tiles_or_sprites(tiles_, tiles, 0x800, false);

    std::vector<uint8_t> sprites(0x60000, 0);
    if (!loader.load(kSpriteRoms, sprites, error)) return false;
    decode_tiles_or_sprites(sprites_, sprites, 0x1000, true);

    std::vector<uint8_t> adpcm(0x18000, 0);
    if (!loader.load(kAdpcmRoms, adpcm, error)) return false;
    msm_.set_rom(std::move(adpcm));

    warnings_ = loader.warnings();
    reset();
    return true;
}

void Renegade::reset() {
    main_cpu_.reset();
    sound_cpu_.reset();
    mcu_.reset();
    ym_.reset();
    msm_.reset();
    msm_.set_reset(true);
    in0_ = in1_ = 0xff;
    rom_bank_sel_ = 0;
    sound_latch_ = 0;
    scroll_x_ = 0;
    scroll_comp_ = 256;
    audio_accum_ = 0;
    audio_.clear();
    palette_ram_.fill(0);
    palette_.fill(0xff000000u);
    std::fill(bg_layer_.begin(), bg_layer_.end(), 0);
    std::fill(fg_layer_.begin(), fg_layer_.end(), 0);
    std::fill(framebuffer_.begin(), framebuffer_.end(), 0);
}

uint8_t Renegade::main_read(uint16_t address) {
    if (address <= 0x1fff || (address >= 0x2800 && address <= 0x2fff) || address >= 0x8000) {
        return memory_[address];
    }
    if (address >= 0x2000 && address <= 0x27ff) return memory_[0x2000 + (address & 0x1ff)];
    if (address >= 0x3000 && address <= 0x31ff) return palette_ram_[address & 0x1ff];
    if (address == 0x3800) return in0_;
    if (address == 0x3801) return in1_;
    if (address == 0x3802) {
        return uint8_t(dswb_ | ((!mcu_.main_sent()) << 4) | ((!mcu_.mcu_sent()) << 5));
    }
    if (address == 0x3803) return dswa_;
    if (address == 0x3804) return mcu_.read();
    if (address == 0x3805) {
        // Pascal: change_reset(PULSE_LINE) as side-effect of the read
        mcu_.set_reset(true);
        mcu_.set_reset(false);
        return 0;
    }
    if (address >= 0x4000 && address <= 0x7fff) {
        return rom_bank_[rom_bank_sel_ & 1][address & 0x3fff];
    }
    return 0xff;
}

void Renegade::main_write(uint16_t address, uint8_t value) {
    if (address <= 0x17ff) {
        memory_[address] = value;
        return;
    }
    if (address >= 0x1800 && address <= 0x1fff) {
        memory_[address] = value;
        return;
    }
    if (address >= 0x2000 && address <= 0x27ff) {
        memory_[0x2000 + (address & 0x1ff)] = value;
        return;
    }
    if (address >= 0x2800 && address <= 0x2fff) {
        memory_[address] = value;
        return;
    }
    if (address >= 0x3000 && address <= 0x31ff) {
        const uint8_t idx = uint8_t(address & 0xff);
        palette_ram_[address & 0x1ff] = value;
        update_palette(idx);
        return;
    }
    if (address == 0x3800) {
        scroll_x_ = uint16_t((scroll_x_ & 0xff00) | value);
        return;
    }
    if (address == 0x3801) {
        scroll_x_ = uint16_t((scroll_x_ & 0x00ff) | (value << 8));
        return;
    }
    if (address == 0x3802) {
        sound_latch_ = value;
        sound_cpu_.set_irq(IrqLine::Assert);
        return;
    }
    if (address == 0x3803) {
        if ((value & 1) == 0) {
            scroll_comp_ = 0;
        } else {
            scroll_comp_ = 256;
        }
        return;
    }
    if (address == 0x3804) {
        mcu_.write(value);
        return;
    }
    if (address == 0x3805) {
        rom_bank_sel_ = uint8_t(value & 1);
        return;
    }
    if (address == 0x3806) {
        main_cpu_.set_nmi(IrqLine::Clear);
        return;
    }
    if (address == 0x3807) {
        main_cpu_.set_irq(IrqLine::Clear);
        return;
    }
}

uint8_t Renegade::sound_read(uint16_t address) {
    if (address <= 0x0fff || address >= 0x8000) return sound_mem_[address];
    if (address == 0x1000) {
        sound_cpu_.set_irq(IrqLine::Clear);
        return sound_latch_;
    }
    if (address == 0x2800) return ym_.status();
    return 0xff;
}

void Renegade::sound_write(uint16_t address, uint8_t value) {
    if (address <= 0x0fff) {
        sound_mem_[address] = value;
        return;
    }
    if (address == 0x1800) {
        msm_.set_reset(false);  // start
        return;
    }
    if (address == 0x2000) {
        // Pascal uses nibble addressing; our MSM5205 uses byte position.
        uint32_t pos = 0;
        switch (value & 0x1c) {
            case 0x0c: pos = 2 * 0x8000; break;  // ic31
            case 0x14: pos = 1 * 0x8000; break;  // ic32
            case 0x18: pos = 0 * 0x8000; break;  // ic33
            default:
                msm_.set_start(0);
                msm_.set_end(0);
                return;
        }
        pos |= uint32_t(value & 3) * 0x2000;
        msm_.set_start(pos);
        msm_.set_end(pos + 0x2000);
        return;
    }
    if (address == 0x2800) {
        ym_.control(value);
        return;
    }
    if (address == 0x2801) {
        ym_.write(value);
        return;
    }
    if (address == 0x3000) {
        msm_.set_reset(true);  // stop
        return;
    }
}

void Renegade::msm_vclk() {
    if (msm_.idle()) return;
    // Feed next nibble from ROM (Pascal snd_adpcm)
    // MSM5205 internal ROM mode may already advance; we stream via data_w.
    // Our chip uses position_/end_ in vclk when ROM is set — ensure it works.
    // If idle after vclk ends sample, pulse NMI on sound CPU.
}

void Renegade::on_sound_cycles(int cycles) {
    const uint32_t msm_rate = msm_.sample_frequency();
    static int64_t msm_accum = 0;
    if (msm_rate > 0 && !msm_.idle()) {
        msm_accum += int64_t(cycles) * msm_rate;
        while (msm_accum >= kSoundClock) {
            msm_accum -= kSoundClock;
            // Streamed ADPCM: feed nibbles like Pascal
            // Use ROM-driven path via vclk which advances position_
            msm_.vclk();
            if (msm_.idle()) {
                sound_cpu_.set_nmi(IrqLine::Pulse);
            }
        }
    }
    audio_accum_ += int64_t(cycles) * YM3812::kSampleRate;
    while (audio_accum_ >= kSoundClock) {
        audio_accum_ -= kSoundClock;
        int32_t sample = ym_.update();
        sample += msm_.output();
        audio_.push_back(int16_t(std::clamp(sample, int32_t(-32768), int32_t(32767))));
    }
}

void Renegade::update_palette(uint8_t index) {
    const uint8_t lo = palette_ram_[index];
    const uint8_t hi = palette_ram_[index + 0x100];
    const uint8_t r = pal4bit(lo);
    const uint8_t g = pal4bit(lo >> 4);
    const uint8_t b = pal4bit(hi);
    palette_[index] = 0xff000000u | (uint32_t(r) << 16) | (uint32_t(g) << 8) | b;
}

void Renegade::update_video() {
    std::fill(bg_layer_.begin(), bg_layer_.end(), 0);
    std::fill(fg_layer_.begin(), fg_layer_.end(), 0);
    std::fill(sprite_layer_.begin(), sprite_layer_.end(), 0);

    // Background 64x16 tiles of 16x16
    for (int f = 0; f < 0x400; f++) {
        const uint8_t atrib = memory_[0x2c00 + f];
        const int color = atrib >> 5;
        const int x = f % 64;
        const int y = f / 64;
        const int nchar = memory_[0x2800 + f] + ((atrib & 7) << 8);
        blit_trans(bg_layer_.data(), 1024, 256, x * 16, y * 16, tiles_.element(nchar), 16, 16,
                   palette_.data(), (color << 3) + 192, false, false);
    }

    // Foreground 32x32 chars of 8x8
    for (int f = 0; f < 0x400; f++) {
        const uint8_t atrib = memory_[0x1c00 + f];
        const int color = atrib >> 6;
        const int x = f % 32;
        const int y = f / 32;
        const int nchar = memory_[0x1800 + f] + ((atrib & 3) << 8);
        blit_trans(fg_layer_.data(), 256, 256, x * 8, y * 8, chars_.element(nchar), 8, 8,
                   palette_.data(), color << 3, false, false);
    }

    // Sprites
    for (int f = 0; f < 0x80; f++) {
        const int y = 224 - memory_[0x2000 + f * 4];
        if (y < 16) continue;
        const uint8_t atrib = memory_[0x2001 + f * 4];
        const int x = memory_[0x2003 + f * 4];
        int nchar = memory_[0x2002 + f * 4] + ((atrib & 0x0f) << 8);
        const int color = ((atrib & 0x30) >> 1) + 128;
        const bool flip_x = (atrib & 0x40) != 0;
        if (atrib & 0x80) {
            nchar &= ~1;
            blit_trans(sprite_layer_.data(), 256, 256, x, y + 16, sprites_.element(nchar | 1), 16,
                       16, palette_.data(), color, flip_x, false);
            blit_trans(sprite_layer_.data(), 256, 256, x, y, sprites_.element(nchar), 16, 16,
                       palette_.data(), color, flip_x, false);
        } else {
            blit_trans(sprite_layer_.data(), 256, 256, x, y + 16, sprites_.element(nchar), 16, 16,
                       palette_.data(), color, flip_x, false);
        }
    }

    // Compose: scrolled BG, then FG, then sprites. Visible 256x238 starting at y=9.
    const int scroll = (scroll_x_ - scroll_comp_) & 0x3ff;
    for (int y = 0; y < kScreenHeight; y++) {
        const int sy = y + 9;
        for (int x = 0; x < kScreenWidth; x++) {
            uint32_t pix = bg_layer_[sy * 1024 + ((x + scroll) & 0x3ff)];
            const uint32_t fg = fg_layer_[sy * 256 + x];
            if (fg) pix = fg;
            const uint32_t sp = sprite_layer_[sy * 256 + x];
            if (sp) pix = sp;
            framebuffer_[size_t(y * kScreenWidth + x)] = pix ? pix : 0xff000000u;
        }
    }
}

void Renegade::run_frame() {
    const int total = int(kMainClock / kFramesPerSecond);
    const int slice = std::max(1, total / kScanlines);
    const int sound_slice = std::max(1, int(kSoundClock / kFramesPerSecond) / kScanlines);
    const int mcu_slice = std::max(1, int(kMcuClock / kFramesPerSecond) / kScanlines);

    for (int line = 0; line < kScanlines; line++) {
        // IRQs on specific scanlines (Pascal principal_renegade)
        switch (line) {
            case 16:
            case 40:
            case 56:
            case 72:
            case 88:
            case 104:
            case 120:
            case 136:
            case 152:
            case 168:
            case 184:
            case 200:
            case 216:
            case 232:
            case 248:
            case 264:
                main_cpu_.set_irq(IrqLine::Assert);
                break;
            case 19:
                dswb_ = uint8_t(dswb_ & ~0x40);
                break;
            case 257:
                update_video();
                dswb_ = uint8_t(dswb_ | 0x40);
                break;
            case 265:
                main_cpu_.set_nmi(IrqLine::Assert);
                break;
            default:
                break;
        }
        main_cpu_.run(slice);
        sound_cpu_.run(sound_slice);
        on_sound_cycles(sound_slice);
        mcu_.run(mcu_slice);
    }
}

void Renegade::set_inputs(const MachineInputs& inputs) {
    in0_ = 0xff;
    in1_ = 0xff;
    const auto& p1 = inputs.player1;
    const auto& p2 = inputs.player2;
    if (p1.right) in0_ &= ~0x01;
    if (p1.left) in0_ &= ~0x02;
    if (p1.up) in0_ &= ~0x04;
    if (p1.down) in0_ &= ~0x08;
    if (p1.button1) in0_ &= ~0x10;
    if (p1.button2) in0_ &= ~0x20;
    if (p1.start) in0_ &= ~0x40;
    if (p2.start) in0_ &= ~0x80;

    if (p2.right) in1_ &= ~0x01;
    if (p2.left) in1_ &= ~0x02;
    if (p2.up) in1_ &= ~0x04;
    if (p2.down) in1_ &= ~0x08;
    if (p2.button1) in1_ &= ~0x10;
    if (p2.button2) in1_ &= ~0x20;
    if (inputs.coin1) in1_ &= ~0x40;
    if (inputs.coin2) in1_ &= ~0x80;

    // Extra buttons on dswb bits 2/3 (active low)
    if (p1.button3) dswb_ &= ~0x04;
    else dswb_ |= 0x04;
    if (p2.button3) dswb_ &= ~0x08;
    else dswb_ |= 0x08;
}

void Renegade::set_dip_switch(int bank, uint8_t value) {
    if (bank == 0) dswa_ = value;
    if (bank == 1) dswb_ = uint8_t((dswb_ & 0x40) | (value & ~0x40));  // preserve vblank bit
}

void Renegade::drain_audio(std::vector<int16_t>& out) {
    out.insert(out.end(), audio_.begin(), audio_.end());
    audio_.clear();
}

}  // namespace dsp
