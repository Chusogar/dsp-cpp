#include "drivers/c64.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>

#include "core/rom_loader.h"

namespace dsp {
namespace {

const std::vector<RomEntry> kKernalRom = {
    {"901227-03.u4|kernal.bin|kernal.rom|kernal", 0x2000, 0x0000, 0xdbe3e7c7},
};
const std::vector<RomEntry> kBasicRom = {
    {"901226-01.u3|basic.bin|basic.rom|basic", 0x2000, 0x0000, 0xf833d117},
};
const std::vector<RomEntry> kCharRom = {
    {"901225-01.u5|chargen.bin|chargen.rom|chargen|characters.bin", 0x1000, 0x0000, 0xec4272ee},
};

std::string lower_copy(std::string value) {
    for (char& ch : value) ch = char(std::tolower(static_cast<unsigned char>(ch)));
    return value;
}

bool ends_with(const std::string& value, const char* ext) {
    const size_t n = std::strlen(ext);
    return value.size() >= n && value.compare(value.size() - n, n, ext) == 0;
}

bool read_whole_file(const std::string& path, std::vector<uint8_t>& out, std::string* error) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        if (error) *error = "cannot read " + path;
        return false;
    }
    stream.seekg(0, std::ios::end);
    const std::streamoff size = stream.tellg();
    if (size < 0) {
        if (error) *error = "cannot size " + path;
        return false;
    }
    stream.seekg(0, std::ios::beg);
    out.resize(size_t(size));
    stream.read(reinterpret_cast<char*>(out.data()), size);
    return bool(stream);
}

void key_bit(std::array<uint8_t, 8>& matrix, int col, uint8_t mask, bool pressed) {
    if (pressed) {
        matrix[size_t(col)] = uint8_t(matrix[size_t(col)] & uint8_t(~mask));
    } else {
        matrix[size_t(col)] = uint8_t(matrix[size_t(col)] | mask);
    }
}

}  // namespace

C64::C64() : cpu_(kClock), vic_(kClock), cia1_(kClock), cia2_(kClock), sid_(kClock, Sid::Type6581) {
    cpu_.set_memory_handlers([this](uint16_t a) { return read_byte(a); },
                             [this](uint16_t a, uint8_t v) { write_byte(a, v); });
    cpu_.set_cycle_handler([this](int cycles) { on_cpu_cycles(cycles); });
    vic_.set_irq_callback([this](IrqLine state) {
        vic_irq_ = (state == IrqLine::Assert);
        if (!vic_irq_) cpu_.set_irq(IrqLine::Clear);
    });
    vic_.set_vblank_callback([this]() {
        cia1_.clock_tod();
        cia2_.clock_tod();
    });
    cia1_.set_calls([this]() { return cia1_pa_read(); }, [this]() { return cia1_pb_read(); }, nullptr, nullptr,
                    [this](IrqLine state) {
                        cia_irq_ = (state == IrqLine::Assert);
                        if (!cia_irq_) cpu_.set_irq(IrqLine::Clear);
                    });
    cia2_.set_calls(nullptr, nullptr, [this](uint8_t value) { cia2_pa_write(value); }, nullptr,
                    [this](IrqLine state) {
                        cia_nmi_ = (state == IrqLine::Assert);
                        if (!cia_nmi_) cpu_.set_nmi(IrqLine::Clear);
                    });
    vic_.set_memory(ram_.data(), chargen_.data(), color_ram_.data());
}

bool C64::init(const std::string& rom_path, std::string* error) {
    RomLoader loader;
    if (!loader.open(rom_path, error)) return false;
    std::vector<uint8_t> kernal, basic, chargen;
    if (!loader.load(kKernalRom, kernal, error)) return false;
    if (!loader.load(kBasicRom, basic, error)) return false;
    if (!loader.load(kCharRom, chargen, error)) return false;
    std::memcpy(kernal_.data(), kernal.data(), kernal_.size());
    std::memcpy(basic_.data(), basic.data(), basic_.size());
    std::memcpy(chargen_.data(), chargen.data(), chargen_.size());
    warnings_ = loader.warnings();
    reset();
    return true;
}

