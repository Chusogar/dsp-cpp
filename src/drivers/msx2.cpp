#include "drivers/msx2.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>

#include "core/rom_loader.h"
#include "cpu/irq_line.h"

namespace dsp {
namespace {

const std::vector<RomEntry> kBiosRom = {
    {"MSX2.ROM|nms8250_basic-bios2.rom|msx2.rom", 0x8000, 0x0000, 0x6cdaf3a5},
};
const std::vector<RomEntry> kSubRom = {
    {"MSX2EXT.ROM|nms8250_msx2sub.rom|msx2ext.rom", 0x4000, 0x0000, 0x66237ecf},
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
            if (data.size() > max_size) data.resize(max_size);
            return bool(probe);
        }
    }
    RomLoader loader;
    if (!loader.open(path, error)) return false;
    data.reserve(max_size);
    if (!loader.load_first_file(data, error)) return false;
    if (data.size() > max_size) data.resize(max_size);
    return true;
}

bool ends_with_ci(const std::string& lower, const char* ext) {
    size_t n = std::strlen(ext);
    return lower.size() >= n && lower.compare(lower.size() - n, n, ext) == 0;
}

}  // namespace

Msx2::Msx2()
    : z80_(kMainClock),
      vdp_([this](bool asserted) { on_vdp_interrupt(asserted); }),
      ay8910_(kMainClock / 2, 0.8f) {
    z80_.set_memory_handlers([this](uint16_t a) { return read_byte(a); },
                             [this](uint16_t a, uint8_t v) { write_byte(a, v); });
    z80_.set_io_handlers([this](uint16_t p) { return read_port(p); },
                         [this](uint16_t p, uint8_t v) { write_port(p, v); });
    z80_.set_cycle_handler([this](int cycles) { on_main_cycles(cycles); });

    ay8910_.set_port_handlers([this] { return ay_port_a_read(); }, [this] { return ay_port_b_read(); },
                              nullptr, [this](uint8_t v) { ay_port_b_write(v); });

    ppi_.set_port_handlers([this] { return port_a_read(); }, [this] { return port_b_read(); }, nullptr,
                           [this](uint8_t v) { port_a_write(v); }, nullptr,
                           [this](uint8_t v) { port_c_write(v); });

    fdc_.set_disk(&disk_);
    diskrom_.fill(0xff);
}

bool Msx2::init(const std::string& rom_path, std::string* error) {
    RomLoader loader;
    if (!loader.open(rom_path, error)) return false;

    std::vector<uint8_t> bios(0x8000, 0);
    if (!loader.load(kBiosRom, bios, error)) return false;
    std::copy(bios.begin(), bios.end(), bios_.begin());

    std::vector<uint8_t> sub(0x4000, 0);
    if (!loader.load(kSubRom, sub, error)) return false;
    std::copy(sub.begin(), sub.end(), subrom_.begin());

    diskrom_.fill(0xff);
    disk_rom_loaded_ = false;
    const char* disk_names[] = {"nms8250_disk.rom", "DISK.ROM", "disk.rom", "DISKROM.ROM"};
    for (const char* name : disk_names) {
        std::vector<uint8_t> disk;
        if (loader.try_read(name, disk) && disk.size() == 0x4000) {
            std::copy(disk.begin(), disk.end(), diskrom_.begin());
            disk_rom_loaded_ = true;
            break;
        }
    }
    if (!disk_rom_loaded_) {
        warnings_.emplace_back("MSX2 disk ROM not found; floppy support disabled");
    }

    warnings_.insert(warnings_.end(), loader.warnings().begin(), loader.warnings().end());
    reset();
    return true;
}

void Msx2::reset() {
    z80_.reset();
    ay8910_.reset();
    ppi_.reset();
    vdp_.reset();
    rtc_.reset();
    fdc_.reset();

    keypad_.fill(0xff);
    joystick_ = {0x3f, 0x3f};
    joy_select_ = 0;
    port_a_ = 0;
    port_c_ = 0x7f;
    last_irq_ = false;
    audio_accumulator_ = 0;
    audio_.clear();
    mapper_ = {3, 2, 1, 0};
    subslot_[0] = subslot_[1] = subslot_[2] = subslot_[3] = 0;
    cart_bank0_ = 0;
    cart_bank1_ = 1;
    fdc_control_ = 0;
    for (auto& bank : ram_) bank.fill(0);
}

