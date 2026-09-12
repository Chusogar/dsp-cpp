#include "drivers/computers/samcoupe.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace dsp {
namespace {

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

// Default reset CLUT values (Inside the SAM Coupe technical manual): the low
// 8 entries are the classic 8 colours, the high 8 their bright variants.
constexpr uint8_t kDefaultClut[16] = {
    0, 16, 32, 48, 64, 80, 96, 120, 0, 17, 34, 51, 68, 85, 102, 127,
};

// Key matrix position (column 0-8, row-bit 0-7) for every SAM key we map.
struct SamKeyPos { Key host; int col; int bit; };
constexpr SamKeyPos kKeyMap[] = {
    // Position = SimCoupe's eSamKey ordinal: column = k/8, bit = k%8.
    // Note eSamKey starts at SK_SHIFT == 0 (the enum's SK_MINMINUS1 =
    // SK_MIN - 1 trick makes the first real key 0, not 1); getting that
    // wrong shifts the whole table by one, which reads as every key
    // producing its neighbour (F9 typing "1", "1" typing "2", ...).
    {Key::LeftShift, 0, 0}, {Key::RightShift, 0, 0}, {Key::Z, 0, 1}, {Key::X, 0, 2},
    {Key::C, 0, 3}, {Key::V, 0, 4}, {Key::F1, 0, 5}, {Key::F2, 0, 6},
    {Key::F3, 0, 7},
    {Key::A, 1, 0}, {Key::S, 1, 1}, {Key::D, 1, 2}, {Key::F, 1, 3},
    {Key::G, 1, 4}, {Key::F4, 1, 5}, {Key::F5, 1, 6}, {Key::F6, 1, 7},
    {Key::Q, 2, 0}, {Key::W, 2, 1}, {Key::E, 2, 2}, {Key::R, 2, 3},
    {Key::T, 2, 4}, {Key::F7, 2, 5}, {Key::F8, 2, 6}, {Key::F9, 2, 7},
    {Key::Num1, 3, 0}, {Key::Num2, 3, 1}, {Key::Num3, 3, 2}, {Key::Num4, 3, 3},
    {Key::Num5, 3, 4}, {Key::Escape, 3, 5}, {Key::Tab, 3, 6}, {Key::CapsLock, 3, 7},
    {Key::Num0, 4, 0}, {Key::Num9, 4, 1}, {Key::Num8, 4, 2}, {Key::Num7, 4, 3},
    {Key::Num6, 4, 4}, {Key::Minus, 4, 5}, {Key::Plus, 4, 6}, {Key::Backspace, 4, 7},
    {Key::P, 5, 0}, {Key::O, 5, 1}, {Key::I, 5, 2}, {Key::U, 5, 3},
    {Key::Y, 5, 4}, {Key::Equals, 5, 5}, {Key::Quote, 5, 6}, {Key::F10, 5, 7},
    {Key::Enter, 6, 0}, {Key::L, 6, 1}, {Key::K, 6, 2}, {Key::J, 6, 3},
    {Key::H, 6, 4}, {Key::Semicolon, 6, 5}, {Key::Slash, 6, 6}, {Key::Home, 6, 7},
    {Key::Space, 7, 0}, {Key::RightCtrl, 7, 1}, {Key::M, 7, 2}, {Key::N, 7, 3},
    {Key::B, 7, 4}, {Key::Comma, 7, 5}, {Key::Period, 7, 6},
    {Key::LeftCtrl, 8, 0}, {Key::Up, 8, 1}, {Key::Down, 8, 2}, {Key::Left, 8, 3},
    {Key::Right, 8, 4},
};

}  // namespace

SamCoupe::SamCoupe() : cpu_(kClock), saa_(kSaaClock) {}

