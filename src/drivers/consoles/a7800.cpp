#include "drivers/consoles/a7800.h"

#include <algorithm>
#include <cstring>
#include <fstream>

#include <cmath>

namespace dsp {
namespace {
// The 7800 uses the same colour encoding as Atari's 8-bit computers: the
// high nibble picks one of 16 hues on the NTSC colour wheel, the low nibble
// the luminance. Built once from the standard decode rather than a table.
const std::array<uint32_t, 256>& palette() {
    static const std::array<uint32_t, 256> table = [] {
        std::array<uint32_t, 256> p{};
        for (int i = 0; i < 256; i++) {
            const int hue = (i >> 4) & 0x0f;
            const int lum = i & 0x0f;
            const double y = 0.0625 + double(lum) * 0.0625 * 0.9375;
            double r = y, g = y, b = y;
            if (hue != 0) {
                const double angle = (double(hue) - 1.0) * (2.0 * 3.14159265358979 / 15.0) - 0.4;
                const double sat = 0.35;
                const double iq_i = sat * std::cos(angle);
                const double iq_q = sat * std::sin(angle);
                r = y + 0.956 * iq_i + 0.621 * iq_q;
                g = y - 0.272 * iq_i - 0.647 * iq_q;
                b = y - 1.106 * iq_i + 1.703 * iq_q;
            }
            const auto clamp8 = [](double v) {
                return uint32_t(std::clamp(int(v * 255.0 + 0.5), 0, 255));
            };
            p[size_t(i)] = 0xff000000u | (clamp8(r) << 16) | (clamp8(g) << 8) | clamp8(b);
        }
        return p;
    }();
    return table;
}
}  // namespace

A7800::A7800(Region region)
    : region_(region), cpu_(kCpuClock, M6502::Type::Nmos) {
    cpu_.set_memory_handlers([this](uint16_t a) { return cpu_read(a); },
                             [this](uint16_t a, uint8_t v) { cpu_write(a, v); });
    maria_.set_read_handler([this](uint16_t a) { return cpu_read(a); });
    maria_.set_dli_handler([this] { cpu_.set_nmi(IrqLine::Pulse); });
}

bool A7800::load_cart(const std::string& path, std::string* error) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        if (error) *error = "cannot open cartridge: " + path;
        return false;
    }
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(f)),
                              std::istreambuf_iterator<char>());
    if (data.size() < 0x1000) {
        if (error) *error = "cartridge too small";
        return false;
    }
    // .a78 files carry a 128-byte header; raw dumps do not.
    if (data.size() > 128 && std::memcmp(data.data() + 1, "ATARI7800", 9) == 0) {
        const uint16_t type = uint16_t((data[53] << 8) | data[54]);
        supergame_ = (type & 0x0002) != 0;
        bank6_at_4000_ = (type & 0x0010) != 0;
        if (data[57] == 1) region_ = Region::Pal;
        data.erase(data.begin(), data.begin() + 128);
    } else {
        supergame_ = data.size() > 0xc000;
    }
    cart_ = std::move(data);
    cart_banks_ = int(cart_.size() / 0x4000);
    return true;
}

bool A7800::init(const std::string& rom_path, std::string* error) {
    if (!load_cart(rom_path, error)) return false;
    reset();
    return true;
}

void A7800::reset() {
    ram_.fill(0);
    riot_ram_.fill(0);
    maria_.reset();
    tia_.reset();
    bank_ = 0;
    riot_timer_ = 0;
    riot_shift_ = 10;
    riot_irq_ = false;
    swcha_ = 0xff;
    swchb_ = 0x0b;   // both difficulty switches B, colour TV, no console switch pressed
    framebuffer_.fill(palette()[0]);
    audio_.clear();
    cpu_.reset();
}

uint8_t A7800::cart_read(uint16_t address) const {
    if (cart_.empty()) return 0xff;
    if (supergame_) {
        // 16K banks: a switchable window at $8000, the last bank fixed at
        // $C000, and optionally bank 6 exposed at $4000.
        if (address >= 0xc000) {
            const size_t base = size_t(cart_banks_ - 1) * 0x4000;
            return cart_[base + (address - 0xc000)];
        }
        if (address >= 0x8000) {
            const size_t base = size_t(bank_ % std::max(cart_banks_, 1)) * 0x4000;
            return cart_[base + (address - 0x8000)];
        }
        if (bank6_at_4000_ && cart_banks_ > 6) return cart_[6 * 0x4000 + (address - 0x4000)];
        return 0xff;
    }
    // Plain carts sit against the top of the address space.
    const size_t size = cart_.size();
    const uint32_t start = uint32_t(0x10000 - size);
    if (address < start) return 0xff;
    return cart_[address - start];
}

void A7800::cart_write(uint16_t address, uint8_t value) {
    // SuperGame carts select the $8000 bank by writing anywhere above it.
    if (supergame_ && address >= 0x8000) bank_ = uint8_t(value & 0x0f);
}

