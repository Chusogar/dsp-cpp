#include "drivers/computers/zx81.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>

#include "core/rom_loader.h"
#include "cpu/irq_line.h"

namespace dsp {
namespace {

// Same 8×5 matrix layout as the ZX Spectrum (active-low half-rows).
const Key kMatrix[8][5] = {
    {Key::LeftShift, Key::Z, Key::X, Key::C, Key::V},
    {Key::A, Key::S, Key::D, Key::F, Key::G},
    {Key::Q, Key::W, Key::E, Key::R, Key::T},
    {Key::Num1, Key::Num2, Key::Num3, Key::Num4, Key::Num5},
    {Key::Num0, Key::Num9, Key::Num8, Key::Num7, Key::Num6},
    {Key::P, Key::O, Key::I, Key::U, Key::Y},
    {Key::Enter, Key::L, Key::K, Key::J, Key::H},
    {Key::Space, Key::Period, Key::M, Key::N, Key::B},
};

bool ends_ci(const std::string& path, const char* ext) {
    std::string lower = path;
    for (char& c : lower) c = char(std::tolower(static_cast<unsigned char>(c)));
    const size_t n = std::strlen(ext);
    return lower.size() >= n && lower.compare(lower.size() - n, n, ext) == 0;
}

bool load_file(const std::string& path, std::vector<uint8_t>& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    f.seekg(0, std::ios::end);
    const auto sz = f.tellg();
    if (sz <= 0) return false;
    f.seekg(0, std::ios::beg);
    out.resize(size_t(sz));
    f.read(reinterpret_cast<char*>(out.data()), sz);
    return bool(f);
}

}  // namespace

Zx81::Zx81() : cpu_(kClock) {}

bool Zx81::load_roms(const std::string& path, std::string* error) {
    RomLoader loader;
    std::string load_err;
    std::vector<uint8_t> blob;

    auto try_named = [&](const char* name) -> bool {
        if (loader.open(path, &load_err) && loader.try_read(name, blob) && blob.size() >= 0x2000) {
            return true;
        }
        namespace fs = std::filesystem;
        std::error_code ec;
        if (fs::is_directory(path, ec)) {
            if (load_file((fs::path(path) / name).string(), blob) && blob.size() >= 0x2000) {
                return true;
            }
        }
        if (ends_ci(path, ".rom") || ends_ci(path, ".bin")) {
            if (load_file(path, blob) && blob.size() >= 0x2000) return true;
        }
        return false;
    };

    // Prefer the modern ZX81 ROM (CRC 522c37b8), then older variants.
    const char* names[] = {"zx81b.rom", "zx81.rom", "zx81a.rom", "zx81.rom.bin"};
    bool ok = false;
    for (const char* n : names) {
        if (try_named(n)) {
            ok = true;
            break;
        }
    }
    if (!ok && load_file(path, blob) && blob.size() >= 0x2000) ok = true;
    if (!ok) {
        if (error) *error = "zx81 ROM (8 KiB: zx81b.rom / zx81.rom) not found in " + path;
        return false;
    }

    std::memcpy(rom_.data(), blob.data(), 0x2000);
    const uint32_t crc = crc32_of(rom_.data(), 0x2000);
    if (crc != 0x522c37b8u && error) {
        // Non-fatal: still boot with whatever ROM we found.
        (void)crc;
    }
    return true;
}

bool Zx81::init(const std::string& rom_path, std::string* error) {
    if (!load_roms(rom_path, error)) return false;

    cpu_.set_memory_handlers([this](uint16_t a) { return mem_read(a); },
                             [this](uint16_t a, uint8_t v) { mem_write(a, v); });
    cpu_.set_io_handlers([this](uint16_t p) { return io_in(p); },
                         [this](uint16_t p, uint8_t v) { io_out(p, v); });
    cpu_.set_opcode_read([this](uint16_t a) { return opcode_read(a); });
    cpu_.set_cycle_handler([this](int c) { on_cycles(c); });

    reset();
    return true;
}

void Zx81::reset() {
    ram_.fill(0);
    cpu_.reset();
    keys_.fill(0xff);
    prev_refresh_ = 0xff;
    ula_char_ = 0xffff;
    nmi_generator_ = false;
    nmi_on_ = false;
    vsync_active_ = false;
    line_t_ = 0;
    scanline_ = 0;
    frame_t_ = 0;
    total_cycles_ = 0;
    cpu_.set_irq(IrqLine::Clear);
    cpu_.set_nmi(IrqLine::Clear);
    std::fill(framebuffer_.begin(), framebuffer_.end(), kWhite);
}

void Zx81::set_dip_switch(int, uint8_t) {}

void Zx81::set_inputs(const MachineInputs& inputs) { apply_keyboard(inputs); }

void Zx81::apply_keyboard(const MachineInputs& in) {
    keys_.fill(0xff);
    for (int row = 0; row < 8; ++row) {
        for (int bit = 0; bit < 5; ++bit) {
            if (in.key(kMatrix[row][bit])) keys_[size_t(row)] &= uint8_t(~(1u << bit));
        }
    }
    // Symbol shift aliases (Spectrum RightCtrl / RightShift).
    if (in.key(Key::RightCtrl) || in.key(Key::RightShift)) keys_[7] &= 0xfd;
    // Cursor keys and backspace use SHIFT + 5/6/7/8/0 like the Spectrum.
    auto caps = [&](int row, int bit) {
        keys_[0] &= 0xfe;
        keys_[size_t(row)] &= uint8_t(~(1u << bit));
    };
    if (in.key(Key::Left)) caps(3, 4);
    if (in.key(Key::Down)) caps(4, 4);
    if (in.key(Key::Up)) caps(4, 3);
    if (in.key(Key::Right)) caps(4, 2);
    if (in.key(Key::Backspace)) caps(4, 0);
}

uint8_t Zx81::mem_read(uint16_t addr) {
    if (addr < 0x4000) return rom_[addr & 0x1fff];
    if (addr < 0x8000) return ram_[addr & 0x3fff];
    if (addr >= 0xc000) return ram_[addr & 0x3fff];  // 16K mirror
    return 0xff;                                    // open bus 0x8000-0xBFFF
}

void Zx81::mem_write(uint16_t addr, uint8_t value) {
    if (addr < 0x4000) return;  // ROM
    if (addr < 0x8000) {
        ram_[addr & 0x3fff] = value;
        return;
    }
    if (addr >= 0xc000) ram_[addr & 0x3fff] = value;
}

uint8_t Zx81::opcode_read(uint16_t addr) {
    // Below 0x8000: normal program fetch (ROM / RAM).
    if (addr < 0x8000) {
        // Optional MAME-style wait stretch while NMI/HSYNC is active near EOL.
        if (nmi_on_ && line_t_ >= 192) {
            // No icount adjust available; timing is approximate.
        }
        return mem_read(addr);
    }

    // A15 high during M1: ULA mirrors RAM (A15 forced low) and may steal the
    // byte as a display character, returning NOP to the CPU when bit 6 is clear.
    const uint8_t cdata = mem_read(uint16_t(addr & 0x7fff));
    if (cpu_.halted) return cdata;
    if (cdata & 0x40) return cdata;  // HALT (0x76) and other bit6-set opcodes pass through

    ula_char_ = cdata;
    return 0x00;  // NOP — ULA latched the character
}

uint8_t Zx81::io_in(uint16_t port) {
    uint8_t data = 0xff;
    if ((port & 1) != 0) return data;

    // Keyboard: upper address bits select half-rows (active low).
    for (int i = 0; i < 8; ++i) {
        if ((port & (0x0100u << i)) == 0) data &= keys_[size_t(i)];
    }

    // PAL TV diode: bit 6 set. Cassette EAR idle high (bit 7).
    data |= 0xc0;

    // IN from an even port starts VSYNC when the NMI generator is off.
    if (!vsync_active_ && !nmi_generator_) {
        vsync_active_ = true;
    }
    return data;
}

void Zx81::io_out(uint16_t port, uint8_t /*value*/) {
    // A0=0 (FE): enable NMI generator. A1=0 (FD): disable it.
    if ((port & 1) == 0 && !nmi_generator_) {
        nmi_generator_ = true;
        nmi_on_ = (line_t_ >= 192);
        cpu_.set_nmi(nmi_on_ ? IrqLine::Assert : IrqLine::Clear);
    }
    if ((port & 2) == 0 && nmi_generator_) {
        nmi_generator_ = false;
        if (nmi_on_) {
            cpu_.set_nmi(IrqLine::Clear);
            nmi_on_ = false;
        }
    }

    // Any OUT ends VSYNC (and is used as the cassette/mic transition).
    if (vsync_active_) {
        vsync_active_ = false;
        // End of a long VSYNC pulse ≈ end of frame: reset raster origin.
        if (frame_t_ > 1000) {
            scanline_ = 0;
            line_t_ = 0;
        }
    }
}

void Zx81::on_cycles(int cycles) {
    // Refresh-address IRQ (bit 6 of R): assert when it falls, clear when it rises.
    const uint8_t refresh = cpu_.r;
    if ((refresh ^ prev_refresh_) & 0x40) {
        cpu_.set_irq((refresh & 0x40) ? IrqLine::Clear : IrqLine::Assert);
    }
    prev_refresh_ = refresh;

    // Draw a latched ULA character row if the refresh just happened with a
    // pending character (bitmap from I:R charset base, normally 0x1E00).
    if (ula_char_ != 0xffff) {
        const int x = 2 * line_t_;
        const int y = scanline_;
        if (x >= 0 && x + 8 <= kScreenWidth && y >= 0 && y < kScreenHeight) {
            const uint16_t base = uint16_t((uint16_t(cpu_.i) << 8) | (refresh & 0xfe00));
            // Charset row: (char & 0x3f) * 8 + (scanline & 7). I is normally 0x1E.
            const uint16_t glyph_addr =
                uint16_t(((uint16_t(cpu_.i) << 8) & 0xfe00) | ((ula_char_ & 0x3f) << 3) |
                         (scanline_ & 7));
            (void)base;
            uint8_t pixels = mem_read(glyph_addr);
            if (ula_char_ & 0x80) pixels = uint8_t(~pixels);
            uint32_t* dest = &framebuffer_[size_t(y) * kScreenWidth + size_t(x)];
            for (int i = 0; i < 8; ++i) {
                dest[i] = (pixels & (0x80 >> i)) ? kBlack : kWhite;
            }
        }
        ula_char_ = 0xffff;
    }

    for (int n = 0; n < cycles; ++n) {
        ++line_t_;
        ++frame_t_;
        ++total_cycles_;

        // HSYNC / NMI window: active for the last 15 T-states of each 207-T line.
        if (line_t_ == 192) {
            if (nmi_generator_) {
                nmi_on_ = true;
                cpu_.set_nmi(IrqLine::Assert);
            }
        }
        if (line_t_ >= kTstatesPerLine) {
            line_t_ = 0;
            if (nmi_generator_) {
                nmi_on_ = false;
                cpu_.set_nmi(IrqLine::Clear);
            }
            ++scanline_;
            if (scanline_ >= kLinesPerFrame) scanline_ = 0;
        }
    }
}

void Zx81::plot_char_row(int x, int y, uint8_t ch, int row) {
    if (y < 0 || y >= kScreenHeight) return;
    const uint16_t glyph_addr = uint16_t(0x1e00 | ((ch & 0x3f) << 3) | (row & 7));
    uint8_t pixels = rom_[glyph_addr & 0x1fff];
    if (ch & 0x80) pixels = uint8_t(~pixels);
    for (int i = 0; i < 8; ++i) {
        const int px = x + i;
        if (px < 0 || px >= kScreenWidth) continue;
        framebuffer_[size_t(y) * kScreenWidth + size_t(px)] =
            (pixels & (0x80 >> i)) ? kBlack : kWhite;
    }
}

void Zx81::render_dfile() {
    // Reliable end-of-frame picture from the display file (D_FILE at 16396).
    std::fill(framebuffer_.begin(), framebuffer_.end(), kWhite);

    const uint16_t dfile =
        uint16_t(mem_read(0x400c) | (uint16_t(mem_read(0x400d)) << 8));
    if (dfile < 0x4000) return;

    uint16_t addr = dfile;
    // First byte is a leading newline (HALT / 0x76).
    if (mem_read(addr) == 0x76) ++addr;

    for (int row = 0; row < 24; ++row) {
        int col = 0;
        while (col < 32) {
            const uint8_t ch = mem_read(addr++);
            if (ch == 0x76) break;  // end of line
            if ((ch & 0x40) == 0) {
                for (int r = 0; r < 8; ++r) {
                    plot_char_row(kPaperX + col * 8, kPaperY + row * 8 + r, ch, r);
                }
            }
            ++col;
            // Safety: collapsed display files are short.
            if (addr < 0x4000) return;
        }
        // If we stopped on a non-newline (line had 32 chars), consume the HALT.
        if (col == 32 && mem_read(addr) == 0x76) ++addr;
    }
}

void Zx81::run_frame() {
    frame_t_ = 0;
    const int target = kTstatesPerFrame;
    int ran = 0;
    while (ran < target) {
        const int slice = std::min(64, target - ran);
        ran += cpu_.run(slice);
    }
    // Always paint from D_FILE so BASIC's K cursor is visible even when the
    // ULA bitmap path only covered part of the frame.
    render_dfile();
}

void Zx81::drain_audio(std::vector<int16_t>& out) {
    // No sound hardware; emit silence matching the frame length.
    const int samples = int(double(kSampleRate) / kFps + 0.5);
    out.assign(size_t(std::max(samples, 0)), 0);
}

}  // namespace dsp