bool SamCoupe::init(const std::string& rom_path, std::string* error) {
    std::vector<uint8_t> rom;
    const char* names[] = {"rom30.rom", "rom30.z5", "samcoupe.rom", "sam_rom.rom", "rom.bin"};
    bool ok = false;
    for (const char* n : names) {
        if (try_rom(rom_path, n, rom) && rom.size() >= 0x8000) { ok = true; break; }
    }
    if (!ok && load_file(rom_path, rom) && rom.size() >= 0x8000) ok = true;
    if (!ok) {
        if (error) *error = "SAM Coupe ROM (32 KB) not found in " + rom_path;
        return false;
    }
    std::memcpy(rom0_.data(), rom.data(), 0x4000);
    std::memcpy(rom1_.data(), rom.data() + 0x4000, 0x4000);

    cpu_.set_memory_handlers(
        [this](uint16_t a) { return mem_read(a); },
        [this](uint16_t a, uint8_t v) { mem_write(a, v); });
    cpu_.set_io_handlers(
        [this](uint16_t p) { return io_in(p); },
        [this](uint16_t p, uint8_t v) { io_out(p, v); });
    cpu_.set_cycle_handler([this](int c) { on_cycles(c); });

    reset();
    return true;
}

void SamCoupe::reset() {
    for (auto& page : ram_) page.fill(0);
    lmpr_ = 0;
    hmpr_ = 0;
    vmpr_ = 0;
    update_paging();
    border_ = 0;
    status_ = 0xff;
    line_int_ = 0xff;
    for (int i = 0; i < 16; ++i) {
        clut_[i] = kDefaultClut[i];
        update_clut_rgb(i);
    }
    key_matrix_.fill(0xff);
    flash_phase_ = false;
    flash_count_ = 0;
    line_ = t_in_line_ = frame_t_ = 0;
    audio_.clear();
    audio_acc_ = 0;
    cpu_.reset();
    saa_.reset();
    fdc_.reset();
    std::fill(framebuffer_.begin(), framebuffer_.end(), clut_rgb_[0]);
}

void SamCoupe::update_paging() {
    section_page_[0] = (lmpr_ & 0x20) ? int(lmpr_ & 0x1f) : kSectRom0;
    section_page_[1] = (lmpr_ + 1) & 0x1f;
    section_page_[2] = hmpr_ & 0x1f;
    section_page_[3] = (lmpr_ & 0x40) ? kSectRom1 : int((hmpr_ + 1) & 0x1f);
}

uint8_t* SamCoupe::section_ptr(int slot) {
    const int page = section_page_[size_t(slot)];
    if (page == kSectRom0) return rom0_.data();
    if (page == kSectRom1) return rom1_.data();
    return ram_[size_t(page)].data();
}

uint8_t SamCoupe::mem_read(uint16_t addr) {
    return section_ptr(addr >> 14)[addr & 0x3fff];
}

void SamCoupe::mem_write(uint16_t addr, uint8_t value) {
    const int slot = addr >> 14;
    const int page = section_page_[size_t(slot)];
    if (page == kSectRom0 || page == kSectRom1) return;  // ROM: read-only
    if (slot == 0 && (lmpr_ & 0x80)) return;              // WPRAM: section A locked
    ram_[size_t(page)][addr & 0x3fff] = value;
}

void SamCoupe::out_lmpr(uint8_t value) {
    lmpr_ = value;
    update_paging();
}

void SamCoupe::out_hmpr(uint8_t value) {
    hmpr_ = value;
    update_paging();
}

void SamCoupe::out_vmpr(uint8_t value) {
    vmpr_ = value & 0x7f;  // bits 0-6 read/write; bit 7 is TXMIDI (write) / RXMIDI (read)
}

void SamCoupe::out_border(uint8_t value) {
    border_ = value;
}

void SamCoupe::update_clut_rgb(int index) {
    const uint8_t v = clut_[size_t(index)];
    const int bright = (v >> 3) & 1;
    const auto level = [&](int lo_bit, int hi_bit) -> uint8_t {
        const int lo = (v >> lo_bit) & 1;
        const int hi = (v >> hi_bit) & 1;
        const int lvl = (hi << 2) | (lo << 1) | bright;  // 0-7
        return uint8_t((lvl * 255 + 3) / 7);
    };
    const uint8_t r = level(1, 5);  // RED0 (bit1), RED1 (bit5)
    const uint8_t g = level(2, 6);  // GRN0 (bit2), GRN1 (bit6)
    const uint8_t b = level(0, 4);  // BLU0 (bit0), BLU1 (bit4)
    clut_rgb_[size_t(index)] = 0xff000000u | (uint32_t(r) << 16) | (uint32_t(g) << 8) | uint32_t(b);
}