int Msx2::sub_slot(int prim, int page) const {
    if (!slot_expanded(prim)) return 0;
    return (subslot_[size_t(prim)] >> (page * 2)) & 3;
}

uint8_t Msx2::read_slot(int prim, int sub, int page, uint16_t address) {
    const uint16_t offset = address & 0x3fff;
    if (prim == 0) {
        if (page <= 1) return bios_[size_t(page) * 0x4000 + offset];
        return 0xff;
    }
    if (prim == 1) {
        if (cart_.empty()) return 0xff;
        if (cart_ascii16_) {
            if (page == 0 || page == 3) return 0xff;
            int bank = (page == 1) ? cart_bank0_ : cart_bank1_;
            size_t pos = size_t(uint8_t(bank)) * 0x4000u + offset;
            return pos < cart_.size() ? cart_[pos] : 0xff;
        }
        if (cart_.size() <= 0x4000) {
            if (page == 1 || page == 2) return cart_[offset];
            return 0xff;
        }
        if (address >= 0x4000) {
            size_t pos = size_t(address - 0x4000);
            return pos < cart_.size() ? cart_[pos] : 0xff;
        }
        if (cart_.size() > 0x8000) {
            size_t pos = 0x8000 + offset;
            return pos < cart_.size() ? cart_[pos] : 0xff;
        }
        return 0xff;
    }
    if (prim == 3) {
        if (sub == 0) {
            return page == 0 ? subrom_[offset] : 0xff;
        }
        if (sub == 1) {
            int seg = mapper_[size_t(page)] % kMapperSegments;
            return ram_[size_t(seg)][offset];
        }
        if (sub == 2) {
            if (page == 1) return diskrom_[offset];
            return 0xff;
        }
    }
    return 0xff;
}

void Msx2::write_slot(int prim, int sub, int page, uint16_t address, uint8_t value) {
    const uint16_t offset = address & 0x3fff;
    if (prim == 1 && cart_ascii16_) {
        if (address >= 0x6000 && address < 0x6800) cart_bank0_ = value;
        else if (address >= 0x7000 && address < 0x7800) cart_bank1_ = value;
        return;
    }
    if (prim == 3 && sub == 1) {
        int seg = mapper_[size_t(page)] % kMapperSegments;
        ram_[size_t(seg)][offset] = value;
    }
}

bool Msx2::slot_is_disk(int page) const {
    return primary_slot(page) == 3 && sub_slot(3, page) == 2;
}

uint8_t Msx2::fdc_read(uint16_t address) {
    const uint16_t a = address & 0x3fff;
    if (a >= 0x3ff8 && a <= 0x3ffb) return fdc_.read_reg(a - 0x3ff8);
    if (a == 0x3ffc) return uint8_t(0xfe | (fdc_control_ & 0x01));
    if (a == 0x3ffd) return fdc_control_;
    if (a == 0x3fff) {
        uint8_t value = 0x3f;
        if (!fdc_.intrq()) value |= 0x40;
        if (!fdc_.drq()) value |= 0x80;
        return value;
    }
    if (a >= 0x3fb8 && a <= 0x3fbb) return fdc_.read_reg(a - 0x3fb8);
    if (a == 0x3fbc) {
        uint8_t value = 0x3f;
        if (!fdc_.intrq()) value |= 0x40;
        if (!fdc_.drq()) value |= 0x80;
        return value;
    }
    return 0xff;
}

void Msx2::fdc_write(uint16_t address, uint8_t value) {
    const uint16_t a = address & 0x3fff;
    if (a >= 0x3ff8 && a <= 0x3ffb) {
        fdc_.write_reg(a - 0x3ff8, value);
        return;
    }
    if (a == 0x3ffc) {
        fdc_.set_side(value & 1);
        fdc_control_ = uint8_t((fdc_control_ & 0xfe) | (value & 1));
        return;
    }
    if (a == 0x3ffd) {
        fdc_control_ = value;
        fdc_.set_drive(value & 3);
        fdc_.set_motor((value & 0x80) != 0);
        return;
    }
    if (a >= 0x3fb8 && a <= 0x3fbb) {
        fdc_.write_reg(a - 0x3fb8, value);
        return;
    }
    if (a == 0x3fbc) {
        fdc_control_ = value;
        fdc_.set_drive(value & 3);
        fdc_.set_side((value >> 2) & 1);
        fdc_.set_motor((value & 0x08) != 0);
    }
}

