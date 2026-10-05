#include "drivers/computers/plus4.h"

#include <cstring>
#include <fstream>

#include "core/rom_loader.h"
#include "cpu/irq_line.h"

namespace dsp {
namespace {

bool ends_ci(const std::string& s, const char* ext) {
    const size_t n = std::strlen(ext);
    if (s.size() < n) return false;
    for (size_t i = 0; i < n; i++) {
        char a = s[s.size() - n + i];
        char b = ext[i];
        if (a >= 'A' && a <= 'Z') a = char(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = char(b - 'A' + 'a');
        if (a != b) return false;
    }
    return true;
}

bool read_file(const std::string& path, std::vector<uint8_t>* out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    f.seekg(0, std::ios::end);
    const auto n = f.tellg();
    if (n <= 0) return false;
    f.seekg(0, std::ios::beg);
    out->resize(size_t(n));
    f.read(reinterpret_cast<char*>(out->data()), n);
    return bool(f);
}

// Simplified Plus/4 PLA decode (from MAME / zimmers pla.c), phi0=1 mux=0 ras=0|1.
void bankswitch(uint16_t offset, int ras, int* scs, int* user, int* acia, int* addr_clk,
                int* keyport, int* kernal) {
    const int a15 = (offset >> 15) & 1;
    const int a14 = (offset >> 14) & 1;
    const int a13 = (offset >> 13) & 1;
    const int a12 = (offset >> 12) & 1;
    const int a11 = (offset >> 11) & 1;
    const int a10 = (offset >> 10) & 1;
    const int a9 = (offset >> 9) & 1;
    const int a8 = (offset >> 8) & 1;
    const int a7 = (offset >> 7) & 1;
    const int a6 = (offset >> 6) & 1;
    const int a5 = (offset >> 5) & 1;
    const int a4 = (offset >> 4) & 1;
    const int phi0 = 1;
    const int i0_f7 = 1;  // mux=0 → F7 via F1 path; treat as selected for I/O

    // SCS_ 0 when 0111 011x 1001 011x  (approx $FD1x with PLA timing)
    const int f0 = ras || !a10 || !a11 || !a13 || a9 || !a8 || !a14 || !a12 || a7 || a6 || !a5 ||
                   a4 || !a15 || !phi0;
    // USER_ 0 when 0111 011x 1000 1111
    const int f2 = ras || !a10 || !a11 || !a13 || a9 || !a8 || !a14 || !a12 || a7 || a6 || a5 ||
                   !a4 || !a15 || !phi0 || !i0_f7;
    // 6551_ 0 when x111 011x 1000 011x
    const int f3 = !a10 || !a11 || !a13 || a9 || !a8 || !a14 || !a12 || a7 || a6 || a5 || a4 ||
                   !a15 || !phi0;
    // ADDR_CLK 0 when 1111 011x 1110 1111 → $FDDx
    const int f4 = ras || !a10 || !a11 || !a13 || a9 || !a8 || !a14 || !a12 || !a7 || !a6 || a5 ||
                   !a4 || !a15 || !phi0 || !i0_f7;
    // KEYPORT_ 0 when 0111 011x 1001 1111 → $FD3x
    const int f5 = ras || !a10 || !a11 || !a13 || a9 || !a8 || !a14 || !a12 || a7 || a6 || !a5 ||
                   !a4 || !a15 || !phi0 || !i0_f7;
    // KERNAL_ 1 when x111 001x 1xxx x1xx → $FCxx forces kernal
    const int f6 = a10 && a11 && a13 && !a9 && !a8 && a14 && a12 && a15;

    *scs = f0;
    *user = f2;
    *acia = f3;
    *addr_clk = f4;
    *keyport = f5;
    *kernal = f6;
}

}  // namespace

Plus4::Plus4(Model model, Region region)
    : model_(model),
      region_(region),
      cpu_(region == Region::Pal ? Mos7360::kPalClock / 2u : Mos7360::kNtscClock / 2u),
      ted_(region == Region::Pal ? Mos7360::Variant::Pal : Mos7360::Variant::Ntsc),
      framebuffer_(size_t(ted_.vis_width() * ted_.vis_height()), 0) {
    const size_t ram_size = (model_ == Model::C16_16K) ? 0x4000u : 0x10000u;
    ram_.assign(ram_size, 0);
    ram_mask_ = uint32_t(ram_size - 1);
    frames_per_second_ = double(ted_.cpu_clock()) /
                         double(ted_.lines() * ted_.cycles_per_line());

    cpu_.set_memory_handlers(
        [this](uint16_t a) { return read_byte(a); },
        [this](uint16_t a, uint8_t v) { write_byte(a, v); });
    cpu_.set_cycle_handler([this](int c) { on_cycles(c); });

    ted_.set_mem_read([this](uint16_t a, bool rom) { return ted_videoram_r(a, rom); });
    ted_.set_key_read([this](uint8_t cols) { return ted_k_r(cols); });
    ted_.set_irq_callback([this](bool asserted) {
        ted_irq_ = asserted;
        update_irq();
    });

    keyboard_.fill(0xff);
}

const char* Plus4::title() const {
    if (model_ == Model::C16_16K) {
        return region_ == Region::Pal ? "Commodore 16 (PAL)" : "Commodore 16 (NTSC)";
    }
    return region_ == Region::Pal ? "Commodore Plus/4 (PAL)" : "Commodore Plus/4 (NTSC)";
}

bool Plus4::init(const std::string& rom_path, std::string* error) {
    return load_roms(rom_path, error);
}

bool Plus4::load_roms(const std::string& path, std::string* error) {
    RomLoader loader;
    if (!loader.open(path, error)) return false;

    auto load_one = [&](const char* name, uint8_t* dst, size_t size) -> bool {
        std::vector<uint8_t> buf;
        if (!loader.try_read(name, buf) || buf.size() < size) return false;
        std::memcpy(dst, buf.data(), size);
        return true;
    };

    // BASIC at kernal_basic_[0x0000], KERNAL at [0x4000] (MAME "kernal" region).
    if (!load_one("318006-01.u23", kernal_basic_.data(), 0x4000) &&
        !load_one("318006-01.u3", kernal_basic_.data(), 0x4000) &&
        !load_one("318006-01", kernal_basic_.data(), 0x4000) &&
        !load_one("basic", kernal_basic_.data(), 0x4000)) {
        if (error) *error = "missing BASIC ROM (318006-01.u23)";
        return false;
    }

    uint8_t* kern = kernal_basic_.data() + 0x4000;
    bool got_kernal = false;
    if (region_ == Region::Pal) {
        got_kernal = load_one("318004-05.u24", kern, 0x4000) ||
                     load_one("318004-05.u4", kern, 0x4000) ||
                     load_one("318004-04.u24", kern, 0x4000) ||
                     load_one("318004-03.u24", kern, 0x4000) ||
                     load_one("318004-05", kern, 0x4000);
    }
    if (!got_kernal) {
        got_kernal = load_one("318005-05.u24", kern, 0x4000) ||
                     load_one("318005-04.u24", kern, 0x4000) ||
                     load_one("318005-05", kern, 0x4000) ||
                     load_one("318005-04", kern, 0x4000) ||
                     load_one("318004-05.u24", kern, 0x4000) ||
                     load_one("kernal", kern, 0x4000);
    }
    // JiffyDOS combined image: BASIC+KERNAL in one 32K file.
    if (!got_kernal) {
        std::vector<uint8_t> buf;
        if (loader.try_read("jiffydos plus4.u24", buf) && buf.size() >= 0x8000) {
            std::memcpy(kernal_basic_.data(), buf.data(), 0x8000);
            got_kernal = true;
        }
    }
    if (!got_kernal) {
        if (error) *error = "missing KERNAL ROM (318005-05.u24 / 318004-05.u24)";
        return false;
    }

    have_function_ = false;
    function_.fill(0xff);
    if (load_one("317053-01.u25", function_.data(), 0x4000) ||
        load_one("317053-01", function_.data(), 0x4000)) {
        if (load_one("317054-01.u26", function_.data() + 0x4000, 0x4000) ||
            load_one("317054-01", function_.data() + 0x4000, 0x4000)) {
            have_function_ = true;
        }
    }

    reset();
    return true;
}

void Plus4::reset() {
    uint8_t data = 0xff;
    for (size_t i = 0; i < ram_.size(); i++) {
        ram_[i] = data;
        if ((i % 64) == 0) data = uint8_t(data ^ 0xff);
    }
    port_ddr_ = 0;
    port_out_ = 0;
    kb_ = 0xff;
    addr_latch_ = 0;
    keyboard_.fill(0xff);
    joy1_ = joy2_ = 0xff;
    ted_irq_ = false;
    pending_prg_.clear();
    boot_frames_ = 0;
    cpu_cycle_debt_ = 0;
    audio_acc_ = 0;
    audio_.clear();

    ted_.reset();
    cpu_.reset();
    update_irq();
}

void Plus4::update_irq() {
    cpu_.set_irq(ted_irq_ ? IrqLine::Assert : IrqLine::Clear);
}

uint8_t Plus4::ted_k_r(uint8_t columns) {
    uint8_t data = 0xff;

    // Joystick bits selected by the value written to $FF08 (MAME ted_k_r).
    if ((columns & 0x04) == 0) {
        data = uint8_t(data & (0xf0 | (joy1_ & 0x0f)));
        if ((joy1_ & 0x20) == 0) data = uint8_t(data & ~0x40);
    }
    if ((columns & 0x02) == 0) {
        data = uint8_t(data & (0xf0 | (joy2_ & 0x0f)));
        if ((joy2_ & 0x20) == 0) data = uint8_t(data & ~0x80);
    }

    // Keyboard: kb_ bits clear = that row selected (6529 output low).
    for (int row = 0; row < 8; row++) {
        if ((kb_ & (1 << row)) == 0) data = uint8_t(data & keyboard_[size_t(row)]);
    }
    return data;
}

uint8_t Plus4::read_memory(uint16_t offset, RomView rom_view) {
    int scs, user, acia, addr_clk, keyport, kernal;
    // ras=0 for CPU; TED video fetches use ras=1 so I/O PLA lines stay inactive.
    const int ras = (rom_view == RomView::Latch) ? 0 : 1;
    bankswitch(offset, ras, &scs, &user, &acia, &addr_clk, &keyport, &kernal);

    bool rom_on = ted_.rom_enabled();
    if (rom_view == RomView::ForceRam) rom_on = false;
    if (rom_view == RomView::ForceRom) rom_on = true;

    int cs0 = 1, cs1 = 1;
    if (rom_on) {
        if (offset >= 0x8000 && offset < 0xc000) cs0 = 0;
        if ((offset >= 0xc000 && offset < 0xfd00) || offset >= 0xff20) cs1 = 0;
    }

    uint8_t data = ted_.bus_r();
    if (rom_view == RomView::Latch && offset >= 0xff00 && offset <= 0xff1f) {
        return ted_.read(offset, &cs0, &cs1);
    }
    if (rom_view == RomView::Latch) {
        // Keep TED last_data / cs side effects consistent with MAME's always-call TED.
        int tcs0 = 1, tcs1 = 1;
        ted_.read(offset, &tcs0, &tcs1);
        cs0 = tcs0;
        cs1 = tcs1;
    }

    if (!scs) {
        // MOS 8706 / unused — open bus.
        return data;
    }
    if (!user) {
        // Cassette sense high (no button) on bit 2.
        return uint8_t(0xfb);
    }
    if (!acia) {
        return 0xff;
    }
    if (!keyport) {
        return kb_;
    }
    if (!cs0) {
        switch (addr_latch_ & 0x03) {
        case 0:  // BASIC
            return kernal_basic_[offset & 0x7fff];
        case 1:  // Function LO
            if (have_function_) return function_[offset & 0x7fff];
            return 0xff;
        default:
            return 0xff;
        }
    }
    if (!cs1) {
        if (kernal) {
            return kernal_basic_[offset & 0x7fff];
        }
        switch ((addr_latch_ >> 2) & 0x03) {
        case 0:  // KERNAL
            return kernal_basic_[offset & 0x7fff];
        case 1:  // Function HI
            if (have_function_) return function_[offset & 0x7fff];
            return 0xff;
        default:
            return 0xff;
        }
    }
    if (offset < 0xfd00 || offset >= 0xff20) {
        return ram_[offset & ram_mask_];
    }
    return data;
}

uint8_t Plus4::ted_videoram_r(uint16_t offset, bool rom_force) {
    return read_memory(offset, rom_force ? RomView::ForceRom : RomView::ForceRam);
}

uint8_t Plus4::read_byte(uint16_t addr) {
    // MOS 7501 I/O port (like 6510).
    if (addr == 0x0000) return port_ddr_;
    if (addr == 0x0001) {
        // External: cassette sense / IEC idle high on bits 4/6/7.
        const uint8_t ext = 0xd0;
        return uint8_t((port_out_ & port_ddr_) | (ext & uint8_t(~port_ddr_)));
    }
    return read_memory(addr, RomView::Latch);
}

void Plus4::write_byte(uint16_t addr, uint8_t value) {
    if (addr == 0x0000) {
        port_ddr_ = value;
        return;
    }
    if (addr == 0x0001) {
        port_out_ = value;
        return;
    }

    int scs, user, acia, addr_clk, keyport, kernal;
    bankswitch(addr, 0, &scs, &user, &acia, &addr_clk, &keyport, &kernal);

    int cs0 = 1, cs1 = 1;
    ted_.write(addr, value, &cs0, &cs1);

    if (!scs) {
        return;
    }
    if (!user) {
        return;
    }
    if (!acia) {
        return;
    }
    if (!addr_clk) {
        addr_latch_ = uint8_t(addr & 0x0f);
        return;
    }
    if (!keyport) {
        kb_ = value;
        return;
    }
    if (addr < 0xfd00 || addr >= 0xff20) {
        ram_[addr & ram_mask_] = value;
    }
}

void Plus4::on_cycles(int cycles) {
    if (cycles <= 0) return;
    audio_acc_ += int64_t(cycles) * Mos7360::kSampleRate;
    const int64_t den = int64_t(ted_.cpu_clock());
    while (audio_acc_ >= den) {
        audio_acc_ -= den;
        audio_.push_back(ted_.update());
    }
}

bool Plus4::load_media(const std::string& path, std::string* error) {
    std::vector<uint8_t> data;
    if (!read_file(path, &data)) {
        if (error) *error = "cannot open: " + path;
        return false;
    }
    if (ends_ci(path, ".prg") || data.size() >= 3) {
        return queue_prg(data, error);
    }
    if (error) *error = "unsupported Plus/4 media: " + path;
    return false;
}

bool Plus4::queue_prg(const std::vector<uint8_t>& data, std::string* error) {
    if (data.size() < 3) {
        if (error) *error = "PRG too small";
        return false;
    }
    pending_prg_ = data;
    boot_frames_ = 0;
    return true;
}

void Plus4::update_pending_prg() {
    if (pending_prg_.empty()) return;
    if (++boot_frames_ < kPrgInjectFrames) return;
    // Plus/4 BASIC text often starts at $1001; TXTTAB at $2B/$2C.
    if (ram_[0x2b & ram_mask_] != 0x01 || ram_[0x2c & ram_mask_] != 0x10) return;

    const std::vector<uint8_t> data = std::move(pending_prg_);
    pending_prg_.clear();
    inject_prg(data);
}

void Plus4::inject_prg(const std::vector<uint8_t>& data) {
    const uint16_t addr = uint16_t(data[0] | (data[1] << 8));
    const size_t n = data.size() - 2;
    for (size_t i = 0; i < n; i++) {
        const uint16_t a = uint16_t(addr + i);
        ram_[a & ram_mask_] = data[i + 2];
    }
    const uint16_t end = uint16_t(addr + n);
    if (addr != 0x1001) return;

    auto poke = [this](uint16_t a, uint8_t v) { ram_[a & ram_mask_] = v; };
    poke(0x2d, uint8_t(end & 0xff));
    poke(0x2e, uint8_t(end >> 8));
    poke(0x2f, uint8_t(end & 0xff));
    poke(0x30, uint8_t(end >> 8));
    poke(0x31, uint8_t(end & 0xff));
    poke(0x32, uint8_t(end >> 8));
    poke(0xae, uint8_t(end & 0xff));
    poke(0xaf, uint8_t(end >> 8));

    // Keyboard buffer / count — Plus/4 uses $0527 / $00EF (same family as C16).
    static constexpr uint8_t kRun[] = {'R', 'U', 'N', 0x0d};
    for (size_t i = 0; i < sizeof(kRun); i++) poke(uint16_t(0x0527 + i), kRun[i]);
    poke(0x00ef, uint8_t(sizeof(kRun)));
}

void Plus4::run_frame() {
    update_pending_prg();
    const int lines = ted_.lines();
    const int cpl = ted_.cycles_per_line();
    for (int line = 0; line < lines; ++line) {
        ted_.update_line(line);
        cpu_cycle_debt_ += cpl;
        if (cpu_cycle_debt_ > 0) {
            const int ran = cpu_.run(cpu_cycle_debt_);
            cpu_cycle_debt_ -= ran;
        }
    }
    ted_.blit_visible(framebuffer_.data());
}

void Plus4::set_inputs(const MachineInputs& inputs) {
    keyboard_.fill(0xff);
    auto press = [this](int row, uint8_t bit_mask) {
        keyboard_[size_t(row)] = uint8_t(keyboard_[size_t(row)] & ~bit_mask);
    };
    auto key = [&inputs](Key id) { return inputs.key(id); };

    // MAME plus4 ROW0..ROW7 (active-low bits).
    // ROW0: DEL, RETURN, £, HELP, F1, F2, F3, @
    if (key(Key::Backspace) || key(Key::Delete)) press(0, 0x01);
    if (key(Key::Enter)) press(0, 0x02);
    if (key(Key::Backslash)) press(0, 0x04);
    if (key(Key::F7) || key(Key::F8)) press(0, 0x08);
    if (key(Key::F1) || key(Key::F4)) press(0, 0x10);
    if (key(Key::F2) || key(Key::F5)) press(0, 0x20);
    if (key(Key::F3) || key(Key::F6)) press(0, 0x40);
    if (key(Key::At)) press(0, 0x80);

    // ROW1: 3, W, A, 4, Z, S, E, SHIFT
    if (key(Key::Num3)) press(1, 0x01);
    if (key(Key::W)) press(1, 0x02);
    if (key(Key::A)) press(1, 0x04);
    if (key(Key::Num4)) press(1, 0x08);
    if (key(Key::Z)) press(1, 0x10);
    if (key(Key::S)) press(1, 0x20);
    if (key(Key::E)) press(1, 0x40);
    if (key(Key::LeftShift) || key(Key::RightShift)) press(1, 0x80);

    // ROW2: 5, R, D, 6, C, F, T, X
    if (key(Key::Num5)) press(2, 0x01);
    if (key(Key::R)) press(2, 0x02);
    if (key(Key::D)) press(2, 0x04);
    if (key(Key::Num6)) press(2, 0x08);
    if (key(Key::C)) press(2, 0x10);
    if (key(Key::F)) press(2, 0x20);
    if (key(Key::T)) press(2, 0x40);
    if (key(Key::X)) press(2, 0x80);

    // ROW3: 7, Y, G, 8, B, H, U, V
    if (key(Key::Num7)) press(3, 0x01);
    if (key(Key::Y)) press(3, 0x02);
    if (key(Key::G)) press(3, 0x04);
    if (key(Key::Num8)) press(3, 0x08);
    if (key(Key::B)) press(3, 0x10);
    if (key(Key::H)) press(3, 0x20);
    if (key(Key::U)) press(3, 0x40);
    if (key(Key::V)) press(3, 0x80);

    // ROW4: 9, I, J, 0, M, K, O, N
    if (key(Key::Num9)) press(4, 0x01);
    if (key(Key::I)) press(4, 0x02);
    if (key(Key::J)) press(4, 0x04);
    if (key(Key::Num0)) press(4, 0x08);
    if (key(Key::M)) press(4, 0x10);
    if (key(Key::K)) press(4, 0x20);
    if (key(Key::O)) press(4, 0x40);
    if (key(Key::N)) press(4, 0x80);

    // ROW5: CRSR DN, P, L, CRSR UP, ., :, -, ,
    if (key(Key::Down)) press(5, 0x01);
    if (key(Key::P)) press(5, 0x02);
    if (key(Key::L)) press(5, 0x04);
    if (key(Key::Up)) press(5, 0x08);
    if (key(Key::Period)) press(5, 0x10);
    if (key(Key::Semicolon) || key(Key::Quote)) press(5, 0x20);  // :
    if (key(Key::Minus)) press(5, 0x40);
    if (key(Key::Comma)) press(5, 0x80);

    // ROW6: CRSR LEFT, *, ;, CRSR RIGHT, ESC, =, +, /
    if (key(Key::Left)) press(6, 0x01);
    if (key(Key::Asterisk)) press(6, 0x02);
    if (key(Key::Quote) || key(Key::Semicolon)) press(6, 0x04);
    if (key(Key::Right)) press(6, 0x08);
    if (key(Key::Escape)) press(6, 0x10);
    if (key(Key::Equals)) press(6, 0x20);
    if (key(Key::Plus)) press(6, 0x40);
    if (key(Key::Slash)) press(6, 0x80);

    // ROW7: 1, CLR/HOME, CTRL, 2, SPACE, CBM, Q, RUN/STOP
    if (key(Key::Num1)) press(7, 0x01);
    if (key(Key::Home) || key(Key::Delete)) press(7, 0x02);
    if (key(Key::Tab) || key(Key::LeftCtrl)) press(7, 0x04);
    if (key(Key::Num2)) press(7, 0x08);
    if (key(Key::Space)) press(7, 0x10);
    if (key(Key::Cbm) || key(Key::LeftGui) || key(Key::RightAlt)) press(7, 0x20);
    if (key(Key::Q)) press(7, 0x40);
    if (key(Key::Escape) || key(Key::Home)) press(7, 0x80);  // RUN/STOP also on Home

    // Caps Lock ties into ROW1 shift bit via LOCK port — approximate with CapsLock.
    if (key(Key::CapsLock)) press(1, 0x80);

    joy1_ = joy2_ = 0xff;
    const auto& p = inputs.player1;
    if (p.up || key(Key::Up)) joy1_ = uint8_t(joy1_ & ~0x01);
    if (p.down || key(Key::Down)) joy1_ = uint8_t(joy1_ & ~0x02);
    if (p.left || key(Key::Left)) joy1_ = uint8_t(joy1_ & ~0x04);
    if (p.right || key(Key::Right)) joy1_ = uint8_t(joy1_ & ~0x08);
    if (p.button1) joy1_ = uint8_t(joy1_ & ~0x20);
}

void Plus4::drain_audio(std::vector<int16_t>& out) {
    out.insert(out.end(), audio_.begin(), audio_.end());
    audio_.clear();
}

}  // namespace dsp