void SamCoupe::out_clut(int index, uint8_t value) {
    if (index < 0 || index >= 16) return;
    clut_[size_t(index)] = value & 0x7f;
    update_clut_rgb(index);
}

int SamCoupe::visible_screen_page() const {
    return (vmpr_ & 0x1f) & ~1;  // always an even page; the mode wraps into the odd partner
}

int SamCoupe::screen_mode() const {
    return int((vmpr_ >> 5) & 3) + 1;
}

void SamCoupe::set_dip_switch(int, uint8_t) {}

void SamCoupe::apply_keyboard(const MachineInputs& in) {
    key_matrix_.fill(0xff);
    for (const auto& k : kKeyMap) {
        if (in.key(k.host)) key_matrix_[size_t(k.col)] &= uint8_t(~(1u << k.bit));
    }
}

void SamCoupe::set_inputs(const MachineInputs& inputs) { apply_keyboard(inputs); }

int SamCoupe::select_disk_port(uint16_t port) {
    const uint16_t low = port & 0xff;
    int base = -1;
    if (low >= 224 && low <= 231) base = 224;
    else if (low >= 240 && low <= 247) base = 240;
    if (base < 0) return -1;
    const int offset = int(low - base);
    fdc_.set_disk(base == 224 ? &disk1_ : &disk2_);
    fdc_.set_side(offset >= 4 ? 1 : 0);
    return offset & 3;
}

uint8_t SamCoupe::io_in(uint16_t port) {
    const uint8_t low = uint8_t(port);
    const uint8_t high = uint8_t(port >> 8);
    uint8_t result = 0xff;

    if (low == 249) {  // STATUS: interrupt flags (low 5) + keyboard K6-K8 (high 3)
        uint8_t keys = 0xff;
        if (!(high & 0x80)) keys &= key_matrix_[7];
        if (!(high & 0x40)) keys &= key_matrix_[6];
        if (!(high & 0x20)) keys &= key_matrix_[5];
        if (!(high & 0x10)) keys &= key_matrix_[4];
        if (!(high & 0x08)) keys &= key_matrix_[3];
        if (!(high & 0x04)) keys &= key_matrix_[2];
        if (!(high & 0x02)) keys &= key_matrix_[1];
        if (!(high & 0x01)) keys &= key_matrix_[0];
        result = uint8_t((status_ & 0x1f) | (keys & 0xe0));
    } else if (low == 254) {  // KEYBOARD: K1-K5 (bits0-4), SPEN, EAR, SOFF
        uint8_t keys = 0xff;
        if (!(high & 0x80)) keys &= key_matrix_[7];
        if (!(high & 0x40)) keys &= key_matrix_[6];
        if (!(high & 0x20)) keys &= key_matrix_[5];
        if (!(high & 0x10)) keys &= key_matrix_[4];
        if (!(high & 0x08)) keys &= key_matrix_[3];
        if (!(high & 0x04)) keys &= key_matrix_[2];
        if (!(high & 0x02)) keys &= key_matrix_[1];
        if (!(high & 0x01)) keys &= key_matrix_[0];
        if (high == 0xff) keys &= key_matrix_[8];  // RDMSEL (all address lines high)
        result = uint8_t((keys & 0x1f) | 0xe0);
    } else if (low == 252) {  // VMPR
        result = vmpr_;
    } else if (low == 251) {  // HMPR
        result = hmpr_;
    } else if (low == 250) {  // LMPR
        result = lmpr_;
    } else if (low == 255) {  // ATTRIBUTE (floating display byte) - not modelled, return 0xff
        result = 0xff;
    } else if (const int off = select_disk_port(port); off >= 0) {
        switch (off) {
            case 0: result = fdc_.status_r(); break;
            case 1: result = fdc_.track_r(); break;
            case 2: result = fdc_.sector_r(); break;
            case 3: result = fdc_.data_r(); break;
            default: break;
        }
    }
    return result;
}

