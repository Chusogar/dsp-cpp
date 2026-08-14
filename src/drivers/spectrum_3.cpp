#include "drivers/spectrum_3.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <filesystem>

namespace dsp {
namespace {

const uint32_t kPalette[16] = {
    0xff000000, 0xffc00000, 0xff0000c0, 0xffc000c0, 0xff00c000, 0xffc0c000, 0xff00c0c0, 0xffc0c0c0,
    0xff000000, 0xffff0000, 0xff0000ff, 0xffff00ff, 0xff00ff00, 0xffffff00, 0xff00ffff, 0xffffffff,
};

const uint8_t kCmemory[128] = {
    6, 5, 4, 3, 2, 1, 0, 0, 6, 5, 4, 3, 2, 1, 0, 0, 6, 5, 4, 3, 2, 1, 0, 0, 6, 5, 4, 3, 2, 1, 0, 0,
    6, 5, 4, 3, 2, 1, 0, 0, 6, 5, 4, 3, 2, 1, 0, 0, 6, 5, 4, 3, 2, 1, 0, 0, 6, 5, 4, 3, 2, 1, 0, 0,
    6, 5, 4, 3, 2, 1, 0, 0, 6, 5, 4, 3, 2, 1, 0, 0, 6, 5, 4, 3, 2, 1, 0, 0, 6, 5, 4, 3, 2, 1, 0, 0,
    6, 5, 4, 3, 2, 1, 0, 0, 6, 5, 4, 3, 2, 1, 0, 0, 6, 5, 4, 3, 2, 1, 0, 0, 6, 5, 4, 3, 2, 1, 0, 0,
};

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

uint32_t to_argb_p3(uint32_t bgr) {
    const uint32_t blue = (bgr >> 16) & 0xff;
    const uint32_t green = (bgr >> 8) & 0xff;
    const uint32_t red = bgr & 0xff;
    return 0xff000000u | (red << 16) | (green << 8) | blue;
}

Spectrum3::Spectrum3()
    : cpu_(kClock), ay0_(kAyClock), ay1_(kAyClock) {
    for (int i = 0; i < 16; ++i) {
        palette_[i] = to_argb_p3(kPalette[i] | 0xff000000);
        palette_ext_[i] = palette_[i];
    }
    for (int i = 16; i < 80; ++i) palette_ext_[i] = 0xff000000;
}



uint32_t Spectrum3::ulaplus_decode(uint8_t value) const {
    const uint8_t b = uint8_t(0x21 * (value & 1) + 0x47 * (value & 1) + 0x97 * ((value >> 1) & 1));
    const uint8_t r = uint8_t(0x21 * ((value >> 2) & 1) + 0x47 * ((value >> 3) & 1) +
                              0x97 * ((value >> 4) & 1));
    const uint8_t g = uint8_t(0x21 * ((value >> 5) & 1) + 0x47 * ((value >> 6) & 1) +
                              0x97 * ((value >> 7) & 1));
    return 0xff000000u | (uint32_t(r) << 16) | (uint32_t(g) << 8) | uint32_t(b);
}

void Spectrum3::ulaplus_set_entry(uint8_t index, uint8_t value) {
    if (index >= 64) return;
    ulaplus_.pal[index] = value;
    palette_ext_[16 + index] = ulaplus_decode(value);
}

uint8_t Spectrum3::border_index() const {
    if (ulaplus_.active && ulaplus_.enabled)
        return uint8_t(16 + (border_ & 7));
    return uint8_t(border_ & 7);
}

uint32_t Spectrum3::border_colour() const {
    const uint8_t idx = border_index();
    if (idx < 16) return palette_[idx];
    return palette_ext_[idx < 80 ? idx : 16];
}

void Spectrum3::border_fill_to(int /*abs_t*/) {
    // No-op with per-T painting; kept for API compatibility.
}

void Spectrum3::border_on_out() {
    // Colour changes immediately; on_cycles paints border_buf_ every T-state
    // with the current border_index().  No deferred fill needed.
}



bool Spectrum3::init(const std::string& rom_path, std::string* error) {
    std::vector<uint8_t> rom;
    const char* names[] = {"plus3.rom", "plus3-0.rom", "zx+3.rom", "spectrum+3.rom", "p3.rom"};
    bool ok = false;
    for (const char* n : names) {
        if (try_rom(rom_path, n, rom) && rom.size() >= 0x10000) { ok = true; break; }
    }
    if (!ok) {
        // Four 16K files
        const char* parts[] = {"plus3-0.rom", "plus3-1.rom", "plus3-2.rom", "plus3-3.rom"};
        rom.assign(0x10000, 0xff);
        ok = true;
        for (int i = 0; i < 4; ++i) {
            std::vector<uint8_t> part;
            if (!try_rom(rom_path, parts[i], part) || part.size() < 0x4000) {
                ok = false;
                break;
            }
            std::memcpy(rom.data() + i * 0x4000, part.data(), 0x4000);
        }
    }
    if (!ok && load_file(rom_path, rom) && rom.size() >= 0x10000) ok = true;
    if (!ok) {
        if (error) *error = "+3 ROM (64 KB) not found in " + rom_path;
        return false;
    }
    for (int i = 0; i < 4; ++i)
        std::memcpy(banks_[8 + i].data(), rom.data() + i * 0x4000, 0x4000);

    cpu_.set_memory_handlers(
        [this](uint16_t a) { return mem_read(a); },
        [this](uint16_t a, uint8_t v) { mem_write(a, v); });
    cpu_.set_io_handlers(
        [this](uint16_t p) { return io_in(p); },
        [this](uint16_t p, uint8_t v) { io_out(p, v); });
    cpu_.set_cycle_handler([this](int c) { on_cycles(c); });

    build_contention();
    for (int f = 0; f < 192; ++f) atrib_scr_[f] = uint16_t(0x1800 + 32 * (f / 8));
    reset();
    return true;
}

void Spectrum3::build_contention() {
    contention_.fill(0);
    // 14361 from Pascal init
    int f = 14361;
    for (int h = 0; h < 192; ++h) {
        for (int i = 0; i < 128; ++i) {
            if (f + i < int(contention_.size())) contention_[size_t(f + i)] = kCmemory[i];
        }
        f += kTstatesPerLine;
    }
}

void Spectrum3::update_memory_map() {
    // spectrum_3.pas memoria_spectrum3
    paging_enabled_ = (port_7ffd_ & 0x20) == 0;
    special_paging_ = (port_1ffd_ & 0x01) != 0;
    if (!special_paging_) {
        // Normal: ROM = ((7ffd>>4)&1) | ((1ffd>>1)&2) + 8  → banks 8..11
        marco_[0] = uint8_t((((port_7ffd_ >> 4) & 1) | ((port_1ffd_ >> 1) & 2)) + 8);
        marco_[1] = 5;
        marco_[2] = 2;
        marco_[3] = port_7ffd_ & 7;
    } else {
        // Special all-RAM configurations
        static const uint8_t kRamBank[4][4] = {
            {0, 1, 2, 3},
            {4, 5, 6, 7},
            {4, 5, 6, 3},
            {4, 7, 6, 3},
        };
        const int cfg = (port_1ffd_ >> 1) & 3;
        for (int i = 0; i < 4; ++i) marco_[i] = kRamBank[cfg][i];
    }
}

void Spectrum3::apply_7ffd(uint8_t value) {
    if (!paging_enabled_) return;  // locked
    pantalla_ = uint8_t(((value & 8) >> 2) + 5);
    port_7ffd_ = value;
    update_memory_map();
}

void Spectrum3::apply_1ffd(uint8_t value) {
    port_1ffd_ = value;
    // bit 3 = disk motor
    fdc_.write_motor((value & 0x08) ? 1 : 0);
    update_memory_map();
}

void Spectrum3::reset() {
    for (int b = 0; b < 8; ++b) banks_[b].fill(0);
    marco_ = {8, 5, 2, 0};
    pantalla_ = 5;
    update_memory_map();
    port_7ffd_ = 0;
    port_1ffd_ = 0;
    paging_enabled_ = true;
    special_paging_ = false;
    fdc_.reset();
    fdc_.write_motor(0);
    ay_select_ = 0;
    if2_switched_ = false;
    if2_delay_ = 0;
    cpu_.reset();
    ay0_.reset();
    ay1_.reset();
    border_ = 7;
    border_pos_ = 0;
    for (auto& row : border_buf_) row.fill(7);
    speaker_ = ear_ = 0;
    keys_.fill(0xff);
    joy_ = 0;
    kempston_enabled_ = true;
    kmouse_x_ = kmouse_y_ = 0;
    kmouse_btn_ = 0xff;
    flash_ = false;
    flash_count_ = 0;
    line_ = t_in_line_ = frame_t_ = 0;
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

void Spectrum3::set_dip_switch(int, uint8_t) {}
void Spectrum3::set_inputs(const MachineInputs& inputs) { apply_keyboard(inputs); }

void Spectrum3::apply_keyboard(const MachineInputs& in) {
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



uint8_t Spectrum3::mem_read(uint16_t addr) {
    const int slot = addr >> 14;
    // Interface 2 cartridge: maps over $0000-$3FFF (lower then upper 16K after delay)
    if (if2_present_ && slot == 0) {
        const uint16_t off = if2_switched_ ? uint16_t(0x4000 + (addr & 0x3fff))
                                           : uint16_t(addr & 0x3fff);
        return if2_rom_[off];
    }
    const uint8_t bank = marco_[slot];
    // Contention: $4000 always (bank 5), $C000 if odd bank
    if (slot == 1 || (slot == 3 && (bank & 1))) {
        if (frame_t_ >= 0 && frame_t_ < int(contention_.size())) {
            const uint8_t extra = contention_[size_t(frame_t_)];
            if (extra) {
                frame_t_ += extra;
                t_in_line_ += extra;
            }
        }
    }
    return banks_[bank][addr & 0x3fff];
}

void Spectrum3::mem_write(uint16_t addr, uint8_t value) {
    const int slot = addr >> 14;
    // In normal paging, slot 0 is ROM and not writable
    if (!special_paging_ && slot == 0) return;
    const uint8_t bank = marco_[slot];
    if (bank >= 8) return;  // ROM bank
    if (slot == 1 || (slot == 3 && (bank & 1))) {
        if (frame_t_ >= 0 && frame_t_ < int(contention_.size())) {
            const uint8_t extra = contention_[size_t(frame_t_)];
            if (extra) {
                frame_t_ += extra;
                t_in_line_ += extra;
            }
        }
    }
    banks_[bank][addr & 0x3fff] = value;
}


uint8_t Spectrum3::floating_bus() const {
    // spectrum_128k.pas: cont = T mod 228, lin = line + T/228
    // Paper lines 63..254, cont < 128, phase from display bank.
    if (line_ < 63 || line_ > 254) return 0xff;
    const int cont = t_in_line_;
    if (cont < 0 || cont >= 128) return 0xff;
    const int y = line_ - 63;
    if (y < 0 || y >= 192) return 0xff;
    const auto& vram = banks_[pantalla_];
    // col index: (cont & $f8) shr 2  → byte offset within 32-attr / pixel row
    // Pascal uses this as offset into attribute/pixel row (not absolute $4000).
    const int col = (cont & 0xf8) >> 2;  // 0,2,4,...,30 style steps of 2
    const uint16_t attr_base = atrib_scr_[y];
    const uint16_t pix_base = kScrTable[y];
    switch (cont & 7) {
        case 1: return vram[(pix_base + col) & 0x3fff];
        case 2: return vram[(attr_base + col) & 0x3fff];
        case 3: return vram[(pix_base + col + 1) & 0x3fff];
        case 4: return vram[(attr_base + col + 1) & 0x3fff];
        default: return 0xff;  // idle slots 0,5,6,7
    }
}


uint8_t Spectrum3::kempston_read() const {
    // Kempston joystick: active-HIGH, bits 0-4 only; upper bits float as 0.
    // Bit0=Right, Bit1=Left, Bit2=Down, Bit3=Up, Bit4=Fire
    return uint8_t(joy_ & 0x1f);
}

uint8_t Spectrum3::io_in(uint16_t port) {
    uint8_t result = 0xff;
    if ((port & 1) == 0) {
        // ULA: keyboard + EAR + speaker bit
        uint8_t keys = 0x1f;
        if ((port & 0x8000) == 0) keys &= keys_[7];
        if ((port & 0x4000) == 0) keys &= keys_[6];
        if ((port & 0x2000) == 0) keys &= keys_[5];
        if ((port & 0x1000) == 0) keys &= keys_[4];
        if ((port & 0x0800) == 0) keys &= keys_[3];
        if ((port & 0x0400) == 0) keys &= keys_[2];
        if ((port & 0x0200) == 0) keys &= keys_[1];
        if ((port & 0x0100) == 0) keys &= keys_[0];
        // Interface 2 joysticks (active low on bits 0-4 when those rows selected)
        // Right IF2: port with A12=0 (0xEFFE family) — mapped via keys already if using matrix
        result = uint8_t((keys & 0x1f) | 0xa0);
        result = uint8_t((result & 0xbf) | ear_ | speaker_);
    } else {
        // Floating bus on all even-parity? odd-A0 ports when not otherwise decoded
        result = floating_bus();
    }

    // Kempston joystick: any port with A5=0 and A0=1 (not ULA).
    // Classic interface only decodes low address bits; A5=0 is the usual test.
    if (kempston_enabled_ && (port & 0x21) == 0x01) {
        result = kempston_read();
    }

    // Kempston mouse (when enabled)
    if (kempston_mouse_) {
        switch (port) {
            case 0xfadf: result = kmouse_btn_; break;  // buttons (active low)
            case 0xfbdf: result = kmouse_x_; break;
            case 0xffdf: result = kmouse_y_; break;
            default: break;
        }
    }

    // FDC +3
    // uPD765 always present on +3 (same core as CPC 6128); disk may or may not be inserted
    if ((port & 0xf002) == 0x2000) result = fdc_.read_status();  // 2ffd
    if ((port & 0xf002) == 0x3000) result = fdc_.read_data();    // 3ffd

    // AY read $FFFD
    if ((port & 0xc002) == 0xc000) {
        result = (ay_select_ == 0) ? ay0_.read() : ay1_.read();
    }

    if (port == 0xff3b && ulaplus_.enabled) {
        if (ulaplus_.mode == 0) result = ulaplus_.pal[ulaplus_.last_reg & 63];
        else if (ulaplus_.mode == 1) result = ulaplus_.active ? 1 : 0;
    }
    return result;
}

void Spectrum3::io_out(uint16_t port, uint8_t value) {
    if ((port & 1) == 0) {
        border_ = value & 7;
        speaker_ = (value & 0x10) ? 0x10 : 0x00;
        beeper_level_ = (value & 0x10) ? int16_t(4096) : int16_t(-4096);
    }

    // +3 port decode (spectrum_3.pas): match on (port & $f002)
    switch (port & 0xf002) {
        case 0x1000:  // $1FFD (mask $F002)
            apply_1ffd(value);
            break;
        case 0x3000:  // 3ffd FDC data
            fdc_.write_data(value);
            break;
        case 0x4000: case 0x5000: case 0x6000: case 0x7000:  // 7ffd
            apply_7ffd(value);
            break;
        case 0x8000: case 0x9000: case 0xa000: case 0xb000:  // bffd AY data
            if (ay_select_ == 0) ay0_.write(value);
            else ay1_.write(value);
            break;
        case 0xc000: case 0xd000: case 0xe000: case 0xf000:  // fffd AY control
            if ((value & 0x9c) == 0x9c) ay_select_ = uint8_t((~value) & 1);
            if (ay_select_ == 0) ay0_.control(value);
            else ay1_.control(value);
            break;
        default:
            break;
    }


    if (port == 0xbf3b && ulaplus_.enabled) {
        ulaplus_.mode = uint8_t(value >> 6);
        if (ulaplus_.mode == 0) ulaplus_.last_reg = value & 0x3f;
    }
    if (port == 0xff3b && ulaplus_.enabled) {
        if (ulaplus_.mode == 0) ulaplus_set_entry(ulaplus_.last_reg & 63, value);
        else if (ulaplus_.mode == 1) ulaplus_.active = (value & 1) != 0;
    }
}

void Spectrum3::on_cycles(int cycles) {
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

    if (tape_.is_playing()) ear_ = tape_.advance(cycles) ? 0x40 : 0x00;

    if (if2_present_ && !if2_switched_) {
        if2_delay_ += cycles;
        if (if2_delay_ > 10500000) if2_switched_ = true;
    }

    audio_acc_ += int64_t(cycles) * kSampleRate;
    while (audio_acc_ >= int64_t(kClock)) {
        audio_acc_ -= int64_t(kClock);
        const int32_t ay = ay0_.update() + ay1_.update();
        const int32_t mixed = ay + beeper_level_;
        audio_.push_back(int16_t(std::clamp(mixed, int32_t(-32768), int32_t(32767))));
    }
}

void Spectrum3::render_line(int line) {
    // Pascal borde_128_full: 228 T/line, Y=line-15, left T203..227, paper lines 63..254
    
    if (line < 14 || line > 296) return;
    if (line == 14) return;  // only buffer fill; drawn as left of line 15
    const int sy = line - 15;
    if (sy < 0 || sy >= kScreenHeight) return;
    uint32_t* dst = framebuffer_.data() + size_t(sy) * kScreenWidth;
    const auto& brow = border_buf_[size_t(line % kLinesPerFrame)];

    auto col_at = [&](uint8_t idx) -> uint32_t {
        if (idx < 16) return palette_[idx];
        if (idx < 80) return palette_ext_[idx];
        return palette_[0];
    };

    // Left border: Pascal T 203..226 of previous line → 48 px
    if (line > 14) {
        const auto& prev = border_buf_[size_t((line - 1) % kLinesPerFrame)];
        for (int f = 0; f < 24; ++f) {
            const uint32_t c = col_at(prev[size_t(203 + f)]);
            const int px = f * 2;
            dst[px] = c;
            dst[px + 1] = c;
        }
    } else {
        const uint32_t c = border_colour();
        for (int x = 0; x < 48; ++x) dst[x] = c;
    }
    if (line >= 296) return;

    // Right border T 128-151
    for (int f = 0; f < 24; ++f) {
        const uint32_t c = col_at(brow[size_t(128 + f)]);
        dst[304 + f * 2] = c;
        dst[304 + f * 2 + 1] = c;
    }

    // Paper from display bank
    if (line >= 63 && line <= 254)  /* Pascal: skip centre border when linea>62 && linea<255 */ {
        const int y = line - 63;
        if (y >= 0 && y < 192) {
            const auto& vram = banks_[pantalla_];
            const uint16_t pix_base = kScrTable[y];
            const int attr_row = (y >> 3) << 5;
            const bool uplus = ulaplus_.active && ulaplus_.enabled;
            for (int col = 0; col < 32; ++col) {
                const uint8_t attrib = vram[0x1800 + attr_row + col];
                const uint8_t pixels = vram[pix_base + col];
                uint32_t c_ink, c_paper;
                if (uplus) {
                    const int bank =
                        ((((attrib & 0x80) >> 6) + ((attrib & 0x40) >> 6)) << 4) + 16;
                    c_ink = palette_ext_[bank + (attrib & 7)];
                    c_paper = palette_ext_[bank + ((attrib >> 3) & 7) + 8];
                } else {
                    int ink = attrib & 7;
                    int paper = (attrib >> 3) & 7;
                    if (attrib & 0x40) {
                        ink += 8;
                        paper += 8;
                    }
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
            return;
        }
    }

    // Top/bottom border centre
    for (int f = 0; f < 128; ++f) {
        const uint32_t c = col_at(brow[size_t(f)]);
        dst[48 + f * 2] = c;
        dst[48 + f * 2 + 1] = c;
    }
}

void Spectrum3::run_frame() {
    line_ = 0;
    t_in_line_ = 0;
    border_pos_ = 0;
    frame_t_ = 0;
    // Seed this frame's border buffer with the current border colour so any
    // line without an OUT still has a valid per-T colour.
    {
        const uint8_t col = border_index();
        for (auto& row : border_buf_) row.fill(col);
    }
    border_pos_ = 0;

    cpu_.set_irq(IrqLine::Hold);
    int remaining = kTstatesPerFrame;
    while (remaining > 0) {
        const int ran = cpu_.run(std::min(remaining, kTstatesPerLine));
        if (ran <= 0) break;
        remaining -= ran;
        if (remaining < kTstatesPerFrame - 32) cpu_.set_irq(IrqLine::Clear);
    }
    // Finish incomplete lines only — do not re-render (preserves per-T border).
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

void Spectrum3::drain_audio(std::vector<int16_t>& out) {
    out.swap(audio_);
    audio_.clear();
}

bool Spectrum3::load_media(const std::string& path, std::string* error) {
    std::string lower = path;
    for (char& ch : lower) ch = char(std::tolower(static_cast<unsigned char>(ch)));
    const auto ends = [&](const char* ext) {
        const size_t n = std::strlen(ext);
        return lower.size() >= n && lower.compare(lower.size() - n, n, ext) == 0;
    };
    if (ends(".tzx") || ends(".tap") || ends(".csw") || ends(".pzx") || ends(".cdt"))
        return load_tape(path, error);
    if (ends(".sna")) return load_sna(path, error);
    if (ends(".dsk")) {
        std::string err;
        if (!fdc_.load_disk(0, path, &err)) {
            if (error) *error = err;
            return false;
        }
        disk_present_ = true;
        return true;
    }
    if (ends(".rom") || ends(".bin") || ends(".if2")) return load_if2(path, error);
    if (error) *error = "unsupported media: " + path;
    return false;
}

bool Spectrum3::load_tape(const std::string& path, std::string* error) {
    if (!tape_.load_file(path, error)) return false;
    tape_.play(true);
    return true;
}

void Spectrum3::tape_play() {
    if (tape_.is_loaded()) tape_.play(tape_.is_paused() ? false : true);
}

void Spectrum3::tape_stop() {
    tape_.stop();
    ear_ = 0;
}

bool Spectrum3::load_sna(const std::string& path, std::string* error) {
    std::vector<uint8_t> buf;
    if (!load_file(path, buf)) {
        if (error) *error = "cannot open SNA";
        return false;
    }
    // 128K SNA: 27-byte header + 16K bank5 + 16K bank2 + 16K bank0 + 4-byte tail + remaining banks
    // Simplified: if size >= 49179 treat as 128K snapshot
    if (buf.size() < 27 + 0xc000) {
        if (error) *error = "SNA too small";
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

    if (buf.size() >= 49179) {
        // 128K format
        std::memcpy(banks_[5].data(), buf.data() + 27, 0x4000);
        std::memcpy(banks_[2].data(), buf.data() + 27 + 0x4000, 0x4000);
        std::memcpy(banks_[0].data(), buf.data() + 27 + 0x8000, 0x4000);
        const uint8_t* tail = buf.data() + 27 + 0xc000;
        const uint16_t pc = uint16_t(tail[0] | (tail[1] << 8));
        apply_7ffd(tail[2]);
        cpu_.set_pc(pc);
        // remaining 6 banks of 16K
        size_t off = 27 + 0xc000 + 4;
        for (int b = 0; b < 8; ++b) {
            if (b == 0 || b == 2 || b == 5) continue;
            if (off + 0x4000 <= buf.size()) {
                std::memcpy(banks_[b].data(), buf.data() + off, 0x4000);
                off += 0x4000;
            }
        }
    } else {
        // 48K-style into bank 5/2/0 mapped at 4000/8000/C000
        std::memcpy(banks_[5].data(), buf.data() + 27, 0x4000);
        std::memcpy(banks_[2].data(), buf.data() + 27 + 0x4000, 0x4000);
        std::memcpy(banks_[0].data(), buf.data() + 27 + 0x8000, 0x4000);
        const uint16_t sp = cpu_.sp;
        const uint16_t pc = uint16_t(banks_[0][sp & 0x3fff] | (banks_[0][(sp + 1) & 0x3fff] << 8));
        cpu_.sp = uint16_t(sp + 2);
        cpu_.set_pc(pc);
        marco_ = {8, 5, 2, 0};
    }
    cpu_.set_irq(IrqLine::Clear);
    return true;
}



bool Spectrum3::load_if2(const std::string& path, std::string* error) {
    std::vector<uint8_t> buf;
    if (!load_file(path, buf)) {
        if (error) *error = "cannot open IF2 ROM: " + path;
        return false;
    }
    if (buf.size() < 0x4000) {
        if (error) *error = "IF2 ROM too small (need 16K or 32K)";
        return false;
    }
    if2_rom_.fill(0xff);
    const size_t n = std::min(buf.size(), size_t(0x8000));
    std::memcpy(if2_rom_.data(), buf.data(), n);
    if (n < 0x8000) {
        // Mirror lower 16K into upper if only 16K provided
        std::memcpy(if2_rom_.data() + 0x4000, if2_rom_.data(), 0x4000);
    }
    if2_present_ = true;
    if2_switched_ = false;
    if2_delay_ = 0;
    return true;
}

void Spectrum3::unload_if2() {
    if2_present_ = false;
    if2_switched_ = false;
    if2_delay_ = 0;
}

bool Spectrum3::load_dsk(const std::string& path, std::string* error) {
    if (!fdc_.load_disk(0, path, error)) return false;
    disk_present_ = true;
    return true;
}

}  // namespace dsp
