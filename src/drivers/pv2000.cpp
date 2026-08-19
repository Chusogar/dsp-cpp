#include "drivers/pv2000.h"

#include <algorithm>
#include <filesystem>
#include <fstream>

#include "core/rom_loader.h"
#include "cpu/irq_line.h"

namespace dsp {
namespace {

// pv2000_bios in pv2000.pas.
const std::vector<RomEntry> kBiosRom = {
    {"hn613128pc64.bin", 0x4000, 0x0000, 0x8f31f297},
};

bool read_plain_or_zip_file(const std::string& path, std::vector<uint8_t>& data, size_t max_size,
                            std::string* error) {
    namespace fs = std::filesystem;
    std::error_code ec;
    if (fs::is_regular_file(path, ec)) {
        std::ifstream probe(path, std::ios::binary);
        char magic[4] = {};
        probe.read(magic, 4);
        bool is_zip = probe.gcount() == 4 && magic[0] == 'P' && magic[1] == 'K' &&
                     magic[2] == 0x03 && magic[3] == 0x04;
        if (!is_zip) {
            probe.clear();
            probe.seekg(0, std::ios::end);
            std::streamoff size = probe.tellg();
            probe.seekg(0, std::ios::beg);
            if (size <= 0) {
                if (error) *error = "cannot read " + path;
                return false;
            }
            data.resize(size_t(size));
            probe.read(reinterpret_cast<char*>(data.data()), size);
            if (!probe) {
                if (error) *error = "cannot read " + path;
                return false;
            }
            if (data.size() > max_size) data.resize(max_size);
            return true;
        }
    }
    RomLoader loader;
    if (!loader.open(path, error)) return false;
    data.reserve(max_size);
    if (!loader.load_first_file(data, error)) return false;
    if (data.size() > max_size) data.resize(max_size);
    return true;
}

}  // namespace

Pv2000::Pv2000()
    : z80_(kMainClock),
      vdp_(1, [this](bool asserted) { on_vdp_interrupt(asserted); }),
      sn76489_(kMainClock) {
    z80_.set_memory_handlers([this](uint16_t a) { return read_byte(a); },
                             [this](uint16_t a, uint8_t v) { write_byte(a, v); });
    z80_.set_io_handlers([this](uint16_t p) { return read_port(p); },
                         [this](uint16_t p, uint8_t v) { write_port(p, v); });
    z80_.set_cycle_handler([this](int cycles) { on_main_cycles(cycles); });
}

bool Pv2000::init(const std::string& rom_path, std::string* error) {
    RomLoader loader;
    if (!loader.open(rom_path, error)) return false;
    std::vector<uint8_t> bios(0x4000, 0);
    if (!loader.load(kBiosRom, bios, error)) return false;
    memory_.fill(0);
    std::copy(bios.begin(), bios.end(), memory_.begin());
    warnings_ = loader.warnings();
    reset();
    return true;
}

bool Pv2000::load_media(const std::string& path, std::string* error) {
    std::vector<uint8_t> data;
    if (!read_plain_or_zip_file(path, data, kMaxCartridge, error)) return false;

    std::fill(memory_.begin() + 0xc000, memory_.end(), 0);
    std::copy(data.begin(), data.end(), memory_.begin() + 0xc000);
    reset();
    return true;
}

void Pv2000::reset() {
    z80_.reset();
    sn76489_.reset();
    vdp_.reset();
    last_nmi_ = false;
    keyb_column_ = 0;
    last_key_ = 0;
    keys_.fill(0);
    audio_accumulator_ = 0;
    audio_.clear();
}

uint8_t Pv2000::read_byte(uint16_t address) {
    switch (address) {
        case 0x4000:
            return vdp_.vram_read();
        case 0x4001:
            return vdp_.register_read();
        default:
            break;
    }
    // BIOS $0000-$3fff, RAM $7000-$7fff, cart $c000-$ffff (pv2000_getbyte).
    if (address <= 0x3fff || (address >= 0x7000 && address <= 0x7fff) || address >= 0xc000) {
        return memory_[address];
    }
    return 0;
}

void Pv2000::write_byte(uint16_t address, uint8_t value) {
    switch (address) {
        case 0x4000:
            vdp_.vram_write(value);
            return;
        case 0x4001:
            vdp_.register_write(value);
            return;
        default:
            break;
    }
    if (address >= 0x7000 && address <= 0x7fff) memory_[address] = value;
    // $0000-$3fff / $c000-$ffff are ROM.
}

uint8_t Pv2000::read_port(uint16_t port) {
    switch (port & 0xff) {
        case 0x10:
            if (keyb_column_ < 10) return uint8_t(keys_[keyb_column_] >> 4);
            return 0;
        case 0x20:
            if (keyb_column_ < 10) return uint8_t(0xf0 | (keys_[keyb_column_] & 0x0f));
            return 0xf0;
        case 0x40:
            return uint8_t(0xf0 | (keys_[10] & 0x0f));
        case 0x60:
            return 0;  // cassette, stubbed like pv2000_in
        default:
            return 0;
    }
}

void Pv2000::write_port(uint16_t port, uint8_t value) {
    switch (port & 0xff) {
        case 0x00:
        case 0x60:
            break;  // cassette
        case 0x20:
            keyb_column_ = value;
            z80_.set_irq(IrqLine::Clear);
            break;
        case 0x40:
            sn76489_.write(value);
            break;
        default:
            break;
    }
}

void Pv2000::on_vdp_interrupt(bool asserted) {
    // TMS INT rising edge pulses NMI (pv2000_interrupt / ColecoVision).
    if (asserted && !last_nmi_) z80_.set_nmi(IrqLine::Pulse);
    last_nmi_ = asserted;

    // While the BIOS has selected column $0f it wants an IRQ on a new key.
    if (keyb_column_ == 0x0f) {
        uint8_t key_pressed = 0;
        for (int i = 0; i < 9; i++) key_pressed = uint8_t(key_pressed | keys_[size_t(i)]);
        if (key_pressed != 0 && key_pressed != last_key_) z80_.set_irq(IrqLine::Assert);
        last_key_ = key_pressed;
    }
}

void Pv2000::on_main_cycles(int cycles) {
    audio_accumulator_ += uint64_t(cycles) * uint64_t(SN76496::kSampleRate);
    while (audio_accumulator_ >= kMainClock) {
        audio_accumulator_ -= kMainClock;
        audio_.push_back(int16_t(sn76489_.update()));
    }
}

void Pv2000::run_frame() {
    for (int line = 0; line < kScanlines; line++) {
        z80_.run(kCyclesPerLine);
        vdp_.refresh_ntsc(line);
    }
}

void Pv2000::set_inputs(const MachineInputs& inputs) {
    // eventos_pv2000 rebuilds the 11-column matrix every poll.
    keys_.fill(0);
    auto press = [&](int column, uint8_t bit, bool down) {
        if (down) keys_[size_t(column)] = uint8_t(keys_[size_t(column)] | bit);
    };

    // in0
    press(0, 0x01, inputs.key(Key::Num4));
    press(0, 0x02, inputs.key(Key::Num3));
    press(0, 0x04, inputs.key(Key::Num2));
    press(0, 0x08, inputs.key(Key::Num1));
    press(0, 0x10, inputs.key(Key::Num8));
    press(0, 0x20, inputs.key(Key::Num7));
    press(0, 0x40, inputs.key(Key::Num6));
    press(0, 0x80, inputs.key(Key::Num5));
    // in1
    press(1, 0x01, inputs.key(Key::R));
    press(1, 0x02, inputs.key(Key::E));
    press(1, 0x04, inputs.key(Key::W));
    press(1, 0x08, inputs.key(Key::Q));
    press(1, 0x10, inputs.key(Key::I));
    press(1, 0x20, inputs.key(Key::U));
    press(1, 0x40, inputs.key(Key::Y));
    press(1, 0x80, inputs.key(Key::T));
    // in2
    press(2, 0x01, inputs.key(Key::F));
    press(2, 0x02, inputs.key(Key::D));
    press(2, 0x04, inputs.key(Key::S));
    press(2, 0x08, inputs.key(Key::A));
    press(2, 0x10, inputs.key(Key::K));
    press(2, 0x20, inputs.key(Key::J));
    press(2, 0x40, inputs.key(Key::H));
    press(2, 0x80, inputs.key(Key::G));
    // in3
    press(3, 0x01, inputs.key(Key::C));
    press(3, 0x02, inputs.key(Key::X));
    press(3, 0x04, inputs.key(Key::Z));
    press(3, 0x08, inputs.key(Key::CapsLock));  // Hiragana (unmapped in Pascal)
    press(3, 0x10, inputs.key(Key::Space));
    press(3, 0x20, inputs.key(Key::N));
    press(3, 0x40, inputs.key(Key::B));
    press(3, 0x80, inputs.key(Key::V));
    // in4: HOME, 9, '-', '^' (no host key), 0
    press(4, 0x08, inputs.key(Key::Tab));  // HOME
    press(4, 0x10, inputs.key(Key::Num9));
    press(4, 0x20, inputs.key(Key::Minus));
    press(4, 0x80, inputs.key(Key::Num0));
    // in5: keypad diagonals omitted; O, '@', '[', P
    press(5, 0x10, inputs.key(Key::O));
    press(5, 0x80, inputs.key(Key::P));
    // in6: down/right (also the joystick), L, ':', ']', ';'
    press(6, 0x01, inputs.key(Key::Down) || inputs.player1.down);
    press(6, 0x02, inputs.key(Key::Right) || inputs.player1.right);
    press(6, 0x10, inputs.key(Key::L));
    press(6, 0x20, inputs.key(Key::Quote));
    press(6, 0x80, inputs.key(Key::Semicolon));
    // in7: left/up (also the joystick), M, '.', '/', ','
    press(7, 0x01, inputs.key(Key::Left) || inputs.player1.left);
    press(7, 0x02, inputs.key(Key::Up) || inputs.player1.up);
    press(7, 0x10, inputs.key(Key::M));
    press(7, 0x20, inputs.key(Key::Period));
    press(7, 0x40, inputs.key(Key::Slash));
    press(7, 0x80, inputs.key(Key::Comma));
    // in8: Attack 0/1, Return, Del
    press(8, 0x01, inputs.player1.button1);
    press(8, 0x02, inputs.player1.button2);
    press(8, 0x10, inputs.key(Key::Enter));
    press(8, 0x40, inputs.key(Key::Backspace));
    // in10 / MOD: Shift
    press(10, 0x04, inputs.key(Key::LeftShift) || inputs.key(Key::RightShift));
}

void Pv2000::set_dip_switch(int, uint8_t) {
    // Cartridge console, no DIP switches.
}

void Pv2000::drain_audio(std::vector<int16_t>& out) {
    out.insert(out.end(), audio_.begin(), audio_.end());
    audio_.clear();
}

}  // namespace dsp