void SamCoupe::io_out(uint16_t port, uint8_t value) {
    const uint8_t low = uint8_t(port);
    // SOUND: data port 255 (0x00FF) and address port 511 (0x01FF) differ
    // only in address bit 8, unlike the other single-byte-decoded ports.
    if (low == 0xff) {
        if (port & 0x100) saa_.select(value);
        else saa_.write(value);
        return;
    }
    if (low == 253) {
        // MIDI OUT: no MIDI device modelled.
    } else if (low == 254) {
        out_border(value);
    } else if (low == 249) {
        line_int_ = value;
    } else if (low == 252) {
        out_vmpr(value);
    } else if (low == 251) {
        out_hmpr(value);
    } else if (low == 250) {
        out_lmpr(value);
    } else if (low == 0xf8) {  // CLUT: base port 248, register selected by port bits 8-11
        out_clut(int(port >> 8), value);
    } else if (const int off = select_disk_port(port); off >= 0) {
        switch (off) {
            case 0: fdc_.command_w(value); break;
            case 1: fdc_.track_w(value); break;
            case 2: fdc_.sector_w(value); break;
            case 3: fdc_.data_w(value); break;
            default: break;
        }
    }
}

void SamCoupe::render_mode1(uint32_t* dst, const uint8_t* page, int y) const {
    static const auto line_to_byte = [](int line) -> uint16_t {
        const int byte_off = (line & 0xc0) | ((line << 3) & 0x38) | ((line >> 3) & 0x07);
        return uint16_t(byte_off << 5);
    };
    const uint16_t data_base = line_to_byte(y);
    const uint16_t attr_base = uint16_t(6144 + ((y & 0xf8) << 2));
    for (int cell = 0; cell < kScreenCells; ++cell) {
        const uint8_t data = page[data_base + cell];
        const uint8_t attr = page[attr_base + cell];
        int ink_idx = ((attr >> 3) & 8) | (attr & 7);
        int paper_idx = (attr >> 3) & 0xf;
        if (flash_phase_ && (attr & 0x80)) std::swap(ink_idx, paper_idx);
        const uint32_t ink = clut_rgb_[size_t(ink_idx)];
        const uint32_t paper = clut_rgb_[size_t(paper_idx)];
        uint32_t* p = dst + cell * 16;
        for (int b = 0; b < 8; ++b) {
            const uint32_t c = (data & (0x80 >> b)) ? ink : paper;
            p[b * 2] = p[b * 2 + 1] = c;
        }
    }
}

void SamCoupe::render_mode2(uint32_t* dst, const uint8_t* page, int y) const {
    const uint16_t data_base = uint16_t(y << 5);
    const uint16_t attr_base = uint16_t(data_base + 0x2000);
    for (int cell = 0; cell < kScreenCells; ++cell) {
        const uint8_t data = page[data_base + cell];
        const uint8_t attr = page[attr_base + cell];
        int ink_idx = ((attr >> 3) & 8) | (attr & 7);
        int paper_idx = (attr >> 3) & 0xf;
        if (flash_phase_ && (attr & 0x80)) std::swap(ink_idx, paper_idx);
        const uint32_t ink = clut_rgb_[size_t(ink_idx)];
        const uint32_t paper = clut_rgb_[size_t(paper_idx)];
        uint32_t* p = dst + cell * 16;
        for (int b = 0; b < 8; ++b) {
            const uint32_t c = (data & (0x80 >> b)) ? ink : paper;
            p[b * 2] = p[b * 2 + 1] = c;
        }
    }
}

uint8_t SamCoupe::video_byte(int base_page, uint32_t offset) const {
    const int page = (base_page + int(offset >> 14)) & (kNumPages - 1);
    return ram_[size_t(page)][offset & 0x3fff];
}

