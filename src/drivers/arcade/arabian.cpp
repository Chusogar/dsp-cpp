#include "drivers/arcade/arabian.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "core/rom_loader.h"
#include "cpu/irq_line.h"

namespace dsp {
namespace {

const std::vector<RomEntry> kMain = {
    {"ic1rev2.87", 0x2000, 0x0000, 0x5e1c98b8},
    {"ic2rev2.88", 0x2000, 0x2000, 0x092f587e},
    {"ic3rev2.89", 0x2000, 0x4000, 0x15145f23},
    {"ic4rev2.90", 0x2000, 0x6000, 0x32b77b44},
};
const std::vector<RomEntry> kMcu = {{"sun-8212.ic3", 0x800, 0x0000, 0x8869611e}};
const std::vector<RomEntry> kGfx = {
    {"tvg-91.ic84", 0x2000, 0x0000, 0xc4637822},
    {"tvg-92.ic85", 0x2000, 0x2000, 0xf7c6866d},
    {"tvg-93.ic86", 0x2000, 0x4000, 0x71acd48d},
    {"tvg-94.ic87", 0x2000, 0x6000, 0x82160b9a},
};

}  // namespace

Arabian::Arabian() : cpu_(kCpuClock), mcu_(Mb88::Type::Mb8841, kMcuClock), ay_(kAyClock) {
    framebuffer_.assign(size_t(kScreenWidth) * kScreenHeight, 0xff000000u);
    cpu_.set_memory_handlers([this](uint16_t a) { return main_read(a); },
                             [this](uint16_t a, uint8_t v) { main_write(a, v); });
    cpu_.set_io_handlers([](uint16_t) { return uint8_t(0xff); },
                         [this](uint16_t p, uint8_t v) { main_out(p, v); });
    // Port A: palette bank (video control). Port B: MCU /IRQ (bit 5) and /RESET (bit 4).
    ay_.set_port_handlers(nullptr, nullptr, [this](uint8_t v) { video_control_ = uint8_t(v >> 3); },
                          [this](uint8_t v) {
                              mcu_.set_irq((v & 0x20) == 0 ? IrqLine::Assert : IrqLine::Clear);
                              mcu_.set_reset_line((v & 0x10) == 0);
                          });
    mcu_.set_k_read([this] { return mcu_k_read(); });
    // This MB88 core reports the whole O latch (both nibbles) on every write.
    mcu_.set_o_write([this](uint8_t v) { mcu_port_o_ = v; });
    mcu_.set_p_write([this](uint8_t v) { mcu_port_p_ = uint8_t(v & 0x0f); });
    for (int i = 0; i < 4; ++i) {
        mcu_.set_r_read(i, [this, i] { return mcu_r_read(i); });
        mcu_.set_r_write(i, [this, i](uint8_t v) { mcu_r_write(i, v); });
    }
    create_palette();
}

bool Arabian::init(const std::string& rom_path, std::string* error) {
    RomLoader loader;
    if (!loader.open(rom_path, error)) return false;

    std::vector<uint8_t> main(0x8000, 0);
    if (!loader.load(kMain, main, error)) return false;
    std::copy(main.begin(), main.end(), rom_.begin());

    std::vector<uint8_t> mcu(0x800, 0);
    if (!loader.load(kMcu, mcu, error)) return false;
    mcu_.set_program_rom(mcu.data(), mcu.size());

    std::vector<uint8_t> gfx(0x8000, 0);
    if (!loader.load(kGfx, gfx, error)) return false;
    // convert_gfx_arabian: two ROM halves give 4 bits per pixel, 4 pixels per byte pair.
    for (int f = 0; f < 0x4000; ++f) {
        uint8_t v1 = gfx[size_t(f)];
        uint8_t v2 = gfx[size_t(f) + 0x4000];
        for (int p = 3; p >= 0; --p) {
            gfx_[size_t(f) * 4 + size_t(p)] =
                uint8_t((v1 & 1) | ((v1 & 0x10) >> 3) | ((v2 & 1) << 2) | ((v2 & 0x10) >> 1));
            v1 >>= 1;
            v2 >>= 1;
        }
    }

    warnings_ = loader.warnings();
    reset();
    return true;
}

void Arabian::reset() {
    cpu_.reset();
    mcu_.reset();
    mcu_.set_reset_line(false);
    ay_.reset();
    ram_.fill(0);
    video_ram_.fill(0);
    blitter_.fill(0);
    video_control_ = 0;
    flip_screen_ = false;
    mcu_port_p_ = 0;
    mcu_port_o_ = 0;
    mcu_port_r_.fill(0);
    in0_ = 1;
    in1_ = in2_ = in3_ = 0;
    mcu_acc_ = 0;
    audio_accum_ = 0;
    audio_.clear();
    std::fill(framebuffer_.begin(), framebuffer_.end(), 0xff000000u);
}

// ---------------------------------------------------------------------------
// Main CPU

uint8_t Arabian::main_read(uint16_t address) {
    if (address < 0x8000) return rom_[address];
    if (address >= 0xc000 && address <= 0xc1ff) return in3_;
    if (address >= 0xc200 && address <= 0xc3ff) return dsw_a_;
    if (address >= 0xd000 && address <= 0xdfff) return ram_[address & 0x7ff];
    return 0xff;
}

void Arabian::main_write(uint16_t address, uint8_t value) {
    if (address < 0x8000) return;
    if (address < 0xc000) {
        video_ram_write(address, value);
    } else if (address >= 0xd000 && address <= 0xdfff) {
        ram_[address & 0x7ff] = value;
    } else if (address >= 0xe000 && address <= 0xefff) {
        blitter_[address & 7] = value;
        if ((address & 7) == 6) {
            blit_area(blitter_[0], uint16_t(blitter_[1] | (blitter_[2] << 8)), uint8_t(blitter_[4] << 2),
                      blitter_[3], blitter_[6], blitter_[5]);
        }
    }
}

void Arabian::main_out(uint16_t port, uint8_t value) {
    if (port >= 0xc800 && port <= 0xc9ff) ay_.control(value);
    else if (port >= 0xca00 && port <= 0xcbff) ay_.write(value);
}

// Direct CPU write: one byte sets 4 pixels in the planes enabled by blitter[0].
void Arabian::video_ram_write(uint16_t address, uint8_t value) {
    const uint8_t x = uint8_t((address >> 8) << 2);
    const uint8_t y = uint8_t(address);
    uint8_t* p = &video_ram_[size_t(y) * 256 + x];
    const uint8_t m = blitter_[0];
    if (m & 8) {  // AZ/AR
        p[0] = uint8_t((p[0] & 0xfc) | ((value & 0x10) >> 3) | ((value & 0x01) >> 0));
        p[1] = uint8_t((p[1] & 0xfc) | ((value & 0x20) >> 4) | ((value & 0x02) >> 1));
        p[2] = uint8_t((p[2] & 0xfc) | ((value & 0x40) >> 5) | ((value & 0x04) >> 2));
        p[3] = uint8_t((p[3] & 0xfc) | ((value & 0x80) >> 6) | ((value & 0x08) >> 3));
    }
    if (m & 4) {  // AG/AB
        p[0] = uint8_t((p[0] & 0xf3) | ((value & 0x10) >> 1) | ((value & 0x01) << 2));
        p[1] = uint8_t((p[1] & 0xf3) | ((value & 0x20) >> 2) | ((value & 0x02) << 1));
        p[2] = uint8_t((p[2] & 0xf3) | ((value & 0x40) >> 3) | ((value & 0x04) << 0));
        p[3] = uint8_t((p[3] & 0xf3) | ((value & 0x80) >> 4) | ((value & 0x08) >> 1));
    }
    if (m & 2) {  // BZ/BR
        p[0] = uint8_t((p[0] & 0xcf) | ((value & 0x10) << 1) | ((value & 0x01) << 4));
        p[1] = uint8_t((p[1] & 0xcf) | ((value & 0x20) << 0) | ((value & 0x02) << 3));
        p[2] = uint8_t((p[2] & 0xcf) | ((value & 0x40) >> 1) | ((value & 0x04) << 2));
        p[3] = uint8_t((p[3] & 0xcf) | ((value & 0x80) >> 2) | ((value & 0x08) << 1));
    }
    if (m & 1) {  // BG/BB
        p[0] = uint8_t((p[0] & 0x3f) | ((value & 0x10) << 3) | ((value & 0x01) << 6));
        p[1] = uint8_t((p[1] & 0x3f) | ((value & 0x20) << 2) | ((value & 0x02) << 5));
        p[2] = uint8_t((p[2] & 0x3f) | ((value & 0x40) << 1) | ((value & 0x04) << 4));
        p[3] = uint8_t((p[3] & 0x3f) | ((value & 0x80) << 0) | ((value & 0x08) << 3));
    }
}

// Blitter: copies (sx+1) x (sy+1) groups of 4 pixels from the graphics ROMs;
// pen 8 is transparent. Plane bit 0 = upper nibble, bit 2 = lower nibble.
void Arabian::blit_area(uint8_t plane, uint16_t src, uint8_t x, uint8_t y, uint8_t sx, uint8_t sy) {
    uint16_t srcdata = uint16_t(src * 4);
    for (int i = 0; i <= sx; ++i) {
        for (int j = 0; j <= sy; ++j) {
            const uint8_t p1 = gfx_[srcdata++];
            const uint8_t p2 = gfx_[srcdata++];
            const uint8_t p3 = gfx_[srcdata++];
            const uint8_t p4 = gfx_[srcdata++];
            const size_t base = size_t(uint8_t(y + j)) * 256 + x;
            const uint8_t px[4] = {p4, p3, p2, p1};
            for (int k = 0; k < 4; ++k) {
                if (px[k] == 8) continue;
                uint8_t& d = video_ram_[(base + size_t(k)) & 0xffff];
                if (plane & 1) d = uint8_t((d & 0x0f) | (px[k] << 4));
                if (plane & 4) d = uint8_t((d & 0xf0) | px[k]);
            }
        }
        x = uint8_t(x + 4);
    }
}

// ---------------------------------------------------------------------------
// MCU (MB8841): input multiplexer and access to the shared work RAM.

uint8_t Arabian::mcu_r_read(int port) {
    uint8_t v = mcu_port_r_[size_t(port)];
    if (port == 0) v |= 4;  // RAM mode enabled
    return v;
}

void Arabian::mcu_r_write(int port, uint8_t value) {
    if (port == 0) {
        const uint16_t ram_addr = uint16_t(((mcu_port_p_ & 7) << 8) | mcu_port_o_);
        if (~value & 2) ram_[ram_addr & 0x7ff] = uint8_t(0xf0 | mcu_port_r_[3]);
        flip_screen_ = (value & 8) != 0;
    }
    mcu_port_r_[size_t(port)] = uint8_t(value & 0x0f);
}

uint8_t Arabian::mcu_k_read() {
    uint8_t v = 0x0f;
    if (~mcu_port_r_[0] & 1) {
        const uint16_t ram_addr = uint16_t(((mcu_port_p_ & 7) << 8) | mcu_port_o_);
        v = ram_[ram_addr & 0x7ff];
    } else {
        const uint8_t sel = uint8_t(((mcu_port_r_[2] << 4) | mcu_port_r_[1]) & 0x3f);
        for (int i = 0; i < 6; ++i) {
            if (~sel & (1 << i)) {
                switch (i) {
                    case 0: v = in0_; break;
                    case 1: v = in1_; break;
                    case 2: v = in2_; break;
                    case 3: v = 0; break;  // cocktail player 2 stick
                    case 4: v = 0; break;  // cocktail player 2 button
                    default: v = dsw_b_; break;
                }
                break;
            }
        }
    }
    return uint8_t(v & 0x0f);
}

// ---------------------------------------------------------------------------
// Video

void Arabian::create_palette() {
    for (int i = 0; i < 0x2000; ++i) {
        const int ena = (i >> 12) & 1;
        const int enb = (i & 0x200) ? 1 : 0;
        const int abhf = ((i >> 10) & 1) ^ 1;
        const int aghf = ((i >> 9) & 1) ^ 1;
        const int arhf = ((i >> 8) & 1) ^ 1;
        const int az = (i >> 7) & 1, ar = (i >> 6) & 1, ag = (i >> 5) & 1, ab = (i >> 4) & 1;
        const int bz = (i >> 3) & 1, br = (i >> 2) & 1, bg = (i >> 1) & 1, bb = i & 1;
        const bool planea = ((az | ar | ag | ab) & ena) != 0;
        int rhi, rlo, ghi, glo;
        if (planea) {
            rhi = ar;
            rlo = ((arhf ^ 1) & az) ? 0 : ar;
            ghi = ag;
            glo = ((aghf ^ 1) & az) ? 0 : ag;
        } else {
            rhi = bz * enb;
            rlo = br * enb;
            ghi = bb * enb;
            glo = bg * enb;
        }
        const int bhi = ab;
        const int bbase = ((abhf ^ 1) & az) ? 0 : ab;
        int r = int(std::lround(rhi * 115.7 + rlo * 77.3));
        if (rhi | rlo) r = int(std::lround(rhi * 115.7 + rlo * 77.3 + 62));
        int g = int(std::lround(ghi * 117.9588 + glo * 75.0411));
        if (ghi | glo) g = int(std::lround(ghi * 117.9588 + glo * 75.0411 + 62));
        const int b = bhi * 192 + bbase * 63;
        palette_[size_t(i)] = 0xff000000u | uint32_t(std::min(r, 255)) << 16 | uint32_t(std::min(g, 255)) << 8 |
                              uint32_t(std::min(b, 255));
    }
}

void Arabian::update_video() {
    // Rotated 90 degrees: output column ox shows bitmap row ox + 11.
    const uint32_t* pal = palette_.data() + (size_t(video_control_ & 0x1f) << 8);
    for (int oy = 0; oy < kScreenHeight; ++oy) {
        for (int ox = 0; ox < kScreenWidth; ++ox) {
            int sx = ox, sy = oy;
            if (flip_screen_) {
                sx = kScreenWidth - 1 - ox;
                sy = kScreenHeight - 1 - oy;
            }
            const uint8_t pix = video_ram_[size_t(sx + 11) * 256 + size_t(255 - sy)];
            framebuffer_[size_t(oy) * kScreenWidth + size_t(ox)] = pal[pix];
        }
    }
}

// ---------------------------------------------------------------------------
// Frame

void Arabian::on_cycles(int cycles) {
    audio_accum_ += int64_t(cycles) * AY8910::kSampleRate;
    while (audio_accum_ >= int64_t(kCpuClock)) {
        audio_accum_ -= int64_t(kCpuClock);
        audio_.push_back(int16_t(std::clamp(ay_.update(), int32_t(-32768), int32_t(32767))));
    }
}

void Arabian::run_frame() {
    const int slice = int(kCpuClock / kFramesPerSecond) / kScanlines;
    const double mcu_slice = double(kMcuClock) / 6.0 / kFramesPerSecond / kScanlines;
    for (int line = 0; line < kScanlines; ++line) {
        if (line == 244) {
            update_video();
            cpu_.set_irq(IrqLine::Hold);
        }
        cpu_.run(slice);
        on_cycles(slice);
        mcu_acc_ += mcu_slice;
        const int n = int(mcu_acc_);
        mcu_acc_ -= n;
        mcu_.run(n);
    }
}

void Arabian::set_inputs(const MachineInputs& inputs) {
    in0_ = 1;
    in1_ = in2_ = in3_ = 0;
    const auto& p1 = inputs.player1;
    if (p1.right) in1_ |= 1;
    if (p1.left) in1_ |= 2;
    if (p1.up) in1_ |= 4;
    if (p1.down) in1_ |= 8;
    if (p1.button1) in2_ |= 1;
    if (inputs.coin1) in3_ |= 1;
    if (inputs.coin2) in3_ |= 2;
    if (p1.start) in0_ |= 2;
    if (inputs.player2.start) in0_ |= 4;
}

void Arabian::set_dip_switch(int bank, uint8_t value) {
    if (bank == 0) dsw_a_ = value;
    else if (bank == 1) dsw_b_ = uint8_t(value & 0x0f);
}

void Arabian::drain_audio(std::vector<int16_t>& out) {
    out.insert(out.end(), audio_.begin(), audio_.end());
    audio_.clear();
}

}  // namespace dsp
