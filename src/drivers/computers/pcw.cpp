#include "drivers/computers/pcw.h"

#include <algorithm>
#include <cctype>
#include <cstring>

#include "core/rom_loader.h"
#include "cpu/irq_line.h"

namespace dsp {
namespace {
const std::vector<RomEntry> kPrinterMcuRom = {
    {"40026.ic701", 0x400, 0x0000, 0xee8890ae},
};

// Keyboard MCU is present in the set but unused (matrix is fed directly).
const std::vector<RomEntry> kKeyboardMcuRom = {
    {"40027.ic801", 0x400, 0x0000, 0x25260958},
};

struct KeyBit {
    Key key;
    uint8_t mask;  // bit to clear when pressed (active low)
};

// PCW matrix LINE0..LINE10 at &3FF0..&3FFA (MAME pcw.cpp INPUT_PORTS).
// Host digit keys (Key::Num0..9 ← SDL 0..9) light BOTH the main digit row
// (LINE4–8) and the keypad bits (LINE0–2): Filmation titles such as Knight
// Lore read the keypad 0 bit, while typing and Abadia use the main row.
const std::vector<KeyBit> kLine0 = {
    {Key::F3, 0x01}, {Key::Num0, 0x02}, {Key::F1, 0x04}, {Key::F9, 0x08},
    {Key::Num9, 0x10}, {Key::Num6, 0x20}, {Key::Num3, 0x40}, {Key::Right, 0x40},
    {Key::Num2, 0x80},
};
const std::vector<KeyBit> kLine1 = {
    {Key::F10, 0x01}, {Key::Backslash, 0x02}, {Key::F11, 0x04}, {Key::F12, 0x08},
    {Key::Num8, 0x10}, {Key::Num4, 0x20}, {Key::Num5, 0x40}, {Key::Up, 0x40},
    {Key::Num1, 0x80}, {Key::Left, 0x80},
};
const std::vector<KeyBit> kLine2 = {
    {Key::Delete, 0x01}, {Key::Enter, 0x04}, {Key::Num7, 0x10},
    {Key::LeftShift, 0x20}, {Key::RightShift, 0x20}, {Key::F2, 0x80},
};
const std::vector<KeyBit> kLine3 = {
    {Key::Equals, 0x01}, {Key::Minus, 0x02}, {Key::At, 0x04}, {Key::P, 0x08},
    {Key::Quote, 0x10}, {Key::Semicolon, 0x20}, {Key::Slash, 0x40},
    {Key::Period, 0x80},
};
const std::vector<KeyBit> kLine4 = {
    {Key::Num0, 0x01}, {Key::Num9, 0x02}, {Key::O, 0x04}, {Key::I, 0x08},
    {Key::L, 0x10},    {Key::K, 0x20},    {Key::M, 0x40}, {Key::Comma, 0x80},
};
const std::vector<KeyBit> kLine5 = {
    {Key::Num8, 0x01}, {Key::Num7, 0x02}, {Key::U, 0x04}, {Key::Y, 0x08},
    {Key::H, 0x10},    {Key::J, 0x20},    {Key::N, 0x40}, {Key::Space, 0x80},
};
const std::vector<KeyBit> kLine6 = {
    {Key::Num6, 0x01}, {Key::Num5, 0x02}, {Key::R, 0x04}, {Key::T, 0x08},
    {Key::G, 0x10},    {Key::F, 0x20},    {Key::B, 0x40}, {Key::V, 0x80},
};
const std::vector<KeyBit> kLine7 = {
    {Key::Num4, 0x01}, {Key::Num3, 0x02}, {Key::E, 0x04}, {Key::W, 0x08},
    {Key::S, 0x10},    {Key::D, 0x20},    {Key::C, 0x40}, {Key::X, 0x80},
};
const std::vector<KeyBit> kLine8 = {
    {Key::Num1, 0x01}, {Key::Num2, 0x02}, {Key::Escape, 0x04}, {Key::Q, 0x08},
    {Key::Tab, 0x10},  {Key::A, 0x20},    {Key::CapsLock, 0x40}, {Key::Z, 0x80},
};
const std::vector<KeyBit> kLine9 = {
    {Key::F5, 0x01}, {Key::LeftCtrl, 0x02}, {Key::RightCtrl, 0x02},
    {Key::F4, 0x08}, {Key::F7, 0x10}, {Key::Down, 0x40}, {Key::Backspace, 0x80},
};
const std::vector<KeyBit> kLine10 = {
    {Key::Cbm, 0x80}, {Key::RightAlt, 0x80},
};

const std::vector<KeyBit>* kLines[11] = {
    &kLine0, &kLine1, &kLine2, &kLine3, &kLine4, &kLine5,
    &kLine6, &kLine7, &kLine8, &kLine9, &kLine10,
};

bool ends_with_ci(const std::string& text, const std::string& suffix) {
    if (text.size() < suffix.size()) return false;
    return std::equal(suffix.rbegin(), suffix.rend(), text.rbegin(),
                      [](char a, char b) {
                          return std::tolower(uint8_t(a)) == std::tolower(uint8_t(b));
                      });
}

}  // namespace

Pcw::Pcw(Model model)
    : model_(model),
      ram_banks_(model == Model::PCW8512 ? 32 : 16),
      cpu_(kCpuClock),
      ay_(kCpuClock / 2, 1.0f) {
    framebuffer_.assign(size_t(kScreenWidth) * kScreenHeight, kPenBlack);
    keyboard_.fill(0xff);

    cpu_.set_memory_handlers([this](uint16_t a) { return read_byte(a); },
                             [this](uint16_t a, uint8_t v) { write_byte(a, v); });
    cpu_.set_io_handlers([this](uint16_t p) { return read_port(p); },
                         [this](uint16_t p, uint8_t v) { write_port(p, v); });
    cpu_.set_irq_ack_callback([this] {
        timer_irq_flag_ = false;
        update_irqs();
    });
    ay_.set_port_handlers([this]() { return joystick_porta_; }, nullptr, nullptr, nullptr);
}

const char* Pcw::title() const {
    return model_ == Model::PCW8512 ? "Amstrad PCW8512" : "Amstrad PCW8256";
}

bool Pcw::init(const std::string& rom_path, std::string* error) {
    RomLoader loader;
    if (!loader.open(rom_path, error)) return false;

    if (!loader.load(kPrinterMcuRom, printer_mcu_rom_, error)) return false;
    printer_mcu_rom_.resize(0x400, 0xff);

    // Optional keyboard MCU — not emulated, CRC warning only.
    std::vector<uint8_t> kb;
    std::string kb_err;
    if (!loader.load(kKeyboardMcuRom, kb, &kb_err)) {
        warnings_.push_back("40027.ic801 not loaded (" + kb_err + ")");
    }

    for (const std::string& w : loader.warnings()) warnings_.push_back(w);

    ram_.assign(ram_size(), 0);
    reset();
    return true;
}

void Pcw::reset() {
    cpu_.reset();
    fdc_.reset();
    std::fill(ram_.begin(), ram_.end(), 0);

    bank_force_ = 0xf0;
    banks_ = {0x80, 0x81, 0x82, 0x83};
    for (int i = 0; i < 4; ++i) update_mem(i, banks_[size_t(i)]);

    interrupt_counter_ = 0;
    system_status_bits_ = 0;
    fdc_interrupt_code_ = 2;
    timer_irq_flag_ = false;
    nmi_flag_ = false;
    timer_pulse_lines_ = 0;
    roller_ram_addr_ = 0;
    roller_ram_offset_ = 0;
    vdu_video_control_ = 0;
    beeper_on_ = false;
    disk_motor_ = false;
    in_vblank_ = false;
    timer_line_counter_ = 0;
    keyboard_.fill(0xff);
    audio_.clear();
    audio_accumulator_ = 0;
    beeper_phase_ = 0;
    ay_.reset();
    ay_latch_ = 0;
    blit_setup_patched_ = false;
    abadia_keyboard_patched_ = false;
    abadia_ingame_ = false;

    // MAME machine_reset: copy printer-MCU bootstrap into RAM[2..257].
    // Z80 starts at 0 → two NOPs (zeros) then the stub.
    if (printer_mcu_rom_.size() >= 0x400) {
        for (int x = 0; x < 256; ++x) {
            ram_[size_t(x + 2)] = printer_mcu_rom_[size_t(0x300 + x)];
        }
    }

    cpu_.set_irq(IrqLine::Clear);
    cpu_.set_nmi(IrqLine::Clear);
    std::fill(framebuffer_.begin(), framebuffer_.end(), kPenBlack);
}

uint8_t* Pcw::bank_ptr(int bank) {
    const int n = bank % ram_banks_;
    return ram_.data() + size_t(n) * 0x4000;
}

void Pcw::update_mem(int block, uint8_t data) {
    if (data & 0x80) {
        const int bank = data & 0x7f;
        read_bank_[size_t(block)] = bank;
        write_bank_[size_t(block)] = bank;
    } else {
        int mask = 0;
        switch (block) {
            case 0: mask = 1 << 6; break;
            case 1: mask = 1 << 4; break;
            case 2: mask = 1 << 5; break;
            case 3: mask = 1 << 7; break;
        }
        const int read_bank =
            (bank_force_ & mask) ? (data & 0x07) : ((data >> 4) & 0x07);
        read_bank_[size_t(block)] = read_bank;
        write_bank_[size_t(block)] = data & 0x07;
    }
    // Bank switches can reveal remaining CALL $32BC sites mid-frame.
    maybe_patch_blit_setup();
    maybe_patch_abadia_keyboard();
}

uint8_t Pcw::read_byte(uint16_t address) {
    const int block = address >> 14;
    const int bank = read_bank_[size_t(block)];
    const uint16_t off = address & 0x3fff;

    // Bank 3 upper 16 bytes: keyboard matrix (MAME installs a handler here).
    if (bank == 3 && off >= 0x3ff0) {
        const int row = off - 0x3ff0;
        if (row < 11) return keyboard_[size_t(row)];
        return 0xff;
    }

    // Habisoft Abadia keeps a CPC-style key buffer at $33D3/$33DD. After the
    // blit prologue patch sets CP $09, that buffer is filled from IY mask tables
    // so $3482/$348D never see host keys. While those helpers run, return the
    // live PCW matrix (both buffers; $3472 uses $33DD).
    if (bank == 0 && address >= 0x33d3 && address <= 0x33e6) {
        const uint16_t pc = cpu_.pc();
        if (pc >= 0x3430 && pc <= 0x34c0) {
            int idx = int(address - 0x33d3);
            if (idx >= 10) idx -= 10;
            return keyboard_[size_t(idx)];
        }
    }

    return bank_ptr(bank)[off];
}

void Pcw::write_byte(uint16_t address, uint8_t value) {
    const int block = address >> 14;
    const int bank = write_bank_[size_t(block)];
    bank_ptr(bank)[address & 0x3fff] = value;
}

uint8_t Pcw::system_status() const {
    uint8_t status = uint8_t(interrupt_counter_ & 0x0f);
    if (in_vblank_) status |= 0x40;
    if (frame_50hz_) status |= 0x10;
    if (system_status_bits_ & 0x20) status |= 0x20;
    return status;
}

void Pcw::update_irqs() {
    // Refresh FDC interrupt latch from the UPD765 INT line.
    if (fdc_.irq_pending()) {
        system_status_bits_ |= 0x20;
        if (fdc_interrupt_code_ == 0) nmi_flag_ = true;
    } else {
        system_status_bits_ &= uint8_t(~0x20);
    }

    if (nmi_flag_) {
        cpu_.set_nmi(IrqLine::Assert);
    } else {
        cpu_.set_nmi(IrqLine::Clear);
    }

    const bool fdc_irq = (fdc_interrupt_code_ == 1) && (system_status_bits_ & 0x20);
    if (fdc_irq || timer_irq_flag_) {
        cpu_.set_irq(IrqLine::Hold, 0xff);
    } else {
        cpu_.set_irq(IrqLine::Clear);
    }
}

void Pcw::timer_tick() {
    if (interrupt_counter_ < 0x0f) ++interrupt_counter_;
    timer_irq_flag_ = true;
    // Hold until F4 (or Z80 INT ack). A short pulse is lost while the game DI's.
    timer_pulse_lines_ = 0;
    update_irqs();
}

void Pcw::system_control(uint8_t data) {
    switch (data) {
        case 0:
            break;
        case 1:
            nmi_flag_ = true;
            update_irqs();
            nmi_flag_ = false;
            cpu_.set_nmi(IrqLine::Pulse);
            break;
        case 2: {
            const int prev = fdc_interrupt_code_;
            fdc_interrupt_code_ = 0;
            if (prev == 1) update_irqs();
            break;
        }
        case 3: {
            const int prev = fdc_interrupt_code_;
            fdc_interrupt_code_ = 1;
            if (prev == 0) nmi_flag_ = false;
            update_irqs();
            break;
        }
        case 4: {
            const int prev = fdc_interrupt_code_;
            fdc_interrupt_code_ = 2;
            if (prev == 0 || prev == 1) nmi_flag_ = false;
            update_irqs();
            break;
        }
        case 5:
            // Set FDC terminal count (MAME pcw_system_control_w).
            fdc_.tc_w(true);
            update_irqs();
            break;
        case 6:
            // Clear FDC terminal count.
            fdc_.tc_w(false);
            break;
        case 7:
        case 8:
            break;
        case 9:
            disk_motor_ = true;
            fdc_.write_motor(1);
            break;
        case 10:
            disk_motor_ = false;
            fdc_.write_motor(0);
            break;
        case 11:
            beeper_on_ = true;
            break;
        case 12:
            beeper_on_ = false;
            break;
        default:
            break;
    }
}

uint8_t Pcw::read_port(uint16_t port) {
    const uint8_t p = uint8_t(port & 0xff);

    // FDC at 0x00/0x01, mirrored through 0x7e (bits 7..1 don't care except bit0).
    if (p <= 0x7f) {
        if ((p & 1) == 0) return fdc_.read_status();
        return fdc_.read_data();
    }

    // DK'Tronics AY-3-8912: A9 = read currently selected register.
    if (p == 0xa9) return ay_.read();

    switch (p) {
        case 0xf4:
            // Interrupt counter + status; reading F4 acknowledges the timer IRQ
            // (systemed.net / JOYCE: "read to re-enable interrupts").
            {
                const uint8_t data = system_status();
                interrupt_counter_ = 0;
                timer_irq_flag_ = false;
                update_irqs();
                return data;
            }
        case 0xf8:
            return system_status();
        case 0xfc:
            // Printer MCU data — idle / no error (MAME: 0xF8).
            return 0xf8;
        case 0xfd:
            // Printer MCU status: bail in, not busy, paper present, ready.
            return 0xc4;
        default:
            if (p >= 0x80 && p <= 0xef) return 0xff;
            return 0xff;
    }
}

void Pcw::write_port(uint16_t port, uint8_t value) {
    const uint8_t p = uint8_t(port & 0xff);

    if (p <= 0x7f) {
        if ((p & 1) == 0) {
            // Status port is read-only on UPD765; ignore writes.
        } else {
            fdc_.write_data(value);
            update_irqs();
        }
        return;
    }

    switch (p) {
        case 0xf0:
        case 0xf1:
        case 0xf2:
        case 0xf3:
            banks_[size_t(p - 0xf0)] = value;
            update_mem(p - 0xf0, value);
            break;
        case 0xf4:
            bank_force_ = value;
            for (int i = 0; i < 4; ++i) update_mem(i, banks_[size_t(i)]);
            break;
        case 0xf5:
            // b7-5: bank (0-7). b4-1: address / 512.
            roller_ram_addr_ = (((unsigned(value) >> 5) & 7u) << 14) |
                               ((unsigned(value) & 0x1fu) << 9);
            break;
        case 0xf6:
            roller_ram_offset_ = value;
            break;
        case 0xf7:
            vdu_video_control_ = value;
            break;
        case 0xf8:
            system_control(value);
            break;
        case 0xaa:
            // DK'Tronics AY register select.
            ay_latch_ = uint8_t(value & 0x0f);
            ay_.control(value);
            break;
        case 0xab:
            // DK'Tronics AY register write.
            ay_.write(value);
            break;
        case 0xfc:
        case 0xfd:
            break;
        default:
            break;
    }
}

void Pcw::render_screen() {
    uint32_t pen0 = kPenBlack;
    uint32_t pen1 = kPenGreen;
    if (vdu_video_control_ & 0x80) {
        std::swap(pen0, pen1);
    }

    if ((vdu_video_control_ & 0x40) == 0) {
        // Video disabled — fill with pen1 (MAME fills pen1 when off).
        std::fill(framebuffer_.begin(), framebuffer_.end(), pen1);
        return;
    }

    // Borders.
    for (int y = 0; y < kBorderHeight; ++y) {
        uint32_t* row = framebuffer_.data() + size_t(y) * kScreenWidth;
        std::fill(row, row + kScreenWidth, pen0);
    }
    for (int y = kBorderHeight + kDisplayHeight; y < kScreenHeight; ++y) {
        uint32_t* row = framebuffer_.data() + size_t(y) * kScreenWidth;
        std::fill(row, row + kScreenWidth, pen0);
    }

    unsigned roller_offs = (unsigned(roller_ram_offset_) << 1) & 511u;

    for (int y = 0; y < kDisplayHeight; ++y) {
        uint32_t* dst =
            framebuffer_.data() + size_t(y + kBorderHeight) * kScreenWidth;

        // Side borders.
        for (int x = 0; x < kBorderWidth; ++x) dst[x] = pen0;
        for (int x = 0; x < kBorderWidth; ++x) {
            dst[kBorderWidth + kDisplayWidth + x] = pen0;
        }

        const unsigned addr = (roller_ram_addr_ + roller_offs) % ram_size();
        const uint16_t line_data =
            uint16_t(ram_[addr]) | (uint16_t(ram_[(addr + 1) % ram_size()]) << 8);

        // b16-14 bank, b13-3 address/16, b2-0 offset. Addition matches MAME pcw_v.cpp.
        unsigned line_ptr = ((unsigned(line_data) & 0xe000u) << 1) +
                            ((unsigned(line_data) & 0x1ff8u) << 1) +
                            (unsigned(line_data) & 0x07u);
        line_ptr %= ram_size();

        int x = kBorderWidth;
        for (int by = 0; by < 90; ++by) {
            const uint8_t byte = ram_[line_ptr % ram_size()];
            uint8_t bits = byte;
            for (int b = 0; b < 8; ++b) {
                dst[x + b] = (bits & 0x80) ? pen1 : pen0;
                bits = uint8_t(bits << 1);
            }
            x += 8;
            line_ptr = (line_ptr + 8) % ram_size();
        }

        roller_offs = (roller_offs + 2) & 511u;
    }
}

void Pcw::run_frame() {
    const int cycles_per_line = kCyclesPerFrame / kLinesPerFrame;
    int cycles_left = kCyclesPerFrame;
    // 300 Hz timer → 6 ticks per 50 Hz frame.
    const int lines_per_timer = std::max(1, kLinesPerFrame / 6);

    in_vblank_ = false;
    maybe_patch_blit_setup();
    maybe_patch_abadia_keyboard();
    // Once Abadia leaves the parchment Space-wait ($2517), switch the $32B3
    // prologue to CP $00 so IY masks no longer replace the key matrix or
    // corrupt Habisoft's first in-game redraw. Live feed covers the wait itself.
    if (blit_setup_patched_ && !abadia_ingame_) {
        const uint16_t pc = cpu_.pc();
        if (pc == 0x2517 || (pc >= 0x2518 && pc < 0x2560) || pc == 0x381e) {
            if (bank_ptr(0)[0x32b8] == 0x09) bank_ptr(0)[0x32b8] = 0x00;
            if (bank_ptr(0)[0x3309] == 0x09) bank_ptr(0)[0x3309] = 0x00;
            abadia_ingame_ = true;
        }
    }

    for (int line = 0; line < kLinesPerFrame; ++line) {
        // VBlank roughly covers the bottom border region.
        in_vblank_ = (line >= kBorderHeight + kDisplayHeight);

        if (timer_pulse_lines_ > 0) {
            --timer_pulse_lines_;
            if (timer_pulse_lines_ == 0) {
                timer_irq_flag_ = false;
                update_irqs();
            }
        }

        ++timer_line_counter_;
        if (timer_line_counter_ >= lines_per_timer) {
            timer_line_counter_ = 0;
            timer_tick();
        }

        update_irqs();

        int slice = cycles_per_line;
        // Video steals ~47 T-states per active display line (MAME).
        if (line >= kBorderHeight && line < kBorderHeight + kDisplayHeight) {
            slice = std::max(1, slice - 47);
        }
        if (slice > cycles_left) slice = cycles_left;
        if (slice > 0) {
            const int ran = cpu_.run(slice);
            cycles_left -= (ran > 0 ? ran : slice);
        }

        // Beeper samples.
        if (beeper_on_) {
            const int samples =
                int((int64_t(slice) * kSampleRate) / int64_t(kCpuClock));
            for (int s = 0; s < samples; ++s) {
                beeper_phase_ = (beeper_phase_ + 1) % (kSampleRate / 800);
                audio_.push_back(beeper_phase_ < (kSampleRate / 1600) ? int16_t(2000)
                                                                      : int16_t(-2000));
            }
        }
    }

    if (cycles_left > 0) cpu_.run(cycles_left);

    in_vblank_ = true;
    render_screen();
}

void Pcw::maybe_patch_blit_setup() {
    // Habisoft Abadia blit: several sites CALL $32BC (DI; LD HL,$33D3; …) but the
    // required prologue at $32B3 (LD IY,$33E7 / LD ($3309),$09) is only reached by
    // falling through from a sprite path that never runs after the game bank loads.
    // Without it, $3305 keeps CP $00 so every pixel mask comes from the idle
    // keyboard (CPL $FF → $00) and the screen is inverted to solid green.
    //
    // Wait until the post-load map has matching read/write banks (expanded $8x
    // selects). A transient $15 mapping can make read_bank1==1 while write_bank1
    // is 5 — scanning then "patches" the wrong physical RAM and latches done.
    if (blit_setup_patched_) return;
    if (read_bank_[0] != 0 || read_bank_[1] != 1) return;
    if (read_bank_[0] != write_bank_[0] || read_bank_[1] != write_bank_[1] ||
        read_bank_[2] != write_bank_[2] || read_bank_[3] != write_bank_[3]) {
        return;
    }
    if (read_byte(0x32b3) != 0xfd || read_byte(0x32b4) != 0x21 ||
        read_byte(0x32b5) != 0xe7 || read_byte(0x32b6) != 0x33 ||
        read_byte(0x32b7) != 0x3e || read_byte(0x32b8) != 0x09 ||
        read_byte(0x32bc) != 0xf3) {
        return;
    }

    int patched = 0;
    for (int a = 0; a <= 0xfffd; ++a) {
        // Internal blit tail at $3311 must stay CALL $32BC: retargeting it to
        // $32B3 re-enters DI without the matching EI at $3319 and freezes after
        // the parchment on a cleared (0x55) playfield.
        if (a == 0x3311) continue;
        if (read_byte(uint16_t(a)) == 0xcd && read_byte(uint16_t(a + 1)) == 0xbc &&
            read_byte(uint16_t(a + 2)) == 0x32) {
            // Write through the read bank so CPC-style split maps cannot redirect
            // the patch into a different physical page.
            const int block = a >> 14;
            bank_ptr(read_bank_[size_t(block)])[(a & 0x3fff) + 1] = 0xb3;
            ++patched;
        }
    }
    if (patched > 0) {
        // Space-wait at $2509 DI's without EI; NOP it so IRQs survive. Keep DI
        // at $32BC so the blit itself stays IRQ-safe (Abadia uses SP as data).
        if (read_byte(0x2509) == 0xf3) bank_ptr(0)[0x2509] = 0x00;
        blit_setup_patched_ = true;
    }
}

void Pcw::maybe_patch_abadia_keyboard() {
    // Habisoft Abadia maps logical space ($2F) to CPC encoding $1E (row3 bit6).
    // On the PCW matrix Space is row5 bit7 ($2F). Retarget the table used by
    // CALL $3482 so parchment "PULSA ESPACIO" and in-game Space match the host
    // key, together with the live-matrix feed in read_byte().
    if (abadia_keyboard_patched_) return;
    if (read_bank_[0] != 0 || write_bank_[0] != 0) return;
    if (read_byte(0x3427) != 0x1e || read_byte(0x3428) != 0x2f) return;
    bool found_wait = false;
    for (int a = 0; a <= 0xfffc; ++a) {
        if (read_byte(uint16_t(a)) == 0x3e && read_byte(uint16_t(a + 1)) == 0x2f &&
            read_byte(uint16_t(a + 2)) == 0xcd && read_byte(uint16_t(a + 3)) == 0x82 &&
            read_byte(uint16_t(a + 4)) == 0x34) {
            found_wait = true;
            break;
        }
    }
    if (!found_wait) return;
    bank_ptr(0)[0x3427] = 0x2f;
    abadia_keyboard_patched_ = true;
}

void Pcw::set_inputs(const MachineInputs& inputs) {
    keyboard_.fill(0xff);
    for (int row = 0; row < 11; ++row) {
        uint8_t value = 0xff;
        for (const KeyBit& bit : *kLines[row]) {
            if (inputs.key(bit.key)) value = uint8_t(value & ~bit.mask);
        }
        keyboard_[size_t(row)] = value;
    }

    // DK'Tronics stick on AY port A. Habisoft Filmation (Knight Lore) CPL's the
    // read and maps: bit2→left, bit3→right, bit4→fire, bit5/6/7→remaining dirs.
    // Also accept Q/A/O/P so the Spectrum layout works without a real stick.
    uint8_t joy = 0xff;
    const bool up = inputs.key(Key::Up) || inputs.key(Key::Q) || inputs.player1.up;
    const bool down = inputs.key(Key::Down) || inputs.key(Key::A) || inputs.player1.down;
    const bool left = inputs.key(Key::Left) || inputs.key(Key::O) || inputs.player1.left;
    const bool right = inputs.key(Key::Right) || inputs.key(Key::P) || inputs.player1.right;
    const bool fire = inputs.key(Key::Space) || inputs.player1.button1 || inputs.player1.button2;
    // CPC/DK'Tronics low bits (active-low) plus the Filmation-checked high bits.
    if (up) joy = uint8_t(joy & ~0x21);       // bit0 + bit5
    if (down) joy = uint8_t(joy & ~0x42);     // bit1 + bit6
    if (left) joy = uint8_t(joy & ~0x04);     // bit2
    if (right) joy = uint8_t(joy & ~0x08);    // bit3
    if (fire) joy = uint8_t(joy & ~0x90);     // bit4 + bit7
    joystick_porta_ = joy;

    // Habisoft Abadia accepts keypad cursors; also mirror Q-A-O-P onto those
    // bits so the classic Opera Soft layout moves Guillermo without needing
    // the CPC-encoded letter tests that the IY key-buffer path breaks.
    if (abadia_keyboard_patched_) {
        if (inputs.key(Key::Q) || inputs.key(Key::Up))
            keyboard_[1] = uint8_t(keyboard_[1] & ~0x40);  // Up
        if (inputs.key(Key::A) || inputs.key(Key::Down))
            keyboard_[9] = uint8_t(keyboard_[9] & ~0x40);  // Down
        if (inputs.key(Key::O) || inputs.key(Key::Left))
            keyboard_[1] = uint8_t(keyboard_[1] & ~0x80);  // Left
        if (inputs.key(Key::P) || inputs.key(Key::Right))
            keyboard_[0] = uint8_t(keyboard_[0] & ~0x40);  // Right
    }
}

void Pcw::set_dip_switch(int bank, uint8_t value) {
    if (bank == 0) frame_50hz_ = (value & 0x10) != 0;
}

bool Pcw::load_media(const std::string& path, std::string* error) {
    if (!ends_with_ci(path, ".dsk") && !ends_with_ci(path, ".edsk")) {
        if (error) *error = "PCW expects a .dsk/.edsk image: " + path;
        return false;
    }
    // First image → drive A; further --disk args fill drive B (Cozumel side B,
    // CP/M system + game, etc.).
    const int drive = fdc_.disk_inserted(0) ? 1 : 0;
    if (drive == 1 && fdc_.disk_inserted(1)) {
        if (error) *error = "PCW already has disks in A: and B:";
        return false;
    }
    return fdc_.load_disk(drive, path, error);
}

void Pcw::drain_audio(std::vector<int16_t>& out) {
    out.swap(audio_);
    audio_.clear();
}

}  // namespace dsp