void SamCoupe::render_mode3(uint32_t* dst, int base_page, int y) const {
    // 2 bits/pixel, 128 bytes/line (512px * 2bpp / 8); the CLUT index is
    // extended by HMPR bits 5-6 (MD3S0/MD3S1).
    const uint8_t bcd48 = uint8_t((hmpr_ & 0x60) >> 3);
    const uint32_t mode3_clut[4] = {
        clut_rgb_[size_t(bcd48 | 0)],
        clut_rgb_[size_t(bcd48 | 2)],
        clut_rgb_[size_t(bcd48 | 1)],
        clut_rgb_[size_t(bcd48 | 3)],
    };
    const uint32_t line_base = uint32_t(y) << 7;
    uint32_t* p = dst;
    for (int cell = 0; cell < kScreenCells; ++cell) {
        for (int i = 0; i < 4; ++i) {
            const uint8_t data = video_byte(base_page, line_base + uint32_t(cell * 4 + i));
            p[0] = mode3_clut[data >> 6];
            p[1] = mode3_clut[(data >> 4) & 3];
            p[2] = mode3_clut[(data >> 2) & 3];
            p[3] = mode3_clut[data & 3];
            p += 4;
        }
    }
}

void SamCoupe::render_mode4(uint32_t* dst, int base_page, int y) const {
    // 4 bits/pixel. Verified directly against SimCoupe's Mode4Line(): SAM's
    // mode 4 shares mode 3's 128-byte/line stride (4 bytes/cell) and each
    // nibble is displayed at double width, giving an effective 256 distinct
    // colours across the 512-pixel-wide line rather than 512 fully
    // independent pixels. (An earlier version of this function assumed
    // 256 bytes/line from raw 512px*4bpp/8 arithmetic; that doesn't match
    // the real ASIC and produced a corrupted, half-width image.)
    const uint32_t line_base = uint32_t(y) << 7;
    uint32_t* p = dst;
    for (int cell = 0; cell < kScreenCells; ++cell) {
        for (int i = 0; i < 4; ++i) {
            const uint8_t data = video_byte(base_page, line_base + uint32_t(cell * 4 + i));
            const uint32_t hi = clut_rgb_[size_t(data >> 4)];
            const uint32_t lo = clut_rgb_[size_t(data & 0xf)];
            p[0] = p[1] = hi;
            p[2] = p[3] = lo;
            p += 4;
        }
    }
}

void SamCoupe::render_line(int line) {
    if (line < 0 || line >= kLinesPerFrame) return;
    uint32_t* dst = framebuffer_.data() + size_t(line) * kScreenWidth;
    // BORDER register: bits 0-2 are CLUT address bits 0/1/2 (BCD1/2/4),
    // bit 5 is CLUT address bit 3 (BCD8) -- not a contiguous 4-bit field.
    const uint32_t border_colour = clut_rgb_[size_t(((border_ & 0x20) >> 2) | (border_ & 0x07))];

    if (line < kTopBorderLines || line >= kTopBorderLines + kScreenLines) {
        std::fill(dst, dst + kScreenWidth, border_colour);
        return;
    }

    std::fill(dst, dst + kSideBorderCells * 16, border_colour);
    std::fill(dst + (kSideBorderCells + kScreenCells) * 16, dst + kScreenWidth, border_colour);

    const int y = line - kTopBorderLines;
    const int base_page = visible_screen_page() & (kNumPages - 1);
    const uint8_t* page = ram_[size_t(base_page)].data();
    uint32_t* screen = dst + kSideBorderCells * 16;
    switch (screen_mode()) {
        case 1: render_mode1(screen, page, y); break;
        case 2: render_mode2(screen, page, y); break;
        case 3: render_mode3(screen, base_page, y); break;
        case 4: render_mode4(screen, base_page, y); break;
        default: break;
    }
}