uint8_t A7800::cpu_read(uint16_t a) {
    if (a < 0x0020) {                         // TIA
        if (a >= 0x08 && a <= 0x0d) return inpt_[a - 0x08];
        return 0;
    }
    if (a < 0x0040) return maria_.read(a);
    if (a < 0x0100) return ram_[size_t(a) + 0x800];
    if (a < 0x0120) return (a >= 0x108 && a <= 0x10d) ? inpt_[a - 0x108] : 0;
    if (a < 0x0140) return maria_.read(a);
    if (a < 0x0200) return ram_[size_t(a) + 0x800];
    if (a < 0x0220) return 0;
    if (a < 0x0240) return maria_.read(a);
    if (a < 0x0280) return 0;
    if (a < 0x0300) {                         // RIOT I/O
        switch (a & 0x07) {
            case 0: return swcha_;
            case 2: return swchb_;
            case 4: case 6: {                 // INTIM
                riot_irq_ = false;
                return uint8_t(riot_timer_ >> riot_shift_);
            }
            case 5: case 7: return riot_irq_ ? 0x80 : 0x00;
            default: return 0;
        }
    }
    if (a < 0x0480) return 0;
    if (a < 0x0500) return riot_ram_[a & 0x7f];
    if (a < 0x1800) return 0;
    if (a < 0x2800) return ram_[a - 0x1800];
    if (a < 0x4000) return ram_[(a - 0x2800) & 0xfff];
    return cart_read(a);
}

void A7800::cpu_write(uint16_t a, uint8_t v) {
    if (a < 0x0020) { tia_.write(uint8_t(a), v); return; }   // TIA: audio registers
    if (a < 0x0040) { maria_.write(a, v); return; }
    if (a < 0x0100) { ram_[size_t(a) + 0x800] = v; return; }
    if (a < 0x0120) return;
    if (a < 0x0140) { maria_.write(a, v); return; }
    if (a < 0x0200) { ram_[size_t(a) + 0x800] = v; return; }
    if (a < 0x0220) return;
    if (a < 0x0240) { maria_.write(a, v); return; }
    if (a < 0x0280) return;
    if (a < 0x0300) {
        if (a & 0x04) {                       // TIM1T/TIM8T/TIM64T/T1024T
            static const int shifts[4] = {0, 3, 6, 10};
            riot_shift_ = shifts[a & 0x03];
            riot_timer_ = uint32_t(v) << riot_shift_;
            riot_irq_ = false;
        }
        return;
    }
    if (a < 0x0480) return;
    if (a < 0x0500) { riot_ram_[a & 0x7f] = v; return; }
    if (a < 0x1800) return;
    if (a < 0x2800) { ram_[a - 0x1800] = v; return; }
    if (a < 0x4000) { ram_[(a - 0x2800) & 0xfff] = v; return; }
    cart_write(a, v);
}

void A7800::set_inputs(const MachineInputs& in) {
    // SWCHA holds both joystick directions, active low.
    uint8_t a = 0xff;
    if (in.player1.up) a &= uint8_t(~0x10);
    if (in.player1.down) a &= uint8_t(~0x20);
    if (in.player1.left) a &= uint8_t(~0x40);
    if (in.player1.right) a &= uint8_t(~0x80);
    if (in.player2.up) a &= uint8_t(~0x01);
    if (in.player2.down) a &= uint8_t(~0x02);
    if (in.player2.left) a &= uint8_t(~0x04);
    if (in.player2.right) a &= uint8_t(~0x08);
    swcha_ = a;

    // Fire buttons arrive on the TIA's INPT4/INPT5, active low.
    inpt_[4] = in.player1.button1 ? 0x00 : 0x80;
    inpt_[5] = in.player2.button1 ? 0x00 : 0x80;

    uint8_t b = 0x0b;
    if (in.player1.start) b &= uint8_t(~0x02);       // console RESET
    if (in.coin1) b &= uint8_t(~0x01);        // console SELECT
    swchb_ = b;
}

void A7800::run_frame() {
    const int lines = region_ == Region::Pal ? 312 : 262;
    for (int line = 0; line < lines; line++) {
        maria_.scanline(line, lines);

        int budget = kCyclesPerLine;
        if (maria_.dma_on() && line > 15 && line < lines - 5) {
            // MARIA holds the CPU off the bus while it fetches the line.
            budget -= 24;
        }
        if (maria_.wsync()) {
            // WSYNC parks the CPU until the start of the next line.
            maria_.clear_wsync();
            budget = 0;
        }
        const int ran = budget > 0 ? cpu_.run(budget) : 0;
        // Advance the two TIA audio channels one line and emit the samples
        // that belong to the CPU time just executed.
        tia_.clock_audio();
        tia_.emit_audio(kCyclesPerLine, kCpuClock, audio_);
        (void)ran;

        // The RIOT timer keeps counting regardless.
        const uint32_t ticks = uint32_t(kCyclesPerLine);
        if (riot_timer_ > ticks) {
            riot_timer_ -= ticks;
        } else {
            riot_timer_ = 0;
            riot_irq_ = true;
        }

        const int y = line - 16;
        if (y >= 0 && y < kHeight) {
            maria_.emit(line_.data());
            uint32_t* dst = framebuffer_.data() + size_t(y) * kWidth;
            for (int x = 0; x < kWidth; x++) dst[x] = palette()[line_[size_t(x)]];
        }
    }
}

void A7800::drain_audio(std::vector<int16_t>& out) {
    out.swap(audio_);
    audio_.clear();
}

}  // namespace dsp
