#include "drivers/arcade/appoooh.h"

#include <algorithm>
#include <cstring>

#include "core/rom_loader.h"
#include "cpu/irq_line.h"
#include "machine/sega_decrypt.h"

namespace dsp {
namespace {

const std::vector<RomEntry> kAppMain = {
    {"epr-5906.bin", 0x2000, 0x00000, 0xfffae7fe},
    {"epr-5907.bin", 0x2000, 0x02000, 0x57696cd6},
    {"epr-5908.bin", 0x2000, 0x04000, 0x4537cddc},
    {"epr-5909.bin", 0x2000, 0x06000, 0xcf82718d},
    {"epr-5910.bin", 0x2000, 0x08000, 0x312636da},
    {"epr-5911.bin", 0x2000, 0x0a000, 0x0bc2acaa},
    {"epr-5913.bin", 0x2000, 0x0c000, 0xf5a0e6a7},
    {"epr-5912.bin", 0x2000, 0x0e000, 0x3c3915ab},
    {"epr-5914.bin", 0x2000, 0x10000, 0x58792d4a},
};
const std::vector<RomEntry> kAppChar1 = {
    {"epr-5895.bin", 0x4000, 0x0000, 0x4b0d4294},
    {"epr-5896.bin", 0x4000, 0x4000, 0x7bc84d75},
    {"epr-5897.bin", 0x4000, 0x8000, 0x745f3ffa},
};
const std::vector<RomEntry> kAppChar2 = {
    {"epr-5898.bin", 0x4000, 0x0000, 0xcf01644d},
    {"epr-5899.bin", 0x4000, 0x4000, 0x885ad636},
    {"epr-5900.bin", 0x4000, 0x8000, 0xa8ed13f3},
};
const std::vector<RomEntry> kAppProm = {
    {"pr5921.prm", 0x20, 0x000, 0xf2437229},
    {"pr5922.prm", 0x100, 0x020, 0x85c542bf},
    {"pr5923.prm", 0x100, 0x120, 0x16acbd53},
};
const std::vector<RomEntry> kAppAdpcm = {
    {"epr-5901.bin", 0x2000, 0x0000, 0x170a10a4},
    {"epr-5902.bin", 0x2000, 0x2000, 0xf6981640},
    {"epr-5903.bin", 0x2000, 0x4000, 0x0439df50},
    {"epr-5904.bin", 0x2000, 0x6000, 0x9988f2ae},
    {"epr-5905.bin", 0x2000, 0x8000, 0xfb5cd70e},
};

const std::vector<RomEntry> kRoboMain = {
    {"epr-7540.13d", 0x8000, 0x00000, 0xa2a54237},
    {"epr-7541.14d", 0x8000, 0x08000, 0xcbf7d1a8},
    {"epr-7542.15d", 0x8000, 0x10000, 0x3475fbd4},
};
const std::vector<RomEntry> kRoboChar1 = {
    {"epr-7544.7h", 0x8000, 0x00000, 0x07b846ce},
    {"epr-7545.6h", 0x8000, 0x08000, 0xe99897be},
    {"epr-7546.5h", 0x8000, 0x10000, 0x1559235a},
};
const std::vector<RomEntry> kRoboChar2 = {
    {"epr-7547.7d", 0x8000, 0x00000, 0xb87ad4a4},
    {"epr-7548.6d", 0x8000, 0x08000, 0x8b9c75b3},
    {"epr-7549.5d", 0x8000, 0x10000, 0xf640afbb},
};
const std::vector<RomEntry> kRoboProm = {
    {"pr7571.10a", 0x20, 0x000, 0xe82c6d5c},
    {"pr7572.7f", 0x100, 0x020, 0x2b083d0c},
    {"pr7573.7g", 0x100, 0x120, 0x2b083d0c},
};
const std::vector<RomEntry> kRoboAdpcm = {
    {"epr-7543.12b", 0x8000, 0x0000, 0x4d108c49},
};

void blit(uint32_t* dest, int dw, int dh, int dx, int dy, const uint8_t* src, int sw, int sh,
          const uint32_t* pal, int color_base, bool transparent, bool flipx, bool flipy) {
    for (int y = 0; y < sh; y++) {
        const int sy = flipy ? (sh - 1 - y) : y;
        const int py = dy + y;
        if (py < 0 || py >= dh) continue;
        for (int x = 0; x < sw; x++) {
            const int sx = flipx ? (sw - 1 - x) : x;
            const int px = dx + x;
            if (px < 0 || px >= dw) continue;
            const uint8_t pen = src[sy * sw + sx];
            if (transparent && pen == 0) continue;
            dest[py * dw + px] = pal[(color_base + pen) & 0x1ff];
        }
    }
}

void decode_chars(GfxSet& out, const std::vector<uint8_t>& rom, int total) {
    GfxLayout layout;
    layout.width = 8;
    layout.height = 8;
    layout.total = total;
    layout.planes = 3;
    layout.char_increment = 8 * 8;
    layout.plane_offsets = {total * 8 * 8 * 2, total * 8 * 8 * 1, total * 8 * 8 * 0};
    layout.x_offsets = {7, 6, 5, 4, 3, 2, 1, 0};
    layout.y_offsets = {0 * 8, 1 * 8, 2 * 8, 3 * 8, 4 * 8, 5 * 8, 6 * 8, 7 * 8};
    out.decode(layout, rom);
}

void decode_sprites(GfxSet& out, const std::vector<uint8_t>& rom, int total) {
    GfxLayout layout;
    layout.width = 16;
    layout.height = 16;
    layout.total = total;
    layout.planes = 3;
    layout.char_increment = 32 * 8;
    // Same plane base as chars (num*8*8*N) — Pascal sprites_gfx uses num*8*8
    layout.plane_offsets = {total * 8 * 8 * 2, total * 8 * 8 * 1, total * 8 * 8 * 0};
    layout.x_offsets = {7, 6, 5, 4, 3, 2, 1, 0, 8 * 8 + 7, 8 * 8 + 6, 8 * 8 + 5, 8 * 8 + 4,
                        8 * 8 + 3, 8 * 8 + 2, 8 * 8 + 1, 8 * 8 + 0};
    layout.y_offsets = {0 * 8,  1 * 8,  2 * 8,  3 * 8,  4 * 8,  5 * 8,  6 * 8,  7 * 8,
                        16 * 8, 17 * 8, 18 * 8, 19 * 8, 20 * 8, 21 * 8, 22 * 8, 23 * 8};
    out.decode(layout, rom);
}

}  // namespace

Appoooh::Appoooh(Variant v)
    : variant_(v),
      cpu_(kCpuClock),
      sn0_(kCpuClock),
      sn1_(kCpuClock),
      sn2_(kCpuClock),
      msm_(kMsmClock, 64, 4) {
    framebuffer_.assign(kScreenWidth * kScreenHeight, 0xff000000u);
    cpu_.set_memory_handlers([this](uint16_t a) { return main_read(a); },
                             [this](uint16_t a, uint8_t v) { main_write(a, v); });
    cpu_.set_io_handlers([this](uint16_t p) { return main_in(p); },
                         [this](uint16_t p, uint8_t v) { main_out(p, v); });
    msm_.set_vclk_handler([this]() { adpcm_vclk(); });
}

bool Appoooh::init(const std::string& rom_path, std::string* error) {
    RomLoader loader;
    if (!loader.open(rom_path, error)) return false;

    const bool robo = (variant_ == Variant::RoboWres);
    const int tile_total = robo ? 0x1000 : 0x800;

    std::vector<uint8_t> main_rom(0x18000, 0);
    if (!loader.load(robo ? kRoboMain : kAppMain, main_rom, error)) return false;

    if (robo) {
        // Decrypt first 32K for opcode fetches (315-5179)
        std::vector<uint8_t> encrypted(main_rom.begin(), main_rom.begin() + 0x8000);
        std::vector<uint8_t> data, opcodes;
        sega_decrypt_type2(encrypted, SegaDecrypt2Chip::S315_5179, 0, &data, &opcodes);
        robowres_opcodes_ = std::move(opcodes);
        std::memcpy(memory_.data(), data.data(), 0x8000);
        cpu_.set_opcode_read([this](uint16_t a) -> uint8_t {
            if (a <= 0x7fff && a < robowres_opcodes_.size()) return robowres_opcodes_[a];
            return main_read(a);
        });
        std::memcpy(memory_.data() + 0x8000, main_rom.data() + 0x8000, 0x2000);
        std::memcpy(rom_bank_[0].data(), main_rom.data() + 0xa000, 0x4000);
        std::memcpy(rom_bank_[1].data(), main_rom.data() + 0x12000, 0x4000);
        sprite_base_ = 0x200;
        dsw_ = 0xe0;
    } else {
        std::memcpy(memory_.data(), main_rom.data(), 0xa000);
        std::memcpy(rom_bank_[0].data(), main_rom.data() + 0xa000, 0x4000);
        std::memcpy(rom_bank_[1].data(), main_rom.data() + 0xe000, 0x4000);
        sprite_base_ = 0;
        dsw_ = 0x60;
    }

    std::vector<uint8_t> char1(robo ? 0x18000 : 0xc000, 0);
    if (!loader.load(robo ? kRoboChar1 : kAppChar1, char1, error)) return false;
    decode_chars(chars0_, char1, tile_total);
    decode_sprites(sprites0_, char1, tile_total);

    std::vector<uint8_t> char2(robo ? 0x18000 : 0xc000, 0);
    if (!loader.load(robo ? kRoboChar2 : kAppChar2, char2, error)) return false;
    decode_chars(chars1_, char2, tile_total);
    decode_sprites(sprites1_, char2, tile_total);

    std::vector<uint8_t> prom(0x220, 0);
    if (!loader.load(robo ? kRoboProm : kAppProm, prom, error)) return false;
    for (int f = 0; f < 0x200; f++) {
        int pen = prom[0x20 + f] & 0x0f;
        if (f > 0xff && !robo) pen |= 0x10;
        const uint8_t v = prom[pen];
        const int r = 0x21 * ((v >> 0) & 1) + 0x47 * ((v >> 1) & 1) + 0x97 * ((v >> 2) & 1);
        const int g = 0x21 * ((v >> 3) & 1) + 0x47 * ((v >> 4) & 1) + 0x97 * ((v >> 5) & 1);
        const int b = 0x00 * 0 + 0x47 * ((v >> 6) & 1) + 0x97 * ((v >> 7) & 1);
        palette_[f] = 0xff000000u | (uint32_t(r) << 16) | (uint32_t(g) << 8) | uint32_t(b);
    }

    adpcm_rom_.assign(0x10000, 0);
    if (!loader.load(robo ? kRoboAdpcm : kAppAdpcm, adpcm_rom_, error)) return false;
    msm_.set_rom(adpcm_rom_);

    warnings_ = loader.warnings();
    reset();
    return true;
}

void Appoooh::reset() {
    cpu_.reset();
    sn0_.reset();
    sn1_.reset();
    sn2_.reset();
    msm_.reset();
    in0_ = in1_ = in2_ = 0;
    nmi_vblank_ = false;
    adpcm_playing_ = false;
    adpcm_pos_ = 0;
    priority_ = 0xff;
    rom_bank_sel_ = 0;
    flip_screen_ = false;
    audio_accum_ = 0;
    msm_accum_ = 0;
    audio_.clear();
    std::fill(framebuffer_.begin(), framebuffer_.end(), 0xff000000u);
}

uint8_t Appoooh::main_read(uint16_t address) {
    if (variant_ == Variant::RoboWres && address <= 0x7fff) {
        // Opcode vs data view — Z80 doesn't expose M1 here; use data view
        // (opcodes only needed for true M1; Pascal switches on z80_0.opcode).
        // Prefer data mapping for non-M1; approximate with data.
        return memory_[address];
    }
    if (address <= 0x9fff || address >= 0xe000) return memory_[address];
    if (address >= 0xa000 && address <= 0xdfff) {
        return rom_bank_[rom_bank_sel_ & 1][address - 0xa000];
    }
    return 0xff;
}

void Appoooh::main_write(uint16_t address, uint8_t value) {
    if (address <= 0xdfff) return;  // ROM
    if (address >= 0xe000 && address <= 0xefff) {
        memory_[address] = value;
        return;
    }
    if (address >= 0xf000 && address <= 0xf7ff) {
        memory_[address] = value;
        return;
    }
    if (address >= 0xf800) {
        memory_[address] = value;
        return;
    }
}

uint8_t Appoooh::main_in(uint16_t port) {
    switch (port & 0xff) {
        case 0:
            return in0_;
        case 1:
            return in1_;
        case 3:
            return dsw_;
        case 4:
            return in2_;
        default:
            return 0xff;
    }
}

void Appoooh::main_out(uint16_t port, uint8_t value) {
    switch (port & 0xff) {
        case 0:
            sn0_.write(value);
            break;
        case 1:
            sn1_.write(value);
            break;
        case 2:
            sn2_.write(value);
            break;
        case 3:
            adpcm_pos_ = (uint32_t(value) << 8) * 2;
            msm_.set_reset(false);
            adpcm_playing_ = true;
            break;
        case 4:
            nmi_vblank_ = (value & 1) != 0;
            flip_screen_ = (value & 2) != 0;
            priority_ = (value & 0x30) >> 4;
            rom_bank_sel_ = (value & 0x40) >> 6;
            break;
        case 5:
            // scroll_x unused in Pascal
            break;
        default:
            break;
    }
}

void Appoooh::adpcm_vclk() {
    if (!adpcm_playing_) return;
    if (adpcm_pos_ / 2 >= adpcm_rom_.size()) {
        msm_.set_reset(true);
        adpcm_playing_ = false;
        return;
    }
    const uint8_t data = adpcm_rom_[adpcm_pos_ / 2];
    if (data == 0x70) {
        msm_.set_reset(true);
        adpcm_playing_ = false;
        return;
    }
    if (adpcm_pos_ & 1) {
        msm_.data_w(data & 0x0f);
    } else {
        msm_.data_w(data >> 4);
    }
    adpcm_pos_ = (adpcm_pos_ + 1) & 0x1ffff;
}

void Appoooh::draw_sprites(int bank, uint32_t* dest) {
    const int base = 0xf000 + 0x800 * bank;
    GfxSet& gfx = (bank == 0) ? sprites0_ : sprites1_;
    const int color_bank = 0x100 * bank;
    for (int f = 7; f >= 0; f--) {
        const uint8_t atrib = memory_[base + 0x001 + f * 4];
        const uint8_t atrib2 = memory_[base + 0x002 + f * 4];
        const int nchar = sprite_base_ + ((atrib >> 2) + ((atrib2 & 0xe0) << 1));
        const int color = (atrib2 & 0x0f) << 3;
        const int sx = memory_[base + 0x003 + f * 4];
        const int sy = 240 - memory_[base + 0x000 + f * 4];
        const bool flipx = (atrib & 1) != 0;
        blit(dest, 256, 256, sx, sy, gfx.element(nchar), 16, 16, palette_.data(),
             color + color_bank, true, flipx, false);
    }
}

void Appoooh::update_video() {
    std::array<uint32_t, 256 * 256> bg{};
    std::array<uint32_t, 256 * 256> fg{};
    bg.fill(0);
    fg.fill(0);

    for (int f = 0; f < 0x400; f++) {
        const int y = f >> 5;
        const int x = f & 0x1f;
        // Background
        {
            const uint8_t atrib = memory_[0xfc00 + f];
            const int color = atrib & 0x0f;
            const int nchar = memory_[0xf800 + f] + ((atrib >> 5) & 7) * 256;
            const bool flipx = (atrib & 0x10) != 0;
            blit(bg.data(), 256, 256, x * 8, y * 8, chars1_.element(nchar), 8, 8, palette_.data(),
                 (color << 3) + 256, true, flipx, false);
        }
        // Foreground
        {
            const uint8_t atrib = memory_[0xf400 + f];
            const int color = atrib & 0x0f;
            const int nchar = memory_[0xf000 + f] + ((atrib >> 5) & 7) * 256;
            const bool flipx = (atrib & 0x10) != 0;
            blit(fg.data(), 256, 256, x * 8, y * 8, chars0_.element(nchar), 8, 8, palette_.data(),
                 color << 3, true, flipx, false);
        }
    }

    std::array<uint32_t, 256 * 256> compose{};
    compose.fill(0xff000000u);
    for (int i = 0; i < 256 * 256; i++) {
        if (bg[i]) compose[i] = bg[i];
    }
    if (priority_ == 0) {
        for (int i = 0; i < 256 * 256; i++) {
            if (fg[i]) compose[i] = fg[i];
        }
    }
    if (priority_ == 1) {
        draw_sprites(0, compose.data());
        draw_sprites(1, compose.data());
    } else {
        draw_sprites(1, compose.data());
        draw_sprites(0, compose.data());
    }
    if (priority_ != 0) {
        for (int i = 0; i < 256 * 256; i++) {
            if (fg[i]) compose[i] = fg[i];
        }
    }

    for (int y = 0; y < kScreenHeight; y++) {
        for (int x = 0; x < kScreenWidth; x++) {
            int sx = x, sy = y + 16;
            if (flip_screen_) {
                sx = 255 - x;
                sy = 255 - (y + 16);
            }
            const uint32_t pix = compose[sy * 256 + sx];
            framebuffer_[size_t(y * kScreenWidth + x)] = pix ? pix : 0xff000000u;
        }
    }
}

void Appoooh::on_cycles(int cycles) {
    // MSM5205 VCLK at clock/64
    const uint32_t vclk_hz = msm_.sample_frequency();
    if (vclk_hz > 0) {
        msm_accum_ += int64_t(cycles) * int64_t(vclk_hz);
        while (msm_accum_ >= int64_t(kCpuClock)) {
            msm_accum_ -= int64_t(kCpuClock);
            msm_.vclk();
        }
    }

    audio_accum_ += int64_t(cycles) * SN76496::kSampleRate;
    while (audio_accum_ >= int64_t(kCpuClock)) {
        audio_accum_ -= int64_t(kCpuClock);
        int32_t s = sn0_.update() + sn1_.update() + sn2_.update() + msm_.output();
        audio_.push_back(int16_t(std::clamp(s, int32_t(-32768), int32_t(32767))));
    }
}

void Appoooh::run_frame() {
    const int slice = std::max(1, int(kCpuClock / kFramesPerSecond) / kScanlines);
    for (int line = 0; line < kScanlines; line++) {
        if (line == 240) {
            if (nmi_vblank_) cpu_.set_nmi(IrqLine::Pulse);
            update_video();
        }
        cpu_.run(slice);
        on_cycles(slice);
    }
}

void Appoooh::set_inputs(const MachineInputs& inputs) {
    // Active-high (Pascal ORs bits when pressed)
    in0_ = in1_ = in2_ = 0;
    const auto& p1 = inputs.player1;
    const auto& p2 = inputs.player2;
    if (p1.up) in0_ |= 0x01;
    if (p1.right) in0_ |= 0x02;
    if (p1.down) in0_ |= 0x04;
    if (p1.left) in0_ |= 0x08;
    if (p1.button1) in0_ |= 0x10;
    if (inputs.coin1) in0_ |= 0x20;
    if (inputs.coin2) in0_ |= 0x40;
    if (p1.button2) in0_ |= 0x80;

    if (p2.up) in1_ |= 0x01;
    if (p2.right) in1_ |= 0x02;
    if (p2.down) in1_ |= 0x04;
    if (p2.left) in1_ |= 0x08;
    if (p2.button1) in1_ |= 0x10;
    if (p1.start) in1_ |= 0x20;
    if (p2.start) in1_ |= 0x40;
    if (p2.button2) in1_ |= 0x80;
}

void Appoooh::set_dip_switch(int bank, uint8_t value) {
    if (bank == 0) dsw_ = value;
}

void Appoooh::drain_audio(std::vector<int16_t>& out) {
    out.insert(out.end(), audio_.begin(), audio_.end());
    audio_.clear();
}

}  // namespace dsp
