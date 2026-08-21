#include "drivers/spectrum.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <filesystem>

namespace dsp {
namespace {

// Spectrum palette: index is ink/paper 0..15 (bright in high 8).
// Converted to ARGB8888 (R/G/B from classic $00BBGGRR Delphi order).
const uint32_t kPalette[16] = {
    0x000000, 0xC00000, 0x0000C0, 0xC000C0, 0x00C000, 0xC0C000, 0x00C0C0, 0xC0C0C0,
    0x000000, 0xFF0000, 0x0000FF, 0xFF00FF, 0x00FF00, 0xFFFF00, 0x00FFFF, 0xFFFFFF,
};

// Contention pattern for 128 T-states of pixel display (cmemory in spectrum_misc.pas).
const uint8_t kCmemory[128] = {
    6, 5, 4, 3, 2, 1, 0, 0, 6, 5, 4, 3, 2, 1, 0, 0, 6, 5, 4, 3, 2, 1, 0, 0, 6, 5, 4, 3, 2, 1, 0, 0,
    6, 5, 4, 3, 2, 1, 0, 0, 6, 5, 4, 3, 2, 1, 0, 0, 6, 5, 4, 3, 2, 1, 0, 0, 6, 5, 4, 3, 2, 1, 0, 0,
    6, 5, 4, 3, 2, 1, 0, 0, 6, 5, 4, 3, 2, 1, 0, 0, 6, 5, 4, 3, 2, 1, 0, 0, 6, 5, 4, 3, 2, 1, 0, 0,
    6, 5, 4, 3, 2, 1, 0, 0, 6, 5, 4, 3, 2, 1, 0, 0, 6, 5, 4, 3, 2, 1, 0, 0, 6, 5, 4, 3, 2, 1, 0, 0,
};

// Keyboard matrix: half-row → 5 keys (A8..A15 of port $FE).
const Key kMatrix[8][5] = {
    {Key::LeftShift, Key::Z, Key::X, Key::C, Key::V},
    {Key::A, Key::S, Key::D, Key::F, Key::G},
    {Key::Q, Key::W, Key::E, Key::R, Key::T},
    {Key::Num1, Key::Num2, Key::Num3, Key::Num4, Key::Num5},
    {Key::Num0, Key::Num9, Key::Num8, Key::Num7, Key::Num6},
    {Key::P, Key::O, Key::I, Key::U, Key::Y},
    {Key::Enter, Key::L, Key::K, Key::J, Key::H},
    {Key::Space, Key::RightCtrl, Key::M, Key::N, Key::B},
};

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

bool try_rom(const std::string& dir, const char* name, std::vector<uint8_t>& out) {
    namespace fs = std::filesystem;
    if (load_file((fs::path(dir) / name).string(), out)) return true;
    std::string upper = name;
    for (char& c : upper) c = char(std::toupper(static_cast<unsigned char>(c)));
    return load_file((fs::path(dir) / upper).string(), out);
}

}  // namespace

uint32_t cto_argb(uint32_t bgr) {
    const uint32_t blue = (bgr >> 16) & 0xff;
    const uint32_t green = (bgr >> 8) & 0xff;
    const uint32_t red = bgr & 0xff;
    return 0xff000000u | (red << 16) | (green << 8) | blue;
}

Spectrum48k::Spectrum48k(Model model)
    : model_(model), cpu_(kClock) {
    for (int i = 0; i < 16; ++i) {
        palette_[i] = cto_argb(kPalette[i] | 0xff000000);
        palette_ext_[i] = palette_[i];
    }
    for (int i = 16; i < 80; ++i) palette_ext_[i] = 0xff000000;
}

uint32_t Spectrum48k::ulaplus_decode(uint8_t value) const {
    // GRB332 → ARGB8888 (same weights as spectrum_48k.pas set_pal_color)
    const uint8_t b = uint8_t(0x21 * (value & 1) + 0x47 * (value & 1) + 0x97 * ((value >> 1) & 1));
    const uint8_t r = uint8_t(0x21 * ((value >> 2) & 1) + 0x47 * ((value >> 3) & 1) +
                              0x97 * ((value >> 4) & 1));
    const uint8_t g = uint8_t(0x21 * ((value >> 5) & 1) + 0x47 * ((value >> 6) & 1) +
                              0x97 * ((value >> 7) & 1));
    return 0xff000000u | (uint32_t(r) << 16) | (uint32_t(g) << 8) | uint32_t(b);
}

void Spectrum48k::ulaplus_set_entry(uint8_t index, uint8_t value) {
    if (index >= 64) return;
    ulaplus_.pal[index] = value;
    palette_ext_[16 + index] = ulaplus_decode(value);
}

uint8_t Spectrum48k::border_index() const {
    if (ulaplus_.active && ulaplus_.enabled)
        return uint8_t(16 + (border_ & 7));
    return uint8_t(border_ & 7);
}

uint32_t Spectrum48k::border_colour() const {
    const uint8_t idx = border_index();
    if (idx < 16) return palette_[idx];
    return palette_ext_[idx < 80 ? idx : 16];
}

void Spectrum48k::border_fill_to(int /*abs_t*/) {
    // No-op with per-T painting; kept for API compatibility.
}

void Spectrum48k::border_on_out() {
    // Colour changes immediately; on_cycles paints border_buf_ every T-state
    // with the current border_index().  No deferred fill needed.
}





const char* Spectrum48k::title() const {
    return model_ == Model::Spec16k ? "ZX Spectrum 16K" : "ZX Spectrum 48K";
}

bool Spectrum48k::init(const std::string& rom_path, std::string* error) {
    std::vector<uint8_t> rom;
    const char* names[] = {"spectrum.rom", "48.rom", "48k.rom", "zx48.rom", "Spectrum.rom"};
    bool ok = false;
    for (const char* n : names) {
        if (try_rom(rom_path, n, rom) && rom.size() >= 0x4000) {
            ok = true;
            break;
        }
    }
    if (!ok && load_file(rom_path, rom) && rom.size() >= 0x4000) ok = true;
    if (!ok) {
        if (error) *error = "spectrum.rom (16 KB) not found in " + rom_path;
        return false;
    }
    std::memcpy(rom_.data(), rom.data(), 0x4000);

    cpu_.set_memory_handlers(
        [this](uint16_t a) { return mem_read(a); },
        [this](uint16_t a, uint8_t v) { mem_write(a, v); });
    cpu_.set_io_handlers(
        [this](uint16_t p) { return io_in(p); },
        [this](uint16_t p, uint8_t v) { io_out(p, v); });
    cpu_.set_cycle_handler([this](int c) { on_cycles(c); });

    build_contention();
    reset();
    return true;
}

void Spectrum48k::build_contention() {
    contention_.fill(0);
    // First contended T-state of line 0 is 14335 from frame start (after top border).
    int f = 14335;
    for (int h = 0; h < 192; ++h) {
        for (int i = 0; i < 128; ++i) {
            if (f + i < int(contention_.size())) contention_[size_t(f + i)] = kCmemory[i];
        }
        f += kTstatesPerLine;
    }
}

void Spectrum48k::reset() {
    std::memset(mem_.data(), 0, mem_.size());
    std::memcpy(mem_.data(), rom_.data(), 0x4000);
    cpu_.reset();
    border_ = 7;
    border_pos_ = 0;
    for (auto& row : border_buf_) row.fill(7);
    speaker_ = 0;
    ear_ = 0;
    keys_.fill(0xff);
    joy_ = 0;
    flash_ = false;
    flash_count_ = 0;
    line_ = 0;
    t_in_line_ = 0;
    frame_t_ = 0;
    audio_.clear();
    audio_acc_ = 0;
    beeper_level_ = 0;
    tape_.stop();
    ulaplus_.active = false;
    ulaplus_.mode = 0;
    ulaplus_.last_reg = 0;
    ulaplus_.pal.fill(0);
    for (int i = 0; i < 16; ++i) palette_ext_[i] = palette_[i];
    for (int i = 16; i < 80; ++i) palette_ext_[i] = 0xff000000;
    std::fill(framebuffer_.begin(), framebuffer_.end(), border_colour());
}

void Spectrum48k::set_dip_switch(int, uint8_t) {}

void Spectrum48k::set_inputs(const MachineInputs& inputs) { apply_keyboard(inputs); }


void Spectrum48k::apply_keyboard(const MachineInputs& in) {
#if 0
    keys_.fill(0xff);
    for (int row = 0; row < 8; ++row) {
        for (int bit = 0; bit < 5; ++bit) {
            if (in.key(kMatrix[row][bit])) keys_[row] &= uint8_t(~(1u << bit));
        }
    }
    // Kempston: active high
    joy_ = 0;
    if (in.player1.right) joy_ |= 0x01;
    if (in.player1.left) joy_ |= 0x02;
    if (in.player1.down) joy_ |= 0x04;
    if (in.player1.up) joy_ |= 0x08;
    if (in.player1.button1) joy_ |= 0x10;
#endif

	keys_.fill(0xff);
    for (int row = 0; row < 8; row++) {
        for (int bit = 0; bit < 5; bit++) {
            if (in.key(kMatrix[row][bit])) keys_[row] &= uint8_t(~(1 << bit));
        }
    }
    // Left control doubles as symbol shift and the cursor keys as caps shift
    // plus 5/6/7/8, the combinations the ROM expects.
    if (in.key(Key::LeftCtrl) || in.key(Key::RightShift)) keys_[7] &= 0xfd;
    auto caps_shift_with = [this](int row, int bit) {
        keys_[0] &= 0xfe;
        keys_[row] &= uint8_t(~(1 << bit));
    };
    if (in.key(Key::Left)) caps_shift_with(3, 4);   // 5
    if (in.key(Key::Down)) caps_shift_with(4, 4);   // 6
    if (in.key(Key::Up)) caps_shift_with(4, 3);     // 7
    if (in.key(Key::Right)) caps_shift_with(4, 2);  // 8
    if (in.key(Key::Backspace)) caps_shift_with(4, 0);

    joy_ = 0;
    if (in.player1.right) joy_ |= 0x01;
    if (in.player1.left) joy_ |= 0x02;
    if (in.player1.down) joy_ |= 0x04;
    if (in.player1.up) joy_ |= 0x08;
    if (in.player1.button1) joy_ |= 0x10;
}

uint8_t Spectrum48k::mem_read(uint16_t addr) {
    if (model_ == Model::Spec16k) addr = uint16_t(addr & 0x7fff);
    // Contention on $4000-$7FFF during pixel display
    if ((addr & 0xc000) == 0x4000 && frame_t_ >= 0 && frame_t_ < int(contention_.size())) {
        const uint8_t extra = contention_[size_t(frame_t_)];
        if (extra) {
            // Accounted loosely via cycle handler path; extra T added by bumping frame_t_
            frame_t_ += extra;
            t_in_line_ += extra;
        }
    }
    return mem_[addr];
}

void Spectrum48k::mem_write(uint16_t addr, uint8_t value) {
    if (model_ == Model::Spec16k) addr = uint16_t(addr & 0x7fff);
    if (addr < 0x4000) return;  // ROM
    if ((addr & 0xc000) == 0x4000 && frame_t_ >= 0 && frame_t_ < int(contention_.size())) {
        const uint8_t extra = contention_[size_t(frame_t_)];
        if (extra) {
            frame_t_ += extra;
            t_in_line_ += extra;
        }
    }
    mem_[addr] = value;
}

uint8_t Spectrum48k::io_in(uint16_t port) {
    uint8_t result = 0xff;

    // ULA keyboard + EAR + speaker mirror (port $xxFE with A0=0)
    if ((port & 1) == 0) {
        uint8_t keys = 0x1f;
        if ((port & 0x8000) == 0) keys &= keys_[7];
        if ((port & 0x4000) == 0) keys &= keys_[6];
        if ((port & 0x2000) == 0) keys &= keys_[5];
        if ((port & 0x1000) == 0) keys &= keys_[4];
        if ((port & 0x0800) == 0) keys &= keys_[3];
        if ((port & 0x0400) == 0) keys &= keys_[2];
        if ((port & 0x0200) == 0) keys &= keys_[1];
        if ((port & 0x0100) == 0) keys &= keys_[0];
        // bit5 unused (1), bit6 EAR, bit7 unused (1) — Pascal: (temp and $bf) or cinta or altavoz
        result = uint8_t((keys & 0x1f) | 0xa0 | ear_ | speaker_);
        result = uint8_t(result & 0xbf);  // clear bit6 then OR ear
        result = uint8_t(result | ear_ | speaker_);
    }

    // Kempston joystick (port with A5=0, common $1F)
    if ((port & 0x20) == 0) {
        result = joy_;
    }

    // ULA+ data port $FF3B
    if (port == 0xff3b && ulaplus_.enabled) {
        if (ulaplus_.mode == 0) {
            result = ulaplus_.pal[ulaplus_.last_reg & 63];
        } else if (ulaplus_.mode == 1) {
            result = ulaplus_.active ? 1 : 0;
        }
    }

    // Floating bus (simplified): attribute byte when in display area
    if ((port & 1) != 0) {
        if (line_ >= 64 && line_ <= 255) {
            const int y = line_ - 64;
            const int x = (t_in_line_ - 24) / 4;  // rough
            if (x >= 0 && x < 32 && y >= 0 && y < 192) {
                const int attr = 0x5800 + ((y >> 3) << 5) + x;
                result = mem_[attr];
            }
        }
    }

    return result;
}

void Spectrum48k::io_out(uint16_t port, uint8_t value) {
    if ((port & 1) == 0) {
        border_ = value & 7;
        // The speaker bit is mirrored back on bit 6 of port $FE, next to EAR.
        speaker_ = (value & 0x10) ? 0x40 : 0x00;
        beeper_level_ = (value & 0x10) ? int16_t(8192) : int16_t(-8192);
    }

    // ULA+ register port $BF3B
    if (port == 0xbf3b && ulaplus_.enabled) {
        ulaplus_.mode = uint8_t(value >> 6);
        if (ulaplus_.mode == 0) {
            ulaplus_.last_reg = value & 0x3f;
        }
    }
    // ULA+ data port $FF3B
    if (port == 0xff3b && ulaplus_.enabled) {
        switch (ulaplus_.mode) {
            case 0:
                ulaplus_set_entry(ulaplus_.last_reg & 63, value);
                break;
            case 1:
                ulaplus_.active = (value & 1) != 0;
                break;
            default:
                break;
        }
    }
}

void Spectrum48k::on_cycles(int cycles) {
    // Paint border colour into the per-T buffer for every elapsed T-state, then
    // advance the raster.  OUT ($FE) only updates border_; stripes appear because
    // consecutive T-states keep the colour that was current when they executed.
    for (int n = 0; n < cycles; ++n) {
        if (line_ >= 0 && line_ < kLinesPerFrame &&
            t_in_line_ >= 0 && t_in_line_ < kTstatesPerLine) {
            border_buf_[size_t(line_)][size_t(t_in_line_)] = border_index();
        }
        ++t_in_line_;
        ++frame_t_;
        if (t_in_line_ >= kTstatesPerLine) {
            t_in_line_ -= kTstatesPerLine;
            render_line(line_);
            ++line_;
            if (line_ >= kLinesPerFrame) line_ = 0;
        }
    }

    if (tape_.is_playing()) {
        ear_ = tape_.advance(cycles) ? 0x40 : 0x00;
    }

    audio_acc_ += int64_t(cycles) * kSampleRate;
    while (audio_acc_ >= int64_t(kClock)) {
        audio_acc_ -= int64_t(kClock);
        audio_.push_back(beeper_level_);
    }
}

void Spectrum48k::render_line(int line) {
    // Pascal borde_48_full (spectrum_48k.pas):
    //   fill buffer[line*224 + pos .. contador) with current colour
    //   left:  T 200..223 of previous line → 48 px at X=0,  Y=line-16
    //   right: T 128..151 of current line  → 48 px at X=304
    //   centre (top/bottom only): T 0..127 → 256 px at X=48
    //   paper lines 64..255: video48k draws bitmap; no centre border
    // Finish border buffer up to the end of this scanline (absolute T).
        // Pascal sets posicion := contador - 224 after processing; next line starts at 0.
    // border_pos_ already advanced to absolute end of this line.

    if (line < 15 || line > 296) return;
    if (line == 15) return;  // only buffered; drawn as left of line 16

    const int sy = line - 16;
    if (sy < 0 || sy >= kScreenHeight) return;

    uint32_t* dst = framebuffer_.data() + size_t(sy) * kScreenWidth;
    const auto& brow = border_buf_[size_t(line % kLinesPerFrame)];

    auto col_at = [&](uint8_t idx) -> uint32_t {
        if (idx < 16) return palette_[idx];
        if (idx < 80) return palette_ext_[idx];
        return palette_[0];
    };

    // Left border from previous line (T 200..223)
    if (line > 15) {
        const auto& prev = border_buf_[size_t((line - 1) % kLinesPerFrame)];
        for (int f = 200; f <= 223; ++f) {
            const uint32_t c = col_at(prev[size_t(f)]);
            const int px = (f - 200) * 2;
            dst[px] = c;
            dst[px + 1] = c;
        }
    } else {
        const uint32_t c = col_at(border_index());
        for (int x = 0; x < 48; ++x) dst[x] = c;
    }

    if (line >= 296) return;

    // Right border T 128..151
    for (int f = 128; f <= 151; ++f) {
        const uint32_t c = col_at(brow[size_t(f)]);
        const int px = 304 + (f - 128) * 2;
        dst[px] = c;
        dst[px + 1] = c;
    }

    // Paper / centre
    if (line >= 64 && line <= 255) {
        const int y = line - 64;
        const uint16_t pix_base = kScrTable[y];
        const int attr_row = (y >> 3) << 5;
        const bool uplus = ulaplus_.active && ulaplus_.enabled;
        for (int col = 0; col < 32; ++col) {
            const uint8_t attrib = mem_[0x5800 + attr_row + col];
            const uint8_t pixels = mem_[0x4000 + pix_base + col];
            uint32_t c_ink, c_paper;
            if (uplus) {
                const int bank = ((((attrib & 0x80) >> 6) + ((attrib & 0x40) >> 6)) << 4) + 16;
                c_ink = palette_ext_[bank + (attrib & 7)];
                c_paper = palette_ext_[bank + ((attrib >> 3) & 7) + 8];
            } else {
                int ink = attrib & 7;
                int paper = (attrib >> 3) & 7;
                if (attrib & 0x40) { ink += 8; paper += 8; }
                if ((attrib & 0x80) && flash_) std::swap(ink, paper);
                c_ink = palette_[ink];
                c_paper = palette_[paper];
            }
            uint8_t pix = pixels;
            for (int b = 0; b < 8; ++b) {
                dst[48 + col * 8 + b] = (pix & 0x80) ? c_ink : c_paper;
                pix = uint8_t(pix << 1);
            }
        }
    } else {
        // Top/bottom border centre T 0..127
        for (int f = 0; f <= 127; ++f) {
            const uint32_t c = col_at(brow[size_t(f)]);
            const int px = 48 + f * 2;
            dst[px] = c;
            dst[px + 1] = c;
        }
    }
}

void Spectrum48k::run_frame() {
    line_ = 0;
    t_in_line_ = 0;
    border_pos_ = 0;  // absolute T within frame
    frame_t_ = 0;
    // Seed buffer with current colour (Pascal leaves previous frame data; we
    // start clean so every T-state has a defined colour until the next OUT).
    {
        const uint8_t col = border_index();
        for (auto& row : border_buf_) row.fill(col);
    }

    // IRQ at start of frame (after a few T-states of line 0)
    cpu_.set_irq(IrqLine::Hold);
    int remaining = kTstatesPerFrame;
    while (remaining > 0) {
        const int ran = cpu_.run(std::min(remaining, kTstatesPerLine));
        if (ran <= 0) break;
        remaining -= ran;
        // Clear IRQ after first slice
        if (remaining < kTstatesPerFrame - 32) cpu_.set_irq(IrqLine::Clear);
    }

    // Finish any lines not yet rendered (if frame ended mid-line).
    // Do NOT re-render lines already drawn — that would paint solid border and
    // wipe per-T-state rainbow / loading effects.
    if (line_ != 0 || t_in_line_ != 0) {
        while (line_ < kLinesPerFrame) {
            render_line(line_);
            ++line_;
        }
    }
    line_ = 0;
    t_in_line_ = 0;
    border_pos_ = 0;
    frame_t_ = 0;

    flash_count_ = (flash_count_ + 1) & 0x0f;
    if (flash_count_ == 0) flash_ = !flash_;
}

void Spectrum48k::drain_audio(std::vector<int16_t>& out) {
    out.swap(audio_);
    audio_.clear();
}

bool Spectrum48k::load_media(const std::string& path, std::string* error) {
    std::string lower = path;
    for (char& ch : lower) ch = char(std::tolower(static_cast<unsigned char>(ch)));
    const auto ends = [&](const char* ext) {
        const size_t n = std::strlen(ext);
        return lower.size() >= n && lower.compare(lower.size() - n, n, ext) == 0;
    };
    if (ends(".tzx") || ends(".tap") || ends(".csw") || ends(".pzx") || ends(".cdt")) {
        return load_tape(path, error);
    }
    if (ends(".sna")) {
        return load_sna(path, error);
    }
    if (error) *error = "unsupported media (use .tzx / .tap / .csw / .pzx / .sna): " + path;
    return false;
}

bool Spectrum48k::load_tape(const std::string& path, std::string* error) {
    if (!tape_.load_file(path, error)) return false;
    // Spectrum has no motor bit: auto-start like classic emulators / play_tape.
    tape_.play(true);
    return true;
}

void Spectrum48k::tape_play() {
    if (tape_.is_loaded()) {
        if (tape_.is_paused()) tape_.play(false);
        else tape_.play(true);
    }
}

void Spectrum48k::tape_stop() {
    tape_.stop();
    ear_ = 0;
}

bool Spectrum48k::load_sna(const std::string& path, std::string* error) {
    std::vector<uint8_t> buf;
    if (!load_file(path, buf)) {
        if (error) *error = "cannot open SNA: " + path;
        return false;
    }
    // Classic 48K SNA: 27-byte header + 48 KB memory from $4000
    if (buf.size() < 27 + 0xc000) {
        if (error) *error = "SNA too small for 48K";
        return false;
    }
    const uint8_t* h = buf.data();
    cpu_.i = h[0];
    cpu_.l2 = h[1];
    cpu_.h2 = h[2];
    cpu_.e2 = h[3];
    cpu_.d2 = h[4];
    cpu_.c2 = h[5];
    cpu_.b2 = h[6];
    cpu_.f2 = h[7];
    cpu_.a2 = h[8];
    cpu_.l = h[9];
    cpu_.h = h[10];
    cpu_.e = h[11];
    cpu_.d = h[12];
    cpu_.c = h[13];
    cpu_.b = h[14];
    cpu_.iy = uint16_t(h[15] | (h[16] << 8));
    cpu_.ix = uint16_t(h[17] | (h[18] << 8));
    cpu_.iff1 = (h[19] & 4) != 0;
    cpu_.iff2 = cpu_.iff1;
    cpu_.r = h[20];
    cpu_.f = h[21];
    cpu_.a = h[22];
    cpu_.sp = uint16_t(h[23] | (h[24] << 8));
    cpu_.im = h[25] & 3;
    border_ = h[26] & 7;

    std::memcpy(mem_.data() + 0x4000, buf.data() + 27, 0xc000);
    // PC is on the stack in 48K SNA
    const uint16_t sp = cpu_.sp;
    const uint16_t pc = uint16_t(mem_[sp] | (mem_[(sp + 1) & 0xffff] << 8));
    cpu_.sp = uint16_t(sp + 2);
    cpu_.set_pc(pc);
    cpu_.set_irq(IrqLine::Clear);
    return true;
}

}  // namespace dsp
