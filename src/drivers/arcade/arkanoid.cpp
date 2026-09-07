#include "drivers/arcade/arkanoid.h"

#include <algorithm>
#include <cstring>
#include <cstdio>

#include "core/rom_loader.h"
#include "cpu/irq_line.h"

namespace dsp {
namespace {

const std::vector<RomEntry> kMainRoms = {
    {"a75-01-1.ic17|a75__01-1.ic17|a75-01.ic17", 0x8000, 0x0000, 0x5bcda3b0},
    {"a75-11.ic16|a75__11.ic16", 0x8000, 0x8000, 0xeafd7191},
};
const std::vector<RomEntry> kMcuRom = {
    {"a75__06.ic14|a75-06.ic14|a75-06__bootleg_68705.ic14|arkanoid_mcu.ic14", 0x800, 0,
     0x0be83647},
};
const std::vector<RomEntry> kTileRoms = {
    {"a75-03.ic64|a75__03.ic64", 0x8000, 0x00000, 0x038b74ba},
    {"a75-04.ic63|a75__04.ic63", 0x8000, 0x08000, 0x71fae199},
    {"a75-05.ic62|a75__05.ic62", 0x8000, 0x10000, 0xc76374e2},
};
const std::vector<RomEntry> kProms = {
    {"a75-07.ic24", 0x200, 0x000, 0x0af8b289},
    {"a75-08.ic23", 0x200, 0x200, 0xabb002fb},
    {"a75-09.ic22", 0x200, 0x400, 0xa7c6c277},
};

constexpr uint8_t pal4bit(uint8_t n) {
    n &= 0x0f;
    return uint8_t(n | (n << 4));
}

void decode_tiles(GfxSet& out, const std::vector<uint8_t>& rom) {
    // 8x8, 0x1000 chars, 3bpp; plane offsets: 2*4096*8*8, 4096*8*8, 0
    // convert_gfx(..., true, false) → rotate_cw for vertical monitor layout
    GfxLayout layout;
    layout.width = 8;
    layout.height = 8;
    layout.total = 0x1000;
    layout.planes = 3;
    layout.char_increment = 8 * 8;
    layout.rotate_cw = true;
    layout.plane_offsets = {2 * 4096 * 8 * 8, 4096 * 8 * 8, 0};
    layout.x_offsets = {0, 1, 2, 3, 4, 5, 6, 7};
    layout.y_offsets = {0 * 8, 1 * 8, 2 * 8, 3 * 8, 4 * 8, 5 * 8, 6 * 8, 7 * 8};
    out.decode(layout, rom);
}

void blit_opaque(uint32_t* dest, int dest_w, int dest_h, int dx, int dy, const uint8_t* src,
                 int sw, int sh, const uint32_t* pal, int color_base) {
    for (int y = 0; y < sh; y++) {
        const int py = dy + y;
        if (py < 0 || py >= dest_h) continue;
        for (int x = 0; x < sw; x++) {
            const int px = dx + x;
            if (px < 0 || px >= dest_w) continue;
            const uint8_t pix = src[y * sw + x];
            dest[py * dest_w + px] = pal[(color_base + pix) & 0x1ff];
        }
    }
}

void blit_trans(uint32_t* dest, int dest_w, int dest_h, int dx, int dy, const uint8_t* src, int sw,
                int sh, const uint32_t* pal, int color_base) {
    for (int y = 0; y < sh; y++) {
        const int py = dy + y;
        if (py < 0 || py >= dest_h) continue;
        for (int x = 0; x < sw; x++) {
            const int px = dx + x;
            if (px < 0 || px >= dest_w) continue;
            const uint8_t pix = src[y * sw + x];
            if (pix == 0) continue;
            dest[py * dest_w + px] = pal[(color_base + pix) & 0x1ff];
        }
    }
}

}  // namespace

Arkanoid::Arkanoid()
    : cpu_(kCpuClock),
      mcu_(kMcuClock, Taito68705::Type::Arkanoid),
      ay_(kAyClock, 1.5f) {
    layer_.assign(256 * 256, 0);
    sprite_layer_.assign(256 * 256, 0);
    framebuffer_.assign(size_t(kScreenWidth) * kScreenHeight, 0xff000000u);

    cpu_.set_memory_handlers([this](uint16_t a) { return cpu_read(a); },
                             [this](uint16_t a, uint8_t v) { cpu_write(a, v); });
    mcu_.set_arkanoid_read([this]() { return arkanoid_paddle(); });
    ay_.set_port_handlers(
        []() { return uint8_t(0xff); },
        [this]() { return dswa_; },
        nullptr, nullptr);
}

bool Arkanoid::init(const std::string& rom_path, std::string* error) {
    RomLoader loader;
    if (!loader.open(rom_path, error)) return false;

    std::vector<uint8_t> main(0x10000, 0);
    if (!loader.load(kMainRoms, main, error)) return false;
    std::copy(main.begin(), main.end(), memory_.begin());

    std::vector<uint8_t> mcu(0x800, 0);
    if (!loader.load(kMcuRom, mcu, error)) return false;
    std::memcpy(mcu_.rom_data(), mcu.data(), 0x800);

    std::vector<uint8_t> tiles(0x18000, 0);
    if (!loader.load(kTileRoms, tiles, error)) return false;
    decode_tiles(tiles_, tiles);

    std::vector<uint8_t> prom(0x600, 0);
    if (!loader.load(kProms, prom, error)) return false;
    for (int i = 0; i < 0x200; i++) {
        const uint8_t r = pal4bit(prom[size_t(i)]);
        const uint8_t g = pal4bit(prom[size_t(i + 0x200)]);
        const uint8_t b = pal4bit(prom[size_t(i + 0x400)]);
        palette_[size_t(i)] = 0xff000000u | (uint32_t(r) << 16) | (uint32_t(g) << 8) | b;
    }

    warnings_ = loader.warnings();
    reset();
    return true;
}

void Arkanoid::reset() {
    cpu_.reset();
    mcu_.reset();
    ay_.reset();
    in0_ = 0x0f;
    in1_ = 0xff;
    palette_bank_ = 0;
    gfx_bank_ = 0;
    paddle_select_ = 0;
    paddle_x_[0] = paddle_x_[1] = 0x7f;
    audio_accum_ = 0;
    audio_.clear();
    std::fill(layer_.begin(), layer_.end(), 0);
    std::fill(sprite_layer_.begin(), sprite_layer_.end(), 0);
    std::fill(framebuffer_.begin(), framebuffer_.end(), 0xff000000u);
}

uint8_t Arkanoid::arkanoid_paddle() const {
    return paddle_x_[paddle_select_ & 1];
}

uint8_t Arkanoid::cpu_read(uint16_t address) {
    if (address <= 0xbfff || (address >= 0xe000 && address <= 0xefff)) {
        return memory_[address];
    }
    if (address >= 0xc000 && address <= 0xcfff) {
        return memory_[0xc000 + (address & 0x7ff)];
    }
    if (address >= 0xd000 && address <= 0xdfff) {
        const uint8_t off = uint8_t(address & 0x1f);
        if (off == 1 || off == 3 || off == 5 || off == 7) return ay_.read();
        if (off >= 8 && off <= 0x0b) return 0xff;
        if (off >= 0x0c && off <= 0x0f) {
            return uint8_t((!mcu_.mcu_sent() ? 0x80 : 0) | (!mcu_.main_sent() ? 0x40 : 0) | in0_);
        }
        if (off >= 0x10 && off <= 0x17) return in1_;
        if (off >= 0x18 && off <= 0x1f) return mcu_.read();
    }
    return 0;
}

void Arkanoid::cpu_write(uint16_t address, uint8_t value) {
    if (address <= 0xbfff) return;  // ROM
    if (address >= 0xc000 && address <= 0xcfff) {
        memory_[0xc000 + (address & 0x7ff)] = value;
        return;
    }
    if (address >= 0xd000 && address <= 0xdfff) {
        const uint8_t off = uint8_t(address & 0x1f);
        if (off == 0 || off == 2 || off == 4 || off == 6) {
            ay_.control(value);
            return;
        }
        if (off == 1 || off == 3 || off == 5 || off == 7) {
            ay_.write(value);
            return;
        }
        if (off >= 8 && off <= 0x0f) {
            paddle_select_ = uint8_t((value >> 2) & 1);
            const uint8_t new_gfx = uint8_t((value >> 5) & 1);
            const uint8_t new_pal = uint8_t((value & 0x40) >> 1);  // 0 or 0x20
            if (gfx_bank_ != new_gfx || palette_bank_ != new_pal) {
                gfx_bank_ = new_gfx;
                palette_bank_ = new_pal;
            }
            // bit7: MCU reset (0 = held, 1 = run)
            mcu_.set_reset((value & 0x80) == 0);
            return;
        }
        if (off >= 0x18 && off <= 0x1f) {
            mcu_.write(value);
            return;
        }
        return;
    }
    if (address >= 0xe000 && address <= 0xe7ff) {
        memory_[address] = value;
        return;
    }
    if (address >= 0xe800) {
        memory_[address] = value;
        return;
    }
}

void Arkanoid::update_video() {
    std::fill(layer_.begin(), layer_.end(), 0xff000000u);
    std::fill(sprite_layer_.begin(), sprite_layer_.end(), 0);

    // Background tiles: 32×32 map, rotated placement
    for (int f = 0; f < 0x400; f++) {
        const int x = 31 - (f >> 5);
        const int y = f & 0x1f;
        const uint8_t atrib = memory_[0xe000 + f * 2];
        const int color = ((atrib & 0xf8) >> 3) + palette_bank_;
        const int nchar =
            memory_[0xe001 + f * 2] + ((atrib & 7) << 8) + 2048 * gfx_bank_;
        blit_opaque(layer_.data(), 256, 256, x * 8, y * 8, tiles_.element(nchar), 8, 8,
                    palette_.data(), color << 3);
    }

    // Sprites: 16 entries, each two 8×8 tiles side by side → 16×8
    for (int f = 0; f < 16; f++) {
        const uint8_t atrib = memory_[0xe802 + f * 4];
        const int nchar =
            memory_[0xe803 + f * 4] + ((atrib & 3) << 8) + 1024 * gfx_bank_;
        const int color = ((atrib & 0xf8) >> 3) + palette_bank_;
        const int x = memory_[0xe801 + f * 4];
        const int y = memory_[0xe800 + f * 4];
        blit_trans(sprite_layer_.data(), 256, 256, x + 8, y, tiles_.element(2 * nchar), 8, 8,
                   palette_.data(), color << 3);
        blit_trans(sprite_layer_.data(), 256, 256, x, y, tiles_.element(2 * nchar + 1), 8, 8,
                   palette_.data(), color << 3);
    }

    // Visible window: skip left 16 pixels of the 256 layer → 224×256
    for (int y = 0; y < kScreenHeight; y++) {
        for (int x = 0; x < kScreenWidth; x++) {
            const int sx = x + 16;
            uint32_t pix = layer_[y * 256 + sx];
            const uint32_t sp = sprite_layer_[y * 256 + sx];
            if (sp) pix = sp;
            framebuffer_[size_t(y * kScreenWidth + x)] = pix;
        }
    }
}

void Arkanoid::on_cpu_cycles(int cycles) {
    audio_accum_ += int64_t(cycles) * AY8910::kSampleRate;
    while (audio_accum_ >= kCpuClock) {
        audio_accum_ -= kCpuClock;
        audio_.push_back(int16_t(std::clamp(ay_.update(), int32_t(-32768), int32_t(32767))));
    }
}

void Arkanoid::run_frame() {
    const int total = int(kCpuClock / kFramesPerSecond);
    const int slice = std::max(1, total / kScanlines);
    const int mcu_slice = std::max(1, int(kMcuClock / kFramesPerSecond) / kScanlines);

    for (int line = 0; line < kScanlines; line++) {
        if (line == 240) {
            cpu_.set_irq(IrqLine::Pulse);
            update_video();
        }
        cpu_.run(slice);
        on_cpu_cycles(slice);
        mcu_.run(mcu_slice);
    }
    // Ensure last frame is presented for screenshots
    update_video();
}

void Arkanoid::set_inputs(const MachineInputs& inputs) {
    in0_ = 0x0f;
    in1_ = 0xff;
    if (inputs.player1.start) in0_ &= ~0x01;
    if (inputs.player2.start) in0_ &= ~0x02;
    if (inputs.coin1) in0_ |= 0x10;
    if (inputs.coin2) in0_ |= 0x20;

    if (inputs.player1.button1 || inputs.pointer_button1) in1_ &= ~0x01;
    if (inputs.player2.button1) in1_ &= ~0x02;

    // Paddle: mouse X maps 0..255; keyboard left/right nudges
    if (inputs.has_pointer) {
        const int x = std::clamp(inputs.pointer_x * 256 / std::max(1, kScreenWidth), 0, 255);
        paddle_x_[0] = uint8_t(x);
        paddle_x_[1] = uint8_t(x);
    } else {
        if (inputs.player1.left) {
            paddle_x_[0] = uint8_t(std::max(0, int(paddle_x_[0]) - 8));
        }
        if (inputs.player1.right) {
            paddle_x_[0] = uint8_t(std::min(255, int(paddle_x_[0]) + 8));
        }
        if (inputs.player2.left) {
            paddle_x_[1] = uint8_t(std::max(0, int(paddle_x_[1]) - 8));
        }
        if (inputs.player2.right) {
            paddle_x_[1] = uint8_t(std::min(255, int(paddle_x_[1]) + 8));
        }
    }
}

void Arkanoid::set_dip_switch(int bank, uint8_t value) {
    if (bank == 0) dswa_ = value;
}

void Arkanoid::drain_audio(std::vector<int16_t>& out) {
    out.insert(out.end(), audio_.begin(), audio_.end());
    audio_.clear();
}

}  // namespace dsp