bool Msx2::fdc_mapped(uint16_t address) const {
    const uint16_t a = address & 0x3fff;
    const int page = address >> 14;
    if (!slot_is_disk(page)) return false;
    if (a >= 0x3ff8 && a <= 0x3fff) return true;
    if (a >= 0x3fb8 && a <= 0x3fbc) return true;
    return false;
}

uint8_t Msx2::read_byte(uint16_t address) {
    if (address == 0xffff && primary_slot(3) == 3) {
        return uint8_t(~subslot_[3]);
    }
    if (fdc_mapped(address)) return fdc_read(address);
    int page = address >> 14;
    int prim = primary_slot(page);
    return read_slot(prim, sub_slot(prim, page), page, address);
}

void Msx2::write_byte(uint16_t address, uint8_t value) {
    if (address == 0xffff && primary_slot(3) == 3) {
        subslot_[3] = value;
        return;
    }
    if (fdc_mapped(address)) {
        fdc_write(address, value);
        return;
    }
    int page = address >> 14;
    int prim = primary_slot(page);
    write_slot(prim, sub_slot(prim, page), page, address, value);
}

uint8_t Msx2::read_port(uint16_t port) {
    port &= 0xff;
    switch (port) {
        case 0x98: return vdp_.vram_read();
        case 0x99: return vdp_.status_read();
        case 0xa2: return ay8910_.read();
        case 0xa8:
        case 0xa9:
        case 0xaa:
        case 0xab: return ppi_.read(port & 3);
        case 0xb5: return rtc_.read();
        case 0xfc:
        case 0xfd:
        case 0xfe:
        case 0xff: return mapper_[port & 3];
        default: return 0xff;
    }
}

void Msx2::write_port(uint16_t port, uint8_t value) {
    port &= 0xff;
    switch (port) {
        case 0x98: vdp_.vram_write(value); break;
        case 0x99: vdp_.register_write(value); break;
        case 0x9a: vdp_.palette_write(value); break;
        case 0x9b: vdp_.indirect_write(value); break;
        case 0xa0: ay8910_.control(value); break;
        case 0xa1: ay8910_.write(value); break;
        case 0xa8:
        case 0xa9:
        case 0xaa:
        case 0xab: ppi_.write(port & 3, value); break;
        case 0xb4: rtc_.set_address(value); break;
        case 0xb5: rtc_.write(value); break;
        case 0xfc:
        case 0xfd:
        case 0xfe:
        case 0xff: mapper_[port & 3] = value; break;
        default: break;
    }
}

void Msx2::on_vdp_interrupt(bool asserted) {
    if (asserted && !last_irq_) z80_.set_irq(IrqLine::Hold);
    else if (!asserted && last_irq_) z80_.set_irq(IrqLine::Clear);
    last_irq_ = asserted;
}

uint8_t Msx2::ay_port_a_read() {
    return uint8_t(joystick_[joy_select_] | uint8_t(tape_.level() << 1));
}

void Msx2::ay_port_b_write(uint8_t value) {
    joy_select_ = (value & 0x40) >> 6;
    port_b_ay_ = value;
}

uint8_t Msx2::port_b_read() {
    return teclado_ < keypad_.size() ? keypad_[teclado_] : 0xff;
}

void Msx2::port_a_write(uint8_t value) { port_a_ = value; }

void Msx2::port_c_write(uint8_t value) {
    teclado_ = value & 0x0f;
    if (((port_c_ ^ value) & 0x10) != 0 && tape_.is_loaded()) {
        bool motor_on = (value & 0x10) == 0;
        if (motor_on && !tape_.is_playing()) tape_.play(false);
        if (!motor_on && tape_.is_playing()) tape_.pause();
    }
    port_c_ = value;
}

