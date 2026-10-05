#include "drivers/arcade/bankpanic_hw.h"

#include <algorithm>
#include <cstring>

#include "core/rom_loader.h"
#include "cpu/irq_line.h"

namespace dsp {
namespace {

const std::vector<RomEntry> kBankRom = {
    {"epr-6175.7e", 0x4000, 0x0000, 0x044552b8},
    {"epr-6174.7f", 0x4000, 0x4000, 0xd29b1598},
    {"epr-6173.7h", 0x4000, 0x8000, 0xb8405d38},
    {"epr-6176.7d", 0x2000, 0xc000, 0xc98ac200},
};
const std::vector<RomEntry> kBankChars = {
    {"epr-6165.5l", 0x2000, 0x0000, 0xaef34a93},
    {"epr-6166.5k", 0x2000, 0x2000, 0xca13cb11},
};
const std::vector<RomEntry> kBankBg = {
    {"epr-6172.5b", 0x2000, 0x0000, 0xc4c4878b},
    {"epr-6171.5d", 0x2000, 0x2000, 0xa18165a1},
    {"epr-6170.5e", 0x2000, 0x4000, 0xb58aa8fa},
    {"epr-6169.5f", 0x2000, 0x6000, 0x1aa37fce},
    {"epr-6168.5h", 0x2000, 0x8000, 0x05f3a867},
    {"epr-6167.5i", 0x2000, 0xa000, 0x3fa337e1},
};
const std::vector<RomEntry> kBankProm = {
    {"pr-6177.8a", 0x20, 0x000, 0xeb70c5ae},
    {"pr-6178.6f", 0x100, 0x020, 0x0acca001},
    {"pr-6179.5a", 0x100, 0x120, 0xe53bafdb},
};

const std::vector<RomEntry> kCombhRom = {
    {"epr-10904.7e", 0x4000, 0x0000, 0x4b106335},
    {"epr-10905.7f", 0x4000, 0x4000, 0xa76fc390},
    {"epr-10906.7h", 0x4000, 0x8000, 0x16d54885},
    {"epr-10903.7d", 0x2000, 0xc000, 0xb7a59cab},
};
const std::vector<RomEntry> kCombhChars = {
    {"epr-10914.5l", 0x2000, 0x0000, 0x7d7a2340},
    {"epr-10913.5k", 0x2000, 0x2000, 0xd5c1a8ae},
};
const std::vector<RomEntry> kCombhBg = {
    {"epr-10907.5b", 0x2000, 0x0000, 0x08e5eea3},
    {"epr-10908.5d", 0x2000, 0x2000, 0xd9e413f5},
    {"epr-10909.5e", 0x2000, 0x4000, 0xfec7962c},
    {"epr-10910.5f", 0x2000, 0x6000, 0x33db0fa7},
    {"epr-10911.5h", 0x2000, 0x8000, 0x565d9e6d},
    {"epr-10912.5i", 0x2000, 0xa000, 0xcbe22738},
};
const std::vector<RomEntry> kCombhProm = {
    {"pr-10900.8a", 0x20, 0x000, 0xf95fcd66},
    {"pr-10901.6f", 0x100, 0x020, 0x6fd981c8},
    {"pr-10902.5a", 0x100, 0x120, 0x84d6bded},
};

constexpr uint32_t kTransparent = 0;

}  // namespace

BankPanicHw::BankPanicHw(Game game)
    : game_(game),
      cpu_(kCpuClock),
      sn0_(kCpuClock),
      sn1_(kCpuClock),
      sn2_(kCpuClock) {
    fg_layer_.assign(kWorkSize * kWorkSize, 0);
    bg_layer_.assign(kWorkSize * kWorkSize, 0);
    work_.assign(kWorkSize * kWorkSize, 0xff000000u);
    framebuffer_.assign(kVisWidth * kVisHeight, 0xff000000u);

    cpu_.set_memory_handlers([this](uint16_t a) { return read_byte(a); },
                             [this](uint16_t a, uint8_t v) { write_byte(a, v); });
    cpu_.set_io_handlers([this](uint16_t p) { return read_port(p); },
                         [this](uint16_t p, uint8_t v) { write_port(p, v); });
    cpu_.set_cycle_handler([this](int c) { on_cycles(c); });
}

const char* BankPanicHw::title() const {
    return game_ == Game::CombatHawk ? "Combat Hawk" : "Bank Panic";
}

bool BankPanicHw::init(const std::string& rom_path, std::string* error) {
    if (!load_roms(rom_path, error)) return false;
    fg_dirty_.fill(true);
    bg_dirty_.fill(true);
    reset();
    return true;
}

bool BankPanicHw::load_roms(const std::string& rom_path, std::string* error) {
    RomLoader loader;
    if (!loader.open(rom_path, error)) return false;

    memory_.fill(0);
    std::vector<uint8_t> main_rom(0x10000, 0);
    std::vector<uint8_t> chars(0x4000, 0);
    std::vector<uint8_t> bg(0xc000, 0);
    std::vector<uint8_t> prom(0x220, 0);

    if (game_ == Game::BankPanic) {
        if (!loader.load(kBankRom, main_rom, error)) return false;
        if (!loader.load(kBankChars, chars, error)) return false;
        if (!loader.load(kBankBg, bg, error)) return false;
        if (!loader.load(kBankProm, prom, error)) return false;
        dsw_ = 0xc0;
    } else {
        if (!loader.load(kCombhRom, main_rom, error)) return false;
        if (!loader.load(kCombhChars, chars, error)) return false;
        if (!loader.load(kCombhBg, bg, error)) return false;
        if (!loader.load(kCombhProm, prom, error)) return false;
        dsw_ = 0x10;
    }

    std::copy(main_rom.begin(), main_rom.end(), memory_.begin());
    decode_chars(chars);
    decode_tiles(bg);
    build_palette(prom);
    warnings_ = loader.warnings();
    return true;
}

void BankPanicHw::decode_chars(const std::vector<uint8_t>& rom) {
    GfxLayout layout;
    layout.width = 8;
    layout.height = 8;
    layout.total = 0x400;
    layout.planes = 2;
    layout.char_increment = 16 * 8;
    layout.plane_offsets = {0, 4};
    layout.x_offsets = {8 * 8 + 3, 8 * 8 + 2, 8 * 8 + 1, 8 * 8 + 0, 3, 2, 1, 0};
    layout.y_offsets = {0 * 8, 1 * 8, 2 * 8, 3 * 8, 4 * 8, 5 * 8, 6 * 8, 7 * 8};
    chars_.decode(layout, rom);
}

void BankPanicHw::decode_tiles(const std::vector<uint8_t>& rom) {
    GfxLayout layout;
    layout.width = 8;
    layout.height = 8;
    layout.total = 0x800;
    layout.planes = 3;
    layout.char_increment = 8 * 8;
    layout.plane_offsets = {0, 2048 * 8 * 8, 2048 * 2 * 8 * 8};
    layout.x_offsets = {7, 6, 5, 4, 3, 2, 1, 0};
    layout.y_offsets = {0 * 8, 1 * 8, 2 * 8, 3 * 8, 4 * 8, 5 * 8, 6 * 8, 7 * 8};
    tiles_.decode(layout, rom);
}

void BankPanicHw::build_palette(const std::vector<uint8_t>& prom) {
    for (int f = 0; f < 0x20; f++) {
        const uint8_t v = prom[size_t(f)];
        const int r = 0x21 * ((v >> 0) & 1) + 0x47 * ((v >> 1) & 1) + 0x97 * ((v >> 2) & 1);
        const int g = 0x21 * ((v >> 3) & 1) + 0x47 * ((v >> 4) & 1) + 0x97 * ((v >> 5) & 1);
        const int b = 0x00 + 0x47 * ((v >> 6) & 1) + 0x97 * ((v >> 7) & 1);
        palette_[size_t(f)] =
            0xff000000u | (uint32_t(r) << 16) | (uint32_t(g) << 8) | uint32_t(b);
    }

    // Color lookup tables (gfx[].colores in Pascal / MAME indirect pens).
    fg_lut_.fill(0);
    bg_lut_.fill(0);
    for (int f = 0; f < 256; f++) {
        const int index = ((f << 1) & 0x100) | (f & 0x7f);
        const uint8_t entry = uint8_t(prom[size_t(0x20 + index)] & 0x0f);
        fg_lut_[size_t(index)] = entry;
        bg_lut_[size_t(index)] = entry;
        fg_lut_[size_t(index | 0x80)] = uint8_t(entry | 0x10);
        bg_lut_[size_t(index | 0x80)] = uint8_t(entry | 0x10);
    }
}

void BankPanicHw::reset() {
    cpu_.reset();
    sn0_.reset();
    sn1_.reset();
    sn2_.reset();
    in0_ = 0;
    in1_ = 0;
    in2_ = 0;
    nmi_vblank_ = false;
    scroll_x_ = 0;
    color_hi_ = 0;
    display_on_ = true;
    priority_ = false;
    audio_accumulator_ = 0;
    audio_.clear();
}

void BankPanicHw::set_inputs(const MachineInputs& inputs) {
    in0_ = 0;
    in1_ = 0;
    in2_ = 0;
    if (inputs.player1.up) in0_ = uint8_t(in0_ | 0x01);
    if (inputs.player1.right) in0_ = uint8_t(in0_ | 0x02);
    if (inputs.player1.down) in0_ = uint8_t(in0_ | 0x04);
    if (inputs.player1.left) in0_ = uint8_t(in0_ | 0x08);
    if (inputs.player1.button1) in0_ = uint8_t(in0_ | 0x10);
    if (inputs.coin1) in0_ = uint8_t(in0_ | 0x20);
    if (inputs.player1.button2) in0_ = uint8_t(in0_ | 0x80);

    if (inputs.player2.up) in1_ = uint8_t(in1_ | 0x01);
    if (inputs.player2.right) in1_ = uint8_t(in1_ | 0x02);
    if (inputs.player2.down) in1_ = uint8_t(in1_ | 0x04);
    if (inputs.player2.left) in1_ = uint8_t(in1_ | 0x08);
    if (inputs.player2.button1) in1_ = uint8_t(in1_ | 0x10);
    if (inputs.player1.start) in1_ = uint8_t(in1_ | 0x20);
    if (inputs.player2.start) in1_ = uint8_t(in1_ | 0x40);
    if (inputs.player2.button2) in1_ = uint8_t(in1_ | 0x80);

    if (inputs.player1.button3) in2_ = uint8_t(in2_ | 0x01);
    if (inputs.player2.button3) in2_ = uint8_t(in2_ | 0x02);
    if (inputs.coin2) in2_ = uint8_t(in2_ | 0x04);
}

void BankPanicHw::set_dip_switch(int bank, uint8_t value) {
    if (bank == 0) dsw_ = value;
}

uint8_t BankPanicHw::read_byte(uint16_t address) { return memory_[address]; }

void BankPanicHw::write_byte(uint16_t address, uint8_t value) {
    if (address <= 0xdfff) return;  // ROM
    if (address <= 0xefff) {
        memory_[address] = value;
        return;
    }
    if (address <= 0xf7ff) {
        if (memory_[address] != value) {
            fg_dirty_[address & 0x3ff] = true;
            memory_[address] = value;
        }
        return;
    }
    if (memory_[address] != value) {
        bg_dirty_[address & 0x3ff] = true;
        memory_[address] = value;
    }
}

uint8_t BankPanicHw::read_port(uint16_t port) {
    switch (port & 0xff) {
        case 0:
            return in0_;
        case 1:
            return in1_;
        case 2:
            return in2_;
        case 4:
            return dsw_;
        default:
            return 0xff;
    }
}

void BankPanicHw::write_port(uint16_t port, uint8_t value) {
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
        case 5:
            scroll_x_ = value;
            break;
        case 7: {
            const bool new_prio = (value & 0x02) != 0;
            if (new_prio != priority_) {
                priority_ = new_prio;
                fg_dirty_.fill(true);
                bg_dirty_.fill(true);
            }
            display_on_ = (value & 0x04) != 0;
            const uint8_t new_hi = uint8_t((value & 0x08) >> 3);
            if (new_hi != color_hi_) {
                color_hi_ = new_hi;
                fg_dirty_.fill(true);
                bg_dirty_.fill(true);
            }
            nmi_vblank_ = (value & 0x10) != 0;
            break;
        }
        default:
            break;
    }
}