void C64::init_synthetic_roms() {
    kernal_.fill(0xea);  // NOP
    basic_.fill(0xea);
    chargen_.fill(0);
    // Reset / NMI / IRQ vectors in the KERNAL window ($E000).
    kernal_[0x1ffa] = 0x00;
    kernal_[0x1ffb] = 0xc0;  // NMI -> $C000
    kernal_[0x1ffc] = 0x00;
    kernal_[0x1ffd] = 0xc0;  // RESET -> $C000
    kernal_[0x1ffe] = 0x00;
    kernal_[0x1fff] = 0xc0;  // IRQ -> $C000
    reset();
    ram_[0xc000] = 0x4c;  // JMP $C000
    ram_[0xc001] = 0x00;
    ram_[0xc002] = 0xc0;
}

void C64::reset() {
    port_bits_ = 0xef;
    port_val_ = 0xef;
    update_pla();
    cpu_.reset();
    cia1_.reset();
    cia2_.reset();
    vic_.reset();
    sid_.reset();
    keyboard_.fill(0xff);
    ram_.fill(0);
    color_ram_.fill(0);
    tape_control_ = 0x10;
    vic_irq_ = false;
    cia_irq_ = false;
    cia_nmi_ = false;
    tape_motor_ = false;
    write_ram_ = false;
    read_ram_a_ = false;
    read_ram_e_ = false;
    read_ram_d_ = 2;
    audio_.clear();
    audio_acc_ = 0;
    framebuffer_.fill(Mos6566::palette_color(0));
    cpu_.set_irq(IrqLine::Clear);
    cpu_.set_nmi(IrqLine::Clear);
}

void C64::update_pla() {
    const uint8_t res = uint8_t(port_val_ | uint8_t(~port_bits_));
    tape_motor_ = (port_val_ & 0x20) == 0;
    switch (res & 7) {
        case 0:
        case 4:
            write_ram_ = true;
            read_ram_d_ = 0;
            read_ram_a_ = true;
            read_ram_e_ = true;
            break;
        case 1:
            write_ram_ = true;
            read_ram_d_ = 1;
            read_ram_a_ = true;
            read_ram_e_ = true;
            break;
        case 2:
            write_ram_ = true;
            read_ram_d_ = 1;
            read_ram_a_ = true;
            read_ram_e_ = false;
            break;
        case 3:
            write_ram_ = true;
            read_ram_d_ = 1;
            read_ram_a_ = false;
            read_ram_e_ = false;
            break;
        case 5:
            write_ram_ = false;
            read_ram_d_ = 2;
            read_ram_a_ = true;
            read_ram_e_ = true;
            break;
        case 6:
            write_ram_ = false;
            read_ram_d_ = 2;
            read_ram_a_ = true;
            read_ram_e_ = false;
            break;
        case 7:
            write_ram_ = false;
            read_ram_d_ = 2;
            read_ram_a_ = false;
            read_ram_e_ = false;
            break;
        default:
            break;
    }
}

uint8_t C64::read_byte(uint16_t address) {
    if (address == 0) return port_bits_;
    if (address == 1) {
        return uint8_t(tape_control_ | (uint8_t(!tape_motor_) << 5) | (port_val_ & 7));
    }
    if ((address >= 0x0002 && address <= 0x9fff) || (address >= 0xc000 && address <= 0xcfff)) {
        return ram_[address];
    }
    if (address >= 0xa000 && address <= 0xbfff) {
        return read_ram_a_ ? ram_[address] : basic_[address & 0x1fff];
    }
    if (address >= 0xd000 && address <= 0xdfff) {
        switch (read_ram_d_) {
            case 0:
                return ram_[address];
            case 1:
                return chargen_[address & 0xfff];
            case 2:
                switch ((address >> 8) & 0x0f) {
                    case 0:
                    case 1:
                    case 2:
                    case 3:
                        return vic_.read(uint8_t(address & 0x3f));
                    case 4:
                    case 5:
                    case 6:
                    case 7:
                        return sid_.read(uint8_t(address & 0x1f));
                    case 8:
                    case 9:
                    case 0x0a:
                    case 0x0b:
                        return color_ram_[address & 0x3ff];
                    case 0x0c:
                        return cia1_.read(uint8_t(address & 0x0f));
                    case 0x0d:
                        return cia2_.read(uint8_t(address & 0x0f));
                    default:
                        return 0xff;
                }
            default:
                return ram_[address];
        }
    }
    if (address >= 0xe000) {
        return read_ram_e_ ? ram_[address] : kernal_[address & 0x1fff];
    }
    return ram_[address];
}

