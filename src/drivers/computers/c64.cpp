#include "drivers/computers/c64.h"

#include <algorithm>
#include <cstring>
#include <fstream>

#include <zlib.h>

namespace dsp {
namespace {

// On-screen keyboard picture and key table (tools/gen_c64_vkb.py).
struct C64VkbKey {
    int8_t col, row;
    uint8_t flags;  // 1 modifier, 2 RESTORE, 4 SHIFT LOCK
    int16_t x1, y1, x2, y2;
    int16_t fx1, fy1, fx2, fy2;
    uint8_t glyph_cbm, glyph_shift;
};
#include "drivers/computers/c64_vkb_data.inc"
constexpr int kVkbKeyCount = int(sizeof(kC64VkbKeys) / sizeof(kC64VkbKeys[0]));
constexpr int kVkbModifier = 1, kVkbRestore = 2, kVkbLock = 4;

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

bool load_named(const std::string& dir, const char* name, uint8_t* dst,
                size_t size, std::string* error) {
    std::string path = dir;
    if (!path.empty() && path.back() != '/' && path.back() != '\\') path += '/';
    path += name;
    std::vector<uint8_t> data;
    if (!read_file(path, &data) || data.size() < size) {
        if (error) *error = "missing ROM: " + path;
        return false;
    }
    std::memcpy(dst, data.data(), size);
    return true;
}

std::string join_path(const std::string& dir, const std::string& name) {
    std::string path = dir;
    if (!path.empty() && path.back() != '/' && path.back() != '\\') path += '/';
    return path + name;
}

}  // namespace

C64::C64()
    : cpu_(kCpuClock),
      vic_(kCpuClock),
      sid_(kCpuClock),
      cia1_(kCpuClock),
      cia2_(kCpuClock),
      framebuffer_(size_t(kScreenWidth * kScreenHeight), 0) {
    cpu_.set_memory_handlers(
        [this](uint16_t a) { return read_byte(a); },
        [this](uint16_t a, uint8_t v) { write_byte(a, v); });
    cpu_.set_cycle_handler([this](int c) { on_cycles(c); });

    vic_.set_irq_handler([this](IrqLine s) {
        vic_irq_ = (s != IrqLine::Clear);
        update_irq();
    });
    vic_.set_color_ram(color_ram_.data());
    // Mos6566 already merges the CIA2 bank bits into the address it asks for,
    // so it must be used as-is: masking it back to 14 bits pinned every fetch
    // to bank 0. The character ROM only shadows RAM in banks 0 and 2, at
    // $1000-$1FFF and $9000-$9FFF.
    vic_.set_mem_read([this](uint16_t a) -> uint8_t {
        if ((a & 0x7000) == 0x1000) return char_rom_[a & 0x0FFF];
        return ram_[a];
    });

    cia1_.set_irq_handler([this](IrqLine s) {
        cia_irq_ = (s != IrqLine::Clear);
        update_irq();
    });
    cia1_.set_port_b([this]() { return cia1_portb_r(); }, {});

    cia2_.set_irq_handler([this](IrqLine s) {
        cia_nmi_ = (s != IrqLine::Clear);
        update_nmi();
    });
    cia2_.set_port_a(
        [this]() {
            uint8_t v = 0x3F;
            if (drive_.rom_loaded() && iec_enabled_) {
                if (drive_.bus_clk()) v = uint8_t(v | 0x40);
                if (drive_.bus_data()) v = uint8_t(v | 0x80);
            } else {
                v = uint8_t(v | 0xC0);
            }
            return v;
        },
        [this](uint8_t v) {
            vic_.changed_va(uint16_t(~v & 3));
            if (drive_.rom_loaded() && iec_enabled_) {
                // PA3/PA4/PA5 drive ATN/CLK/DATA through inverting buffers:
                // a set bit pulls the open-collector line low. KERNAL relies on
                // it, e.g. LISTEN asserts ATN with ORA #$08 at $ED31.
                drive_.set_host_atn((v & 0x08) == 0);
                drive_.set_host_clk((v & 0x10) == 0);
                drive_.set_host_data((v & 0x20) == 0);
            }
        });
}

bool C64::init(const std::string& rom_path, std::string* error) {
    return load_roms(rom_path, error);
}

bool C64::load_roms(const std::string& dir, std::string* error) {
    if (!load_named(dir, "901227-03.u4", kernel_rom_.data(), 0x2000, error) &&
        !load_named(dir, "kernal.rom", kernel_rom_.data(), 0x2000, error) &&
        !load_named(dir, "kernal.bin", kernel_rom_.data(), 0x2000, error)) return false;
    if (!load_named(dir, "901226-01.u3", basic_rom_.data(), 0x2000, error) &&
        !load_named(dir, "basic.rom", basic_rom_.data(), 0x2000, error) &&
        !load_named(dir, "basic.bin", basic_rom_.data(), 0x2000, error)) return false;
    if (!load_named(dir, "901225-01.u5", char_rom_.data(), 0x1000, error) &&
        !load_named(dir, "chargen.rom", char_rom_.data(), 0x1000, error) &&
        !load_named(dir, "chargen.bin", char_rom_.data(), 0x1000, error)) return false;

    // The 1541/1540 DOS ROM is optional: without it the drive cannot answer on
    // the serial bus and disk images fall back to direct injection.
    // 1541 first, its DOS also drives a 1540 at the slower VIC-II timing.
    static const char* kDriveRoms[] = {
        "dos1541",   "dos1541.bin",   "1541.rom",   "1541",
        "d1541.rom", "325302-01.uab4",
        "dos1540",   "dos1540.bin",   "1540.rom",   "1540",
    };
    for (const char* name : kDriveRoms) {
        std::vector<uint8_t> rom;
        if (!read_file(join_path(dir, name), &rom)) continue;
        if (drive_.load_rom(rom.data(), rom.size())) break;
    }

    reset();
    return true;
}

bool C64::load_1541_rom(const std::string& path, std::string* error) {
    if (!drive_.load_rom(path, error)) return false;
    drive_.reset();
    return true;
}

bool C64::load_media(const std::string& path, std::string* error) {
    auto ends_ci = [](const std::string& s, const char* ext) {
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
    };
    if (ends_ci(path, ".g64") || ends_ci(path, ".g41")) {
        if (!drive_.rom_loaded()) {
            if (error) *error = "G64 requires 1541 DOS ROM (load_1541_rom)";
            return false;
        }
        return drive_.load_g64(path, error);
    }
    if (ends_ci(path, ".d64")) {
        if (drive_.rom_loaded()) return drive_.load_d64(path, error);
        if (!disk_.load_file(path, error)) return false;
        std::vector<uint8_t> prg;
        if (!disk_.load_first_prg(&prg, error)) return false;
        return queue_prg(prg, error);
    }
    if (ends_ci(path, ".t64")) {
        T64Image t64;
        if (!t64.load_file(path, error)) return false;

        // A .T64 has no disk structure, so serve its files from a disk image
        // built on the fly: the KERNAL then loads them over the serial bus
        // like any other disk, LOAD"$",8 included.
        if (drive_.rom_loaded()) {
            std::vector<D64BuildFile> files;
            for (size_t i = 0; i < t64.directory().size(); i++) {
                D64BuildFile f;
                f.name = t64.directory()[i].name;
                if (t64.load_prg(i, &f.prg)) files.push_back(std::move(f));
            }
            const std::vector<uint8_t> img = build_d64(files, t64.tape_name());
            if (!img.empty()) return drive_.load_d64(img.data(), img.size(), error);
        }

        std::vector<uint8_t> prg;
        if (!t64.load_first_prg(&prg, error)) return false;
        return queue_prg(prg, error);
    }
    if (ends_ci(path, ".tap")) {
        if (!tape_.load_file(path, error)) return false;
        // Press PLAY (sense low) so LOAD leaves "PRESS PLAY ON TAPE".
        // The tape only advances while the motor bit is on, so the interlock
        // cannot consume the image during boot.
        tape_play_ = true;
        tape_.play(true);
        return true;
    }
    std::vector<uint8_t> data;
    if (!read_file(path, &data)) {
        if (error) *error = "cannot open: " + path;
        return false;
    }
    return queue_prg(data, error);
}

bool C64::queue_prg(const std::vector<uint8_t>& data, std::string* error) {
    if (data.size() < 3) {
        if (error) *error = "PRG too small";
        return false;
    }
    pending_prg_ = data;
    boot_frames_ = 0;
    return true;
}

void C64::update_pending_prg() {
    if (pending_prg_.empty()) return;
    if (++boot_frames_ < kPrgInjectFrames) return;
    // TXTTAB must already point at $0801: BASIC has finished its cold start,
    // so the program area and the zero page pointers are ours to fill in.
    if (ram_[0x2B] != 0x01 || ram_[0x2C] != 0x08) return;

    const std::vector<uint8_t> data = std::move(pending_prg_);
    pending_prg_.clear();
    inject_prg(data);
}

void C64::inject_prg(const std::vector<uint8_t>& data) {
    const uint16_t addr = uint16_t(data[0] | (data[1] << 8));
    const size_t n = data.size() - 2;
    for (size_t i = 0; i < n; i++) ram_[uint16_t(addr + i)] = data[i + 2];
    const uint16_t end = uint16_t(addr + n);
    if (addr != 0x0801) return;

    ram_[0x2D] = uint8_t(end & 0xFF); ram_[0x2E] = uint8_t(end >> 8);
    ram_[0x2F] = uint8_t(end & 0xFF); ram_[0x30] = uint8_t(end >> 8);
    ram_[0x31] = uint8_t(end & 0xFF); ram_[0x32] = uint8_t(end >> 8);
    ram_[0xAE] = uint8_t(end & 0xFF); ram_[0xAF] = uint8_t(end >> 8);

    // Autostart through the KERNAL keyboard buffer, exactly as if RUN had been
    // typed at the prompt.
    static constexpr uint8_t kRun[] = {'R', 'U', 'N', 0x0D};
    for (size_t i = 0; i < sizeof(kRun); i++) ram_[0x0277 + i] = kRun[i];
    ram_[0xC6] = uint8_t(sizeof(kRun));
}

void C64::update_pla() {
    const uint8_t res = uint8_t(port_val_ | uint8_t(~port_bits_));
    tape_motor_ = (port_val_ & 0x20) == 0;
    // On real hardware (no cartridge asserting Ultimax mode, which this
    // 8-entry LORAM/HIRAM/CHAREN table can't represent anyway) a CPU write
    // to any address always reaches the underlying RAM chip, even where a
    // ROM or I/O device is currently mapped in for reads: "the PLA insures
    // that whenever ROM coexists with RAM, reading comes from ROM, but
    // writing goes to the hidden RAM (or the I/O chips)." write_ram_ must
    // therefore be true in every one of these configurations; it is not a
    // real write-inhibit signal. Previously it was false for configs 5-7
    // (the power-on default, all-ROM, banking value 7, among them), which
    // silently dropped every RAM write -- including 6502 stack pushes --
    // and left the CPU executing garbage after the first JSR/RTS, hanging
    // long before KERNAL ever reached VIC-II/screen setup (black screen).
    write_ram_ = true;
    switch (res & 7) {
        case 0: case 4: read_ram_d_ = 0; read_ram_a_ = true; read_ram_e_ = true; break;
        case 1: read_ram_d_ = 1; read_ram_a_ = true; read_ram_e_ = true; break;
        case 2: read_ram_d_ = 1; read_ram_a_ = true; read_ram_e_ = false; break;
        case 3: read_ram_d_ = 1; read_ram_a_ = false; read_ram_e_ = false; break;
        case 5: read_ram_d_ = 2; read_ram_a_ = true; read_ram_e_ = true; break;
        case 6: read_ram_d_ = 2; read_ram_a_ = true; read_ram_e_ = false; break;
        case 7: read_ram_d_ = 2; read_ram_a_ = false; read_ram_e_ = false; break;
    }
}

void C64::update_irq() { cpu_.set_irq((cia_irq_ || vic_irq_) ? IrqLine::Assert : IrqLine::Clear); }

void C64::reset() {
    port_bits_ = 0x2F;
    port_val_ = 0x37;
    update_pla();
    vic_.reset(); sid_.reset(); cia1_.reset(); cia2_.reset();
    cia_irq_ = false; vic_irq_ = false; cia_nmi_ = false; restore_ = false;
    cpu_.set_irq(IrqLine::Clear); cpu_.set_nmi(IrqLine::Clear);
    cpu_cycle_debt_ = 0;
    boot_frames_ = 0;
    cpu_.reset();
    if (drive_.rom_loaded()) drive_.reset();
    tape_control_ = 0x10; tape_motor_ = false; keyboard_.fill(0xFF);
    color_ram_.fill(0); audio_.clear(); audio_acc_ = 0;
    std::fill(framebuffer_.begin(), framebuffer_.end(), Mos6566::kPalette[0]);
}

uint8_t C64::cia1_portb_r() {
    uint8_t ret = 0xFF;
    const uint8_t pa = cia1_.pa();
    for (int i = 0; i < 8; i++) if ((pa & (1 << i)) == 0) ret = uint8_t(ret & keyboard_[i]);
    return ret;
}

uint8_t C64::read_byte(uint16_t addr) {
    if (addr == 0) return port_bits_;
    if (addr == 1) {
        // 6510 port read = (latch & DDR) | (external & ~DDR).
        // External: bit4 = cassette SENSE (0 = PLAY pressed when tape armed).
        uint8_t ext = 0xFF;
        if (tape_.is_loaded() && tape_play_)
            ext = uint8_t(ext & ~0x10);  // sense low
        // Bits 0-3,5 are driven as outputs in normal use; still apply the formula.
        return uint8_t((port_val_ & port_bits_) | (ext & uint8_t(~port_bits_)));
    }
    if (addr >= 0xA000 && addr <= 0xBFFF) return read_ram_a_ ? ram_[addr] : basic_rom_[addr & 0x1FFF];
    if (addr >= 0xD000 && addr <= 0xDFFF) {
        switch (read_ram_d_) {
            case 0: return ram_[addr];
            case 1: return char_rom_[addr & 0x0FFF];
            case 2:
                switch ((addr >> 8) & 0x0F) {
                    case 0: case 1: case 2: case 3: return vic_.read(addr & 0x3F);
                    case 4: case 5: case 6: case 7: return sid_.read(addr & 0x1F);
                    case 8: case 9: case 0xA: case 0xB: return uint8_t(color_ram_[addr & 0x3FF] | 0xF0);
                    case 0xC: return cia1_.read(addr & 0x0F);
                    case 0xD: return cia2_.read(addr & 0x0F);
                    default: return 0xFF;
                }
            default: return ram_[addr];
        }
    }
    if (addr >= 0xE000) return read_ram_e_ ? ram_[addr] : kernel_rom_[addr & 0x1FFF];
    return ram_[addr];
}

void C64::write_byte(uint16_t addr, uint8_t value) {
    if (addr == 0) { port_bits_ = value; update_pla(); return; }
    if (addr == 1) { port_val_ = value; update_pla(); return; }
    if (addr >= 0xD000 && addr <= 0xDFFF && read_ram_d_ == 2) {
        switch ((addr >> 8) & 0x0F) {
            case 0: case 1: case 2: case 3: vic_.write(addr & 0x3F, value); return;
            case 4: case 5: case 6: case 7: sid_.write(addr & 0x1F, value); return;
            case 8: case 9: case 0xA: case 0xB: color_ram_[addr & 0x3FF] = value & 0x0F; return;
            case 0xC: cia1_.write(addr & 0x0F, value); return;
            case 0xD: cia2_.write(addr & 0x0F, value); return;
            default: return;
        }
    }
    if (write_ram_) ram_[addr] = value;
}

void C64::on_cycles(int cycles) {
    if (cycles <= 0) return;
    // Tape first so FLAG edges land before CIA timers are clocked for this
    // slice — the KERNAL measures pulse width with Timer B between FLAGs.
    if (tape_motor_ && tape_.is_loaded()) {
        if (!tape_play_) {
            tape_play_ = true;
            tape_.play(true);
        } else if (!tape_.is_playing()) {
            tape_.play(false);
        }
        tape_.advance(cycles, [this]() {
            cia1_.set_flag(true);
            cia1_.set_flag(false);
        });
    }
    // When motor is off we simply don't advance — position is preserved.

    cia1_.tick(cycles);
    cia2_.tick(cycles);
    if (drive_.rom_loaded()) drive_.run(cycles);
    audio_acc_ += int64_t(cycles) * kSampleRate;
    while (audio_acc_ >= int64_t(kCpuClock)) {
        audio_acc_ -= int64_t(kCpuClock);
        int32_t s = sid_.update();
        if (s > 32767) s = 32767;
        if (s < -32768) s = -32768;
        audio_.push_back(int16_t(s));
    }
}

void C64::run_frame() {
    update_pending_prg();
    // The VIC-II always advances through all 312 PAL raster lines. A badline
    // steals 40 CPU bus cycles, but those 40 cycles are still real C64 time:
    // CIA timers, SID, tape, IEC and the raster clock must advance during them.
    for (int line = 0; line < kScanlines; ++line) {
        int cpu_cycles = kCyclesPerLine;
        const int vis_y = line - 16;
        uint32_t* row = (vis_y >= 0 && vis_y < kScreenHeight)
                            ? framebuffer_.data() + size_t(vis_y) * kScreenWidth : nullptr;
        const int stolen = vic_.update_line(line, row);
        cpu_cycles -= stolen;
        if (cpu_cycles < 0) cpu_cycles = 0;

        // Account for the VIC bus steal first. These cycles must never be lost.
        if (stolen > 0) on_cycles(stolen);

        // Execute only the CPU portion of this raster line. Complete 6510
        // instructions can cross the line boundary; preserve the overshoot.
        cpu_cycle_debt_ += cpu_cycles;
        if (cpu_cycle_debt_ > 0) {
            const int ran = cpu_.run(cpu_cycle_debt_);
            cpu_cycle_debt_ -= ran;
        }
    }
}

void C64::set_inputs(const MachineInputs& inputs) {
    keyboard_.fill(0xFF);

    auto press = [this](int column, uint8_t row_mask) {
        keyboard_[static_cast<size_t>(column)] =
            uint8_t(keyboard_[static_cast<size_t>(column)] & ~row_mask);
    };

    const auto& keys = inputs.keys;
    auto key = [&keys](Key id) {
        return keys[static_cast<size_t>(id)];
    };

    // Matriz de teclado completa del Commodore 64.
    // keyboard_[columna], mascara de fila, activo a nivel bajo.

    // Columna 0: DEL, RETURN, CRSR L/R, F7, F1, F3, F5, CRSR U/D.
    if (key(Key::Backspace) /*|| key(Key::Delete)*/) press(0, 0x01);
    if (key(Key::Enter))                         press(0, 0x02);
    if (key(Key::Right) || key(Key::Left))       press(0, 0x04);
    if (key(Key::F7))                            press(0, 0x08);
    if (key(Key::F1))                            press(0, 0x10);
    if (key(Key::F3))                            press(0, 0x20);
    if (key(Key::F5))                            press(0, 0x40);
    if (key(Key::Down) || key(Key::Up))          press(0, 0x80);

    // En el C64, cursor izquierda y arriba son SHIFT + cursor derecha/abajo.
    if (key(Key::Left) || key(Key::Up))          press(1, 0x80);

    // Columna 1: 3, W, A, 4, Z, S, E, LEFT SHIFT.
    if (key(Key::Num3))                          press(1, 0x01);
    if (key(Key::W))                             press(1, 0x02);
    if (key(Key::A))                             press(1, 0x04);
    if (key(Key::Num4))                          press(1, 0x08);
    if (key(Key::Z))                             press(1, 0x10);
    if (key(Key::S))                             press(1, 0x20);
    if (key(Key::E))                             press(1, 0x40);
    if (key(Key::LeftShift))                     press(1, 0x80);

    // Columna 2: 5, R, D, 6, C, F, T, X.
    if (key(Key::Num5))                          press(2, 0x01);
    if (key(Key::R))                             press(2, 0x02);
    if (key(Key::D))                             press(2, 0x04);
    if (key(Key::Num6))                          press(2, 0x08);
    if (key(Key::C))                             press(2, 0x10);
    if (key(Key::F))                             press(2, 0x20);
    if (key(Key::T))                             press(2, 0x40);
    if (key(Key::X))                             press(2, 0x80);

    // Columna 3: 7, Y, G, 8, B, H, U, V.
    if (key(Key::Num7))                          press(3, 0x01);
    if (key(Key::Y))                             press(3, 0x02);
    if (key(Key::G))                             press(3, 0x04);
    if (key(Key::Num8))                          press(3, 0x08);
    if (key(Key::B))                             press(3, 0x10);
    if (key(Key::H))                             press(3, 0x20);
    if (key(Key::U))                             press(3, 0x40);
    if (key(Key::V))                             press(3, 0x80);

    // Columna 4: 9, I, J, 0, M, K, O, N.
    if (key(Key::Num9))                          press(4, 0x01);
    if (key(Key::I))                             press(4, 0x02);
    if (key(Key::J))                             press(4, 0x04);
    if (key(Key::Num0))                          press(4, 0x08);
    if (key(Key::M))                             press(4, 0x10);
    if (key(Key::K))                             press(4, 0x20);
    if (key(Key::O))                             press(4, 0x40);
    if (key(Key::N))                             press(4, 0x80);

    // Columna 5: +, P, L, -, punto, dos puntos, @, coma.
    if (key(Key::Equals))                        press(5, 0x01); // tecla + del C64
    if (key(Key::P))                             press(5, 0x02);
    if (key(Key::L))                             press(5, 0x04);
    if (key(Key::Minus))                         press(5, 0x08);
    if (key(Key::Period))                        press(5, 0x10);
    //if (key(Key::Apostrophe))                    press(5, 0x20); // tecla : del C64
    //if (key(Key::LeftBracket))                   press(5, 0x40); // tecla @ del C64
    if (key(Key::Comma))                         press(5, 0x80);

    // Columna 6: libra, *, ;, HOME/CLR, RIGHT SHIFT, =, flecha arriba, /.
    //if (key(Key::Backslash))                     press(6, 0x01); // libra del C64
    if (key(Key::Asterisk))						 press(6, 0x02); // * del C64
    if (key(Key::Semicolon))                     press(6, 0x04);
    if (key(Key::Home))                          press(6, 0x08);
    if (key(Key::RightShift))                    press(6, 0x10);
    if (key(Key::Equals))                        press(6, 0x20); // = del C64
    if (key(Key::Tab))                           press(6, 0x40); // flecha arriba del C64
    if (key(Key::Slash))                         press(6, 0x80);

    // Columna 7: 1, flecha izquierda, CTRL, 2, SPACE, COMMODORE, Q, RUN/STOP.
    if (key(Key::Num1))                          press(7, 0x01);
    //if (key(Key::Insert))                        press(7, 0x02); // flecha izquierda del C64
    if (key(Key::LeftCtrl))                      press(7, 0x04);
    if (key(Key::Num2))                          press(7, 0x08);
    if (key(Key::Space))                         press(7, 0x10);
    //if (key(Key::LeftAlt))                       press(7, 0x20); // COMMODORE
    if (key(Key::Q))                             press(7, 0x40);
    if (key(Key::Escape))                        press(7, 0x80); // RUN/STOP

    // F11 (not a C64 key) shows / hides the on-screen keyboard.
    const bool toggle = key(Key::F11);
    if (toggle && !vkb_toggle_down_) set_vkb_visible(!vkb_visible_);
    vkb_toggle_down_ = toggle;
    if (vkb_visible_) vkb_input(inputs);
    for (int i = 0; i < kVkbKeyCount; i++) {
        const C64VkbKey& k = kC64VkbKeys[i];
        if (k.col < 0) continue;
        if (i == vkb_pressed_ || vkb_latched_[size_t(i)]) press(k.col, uint8_t(1u << k.row));
    }
    if (vkb_shift_lock_) press(1, 0x80);  // locks the left SHIFT down
    const bool restore = vkb_visible_ && vkb_pressed_ >= 0 &&
                         (kC64VkbKeys[vkb_pressed_].flags & kVkbRestore) != 0;
    if (restore != restore_) {
        restore_ = restore;
        update_nmi();
    }

    auto joy = [](const InputState& p) {
        uint8_t v = 0xFF;
        if (p.up)      v &= uint8_t(~0x01);
        if (p.down)    v &= uint8_t(~0x02);
        if (p.left)    v &= uint8_t(~0x04);
        if (p.right)   v &= uint8_t(~0x08);
        if (p.button1) v &= uint8_t(~0x10);
        return v;
    };

    cia1_.joystick1 = joy(inputs.player1);
    cia1_.joystick2 = joy(inputs.player2);
}

void C64::set_dip_switch(int /*bank*/, uint8_t /*value*/) {}

void C64::update_nmi() {
    // RESTORE and CIA2 share the open-collector NMI line.
    cpu_.set_nmi((cia_nmi_ || restore_) ? IrqLine::Assert : IrqLine::Clear);
}

// ---------------------------------------------------------------------------
// On-screen keyboard

int C64::vkb_width() { return kC64VkbWidth; }
int C64::vkb_height() { return kC64VkbHeight; }

MachineOverlay C64::screen_overlay() const {
    MachineOverlay o;
    if (!vkb_visible_ || vkb_image_.empty()) return o;
    o.pixels = vkb_image_.data();
    o.width = kC64VkbWidth;
    o.height = kC64VkbHeight;
    o.serial = vkb_serial_;
    return o;
}

void C64::vkb_build() {
    const size_t stride = size_t(kC64VkbWidth) * 3;
    std::vector<uint8_t> rgb(stride * kC64VkbHeight);
    uLongf len = uLongf(rgb.size());
    if (uncompress(rgb.data(), &len, kC64VkbPicture, uLong(sizeof(kC64VkbPicture))) != Z_OK) {
        rgb.assign(rgb.size(), 0x40);
    } else {
        for (size_t i = stride; i < rgb.size(); i++) rgb[i] = uint8_t(rgb[i] + rgb[i - stride]);
    }
    vkb_base_.resize(size_t(kC64VkbWidth) * kC64VkbHeight);
    for (size_t i = 0; i < vkb_base_.size(); i++) {
        vkb_base_[i] = 0xff000000u | uint32_t(rgb[i * 3]) << 16 | uint32_t(rgb[i * 3 + 1]) << 8 | rgb[i * 3 + 2];
    }
    // The PETSCII graphics on the front of the keys come from the
    // character ROM in use: C= graphic on the left, SHIFT graphic on the
    // right, anti-aliased down to the height of the front skirt.
    for (const C64VkbKey& k : kC64VkbKeys) {
        const int fh = k.fy2 - k.fy1;
        const int size = std::min(fh - 2, 13);
        if (size < 5) continue;
        const int y0 = k.fy1 + (fh - size) / 2;
        const uint8_t codes[2] = {k.glyph_cbm, k.glyph_shift};
        const float centres[2] = {0.30f, 0.70f};
        for (int g = 0; g < 2; g++) {
            if (codes[g] == 0xff) continue;
            const uint8_t* glyph = &char_rom_[size_t(codes[g]) * 8];
            const int x0 = k.fx1 + int((k.fx2 - k.fx1) * centres[g]) - size / 2;
            for (int py = 0; py < size; py++) {
                for (int px = 0; px < size; px++) {
                    int on = 0;
                    for (int sy = 0; sy < 4; sy++) {
                        for (int sx = 0; sx < 4; sx++) {
                            const int gx = ((px * 4 + sx) * 8) / (size * 4);
                            const int gy = ((py * 4 + sy) * 8) / (size * 4);
                            if (glyph[gy] & (0x80 >> gx)) on++;
                        }
                    }
                    if (!on) continue;
                    const int a = on * 235 / 16;
                    uint32_t& p = vkb_base_[size_t(y0 + py) * kC64VkbWidth + size_t(x0 + px)];
                    auto mix = [&](int shift, int ink) {
                        const int c = int((p >> shift) & 0xff);
                        return uint32_t((c * (255 - a) + ink * a) / 255) << shift;
                    };
                    p = 0xff000000u | mix(16, 230) | mix(8, 224) | mix(0, 208);
                }
            }
        }
    }
    vkb_image_ = vkb_base_;
}

void C64::set_vkb_visible(bool visible) {
    if (visible && vkb_base_.empty()) vkb_build();
    if (!visible) {
        vkb_pressed_ = -1;
        vkb_latched_.fill(false);
        vkb_button_down_ = false;
        if (restore_) {
            restore_ = false;
            update_nmi();
        }
    }
    vkb_visible_ = visible;
    if (visible) vkb_compose();
}

int C64::vkb_hit(int x, int y) const {
    for (int i = 0; i < kVkbKeyCount; i++) {
        const C64VkbKey& k = kC64VkbKeys[i];
        if (x >= k.x1 && x < k.x2 && y >= k.y1 && y < k.y2) return i;
    }
    return -1;
}

bool C64::vkb_key_centre(int column, int row, int* x, int* y) const {
    for (const C64VkbKey& k : kC64VkbKeys) {
        bool match = k.col == column && k.row == row;
        if (column < 0) {
            match = (row == 0 && (k.flags & kVkbRestore)) || (row == 1 && (k.flags & kVkbLock));
        }
        if (!match) continue;
        *x = (k.x1 + k.x2) / 2;
        *y = (k.y1 + k.y2) / 2;
        return true;
    }
    return false;
}

void C64::vkb_input(const MachineInputs& inputs) {
    const bool button = inputs.overlay_pointer && inputs.overlay_button;
    const int before_pressed = vkb_pressed_;
    const auto before_latched = vkb_latched_;
    const bool before_lock = vkb_shift_lock_;
    if (button && !vkb_button_down_) {
        const int hit = vkb_hit(inputs.overlay_x, inputs.overlay_y);
        if (hit >= 0) {
            const C64VkbKey& k = kC64VkbKeys[hit];
            if (k.flags & kVkbLock) {
                vkb_shift_lock_ = !vkb_shift_lock_;
            } else if (k.flags & kVkbModifier) {
                vkb_latched_[size_t(hit)] = !vkb_latched_[size_t(hit)];
            } else {
                vkb_pressed_ = hit;
            }
        }
    }
    if (!button && vkb_pressed_ >= 0) {
        vkb_pressed_ = -1;
        // A clicked key consumes the latched modifiers.
        vkb_latched_.fill(false);
    }
    vkb_button_down_ = button;
    if (before_pressed != vkb_pressed_ || before_latched != vkb_latched_ || before_lock != vkb_shift_lock_) {
        vkb_compose();
    }
}

void C64::vkb_compose() {
    vkb_image_ = vkb_base_;
    // A key that is down sinks into the well: the cap moves down and its
    // front skirt disappears under the frame, in shade.
    constexpr int kTravel = 5;
    const uint32_t well = vkb_base_.empty() ? 0xff261d18u : vkb_base_[size_t(kC64VkbKeys[0].y1 - 4) * kC64VkbWidth +
                                                                    size_t(kC64VkbKeys[0].x1 + 20)];
    for (int i = 0; i < kVkbKeyCount; i++) {
        const C64VkbKey& k = kC64VkbKeys[i];
        const bool down = i == vkb_pressed_ || vkb_latched_[size_t(i)] || ((k.flags & kVkbLock) && vkb_shift_lock_);
        if (!down) continue;
        for (int y = k.y2 - 1; y >= k.y1; y--) {
            for (int x = k.x1; x < k.x2; x++) {
                const size_t dst = size_t(y) * kC64VkbWidth + size_t(x);
                uint32_t p = well;
                if (y - kTravel >= k.y1) {
                    p = vkb_base_[size_t(y - kTravel) * kC64VkbWidth + size_t(x)];
                    const uint32_t r = ((p >> 16) & 0xff) * 13 / 16, g = ((p >> 8) & 0xff) * 13 / 16,
                                   b = (p & 0xff) * 13 / 16;
                    p = 0xff000000u | r << 16 | g << 8 | b;
                }
                vkb_image_[dst] = p;
            }
        }
    }
    vkb_serial_++;
}

void C64::tape_toggle_play() {
    if (!tape_.is_loaded()) return;
    if (tape_play_) {
        tape_.stop();
        tape_play_ = false;  // releases sense → "STOP"
    } else {
        tape_play_ = true;   // presses sense → leaves PRESS PLAY wait
        tape_.play(false);
    }
}
void C64::drain_audio(std::vector<int16_t>& out) { out.insert(out.end(), audio_.begin(), audio_.end()); audio_.clear(); }

}  // namespace dsp