void BankPanicHw::on_cycles(int cycles) {
    audio_accumulator_ += int64_t(cycles) * SN76496::kSampleRate;
    while (audio_accumulator_ >= int64_t(kCpuClock)) {
        audio_accumulator_ -= int64_t(kCpuClock);
        const int32_t sample = sn0_.update() + sn1_.update() + sn2_.update();
        audio_.push_back(int16_t(std::clamp(sample, int32_t(-32768), int32_t(32767))));
    }
}

void BankPanicHw::blit_tile(std::vector<uint32_t>& dest, const GfxSet& gfx,
                            const std::array<uint8_t, 512>& lut, int x, int y, int code,
                            int color_base, bool flip_x, bool masked, uint8_t mask) {
    const uint8_t* pixels = gfx.element(code);
    const int w = gfx.width();
    const int h = gfx.height();
    for (int row = 0; row < h; row++) {
        const int dy = y + row;
        if (dy < 0 || dy >= kWorkSize) continue;
        for (int col = 0; col < w; col++) {
            const int dx = x + col;
            if (dx < 0 || dx >= kWorkSize) continue;
            const int sx = flip_x ? (w - 1 - col) : col;
            const uint8_t pen = pixels[size_t(row * w + sx)];
            const uint8_t punto = lut[size_t((color_base + pen) & 0x1ff)];
            if (masked && (punto & mask) == 0) {
                dest[size_t(dy * kWorkSize + dx)] = kTransparent;
            } else {
                dest[size_t(dy * kWorkSize + dx)] = palette_[size_t(punto & 0x1f)];
            }
        }
    }
}