void C64::write_byte(uint16_t address, uint8_t value) {
    if (address == 0) {
        port_bits_ = value;
        update_pla();
        return;
    }
    if (address == 1) {
        port_val_ = value;
        update_pla();
        return;
    }
    if (address >= 0xd000 && address <= 0xdfff) {
        if (write_ram_) {
            ram_[address] = value;
            return;
        }
        switch ((address >> 8) & 0x0f) {
            case 0:
            case 1:
            case 2:
            case 3:
                vic_.write(uint8_t(address & 0x3f), value);
                break;
            case 4:
            case 5:
            case 6:
            case 7:
                sid_.write(uint8_t(address & 0x1f), value);
                break;
            case 8:
            case 9:
            case 0x0a:
            case 0x0b:
                color_ram_[address & 0x3ff] = uint8_t(value & 0x0f);
                break;
            case 0x0c:
                cia1_.write(uint8_t(address & 0x0f), value);
                break;
            case 0x0d:
                cia2_.write(uint8_t(address & 0x0f), value);
                break;
            default:
                break;
        }
        return;
    }
    ram_[address] = value;
}

void C64::on_cpu_cycles(int cycles) {
    if (cia_irq_ || vic_irq_) cpu_.set_irq(IrqLine::Assert);
    if (cia_nmi_) cpu_.set_nmi(IrqLine::Assert);
    if (tape_loaded_ && tape_playing_ && tape_motor_) advance_tape(cycles);
    cia1_.sync(cycles);
    cia2_.sync(cycles);
    audio_acc_ += int64_t(cycles) * kSampleRate;
    while (audio_acc_ >= int64_t(kClock)) {
        audio_acc_ -= int64_t(kClock);
        audio_.push_back(sid_.update());
    }
}

void C64::run_frame() {
    for (int line = 0; line < kScanlines; line++) {
        const int cycles = vic_.update(uint16_t(line));
        cpu_.run(cycles);
        if (line > 15 && line < 286) {
            const uint32_t* src = vic_.scanline();
            uint32_t* dest = &framebuffer_[size_t(line - 16) * kScreenWidth];
            std::memcpy(dest, src, size_t(kScreenWidth) * sizeof(uint32_t));
        }
    }
}

void C64::drain_audio(std::vector<int16_t>& out) {
    out.swap(audio_);
    audio_.clear();
}

void C64::set_dip_switch(int, uint8_t) {}

uint8_t C64::cia1_pa_read() { return cia1_.joystick1; }

uint8_t C64::cia1_pb_read() {
    uint8_t ret = 0xff;
    const uint8_t pa = cia1_.pa();
    for (int i = 0; i < 8; i++) {
        if ((pa & (1 << i)) == 0) ret = uint8_t(ret & keyboard_[size_t(i)]);
    }
    return uint8_t(ret & cia1_.joystick2);
}

void C64::cia2_pa_write(uint8_t value) { vic_.changed_va(uint16_t((~value) & 3)); }

