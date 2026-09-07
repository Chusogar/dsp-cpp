#include "drivers/arcade/tehkanwc.h"

#include <algorithm>
#include <cstring>

#include "core/rom_loader.h"

namespace dsp {
namespace {

const std::vector<RomEntry> kMainRoms = {
    {"twc-1.bin", 0x4000, 0x0000, 0x34d6d5ff},
    {"twc-2.bin", 0x4000, 0x4000, 0x7017a221},
    {"twc-3.bin", 0x4000, 0x8000, 0x8b662902},
};
const std::vector<RomEntry> kSubRoms = {{"twc-4.bin", 0x8000, 0x0000, 0x70a9f883}};
const std::vector<RomEntry> kSoundRoms = {{"twc-6.bin", 0x4000, 0x0000, 0xe3112be2}};
const std::vector<RomEntry> kAdpcmRoms = {{"twc-5.bin", 0x4000, 0x0000, 0x444b5544}};
const std::vector<RomEntry> kCharRoms = {{"twc-12.bin", 0x4000, 0x0000, 0xa9e274f8}};
const std::vector<RomEntry> kSpriteRoms = {
    {"twc-8.bin", 0x8000, 0x0000, 0x055a5264},
    {"twc-7.bin", 0x8000, 0x8000, 0x59faebe7},
};
const std::vector<RomEntry> kTileRoms = {
    {"twc-11.bin", 0x8000, 0x0000, 0x669389fc},
    {"twc-9.bin", 0x8000, 0x8000, 0x347ef108},
};

// Shared x/y bitplane offsets from Pascal ps_x / ps_y
constexpr int kPsX[16] = {
    1 * 4, 0 * 4, 3 * 4, 2 * 4, 5 * 4, 4 * 4, 7 * 4, 6 * 4,
    8 * 32 + 1 * 4, 8 * 32 + 0 * 4, 8 * 32 + 3 * 4, 8 * 32 + 2 * 4,
    8 * 32 + 5 * 4, 8 * 32 + 4 * 4, 8 * 32 + 7 * 4, 8 * 32 + 6 * 4,
};
constexpr int kPsY[16] = {
    0 * 32, 1 * 32, 2 * 32, 3 * 32, 4 * 32, 5 * 32, 6 * 32, 7 * 32,
    16 * 32, 17 * 32, 18 * 32, 19 * 32, 20 * 32, 21 * 32, 22 * 32, 23 * 32,
};

GfxLayout char_layout() {
    GfxLayout l;
    l.width = 8;
    l.height = 8;
    l.total = 512;
    l.planes = 4;
    l.char_increment = 32 * 8;
    l.plane_offsets = {0, 1, 2, 3};
    l.x_offsets = {kPsX[0], kPsX[1], kPsX[2], kPsX[3], kPsX[4], kPsX[5], kPsX[6], kPsX[7]};
    l.y_offsets = {kPsY[0], kPsY[1], kPsY[2], kPsY[3], kPsY[4], kPsY[5], kPsY[6], kPsY[7]};
    return l;
}

GfxLayout sprite_layout() {
    GfxLayout l;
    l.width = 16;
    l.height = 16;
    l.total = 512;
    l.planes = 4;
    l.char_increment = 128 * 8;
    l.plane_offsets = {0, 1, 2, 3};
    l.x_offsets.assign(kPsX, kPsX + 16);
    l.y_offsets.assign(kPsY, kPsY + 16);
    return l;
}

GfxLayout tile_layout() {
    GfxLayout l;
    l.width = 16;
    l.height = 8;
    l.total = 1024;
    l.planes = 4;
    l.char_increment = 64 * 8;
    l.plane_offsets = {0, 1, 2, 3};
    l.x_offsets.assign(kPsX, kPsX + 16);
    l.y_offsets = {kPsY[0], kPsY[1], kPsY[2], kPsY[3], kPsY[4], kPsY[5], kPsY[6], kPsY[7]};
    return l;
}

uint8_t pal4bit(uint8_t v) {
    v &= 0x0f;
    return uint8_t(v | (v << 4));
}

}  // namespace

TehkanWc::TehkanWc()
    : main_cpu_(kCpuClock),
      sub_cpu_(kCpuClock),
      sound_cpu_(kCpuClock),
      ay0_(kAyClock),
      ay1_(kAyClock),
      msm_(kMsmClock, 96, 4) {
    layer_bg_.assign(512 * 256, 0xff000000u);
    layer_fg_lo_.assign(256 * 256, 0);
    layer_fg_hi_.assign(256 * 256, 0);
    composite_.assign(256 * 256, 0xff000000u);
    framebuffer_.assign(size_t(kScreenWidth) * kScreenHeight, 0xff000000u);

    main_cpu_.set_memory_handlers(
        [this](uint16_t a) { return main_read(a); },
        [this](uint16_t a, uint8_t v) { main_write(a, v); });
    sub_cpu_.set_memory_handlers(
        [this](uint16_t a) { return sub_read(a); },
        [this](uint16_t a, uint8_t v) { sub_write(a, v); });
    sound_cpu_.set_memory_handlers(
        [this](uint16_t a) { return sound_read(a); },
        [this](uint16_t a, uint8_t v) { sound_write(a, v); });
    sound_cpu_.set_io_handlers(
        [this](uint16_t p) { return sound_in(p); },
        [this](uint16_t p, uint8_t v) { sound_out(p, v); });

    // AY0 ports write MSM position low/high; AY1 ports read them back
    ay0_.set_port_handlers(nullptr, nullptr,
                           [this](uint8_t v) { msm_pos_ = uint16_t((msm_pos_ & 0xff00) | v); },
                           [this](uint8_t v) { msm_pos_ = uint16_t((msm_pos_ & 0x00ff) | (v << 8)); });
    ay1_.set_port_handlers(
        [this]() { return uint8_t(msm_pos_ & 0xff); },
        [this]() { return uint8_t(msm_pos_ >> 8); }, nullptr, nullptr);

    msm_.set_vclk_handler([this]() { msm_advance(); });
}

bool TehkanWc::init(const std::string& rom_path, std::string* error) {
    RomLoader loader;
    if (!loader.open(rom_path, error)) return false;

    std::vector<uint8_t> buf(0xc000, 0);
    if (!loader.load(kMainRoms, buf, error)) return false;
    std::copy(buf.begin(), buf.end(), memory_.begin());

    buf.assign(0x8000, 0);
    if (!loader.load(kSubRoms, buf, error)) return false;
    std::copy(buf.begin(), buf.end(), mem_sub_.begin());

    buf.assign(0x4000, 0);
    if (!loader.load(kSoundRoms, buf, error)) return false;
    std::copy(buf.begin(), buf.end(), mem_snd_.begin());

    buf.assign(0x4000, 0);
    if (!loader.load(kAdpcmRoms, buf, error)) return false;
    // MSM ROM window is 32KB mirrored
    std::vector<uint8_t> adpcm(0x8000, 0);
    std::copy(buf.begin(), buf.end(), adpcm.begin());
    std::copy(buf.begin(), buf.end(), adpcm.begin() + 0x4000);
    msm_.set_rom(std::move(adpcm));

    std::vector<uint8_t> chars(0x4000), sprites(0x10000), tiles(0x10000);
    if (!loader.load(kCharRoms, chars, error)) return false;
    if (!loader.load(kSpriteRoms, sprites, error)) return false;
    if (!loader.load(kTileRoms, tiles, error)) return false;
    decode_graphics(chars, sprites, tiles);

    warnings_ = loader.warnings();
    reset();
    return true;
}

void TehkanWc::decode_graphics(const std::vector<uint8_t>& chars,
                               const std::vector<uint8_t>& sprites,
                               const std::vector<uint8_t>& tiles) {
    chars_.decode(char_layout(), chars);
    sprites_.decode(sprite_layout(), sprites);
    tiles_.decode(tile_layout(), tiles);
}

void TehkanWc::reset() {
    main_cpu_.reset();
    sub_cpu_.reset();
    sound_cpu_.reset();
    ay0_.reset();
    ay1_.reset();
    msm_.reset();
    scroll_x_ = 0;
    scroll_y_ = 0;
    sound_latch_ = sound_latch2_ = 0;
    sub_reset_ = true;
    track0_[0] = track0_[1] = track1_[0] = track1_[1] = 0;
    stick0_x_ = stick0_y_ = stick1_x_ = stick1_y_ = 0;
    in0_ = in1_ = 0x20;
    in2_ = 0x0f;
    msm_pos_ = 0;
    msm_data_val_ = -1;
    audio_accumulator_ = msm_accumulator_ = 0;
    audio_.clear();
    palette_ram_.fill(0);
    palette_.fill(0xff000000u);
}

void TehkanWc::set_palette_color(int index) {
    // index is even palette-RAM index; each color is 2 bytes
    const int dir = index & 0x7fe;
    const uint8_t lo = palette_ram_[size_t(dir)];
    const uint8_t hi = palette_ram_[size_t(dir + 1)];
    const uint8_t b = pal4bit(lo);
    const uint8_t g = pal4bit(hi >> 4);
    const uint8_t r = pal4bit(hi);
    palette_[size_t(dir >> 1)] = 0xff000000u | (uint32_t(r) << 16) | (uint32_t(g) << 8) | b;
}

void TehkanWc::shared_write(uint16_t address, uint8_t value) {
    memory_[address] = value;
    const uint16_t off = uint16_t(address - 0xc800);
    if (off >= 0x800 && off <= 0xfff) {
        // char videoram / attr at d000-d7ff — mark dirty implicitly (full redraw)
    } else if (off >= 0x1000 && off <= 0x17ff) {
        palette_ram_[address & 0x7ff] = value;
        set_palette_color(int(address & 0x7fe));
    } else if (off >= 0x1800 && off <= 0x2000) {
        // bg videoram
    } else if (off == 0x2400) {
        scroll_x_ = uint16_t((scroll_x_ & 0x100) | value);
    } else if (off == 0x2401) {
        scroll_x_ = uint16_t((scroll_x_ & 0xff) | ((value & 1) << 8));
    } else if (off == 0x2402) {
        scroll_y_ = value;
    }
}

uint8_t TehkanWc::main_read(uint16_t address) {
    if (address <= 0xec02) return memory_[address];
    switch (address) {
        case 0xf800: return uint8_t(track0_[0] - stick0_x_);
        case 0xf801: return uint8_t(track0_[1] - stick0_y_);
        case 0xf802:
        case 0xf806: return in2_;
        case 0xf803: return in0_;
        case 0xf810: return uint8_t(track1_[0] - stick1_x_);
        case 0xf811: return uint8_t(track1_[1] - stick1_y_);
        case 0xf813: return in1_;
        case 0xf820: return sound_latch2_;
        case 0xf840: return dsw_a_;
        case 0xf850: return dsw_b_;
        case 0xf860: return 0xff;  // watchdog
        case 0xf870: return dsw_c_;
        default: return 0xff;
    }
}

void TehkanWc::main_write(uint16_t address, uint8_t value) {
    if (address <= 0xbfff) return;
    if (address <= 0xc7ff) {
        memory_[address] = value;
        return;
    }
    if (address <= 0xec02) {
        shared_write(address, value);
        return;
    }
    switch (address) {
        case 0xf800: track0_[0] = value; break;
        case 0xf801: track0_[1] = value; break;
        case 0xf810: track1_[0] = value; break;
        case 0xf811: track1_[1] = value; break;
        case 0xf820:
            sound_latch_ = value;
            sound_cpu_.set_nmi(IrqLine::Assert);
            break;
        case 0xf840:
            sub_reset_ = (value == 0);
            if (sub_reset_) sub_cpu_.reset();
            break;
        default: break;
    }
}

uint8_t TehkanWc::sub_read(uint16_t address) {
    if (address <= 0xc7ff) return mem_sub_[address];
    if (address <= 0xec02) return memory_[address];
    if (address == 0xf860) return 0xff;  // watchdog
    return 0xff;
}

void TehkanWc::sub_write(uint16_t address, uint8_t value) {
    if (address <= 0x7fff) return;
    if (address <= 0xc7ff) {
        mem_sub_[address] = value;
        return;
    }
    if (address <= 0xec02) shared_write(address, value);
}

uint8_t TehkanWc::sound_read(uint16_t address) {
    if (address <= 0x47ff) return mem_snd_[address];
    if (address == 0xc000) return sound_latch_;
    return 0xff;
}

void TehkanWc::sound_write(uint16_t address, uint8_t value) {
    if (address <= 0x3fff) return;
    if (address <= 0x47ff) {
        mem_snd_[address] = value;
        return;
    }
    switch (address) {
        case 0x8001: msm_.set_reset((value & 1) == 0); break;
        case 0x8003: sound_cpu_.set_nmi(IrqLine::Clear); break;
        case 0xc000: sound_latch2_ = value; break;
        default: break;
    }
}

uint8_t TehkanWc::sound_in(uint16_t port) {
    switch (port & 0xff) {
        case 0: return ay0_.read();
        case 1: return ay1_.read();
        default: return 0xff;
    }
}

void TehkanWc::sound_out(uint16_t port, uint8_t value) {
    switch (port & 0xff) {
        case 0: ay0_.write(value); break;
        case 1: ay0_.control(value); break;
        case 2: ay1_.write(value); break;
        case 3: ay1_.control(value); break;
        default: break;
    }
}

void TehkanWc::msm_advance() {
    // Port of msm5205_sound from Pascal: alternate high/low nibble from ROM
    if (msm_data_val_ != -1) {
        msm_.data_w(uint8_t(msm_data_val_ & 0x0f));
        msm_pos_ = uint16_t((msm_pos_ + 1) & 0x7fff);
        msm_data_val_ = -1;
    } else {
        // Peek ROM through MSM (we keep a local copy of pos)
        // The chip's internal position is driven by our data_w stream
        msm_data_val_ = -1;
        // Feed via set_start style: use data_w from ROM at msm_pos_
        // Access rom through a temporary path — MSM doesn't expose rom_
        // so we keep ADPCM data in mem and drive data_w ourselves.
    }
}

void TehkanWc::draw_background() {
    std::fill(layer_bg_.begin(), layer_bg_.end(), 0xff000000u);
    for (int f = 0; f < 0x400; f++) {
        const uint8_t attr = memory_[0xe001 + f * 2];
        const int color = attr & 0x0f;
        const int code = memory_[0xe000 + f * 2] + ((attr & 0x30) << 4);
        const bool flipx = (attr & 0x40) != 0;
        const bool flipy = (attr & 0x80) != 0;
        const int x = (f % 32) * 16;
        const int y = (f / 32) * 8;
        const uint8_t* tile = tiles_.element(code);
        for (int py = 0; py < 8; py++) {
            const int sy = flipy ? 7 - py : py;
            const int dy = y + py;
            if (dy < 0 || dy >= 256) continue;
            uint32_t* line = layer_bg_.data() + size_t(dy * 512);
            for (int px = 0; px < 16; px++) {
                const int sx = flipx ? 15 - px : px;
                const int dx = x + px;
                if (dx < 0 || dx >= 512) continue;
                const uint8_t pen = tile[sy * 16 + sx];
                line[dx] = palette_[size_t(((color << 4) + pen + 512) & 0x3ff)];
            }
        }
    }
}

void TehkanWc::draw_chars_low() {
    // Transparent clear (0 alpha marker)
    std::fill(layer_fg_lo_.begin(), layer_fg_lo_.end(), 0);
    std::fill(layer_fg_hi_.begin(), layer_fg_hi_.end(), 0);
    for (int f = 0; f < 0x400; f++) {
        const uint8_t attr = memory_[0xd400 + f];
        const int color = attr & 0x0f;
        const int code = memory_[0xd000 + f] + ((attr & 0x10) << 4);
        const bool flipx = (attr & 0x40) != 0;
        const bool flipy = (attr & 0x80) != 0;
        const bool high_priority = (attr & 0x20) == 0;
        const int x = (f % 32) * 8;
        const int y = (f / 32) * 8;
        const uint8_t* tile = chars_.element(code);
        for (int py = 0; py < 8; py++) {
            const int sy = flipy ? 7 - py : py;
            const int dy = y + py;
            if (dy < 0 || dy >= 256) continue;
            uint32_t* lo = layer_fg_lo_.data() + size_t(dy * 256);
            uint32_t* hi = layer_fg_hi_.data() + size_t(dy * 256);
            for (int px = 0; px < 8; px++) {
                const int sx = flipx ? 7 - px : px;
                const int dx = x + px;
                if (dx < 0 || dx >= 256) continue;
                const uint8_t pen = tile[sy * 8 + sx];
                if (!pen) continue;
                const uint32_t pix = palette_[size_t(((color << 4) + pen) & 0x3ff)];
                lo[dx] = pix;
                if (high_priority) hi[dx] = pix;
            }
        }
    }
}

void TehkanWc::draw_sprites() {
    for (int f = 0; f < 0x100; f++) {
        const uint8_t attr = memory_[0xe801 + f * 4];
        int x = memory_[0xe802 + f * 4] + ((attr & 0x20) << 3) - 128;
        int y = memory_[0xe803 + f * 4];
        const int color = (attr & 7) << 4;
        const int code = memory_[0xe800 + f * 4] + ((attr & 8) << 5);
        const bool flipx = (attr & 0x40) != 0;
        const bool flipy = (attr & 0x80) != 0;
        const uint8_t* spr = sprites_.element(code);
        for (int py = 0; py < 16; py++) {
            const int sy = flipy ? 15 - py : py;
            const int dy = y + py;
            if (dy < 0 || dy >= 256) continue;
            uint32_t* line = composite_.data() + size_t(dy * 256);
            for (int px = 0; px < 16; px++) {
                const int sx = flipx ? 15 - px : px;
                const int dx = x + px;
                if (dx < 0 || dx >= 256) continue;
                const uint8_t pen = spr[sy * 16 + sx];
                if (!pen) continue;
                line[dx] = palette_[size_t((color + pen + 256) & 0x3ff)];
            }
        }
    }
}

void TehkanWc::update_video() {
    draw_background();
    draw_chars_low();

    // Compose: scroll BG into 256×256, then FG low, sprites, FG high
    std::fill(composite_.begin(), composite_.end(), 0xff000000u);
    for (int y = 0; y < 256; y++) {
        const int sy = (y + scroll_y_) & 0xff;
        uint32_t* dst = composite_.data() + size_t(y * 256);
        const uint32_t* bg = layer_bg_.data() + size_t(sy * 512);
        for (int x = 0; x < 256; x++) {
            const int sx = (x + scroll_x_) & 0x1ff;
            dst[x] = bg[sx];
        }
        // FG low priority
        const uint32_t* lo = layer_fg_lo_.data() + size_t(y * 256);
        for (int x = 0; x < 256; x++) {
            if (lo[x] & 0x00ffffff) dst[x] = lo[x];
        }
    }
    draw_sprites();
    // FG high priority
    for (int y = 0; y < 256; y++) {
        uint32_t* dst = composite_.data() + size_t(y * 256);
        const uint32_t* hi = layer_fg_hi_.data() + size_t(y * 256);
        for (int x = 0; x < 256; x++) {
            if (hi[x] & 0x00ffffff) dst[x] = hi[x];
        }
    }
    // Crop y=16..239 → 224 lines (Pascal actualiza_trozo_final(0,16,256,224,2))
    for (int y = 0; y < kScreenHeight; y++) {
        std::copy_n(composite_.data() + size_t((y + 16) * 256), kScreenWidth,
                    framebuffer_.data() + size_t(y * kScreenWidth));
    }
}

void TehkanWc::run_frame() {
    const int total = int(kCpuClock / kFramesPerSecond);
    const int slice = total / kScanlines;
    // MSM5205 S96 → sample rate = clock/96
    const uint32_t msm_rate = msm_.sample_frequency();

    for (int line = 0; line < kScanlines; line++) {
        main_cpu_.run(slice);
        if (!sub_reset_) sub_cpu_.run(slice);
        sound_cpu_.run(slice);

        // Audio mix
        audio_accumulator_ += int64_t(slice) * AY8910::kSampleRate;
        while (audio_accumulator_ >= kCpuClock) {
            audio_accumulator_ -= kCpuClock;
            const int32_t s = (ay0_.update() + ay1_.update() + msm_.output()) / 3;
            int32_t clipped = s;
            if (clipped > 32767) clipped = 32767;
            if (clipped < -32768) clipped = -32768;
            audio_.push_back(int16_t(clipped));
        }
        // MSM VCLK
        if (msm_rate > 0) {
            msm_accumulator_ += int64_t(slice) * msm_rate;
            while (msm_accumulator_ >= kCpuClock) {
                msm_accumulator_ -= kCpuClock;
                // Stream next nibble from ADPCM ROM at msm_pos_
                // Dual-nibble protocol from Pascal
                if (msm_data_val_ != -1) {
                    msm_.data_w(uint8_t(msm_data_val_ & 0x0f));
                    msm_pos_ = uint16_t((msm_pos_ + 1) & 0x7fff);
                    msm_data_val_ = -1;
                } else {
                    // Need ROM byte — pull from MSM's rom via vclk which uses internal pos
                    // Drive with data_w after reading via set_start
                    msm_.set_start(msm_pos_ & 0x7fff);
                    msm_.vclk();  // fetches & decodes high nibble internally if ROM mode
                    // Fallback: treat as data-streamed
                }
                msm_.vclk();
            }
        }

        if (line == 240) {
            main_cpu_.set_irq(IrqLine::Hold);
            if (!sub_reset_) sub_cpu_.set_irq(IrqLine::Hold);
            sound_cpu_.set_irq(IrqLine::Hold);
            update_video();
        }
    }
}

void TehkanWc::set_inputs(const MachineInputs& inputs) {
    const auto& p1 = inputs.player1;
    const auto& p2 = inputs.player2;

    in0_ = 0x20;
    in1_ = 0x20;
    in2_ = 0x0f;
    if (p1.button1) in0_ &= ~0x20;
    if (p2.button1) in1_ &= ~0x20;
    if (inputs.coin1) in2_ &= ~0x01;
    if (inputs.coin2) in2_ &= ~0x02;
    if (p1.start) in2_ &= ~0x04;
    if (p2.start) in2_ &= ~0x08;

    // Digital stick → trackball delta
    stick0_x_ = int8_t((p1.right ? 8 : 0) - (p1.left ? 8 : 0));
    stick0_y_ = int8_t((p1.down ? 8 : 0) - (p1.up ? 8 : 0));
    stick1_x_ = int8_t((p2.right ? 8 : 0) - (p2.left ? 8 : 0));
    stick1_y_ = int8_t((p2.down ? 8 : 0) - (p2.up ? 8 : 0));
}

void TehkanWc::set_dip_switch(int bank, uint8_t value) {
    if (bank == 0) dsw_a_ = value;
    else if (bank == 1) dsw_b_ = value;
    else if (bank == 2) dsw_c_ = value;
}

void TehkanWc::drain_audio(std::vector<int16_t>& out) {
    out.insert(out.end(), audio_.begin(), audio_.end());
    audio_.clear();
}

}  // namespace dsp