void BankPanicHw::draw_fg_layer(bool masked) {
    for (int f = 0; f < 0x400; f++) {
        if (!fg_dirty_[size_t(f)]) continue;
        fg_dirty_[size_t(f)] = false;
        const int y = f >> 5;
        const int x = f & 0x1f;
        const uint8_t atrib = memory_[size_t(0xf400 + f)];
        const int color = (atrib >> 3) | (int(color_hi_) << 5);
        const int nchar = memory_[size_t(0xf000 + f)] + ((atrib & 3) << 8);
        const bool flip_x = (atrib & 4) != 0;
        blit_tile(fg_layer_, chars_, fg_lut_, x * 8, y * 8, nchar, color << 2, flip_x, masked,
                  0x1f);
    }
}

void BankPanicHw::draw_bg_layer(bool masked) {
    for (int f = 0; f < 0x400; f++) {
        if (!bg_dirty_[size_t(f)]) continue;
        bg_dirty_[size_t(f)] = false;
        const int y = f >> 5;
        const int x = f & 0x1f;
        const uint8_t atrib = memory_[size_t(0xfc00 + f)];
        const int color = (atrib >> 4) | (int(color_hi_) << 4);
        const int nchar = memory_[size_t(0xf800 + f)] + ((atrib & 7) << 8);
        const bool flip_x = (atrib & 8) != 0;
        blit_tile(bg_layer_, tiles_, bg_lut_, x * 8, y * 8, nchar, (color << 3) + 256, flip_x,
                  masked, 0x0f);
    }
}