void C64::apply_keyboard(const MachineInputs& in) {
    keyboard_.fill(0xff);
    const bool rshift = in.key(Key::RightShift);
    const bool lshift = in.key(Key::LeftShift);

    key_bit(keyboard_, 0, 0x01, in.key(Key::Backspace));
    key_bit(keyboard_, 0, 0x02, in.key(Key::Enter));
    key_bit(keyboard_, 0, 0x04, in.key(Key::Right) || (in.key(Key::Left) && lshift));
    key_bit(keyboard_, 0, 0x08, in.key(Key::F7) || (in.key(Key::Num7) && rshift));
    key_bit(keyboard_, 0, 0x10, in.key(Key::F1) || (in.key(Key::Num1) && rshift));
    key_bit(keyboard_, 0, 0x20, in.key(Key::F3) || (in.key(Key::Num3) && rshift));
    key_bit(keyboard_, 0, 0x40, in.key(Key::F5));
    key_bit(keyboard_, 0, 0x80, in.key(Key::Down) || (in.key(Key::Up) && lshift));

    key_bit(keyboard_, 1, 0x01, in.key(Key::Num3) && !rshift);
    key_bit(keyboard_, 1, 0x02, in.key(Key::W));
    key_bit(keyboard_, 1, 0x04, in.key(Key::A));
    key_bit(keyboard_, 1, 0x08, in.key(Key::Num4));
    key_bit(keyboard_, 1, 0x10, in.key(Key::Z));
    key_bit(keyboard_, 1, 0x20, in.key(Key::S));
    key_bit(keyboard_, 1, 0x40, in.key(Key::E));
    key_bit(keyboard_, 1, 0x80, lshift);

    key_bit(keyboard_, 2, 0x01, in.key(Key::Num5) && !rshift);
    key_bit(keyboard_, 2, 0x02, in.key(Key::R));
    key_bit(keyboard_, 2, 0x04, in.key(Key::D));
    key_bit(keyboard_, 2, 0x08, in.key(Key::Num6));
    key_bit(keyboard_, 2, 0x10, in.key(Key::C));
    key_bit(keyboard_, 2, 0x20, in.key(Key::F));
    key_bit(keyboard_, 2, 0x40, in.key(Key::T));
    key_bit(keyboard_, 2, 0x80, in.key(Key::X));

    key_bit(keyboard_, 3, 0x01, in.key(Key::Num7) && !rshift);
    key_bit(keyboard_, 3, 0x02, in.key(Key::Y));
    key_bit(keyboard_, 3, 0x04, in.key(Key::G));
    key_bit(keyboard_, 3, 0x08, in.key(Key::Num8));
    key_bit(keyboard_, 3, 0x10, in.key(Key::B));
    key_bit(keyboard_, 3, 0x20, in.key(Key::H));
    key_bit(keyboard_, 3, 0x40, in.key(Key::U));
    key_bit(keyboard_, 3, 0x80, in.key(Key::V));

    key_bit(keyboard_, 4, 0x01, in.key(Key::Num9));
    key_bit(keyboard_, 4, 0x02, in.key(Key::I));
    key_bit(keyboard_, 4, 0x04, in.key(Key::J));
    key_bit(keyboard_, 4, 0x08, in.key(Key::Num0));
    key_bit(keyboard_, 4, 0x10, in.key(Key::M));
    key_bit(keyboard_, 4, 0x20, in.key(Key::K));
    key_bit(keyboard_, 4, 0x40, in.key(Key::O));
    key_bit(keyboard_, 4, 0x80, in.key(Key::N));

    key_bit(keyboard_, 5, 0x02, in.key(Key::P));
    key_bit(keyboard_, 5, 0x04, in.key(Key::L));
    key_bit(keyboard_, 5, 0x08, in.key(Key::Minus));
    key_bit(keyboard_, 5, 0x10, in.key(Key::Period));
    key_bit(keyboard_, 5, 0x20, in.key(Key::Semicolon) && rshift);  // :
    key_bit(keyboard_, 5, 0x80, in.key(Key::Comma));

    key_bit(keyboard_, 6, 0x04, in.key(Key::Semicolon) && !rshift);
    key_bit(keyboard_, 6, 0x10, rshift);
    key_bit(keyboard_, 6, 0x80, in.key(Key::Slash));

    key_bit(keyboard_, 7, 0x01, in.key(Key::Num1) && !rshift);
    key_bit(keyboard_, 7, 0x04, in.key(Key::LeftCtrl));
    key_bit(keyboard_, 7, 0x08, in.key(Key::Num2));
    key_bit(keyboard_, 7, 0x10, in.key(Key::Space));
    key_bit(keyboard_, 7, 0x40, in.key(Key::Q));
    key_bit(keyboard_, 7, 0x80, in.key(Key::Escape) || in.key(Key::Tab));

    // CRSR left/up need C64 right-shift as well as the CRSR key.
    if (in.key(Key::Left) || in.key(Key::Up)) key_bit(keyboard_, 6, 0x10, true);
}