void Msx2::on_main_cycles(int cycles) {
    if (tape_.is_playing()) {
        int tape_cycles = int(double(cycles) * 3500000.0 / double(kMainClock));
        tape_.advance(tape_cycles);
    }
    audio_accumulator_ += uint64_t(cycles) * uint64_t(AY8910::kSampleRate);
    while (audio_accumulator_ >= kMainClock) {
        audio_accumulator_ -= kMainClock;
        int32_t sample = ay8910_.update();
        if ((port_c_ & 0x80) != 0) sample += 3000;
        if (tape_.is_playing()) sample += int32_t(tape_.level()) * 96;
        audio_.push_back(int16_t(std::clamp(sample, -32768, 32767)));
    }
}

void Msx2::run_frame() {
    for (int line = 0; line < kScanlines; line++) {
        z80_.run(kCyclesPerLine);
        vdp_.refresh_line(line, kScanlines);
    }
}

void Msx2::set_inputs(const MachineInputs& inputs) {
    keypad_.fill(0xff);

    bool rshift = inputs.key(Key::RightShift);
    if (inputs.key(Key::Num0)) keypad_[0] &= 0xfe;
    if (inputs.key(Key::Num1) && !rshift) keypad_[0] &= 0xfd;
    if (inputs.key(Key::Num2) && !rshift) keypad_[0] &= 0xfb;
    if (inputs.key(Key::Num3) && !rshift) keypad_[0] &= 0xf7;
    if (inputs.key(Key::Num4) && !rshift) keypad_[0] &= 0xef;
    if (inputs.key(Key::Num5) && !rshift) keypad_[0] &= 0xdf;
    if (inputs.key(Key::Num6)) keypad_[0] &= 0xbf;
    if (inputs.key(Key::Num7)) keypad_[0] &= 0x7f;
    if (inputs.key(Key::Num8)) keypad_[1] &= 0xfe;
    if (inputs.key(Key::Num9)) keypad_[1] &= 0xfd;
    if (inputs.key(Key::Semicolon)) keypad_[1] &= 0xfb;
    if (inputs.key(Key::Comma)) keypad_[1] &= 0xef;
    if (inputs.key(Key::Period)) keypad_[1] &= 0xdf;
    if (inputs.key(Key::Slash)) keypad_[1] &= 0xbf;
    if (inputs.key(Key::Minus)) keypad_[1] &= 0x7f;
    if (inputs.key(Key::Quote)) keypad_[2] &= 0xfe;
    if (inputs.key(Key::A)) keypad_[2] &= 0xbf;
    if (inputs.key(Key::B)) keypad_[2] &= 0x7f;
    if (inputs.key(Key::C)) keypad_[3] &= 0xfe;
    if (inputs.key(Key::D)) keypad_[3] &= 0xfd;
    if (inputs.key(Key::E)) keypad_[3] &= 0xfb;
    if (inputs.key(Key::F)) keypad_[3] &= 0xf7;
    if (inputs.key(Key::G)) keypad_[3] &= 0xef;
    if (inputs.key(Key::H)) keypad_[3] &= 0xdf;
    if (inputs.key(Key::I)) keypad_[3] &= 0xbf;
    if (inputs.key(Key::J)) keypad_[3] &= 0x7f;
    if (inputs.key(Key::K)) keypad_[4] &= 0xfe;
    if (inputs.key(Key::L)) keypad_[4] &= 0xfd;
    if (inputs.key(Key::M)) keypad_[4] &= 0xfb;
    if (inputs.key(Key::N)) keypad_[4] &= 0xf7;
    if (inputs.key(Key::O)) keypad_[4] &= 0xef;
    if (inputs.key(Key::P)) keypad_[4] &= 0xdf;
    if (inputs.key(Key::Q)) keypad_[4] &= 0xbf;
    if (inputs.key(Key::R)) keypad_[4] &= 0x7f;
    if (inputs.key(Key::S)) keypad_[5] &= 0xfe;
    if (inputs.key(Key::T)) keypad_[5] &= 0xfd;
    if (inputs.key(Key::U)) keypad_[5] &= 0xfb;
    if (inputs.key(Key::V)) keypad_[5] &= 0xf7;
    if (inputs.key(Key::W)) keypad_[5] &= 0xef;
    if (inputs.key(Key::X)) keypad_[5] &= 0xdf;
    if (inputs.key(Key::Y)) keypad_[5] &= 0xbf;
    if (inputs.key(Key::Z)) keypad_[5] &= 0x7f;
    if (inputs.key(Key::LeftShift)) keypad_[6] &= 0xfe;
    if (inputs.key(Key::LeftCtrl)) keypad_[6] &= 0xfd;
    if (inputs.key(Key::CapsLock)) keypad_[6] &= 0xf7;
    if (inputs.key(Key::F1)) keypad_[6] &= 0xdf;
    if (inputs.key(Key::F2) || (inputs.key(Key::Num2) && rshift)) keypad_[6] &= 0xbf;
    if (inputs.key(Key::F3) || (inputs.key(Key::Num3) && rshift)) keypad_[6] &= 0x7f;
    if (inputs.key(Key::F4) || (inputs.key(Key::Num4) && rshift)) keypad_[7] &= 0xfe;
    if (inputs.key(Key::F5) || (inputs.key(Key::Num5) && rshift)) keypad_[7] &= 0xfd;
    if (inputs.key(Key::Escape)) keypad_[7] &= 0xfb;
    if (inputs.key(Key::Tab)) keypad_[7] &= 0xf7;
    if (inputs.key(Key::Backspace)) keypad_[7] &= 0xdf;
    if (inputs.key(Key::Enter)) keypad_[7] &= 0x7f;
    if (inputs.key(Key::Space)) keypad_[8] &= 0xfe;
    if (inputs.key(Key::Left)) keypad_[8] &= 0xef;
    if (inputs.key(Key::Up)) keypad_[8] &= 0xdf;
    if (inputs.key(Key::Down)) keypad_[8] &= 0xbf;
    if (inputs.key(Key::Right)) keypad_[8] &= 0x7f;

    joystick_ = {0x3f, 0x3f};
    const InputState* players[2] = {&inputs.player1, &inputs.player2};
    for (int p = 0; p < 2; p++) {
        const InputState& player = *players[p];
        uint8_t joy = 0x3f;
        if (player.up) joy &= 0xfe;
        if (player.down) joy &= 0xfd;
        if (player.left) joy &= 0xfb;
        if (player.right) joy &= 0xf7;
        if (player.button1) joy &= 0xef;
        if (player.button2) joy &= 0xdf;
        joystick_[size_t(p)] = joy;
    }
}