void BankPanicHw::update_video() {
    if (!display_on_) {
        std::fill(framebuffer_.begin(), framebuffer_.end(), 0xff000000u);
        return;
    }

    if (priority_) {
        draw_fg_layer(false);
        draw_bg_layer(true);
    } else {
        draw_bg_layer(false);
        draw_fg_layer(true);
    }

    std::fill(work_.begin(), work_.end(), 0xff000000u);

    auto blit_layer = [&](const std::vector<uint32_t>& layer, int scroll_x) {
        for (int y = 0; y < kWorkSize; y++) {
            for (int x = 0; x < kWorkSize; x++) {
                const int sx = (x + scroll_x) & 0xff;
                const uint32_t pixel = layer[size_t(y * kWorkSize + sx)];
                if (pixel) work_[size_t(y * kWorkSize + x)] = pixel;
            }
        }
    };

    if (!priority_) {
        // BG opaque, then scrolled FG on top.
        for (int i = 0; i < kWorkSize * kWorkSize; i++) {
            if (bg_layer_[size_t(i)]) work_[size_t(i)] = bg_layer_[size_t(i)];
        }
        blit_layer(fg_layer_, scroll_x_);
    } else {
        // Scrolled FG first, then BG (masked) on top.
        blit_layer(fg_layer_, scroll_x_);
        for (int i = 0; i < kWorkSize * kWorkSize; i++) {
            if (bg_layer_[size_t(i)]) work_[size_t(i)] = bg_layer_[size_t(i)];
        }
    }

    // Crop (24,16)-(248,240) → 224×224. Combat Hawk rotates 270° (match Williams).
    if (game_ == Game::CombatHawk) {
        for (int y = 0; y < kVisHeight; y++) {
            for (int x = 0; x < kVisWidth; x++) {
                // rot[(W-1-sx)*W + sy] = vis[sy*W + sx]  ⇒  fb[y][x] = crop[x][W-1-y]
                const int sx = 24 + x;
                const int sy = 16 + (kVisWidth - 1 - y);
                framebuffer_[size_t(y * kVisWidth + x)] = work_[size_t(sy * kWorkSize + sx)];
            }
        }
    } else {
        for (int y = 0; y < kVisHeight; y++) {
            for (int x = 0; x < kVisWidth; x++) {
                framebuffer_[size_t(y * kVisWidth + x)] =
                    work_[size_t((y + 16) * kWorkSize + (x + 24))];
            }
        }
    }
}

void BankPanicHw::run_frame() {
    const int cycles_per_line = int(double(kCpuClock) / kFramesPerSecond / kScanlines);
    for (int line = 0; line < kScanlines; line++) {
        if (line == 240) {
            if (nmi_vblank_) cpu_.set_nmi(IrqLine::Pulse);
            update_video();
        }
        cpu_.run(cycles_per_line);
    }
}

void BankPanicHw::drain_audio(std::vector<int16_t>& out) {
    out.swap(audio_);
    audio_.clear();
}

}  // namespace dsp