void C64::set_inputs(const MachineInputs& inputs) {
    apply_keyboard(inputs);
    auto joy = [](const InputState& p) {
        uint8_t v = 0xff;
        if (p.up) v = uint8_t(v & 0xfe);
        if (p.down) v = uint8_t(v & 0xfd);
        if (p.left) v = uint8_t(v & 0xfb);
        if (p.right) v = uint8_t(v & 0xf7);
        if (p.button1) v = uint8_t(v & 0xef);
        return v;
    };
    cia1_.joystick1 = joy(inputs.player1);
    cia1_.joystick2 = joy(inputs.player2);
}

void C64::inject_prg_payload(uint16_t address, const uint8_t* data, size_t size) {
    for (size_t i = 0; i < size; i++) {
        const uint32_t dest = uint32_t(address) + uint32_t(i);
        if (dest > 0xffff) break;
        ram_[dest] = data[i];
    }
    const uint16_t end = uint16_t(address + size);
    if (address == 0x0801) {
        ram_[0x2d] = uint8_t(end);
        ram_[0x2e] = uint8_t(end >> 8);
        ram_[0x2f] = ram_[0x2d];
        ram_[0x30] = ram_[0x2e];
        ram_[0x31] = ram_[0x2d];
        ram_[0x32] = ram_[0x2e];
    }
}

bool C64::load_prg(const uint8_t* data, size_t size, std::string* error) {
    if (size < 3) {
        if (error) *error = "PRG too small";
        return false;
    }
    const uint16_t address = uint16_t(data[0] | (uint16_t(data[1]) << 8));
    inject_prg_payload(address, data + 2, size - 2);
    return true;
}

bool C64::load_t64(const uint8_t* data, size_t size, std::string* error) {
    if (size < 96) {
        if (error) *error = "T64 too small";
        return false;
    }
    const int max_entries = data[34] | (data[35] << 8);
    for (int i = 0; i < max_entries; i++) {
        const size_t off = 64 + size_t(i) * 32;
        if (off + 32 > size) break;
        if (data[off] == 0) continue;
        if (data[off + 1] != 1 && data[off + 1] != 0x82 && data[off + 1] != 0x81) continue;
        const uint16_t start = uint16_t(data[off + 2] | (uint16_t(data[off + 3]) << 8));
        const uint16_t end = uint16_t(data[off + 4] | (uint16_t(data[off + 5]) << 8));
        const uint32_t file_off = uint32_t(data[off + 8]) | (uint32_t(data[off + 9]) << 8) |
                                  (uint32_t(data[off + 10]) << 16) | (uint32_t(data[off + 11]) << 24);
        size_t payload = (end > start) ? size_t(end - start) : 0;
        if (file_off >= size) continue;
        if (file_off + payload > size) payload = size - file_off;
        if (payload == 0) continue;
        inject_prg_payload(start, data + file_off, payload);
        return true;
    }
    if (error) *error = "T64 has no PRG entry";
    return false;
}

bool C64::load_tap(const uint8_t* data, size_t size, std::string* error) {
    if (size < 0x14 || std::memcmp(data, "C64-TAPE-RAW", 12) != 0) {
        if (error) *error = "not a C64 TAP image";
        return false;
    }
    tape_version_ = data[0x0c];
    const uint32_t data_size = uint32_t(data[0x10]) | (uint32_t(data[0x11]) << 8) |
                               (uint32_t(data[0x12]) << 16) | (uint32_t(data[0x13]) << 24);
    size_t start = 0x14;
    size_t count = data_size;
    if (start + count > size) count = size - start;
    tape_data_.assign(data + start, data + start + count);
    tape_pos_ = 0;
    tape_cycles_left_ = 0;
    tape_level_ = 0;
    tape_loaded_ = true;
    tape_playing_ = false;
    tape_control_ = 0x10;
    return true;
}

