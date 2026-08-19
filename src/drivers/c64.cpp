#include "drivers/c64.h"

#include <algorithm>
#include <cstring>
#include <fstream>

namespace dsp {
namespace {

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
    vic_.set_mem_read([this](uint16_t a14) -> uint8_t {
        const uint16_t a = a14 & 0x3FFF;
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
        cpu_.set_nmi(cia_nmi_ ? IrqLine::Assert : IrqLine::Clear);
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
                drive_.set_host_atn((v & 0x08) != 0);
                drive_.set_host_clk((v & 0x10) != 0);
                drive_.set_host_data((v & 0x20) != 0);
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
        return inject_prg(prg, error);
    }
    if (ends_ci(path, ".tzx") || ends_ci(path, ".tap")) {
        if (!tape_.load_file(path, error)) return false;
        tape_play_ = false;
        return true;
    }
    std::vector<uint8_t> data;
    if (!read_file(path, &data)) {
        if (error) *error = "cannot open: " + path;
        return false;
    }
    return inject_prg(data, error);
}

bool C64::inject_prg(const std::vector<uint8_t>& data, std::string* error) {
    if (data.size() < 3) {
        if (error) *error = "PRG too small";
        return false;
    }
    const uint16_t addr = uint16_t(data[0] | (data[1] << 8));
    const size_t n = data.size() - 2;
    for (size_t i = 0; i < n; i++) ram_[uint16_t(addr + i)] = data[i + 2];
    const uint16_t end = uint16_t(addr + n);
    if (addr == 0x0801) {
        ram_[0x2D] = uint8_t(end & 0xFF); ram_[0x2E] = uint8_t(end >> 8);
        ram_[0x2F] = uint8_t(end & 0xFF); ram_[0x30] = uint8_t(end >> 8);
        ram_[0x31] = uint8_t(end & 0xFF); ram_[0x32] = uint8_t(end >> 8);
        ram_[0xAE] = uint8_t(end & 0xFF); ram_[0xAF] = uint8_t(end >> 8);
    }
    return true;
}

void C64::update_pla() {
    const uint8_t res = uint8_t(port_val_ | uint8_t(~port_bits_));
    tape_motor_ = (port_val_ & 0x20) == 0;
    switch (res & 7) {
        case 0: case 4: write_ram_ = true; read_ram_d_ = 0; read_ram_a_ = true; read_ram_e_ = true; break;
        case 1: write_ram_ = true; read_ram_d_ = 1; read_ram_a_ = true; read_ram_e_ = true; break;
        case 2: write_ram_ = true; read_ram_d_ = 1; read_ram_a_ = true; read_ram_e_ = false; break;
        case 3: write_ram_ = true; read_ram_d_ = 1; read_ram_a_ = false; read_ram_e_ = false; break;
        case 5: write_ram_ = false; read_ram_d_ = 2; read_ram_a_ = true; read_ram_e_ = true; break;
        case 6: write_ram_ = false; read_ram_d_ = 2; read_ram_a_ = true; read_ram_e_ = false; break;
        case 7: write_ram_ = false; read_ram_d_ = 2; read_ram_a_ = false; read_ram_e_ = false; break;
    }
}

void C64::update_irq() { cpu_.set_irq((cia_irq_ || vic_irq_) ? IrqLine::Assert : IrqLine::Clear); }

void C64::reset() {
    port_bits_ = 0x2F;
    port_val_ = 0x37;
    update_pla();
    vic_.reset(); sid_.reset(); cia1_.reset(); cia2_.reset();
    cia_irq_ = false; vic_irq_ = false; cia_nmi_ = false;
    cpu_.set_irq(IrqLine::Clear); cpu_.set_nmi(IrqLine::Clear);
    cpu_cycle_debt_ = 0;
    cpu_.reset();
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
    if (addr == 1) return uint8_t(tape_control_ | (tape_motor_ ? 0 : 0x20) | (port_val_ & 7));
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
    // Mos6526 exposes tick(), not run(). Advance both CIAs for every real
    // PHI2 cycle, including VIC-II stolen cycles passed through this path.
    cia1_.tick(cycles);
    cia2_.tick(cycles);
    if (drive_.rom_loaded()) drive_.run(cycles);
    if (tape_motor_ && tape_.is_loaded()) {
        if (!tape_play_) { tape_.play(true); tape_play_ = true; }
        tape_.advance(cycles);
        static uint8_t prev = 0;
        const uint8_t lv = tape_.level();
        if (prev && !lv) cia1_.set_flag(false);
        if (!prev && lv) cia1_.set_flag(true);
        prev = lv;
        tape_control_ = lv ? 0x10 : 0x00;
    } else if (tape_play_ && !tape_motor_) {
        tape_.stop(); tape_play_ = false;
    }
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
    auto press = [this](int row, uint8_t mask) { keyboard_[row] = uint8_t(keyboard_[row] & ~mask); };
    auto& k = inputs.keys;
    auto key = [&](Key id) { return k[size_t(id)]; };
    if (key(Key::A)) press(1, 0x04);
    if (key(Key::B)) press(3, 0x10);
    if (key(Key::C)) press(2, 0x10);
    if (key(Key::D)) press(2, 0x04);
    if (key(Key::E)) press(1, 0x40);
    if (key(Key::F)) press(2, 0x20);
    if (key(Key::G)) press(3, 0x04);
    if (key(Key::H)) press(3, 0x20);
    if (key(Key::I)) press(4, 0x02);
    if (key(Key::J)) press(4, 0x04);
    if (key(Key::K)) press(4, 0x20);
    if (key(Key::L)) press(5, 0x04);
    if (key(Key::M)) press(4, 0x10);
    if (key(Key::N)) press(4, 0x80);
    if (key(Key::O)) press(4, 0x40);
    if (key(Key::P)) press(5, 0x02);
    if (key(Key::Q)) press(7, 0x40);
    if (key(Key::R)) press(2, 0x02);
    if (key(Key::S)) press(1, 0x20);
    if (key(Key::T)) press(2, 0x40);
    if (key(Key::U)) press(3, 0x40);
    if (key(Key::V)) press(3, 0x80);
    if (key(Key::W)) press(1, 0x02);
    if (key(Key::X)) press(2, 0x80);
    if (key(Key::Y)) press(3, 0x02);
    if (key(Key::Z)) press(1, 0x10);
    if (key(Key::Space)) press(7, 0x10);
    if (key(Key::Enter)) press(0, 0x02);
    if (key(Key::Num1)) press(7, 0x01);
    if (key(Key::Num2)) press(7, 0x08);
    if (key(Key::Num3)) press(1, 0x01);
    if (key(Key::Num0)) press(4, 0x08);
    if (key(Key::Escape)) press(7, 0x80);
    if (key(Key::LeftCtrl)) press(7, 0x04);
    if (key(Key::Backspace)) press(0, 0x01);
    auto joy = [](const InputState& p) {
        uint8_t v = 0xFF;
        if (p.up) v &= ~0x01;
        if (p.down) v &= ~0x02;
        if (p.left) v &= ~0x04;
        if (p.right) v &= ~0x08;
        if (p.button1) v &= ~0x10;
        return v;
    };
    cia1_.joystick1 = joy(inputs.player1);
    cia1_.joystick2 = joy(inputs.player2);
}

void C64::set_dip_switch(int /*bank*/, uint8_t /*value*/) {}
void C64::drain_audio(std::vector<int16_t>& out) { out.insert(out.end(), audio_.begin(), audio_.end()); audio_.clear(); }

}  // namespace dsp