void Msx2::set_dip_switch(int, uint8_t) {}

void Msx2::drain_audio(std::vector<int16_t>& out) {
    out.insert(out.end(), audio_.begin(), audio_.end());
    audio_.clear();
}

bool Msx2::load_media(const std::string& path, std::string* error) {
    std::string lower = path;
    for (char& ch : lower) ch = char(std::tolower(static_cast<unsigned char>(ch)));
    if (ends_with_ci(lower, ".dsk") || ends_with_ci(lower, ".edsk")) {
        if (!disk_.load_file(path, error)) return false;
        fdc_.set_disk(&disk_);
        return true;
    }
    if (ends_with_ci(lower, ".tzx") || ends_with_ci(lower, ".tsx") || ends_with_ci(lower, ".cas") ||
        ends_with_ci(lower, ".wav")) {
        return load_tape(path, error);
    }
    return load_cartridge(path, error);
}

bool Msx2::load_tape(const std::string& path, std::string* error) {
    if (!tape_.load_file(path, error)) return false;
    tape_.stop();
    return true;
}

void Msx2::tape_toggle_play() {
    if (!tape_.is_loaded()) return;
    if (tape_.is_playing()) tape_.pause();
    else tape_.play(false);
}

bool Msx2::load_cartridge(const std::string& path, std::string* error) {
    std::vector<uint8_t> data;
    if (!read_plain_or_zip_file(path, data, kMaxCartridge, error)) return false;
    if (data.empty()) {
        if (error) *error = "empty cartridge";
        return false;
    }
    cart_ = std::move(data);
    cart_ascii16_ = cart_.size() > 0xc000;
    cart_bank0_ = 0;
    cart_bank1_ = 1;
    return true;
}

}  // namespace dsp