bool C64::load_d64(const uint8_t* data, size_t size, std::string* error) {
    // No 1541: inject the first closed PRG from the directory (track 18/sector 1).
    constexpr int kSectors[35] = {21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21,
                                  19, 19, 19, 19, 19, 19, 19, 18, 18, 18, 18, 18, 18, 17, 17, 17, 17, 17};
    auto sector_off = [&](int track, int sector) -> size_t {
        size_t off = 0;
        for (int t = 1; t < track; t++) off += size_t(kSectors[t - 1]) * 256;
        return off + size_t(sector) * 256;
    };
    if (size < sector_off(18, 1) + 256) {
        if (error) *error = "D64 too small";
        return false;
    }
    int dir_track = 18, dir_sector = 1;
    for (int safety = 0; safety < 20 && dir_track >= 1 && dir_track <= 35; safety++) {
        const size_t dir = sector_off(dir_track, dir_sector);
        if (dir + 256 > size) break;
        for (int e = 0; e < 8; e++) {
            const size_t entry = dir + size_t(e) * 32;
            const uint8_t type = data[entry + 2];
            if ((type & 0x0f) != 0x02) continue;  // PRG
            if ((type & 0x80) == 0) continue;     // not closed
            int track = data[entry + 3];
            int sector = data[entry + 4];
            std::vector<uint8_t> payload;
            for (int hops = 0; hops < 1024 && track >= 1 && track <= 35; hops++) {
                const size_t off = sector_off(track, sector);
                if (off + 256 > size) break;
                const int next_t = data[off];
                const int next_s = data[off + 1];
                const int used = (next_t == 0) ? next_s : 254;
                if (used > 0) payload.insert(payload.end(), data + off + 2, data + off + 2 + used);
                if (next_t == 0) break;
                track = next_t;
                sector = next_s;
            }
            if (payload.size() < 3) continue;
            const uint16_t address = uint16_t(payload[0] | (uint16_t(payload[1]) << 8));
            inject_prg_payload(address, payload.data() + 2, payload.size() - 2);
            return true;
        }
        dir_track = data[dir];
        dir_sector = data[dir + 1];
        if (dir_track == 0) break;
    }
    if (error) *error = "D64 has no PRG file";
    return false;
}

bool C64::load_media(const std::string& path, std::string* error) {
    std::vector<uint8_t> data;
    if (!read_whole_file(path, data, error)) return false;
    const std::string lower = lower_copy(path);
    if (ends_with(lower, ".prg")) return load_prg(data.data(), data.size(), error);
    if (ends_with(lower, ".t64")) return load_t64(data.data(), data.size(), error);
    if (ends_with(lower, ".tap")) return load_tap(data.data(), data.size(), error);
    if (ends_with(lower, ".d64") || ends_with(lower, ".g64")) return load_d64(data.data(), data.size(), error);
    if (error) *error = "unsupported media (use .prg / .t64 / .tap / .d64): " + path;
    return false;
}

void C64::tape_toggle_play() {
    if (!tape_loaded_) return;
    tape_playing_ = !tape_playing_;
    tape_control_ = tape_playing_ ? uint8_t(0x00) : uint8_t(0x10);
    if (!tape_playing_) tape_level_ = 0;
}

void C64::advance_tape(int cycles) {
    tape_cycles_left_ -= cycles;
    while (tape_cycles_left_ <= 0 && tape_pos_ < tape_data_.size()) {
        const uint8_t pulse = tape_data_[tape_pos_++];
        int duration = 0;
        if (tape_version_ >= 1 && pulse == 0) {
            if (tape_pos_ + 2 >= tape_data_.size()) break;
            duration = int(tape_data_[tape_pos_]) | (int(tape_data_[tape_pos_ + 1]) << 8) |
                       (int(tape_data_[tape_pos_ + 2]) << 16);
            tape_pos_ += 3;
        } else {
            duration = int(pulse) * 8;
        }
        if (duration <= 0) duration = 8;
        tape_level_ = uint8_t(tape_level_ ^ 1);
        tape_cycles_left_ += duration;
    }
    if (tape_pos_ >= tape_data_.size() && tape_cycles_left_ <= 0) {
        tape_playing_ = false;
        tape_control_ = 0x10;
    }
    cia1_.flag_w(tape_level_);
}

}  // namespace dsp
