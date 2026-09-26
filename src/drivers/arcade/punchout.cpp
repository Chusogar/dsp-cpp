#include "drivers/arcade/punchout.h"

#include <algorithm>
#include <cstring>

#include "core/rom_loader.h"

namespace dsp {
namespace {

const std::vector<RomEntry> kMainRoms = {
    {"chp1-c.8l", 0x2000, 0x0000, 0xa4003adc},
    {"chp1-c.8k", 0x2000, 0x2000, 0x745ecf40},
    {"chp1-c.8j", 0x2000, 0x4000, 0x7a7f870e},
    {"chp1-c.8h", 0x2000, 0x6000, 0x5d8123d7},
    {"chp1-c.8f", 0x4000, 0x8000, 0xc8a55ddb},
};
const std::vector<RomEntry> kSoundRom = {{"chp1-c.4k", 0x2000, 0x0000, 0xcb6ef376}};
const std::vector<RomEntry> kVlmRom = {{"chp1-c.6p", 0x4000, 0x0000, 0xea0bbb31}};

// Character ROMs whose 2 KiB quarters are stored in 0, 2, 1, 3 order
// (MAME ROM_LOAD + ROM_CONTINUE at +0x1000, +0x0800, +0x1800).
struct Swizzled {
    const char* name;
    uint32_t crc;
    uint32_t offset;
};
const Swizzled kGfx1[] = {{"chp1-b.4c", 0x49b763bc, 0x0000}, {"chp1-b.4d", 0x08bc6d67, 0x2000}};
const Swizzled kGfx2[] = {{"chp1-b.4a", 0xc075f831, 0x0000}, {"chp1-b.4b", 0xc4cc2b5a, 0x2000}};
const Swizzled kGfx4[] = {{"chp1-v.6p", 0x75be7aae, 0x0000}, {"chp1-v.6n", 0xdaf74de0, 0x2000},
                          {"chp1-v.8p", 0x4cb7ea82, 0x8000}, {"chp1-v.8n", 0x1c0d09aa, 0xa000}};

const std::vector<RomEntry> kGfx3 = {
    {"chp1-v.2r", 0x4000, 0x00000, 0xbd1d4b2e}, {"chp1-v.2t", 0x4000, 0x04000, 0xdd9a688a},
    {"chp1-v.2u", 0x2000, 0x08000, 0xda6a3c4b}, {"chp1-v.2v", 0x2000, 0x0c000, 0x8c734a67},
    {"chp1-v.3r", 0x4000, 0x10000, 0x2e74ad1d}, {"chp1-v.3t", 0x4000, 0x14000, 0x630ba9fb},
    {"chp1-v.3u", 0x2000, 0x18000, 0x6440321d}, {"chp1-v.3v", 0x2000, 0x1c000, 0xbb7b7198},
    {"chp1-v.4r", 0x4000, 0x20000, 0x4e5b0fe9}, {"chp1-v.4t", 0x4000, 0x24000, 0x37ffc940},
    {"chp1-v.4u", 0x2000, 0x28000, 0x1a7521d4},
};

// Pink-labelled colour PROMs: R, G, B top monitor then R, G, B bottom.
const std::vector<RomEntry> kProms = {
    {"chp1-b-6e_pink.6e", 0x200, 0x000, 0xe9ca3ac6}, {"chp1-b-6f_pink.6f", 0x200, 0x200, 0x02be56ab},
    {"chp1-b-7f_pink.7f", 0x200, 0x400, 0x11de55f1}, {"chp1-b-7e_pink.7e", 0x200, 0x600, 0xfddaa777},
    {"chp1-b-8e_pink.8e", 0x200, 0x800, 0xc3d5d71f}, {"chp1-b-8f_pink.8f", 0x200, 0xa00, 0xa3037155},
};

bool load_swizzled(RomLoader& loader, const Swizzled& rom, std::vector<uint8_t>& region, std::string* error) {
    std::vector<uint8_t> data(0x2000, 0);
    if (!loader.load({{rom.name, 0x2000, 0, rom.crc}}, data, error)) return false;
    static const uint32_t kDest[4] = {0x0000, 0x1000, 0x0800, 0x1800};
    for (int q = 0; q < 4; q++) {
        std::memcpy(region.data() + rom.offset + kDest[q], data.data() + q * 0x800, 0x800);
    }
    return true;
}

// Planar 8x8 characters: the first plane listed by MAME (the highest part of
// the region) is the most significant bit.
std::vector<uint8_t> decode_planar(const std::vector<uint8_t>& region, int planes) {
    const size_t plane_size = region.size() / size_t(planes);
    const size_t count = plane_size / 8;
    std::vector<uint8_t> out(count * 64);
    for (size_t c = 0; c < count; c++) {
        for (int y = 0; y < 8; y++) {
            for (int x = 0; x < 8; x++) {
                int v = 0;
                for (int p = 0; p < planes; p++) {
                    const uint8_t b = region[size_t(p) * plane_size + c * 8 + size_t(y)];
                    v |= ((b >> (7 - x)) & 1) << p;
                }
                out[c * 64 + size_t(y * 8 + x)] = uint8_t(v);
            }
        }
    }
    return out;
}

}  // namespace

PunchOut::PunchOut()
    : main_cpu_(kMainClock),
      sound_cpu_(kSoundClock, M6502::Type::Nes),
      apu_(kSoundClock),
      vlm_(3579545, 0x4000),
      framebuffer_(size_t(kScreenWidth * kScreenHeight), 0xff000000u) {
    main_cpu_.set_memory_handlers([this](uint16_t a) { return main_read(a); },
                                  [this](uint16_t a, uint8_t v) { main_write(a, v); });
    main_cpu_.set_io_handlers([this](uint16_t p) { return port_read(p); },
                              [this](uint16_t p, uint8_t v) { port_write(p, v); });
    sound_cpu_.set_memory_handlers([this](uint16_t a) { return sound_read(a); },
                                   [this](uint16_t a, uint8_t v) { sound_write(a, v); });
    sound_cpu_.set_cycle_handler([this](int cycles) { on_sound_cycles(cycles); });
    apu_.set_dpcm_reader([this](uint16_t a) { return sound_read(a); });
    apu_.set_irq_handler([this]() { sound_cpu_.set_irq(IrqLine::Hold); });
}

bool PunchOut::init(const std::string& rom_path, std::string* error) {
    RomLoader loader;
    if (!loader.open(rom_path, error)) return false;

    std::vector<uint8_t> main(0xc000, 0);
    if (!loader.load(kMainRoms, main, error)) return false;
    std::copy(main.begin(), main.end(), main_rom_.begin());

    std::vector<uint8_t> sound(0x2000, 0);
    if (!loader.load(kSoundRom, sound, error)) return false;
    std::copy(sound.begin(), sound.end(), sound_rom_.begin());

    std::vector<uint8_t> vlm(0x4000, 0);
    if (!loader.load(kVlmRom, vlm, error)) return false;
    vlm_.set_rom(vlm);

    std::vector<uint8_t> gfx1(0x4000, 0xff), gfx2(0x4000, 0xff), gfx4(0x10000, 0xff);
    for (const auto& r : kGfx1) if (!load_swizzled(loader, r, gfx1, error)) return false;
    for (const auto& r : kGfx2) if (!load_swizzled(loader, r, gfx2, error)) return false;
    for (const auto& r : kGfx4) if (!load_swizzled(loader, r, gfx4, error)) return false;
    std::vector<uint8_t> gfx3(0x30000, 0xff);
    for (const RomEntry& e : kGfx3) {
        std::vector<uint8_t> d(e.length, 0);
        if (!loader.load({{e.name, e.length, 0, e.crc}}, d, error)) return false;
        std::copy(d.begin(), d.end(), gfx3.begin() + e.offset);
    }
    gfx_top_ = decode_planar(gfx1, 2);
    gfx_bot_ = decode_planar(gfx2, 2);
    gfx_spr1_ = decode_planar(gfx3, 3);
    gfx_spr2_ = decode_planar(gfx4, 2);

    proms_.assign(0xc00, 0);
    if (!loader.load(kProms, proms_, error)) return false;
    build_palettes();

    warnings_ = loader.warnings();
    reset();
    return true;
}

void PunchOut::build_palettes() {
    auto pal4 = [](uint8_t v) { return 255 - (v & 0x0f) * 0x11; };
    for (int bank = 0; bank < 2; bank++) {
        for (int i = 0; i < 0x100; i++) {
            const int t = i + 0x100 * bank;
            top_pal_[size_t(bank)][size_t(i)] = 0xff000000u | uint32_t(pal4(proms_[size_t(t)]) << 16) |
                                                uint32_t(pal4(proms_[size_t(0x200 + t)]) << 8) |
                                                uint32_t(pal4(proms_[size_t(0x400 + t)]));
            bot_pal_[size_t(bank)][size_t(i)] = 0xff000000u | uint32_t(pal4(proms_[size_t(0x600 + t)]) << 16) |
                                                uint32_t(pal4(proms_[size_t(0x800 + t)]) << 8) |
                                                uint32_t(pal4(proms_[size_t(0xa00 + t)]));
        }
    }
}

void PunchOut::reset() {
    main_cpu_.reset();
    sound_cpu_.reset();
    apu_.reset();
    vlm_.reset();
    work_ram_.fill(0);
    top_vram_.fill(0);
    spr1_vram_.fill(0);
    spr2_vram_.fill(0);
    bot_vram_.fill(0);
    sound_ram_.fill(0);
    latch_ = 0;
    sound_latch_[0] = sound_latch_[1] = 0;
    nmi_mask_ = false;
    sound_reset_ = false;
    vlm_cycle_acc_ = 0;
    audio_accumulator_ = 0;
    apu_cycles_ = 0;
    audio_.clear();
}

// ---------------------------------------------------------------------------
// Main CPU

uint8_t PunchOut::main_read(uint16_t address) {
    if (address < 0xc000) return main_rom_[address];
    if (address < 0xc400) return nvram_[address & 0x3ff];
    if (address < 0xd000) return 0xff;
    if (address < 0xd800) return work_ram_[address & 0x7ff];
    if (address < 0xe000) return top_vram_[address & 0x7ff];
    if (address < 0xe800) return spr1_vram_[address & 0x7ff];
    if (address < 0xf000) return spr2_vram_[address & 0x7ff];
    return bot_vram_[address & 0xfff];
}

void PunchOut::main_write(uint16_t address, uint8_t value) {
    if (address < 0xc000) return;
    if (address < 0xc400) nvram_[address & 0x3ff] = value;
    else if (address < 0xd000) return;
    else if (address < 0xd800) work_ram_[address & 0x7ff] = value;
    else if (address < 0xe000) top_vram_[address & 0x7ff] = value;  // includes $DFF0-$DFFF controls
    else if (address < 0xe800) spr1_vram_[address & 0x7ff] = value;
    else if (address < 0xf000) spr2_vram_[address & 0x7ff] = value;
    else bot_vram_[address & 0xfff] = value;
}

uint8_t PunchOut::port_read(uint16_t port) {
    switch (port & 0xff) {
        case 0x00: return in0_;
        case 0x01: return in1_;
        case 0x02: return dsw2_;
        case 0x03:
            // Bit 4: VLM5030 BSY, active low.
            return uint8_t((dsw1_ & ~0x10) | (vlm_.get_bsy() ? 0x00 : 0x10));
        default: return 0xff;
    }
}

void PunchOut::port_write(uint16_t port, uint8_t value) {
    const uint8_t p = uint8_t(port);
    switch (p) {
        case 0x02: sound_latch_[0] = value; break;
        case 0x03: sound_latch_[1] = value; break;
        case 0x04: vlm_.data_w(value); break;
        default:
            if (p >= 0x08 && p <= 0x0f) latch_w(p - 0x08, value & 1);
            break;
    }
}

// LS259 at 2B.
void PunchOut::latch_w(int bit, bool state) {
    const uint8_t mask = uint8_t(1 << bit);
    latch_ = state ? uint8_t(latch_ | mask) : uint8_t(latch_ & ~mask);
    switch (bit) {
        case 0:  // NMI enable
            nmi_mask_ = state;
            if (!state) main_cpu_.set_nmi(IrqLine::Clear);
            break;
        case 3:  // 2A03 reset
            if (state && !sound_reset_) {
                sound_cpu_.reset();
                apu_.reset();
            }
            sound_reset_ = state;
            break;
        case 4: vlm_.set_rst(state ? 1 : 0); break;
        case 5: vlm_.set_st(state ? 1 : 0); break;
        case 6: vlm_.update_vcu(state ? 1 : 0); break;
        default: break;
    }
}

// ---------------------------------------------------------------------------
// Sound CPU (2A03)

uint8_t PunchOut::sound_read(uint16_t address) {
    if (address < 0x0800) return sound_ram_[address];
    if (address == 0x4015) return apu_.read(address);
    if (address == 0x4016) return sound_latch_[0];
    if (address == 0x4017) return sound_latch_[1];
    if (address >= 0xe000) return sound_rom_[address - 0xe000];
    return 0;
}

void PunchOut::sound_write(uint16_t address, uint8_t value) {
    if (address < 0x0800) {
        sound_ram_[address] = value;
    } else if (address >= 0x4000 && address <= 0x4017 && address != 0x4014 && address != 0x4016) {
        apu_.write(address, value);
    }
}

void PunchOut::on_sound_cycles(int cycles) {
    apu_cycles_ += cycles;
    while (apu_cycles_ >= 4) {
        apu_cycles_ -= 4;
        apu_.advance();
    }
    const int vlm_period = vlm_.cycles_per_sample(kSoundClock);
    vlm_cycle_acc_ += cycles;
    while (vlm_cycle_acc_ >= vlm_period) {
        vlm_cycle_acc_ -= vlm_period;
        vlm_.update_stream();
    }
    audio_accumulator_ += int64_t(cycles) * kSampleRate;
    while (audio_accumulator_ >= int64_t(kSoundClock)) {
        audio_accumulator_ -= int64_t(kSoundClock);
        // MAME: VLM5030 0.50 left, 2A03 0.50 right; mixed to mono here.
        const int32_t s = int32_t(apu_.update()) / 2 + vlm_.update() / 2;
        audio_.push_back(int16_t(std::clamp(s, int32_t(-32768), int32_t(32767))));
    }
}

void PunchOut::run_frame() {
    const double main_per_line = double(kMainClock) / kFramesPerSecond / kLines;
    const double sound_per_line = double(kSoundClock) / kFramesPerSecond / kLines;
    double main_acc = 0, sound_acc = 0;
    for (int line = 0; line < kLines; line++) {
        if (line == 240) {
            // Top monitor VBLANK: Z80 NMI (when enabled) and 2A03 NMI.
            render();
            if (nmi_mask_) main_cpu_.set_nmi(IrqLine::Assert);
            if (!sound_reset_) sound_cpu_.set_nmi(IrqLine::Pulse);
        }
        main_acc += main_per_line;
        const int m = int(main_acc);
        main_acc -= m;
        main_cpu_.run(m);
        sound_acc += sound_per_line;
        const int s = int(sound_acc);
        sound_acc -= s;
        if (sound_reset_) {
            on_sound_cycles(s);  // held in reset: sound chips keep running
        } else {
            sound_cpu_.run(s);
        }
    }
}

void PunchOut::drain_audio(std::vector<int16_t>& out) {
    out.insert(out.end(), audio_.begin(), audio_.end());
    audio_.clear();
}

void PunchOut::set_inputs(const MachineInputs& inputs) {
    const InputState& p = inputs.player1;
    // IN0: buttons (active high).  Button 1 / 2 punch, button 3 is the
    // third cabinet button; Start also works as button 3.
    in0_ = 0;
    if (p.button1) in0_ |= 0x01;
    if (p.button2) in0_ |= 0x04;
    if (p.button3 || p.start) in0_ |= 0x08;
    in1_ = 0;
    if (p.right) in1_ |= 0x01;
    if (p.left) in1_ |= 0x02;
    if (p.up) in1_ |= 0x04;
    if (p.down) in1_ |= 0x08;
    if (inputs.service) in1_ |= 0x40;
    if (inputs.coin1 || inputs.coin2) in1_ |= 0x80;
}

void PunchOut::set_dip_switch(int bank, uint8_t value) {
    if (bank == 0) dsw2_ = value;  // difficulty, time, demo sounds...
    if (bank == 1) dsw1_ = value;  // coinage
}

// ---------------------------------------------------------------------------
// Video

// draw_roz as MAME: source = start + dest * inc (16.16, unsigned wrap), no
// wraparound, transparent pen skipped.
namespace {
template <typename Pixel>
void roz(uint16_t* pens, uint32_t startx, uint32_t starty, int32_t incxx, int32_t incyy, int width,
         int height, Pixel pixel) {
    for (int y = 16; y < 240; y++) {
        uint32_t cx = startx;
        const uint32_t cy = starty + uint32_t(y) * uint32_t(incyy);
        const uint32_t py = cy >> 16;
        if (py < uint32_t(height)) {
            for (int x = 0; x < 256; x++) {
                const uint32_t px = cx >> 16;
                if (px < uint32_t(width)) {
                    const int pen = pixel(int(px), int(py));
                    if (pen >= 0) pens[y * 256 + x] = uint16_t(pen);
                }
                cx += uint32_t(incxx);
            }
        }
    }
}
}  // namespace

void PunchOut::draw_big_sprite(uint16_t* pens, int palette) {
    const uint8_t* c = &top_vram_[0x7f0];
    const int zoom = c[0] + 256 * (c[1] & 0x0f);
    if (!zoom) return;
    int sx = 4096 - (c[2] + 256 * (c[3] & 0x0f));
    if (sx > 4096 - 4 * 127) sx -= 4096;
    int sy = -(c[4] + 256 * (c[5] & 1));
    if (sy <= -256 + zoom / 0x40) sy += 512;
    sy += 12;
    int32_t incxx = zoom << 6;
    const int32_t incyy = zoom << 6;
    uint32_t startx = uint32_t(-sx * 0x4000);
    uint32_t starty = uint32_t(-sy * 0x10000);
    startx += uint32_t(3740 * zoom);  // MAME's screenshot alignment
    starty -= uint32_t(178 * zoom);
    if (c[6] & 1) {  // flip x
        startx = ((16 * 8) << 16) - startx - 1;
        incxx = -incxx;
    }
    (void)palette;
    roz(pens, startx, starty + uint32_t(0x200 * 2 * zoom), incxx, incyy, 128, 256, [&](int px, int py) {
        const int tile = (py >> 3) * 16 + (px >> 3);
        const uint8_t* v = &spr1_vram_[size_t(tile * 4)];
        const int code = v[0] + ((v[1] & 0x1f) << 8);
        const int color = v[3] & 0x1f;
        const int tx = (v[3] & 0x80) ? 7 - (px & 7) : (px & 7);
        const int pix = gfx_spr1_[size_t(code) * 64 + size_t((py & 7) * 8 + tx)];
        return pix == 7 ? -1 : color * 8 + pix;
    });
}

void PunchOut::draw_big_sprite2(uint16_t* pens) {
    const uint8_t* c = &top_vram_[0x7f8];
    int sx = 512 - (c[0] + 256 * (c[1] & 1));
    if (sx > 512 - 127) sx -= 512;
    sx -= 55;
    int sy = -c[2] + 256 * (c[3] & 1);
    sy += 3;
    uint32_t startx = uint32_t(-sx) << 16;
    const uint32_t starty = uint32_t(-sy) << 16;
    int32_t incxx = 1 << 16;
    if (c[4] & 1) {
        startx = ((16 * 8) << 16) - startx - 1;
        incxx = -incxx;
    }
    roz(pens, startx, starty, incxx, 1 << 16, 128, 256, [&](int px, int py) {
        const int tile = (py >> 3) * 16 + (px >> 3);
        const uint8_t* v = &spr2_vram_[size_t(tile * 4)];
        const int code = v[0] + ((v[1] & 0x0f) << 8);
        const int color = v[3] & 0x3f;
        const int tx = (v[3] & 0x80) ? 7 - (px & 7) : (px & 7);
        const int pix = gfx_spr2_[size_t(code) * 64 + size_t((py & 7) * 8 + tx)];
        return pix == 3 ? -1 : color * 4 + pix;
    });
}

void PunchOut::draw_top(uint32_t* out) {
    std::vector<uint16_t> pens(256 * 256, 0);
    for (int y = 16; y < 240; y++) {
        for (int x = 0; x < 256; x++) {
            const int tile = (y >> 3) * 32 + (x >> 3);
            const uint8_t attr = top_vram_[size_t(tile * 2 + 1)];
            const int code = top_vram_[size_t(tile * 2)] + ((attr & 0x03) << 8);
            const int color = (attr & 0x7c) >> 2;
            const int tx = (attr & 0x80) ? 7 - (x & 7) : (x & 7);
            pens[size_t(y * 256 + x)] =
                uint16_t(color * 4 + gfx_top_[size_t(code) * 64 + size_t((y & 7) * 8 + tx)]);
        }
    }
    if (top_vram_[0x7f7] & 1) draw_big_sprite(pens.data(), 0);
    const auto& pal = top_pal_[(top_vram_[0x7fd] >> 1) & 1];
    for (int y = 16; y < 240; y++) {
        for (int x = 0; x < 256; x++) out[(y - 16) * 256 + x] = pal[pens[size_t(y * 256 + x)] & 0xff];
    }
}

void PunchOut::draw_bottom(uint32_t* out) {
    std::vector<uint16_t> pens(256 * 256, 0);
    for (int y = 16; y < 240; y++) {
        const int row = y >> 3;
        const int scroll = 58 + bot_vram_[size_t(2 * row)] + 256 * (bot_vram_[size_t(2 * row + 1)] & 1);
        for (int x = 0; x < 256; x++) {
            const int tx0 = (x + scroll) & 511;
            const int tile = row * 64 + (tx0 >> 3);
            const uint8_t attr = bot_vram_[size_t(tile * 2 + 1)];
            const int code = bot_vram_[size_t(tile * 2)] + ((attr & 0x03) << 8);
            const int color = (attr & 0x7c) >> 2;
            const int tx = (attr & 0x80) ? 7 - (tx0 & 7) : (tx0 & 7);
            pens[size_t(y * 256 + x)] =
                uint16_t(color * 4 + gfx_bot_[size_t(code) * 64 + size_t((y & 7) * 8 + tx)]);
        }
    }
    if (top_vram_[0x7f7] & 2) draw_big_sprite(pens.data(), 1);
    draw_big_sprite2(pens.data());
    const auto& pal = bot_pal_[top_vram_[0x7fd] & 1];
    for (int y = 16; y < 240; y++) {
        for (int x = 0; x < 256; x++) out[(y - 16) * 256 + x] = pal[pens[size_t(y * 256 + x)] & 0xff];
    }
}

void PunchOut::render() {
    draw_top(framebuffer_.data());
    draw_bottom(framebuffer_.data() + kScreenWidth * kMonitorHeight);
}

}  // namespace dsp