void SamCoupe::on_cycles(int cycles) {
    for (int n = 0; n < cycles; ++n) {
        ++t_in_line_;
        ++frame_t_;
        if (t_in_line_ >= kTstatesPerLine) {
            t_in_line_ -= kTstatesPerLine;
            render_line(line_);
            ++line_;
            if (line_int_ < 192 && line_ == kTopBorderLines + int(line_int_)) {
                status_ &= uint8_t(~0x01);  // LINE int (active low)
                cpu_.set_irq(IrqLine::Hold);
            }
            if (line_ >= kLinesPerFrame) line_ = 0;
        }
    }
    audio_acc_ += int64_t(cycles) * kSampleRate;
    while (audio_acc_ >= int64_t(kClock)) {
        audio_acc_ -= int64_t(kClock);
        int32_t left = 0, right = 0;
        saa_.update(left, right);
        const int32_t mixed = (left + right) / 2;
        audio_.push_back(int16_t(std::clamp(mixed, int32_t(-32768), int32_t(32767))));
    }
}

void SamCoupe::run_frame() {
    line_ = 0;
    t_in_line_ = 0;
    frame_t_ = 0;
    fdc_.tick_frame();

    status_ |= 0x01;  // clear LINE int from the previous frame
    status_ &= uint8_t(~0x08);  // FRAME int (active low), raised for the whole frame
    cpu_.set_irq(IrqLine::Hold);

    int remaining = kTstatesPerFrame;
    while (remaining > 0) {
        const int ran = cpu_.run(std::min(remaining, kTstatesPerLine));
        if (ran <= 0) break;
        remaining -= ran;
        // Both the IRQ line and the STATUS flag are released a short way
        // into the frame. Widening this window (so polling loops see the
        // flag for longer) was tried and measurably *hurt*: Lemmings'
        // loader derailed at frame 80 instead of 127, so the narrow pulse
        // is closer to what its timing expects.
        if (remaining < kTstatesPerFrame - 128) {
            status_ |= 0x08;
            cpu_.set_irq(IrqLine::Clear);
        }
    }
    if (line_ != 0 || t_in_line_ != 0) {
        while (line_ < kLinesPerFrame) {
            render_line(line_);
            ++line_;
        }
    }
    flash_count_ = (flash_count_ + 1) & 0x1f;
    if (flash_count_ == 0) flash_phase_ = !flash_phase_;
}

void SamCoupe::drain_audio(std::vector<int16_t>& out) {
    out.swap(audio_);
    audio_.clear();
}

bool SamCoupe::load_media(const std::string& path, std::string* error) {
    return disk1_.load(path, error);
}

bool SamCoupe::auto_boot() {
    if (!disk1_.loaded()) return false;
    // Hand control to the ROM's own BOOT implementation rather than
    // emulating it. BOOTNR ($D8DF in ROM1) is the path the BOOT keyword
    // takes when no DOS is resident yet: it calls BOOTEX (which finds a
    // free page, resets the FDC, reads the boot record from track 4 and
    // jumps into it) and then issues "RST 8 / DB BTHK", the auto-load hook
    // that finds and runs the disk's AUTO file.
    //
    // That trailing hook is why this matters. Games with a self-contained
    // loader in their boot record (Prince of Persia, Rick Dangerous) run
    // fine if you just jump straight at the boot sector, but disks that
    // boot via SAMDOS (e.g. this Tetris port) only load the DOS itself
    // that way -- the DOS then relies on the ROM hook to load and start
    // the actual game. Skipping the hook left them sitting at the BASIC
    // prompt with a loaded-but-idle DOS.
    //
    // Section A must hold ROM0 (so the RST vectors are real) and section D
    // ROM1 (so $D8DF is the routine and not RAM).
    lmpr_ = uint8_t((lmpr_ & 0x1f) | 0x40);  // bit5 clear = ROM0 in, bit6 set = ROM1 in
    update_paging();
    // Mask interrupts across the hand-over. Without this a frame interrupt
    // that happens to be pending fires on the very first instruction at
    // the new PC, vectors to the ROM's $0038 handler and drops into the
    // BASIC main loop -- so BOOT never gets a chance to run. BOOTEX
    // re-enables interrupts itself once it has set the machine up.
    cpu_.iff1 = cpu_.iff2 = false;
    cpu_.set_irq(IrqLine::Clear);
    cpu_.halted = false;
    cpu_.set_pc(0xd8df);
    return true;
}

}  // namespace dsp
