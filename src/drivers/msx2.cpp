#include "drivers/msx2.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <unordered_map>

#include "core/rom_loader.h"

namespace dsp {
namespace {

std::string ascii_lower(std::string value) {
    for (char& c : value) c = char(std::tolower(static_cast<unsigned char>(c)));
    return value;
}

bool ends_ci(const std::string& path, const char* ext) {
    const std::string lower = ascii_lower(path);
    const size_t n = std::strlen(ext);
    return lower.size() >= n && lower.compare(lower.size() - n, n, ext) == 0;
}

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

bool skip_dir(const std::string& name) {
    const std::string n = ascii_lower(name);
    return n == ".git" || n == "build" || n == "cmakefiles" || n == "node_modules";
}

const uint8_t kCasHeader[8] = {0x1f, 0xa6, 0xde, 0xba, 0xcc, 0x13, 0x7d, 0x74};

const uint8_t kDpb720[18] = {
    0xf9, 0x00, 0x02, 0x0f, 0x04, 0x01, 0x02, 0x01, 0x00,
    0x02, 0x70, 0x0e, 0x00, 0x5a, 0x02, 0x03, 0x07, 0x00,
};

}  // namespace

Msx2::Msx2() : z80_(kMainClock), ay8910_(kMainClock / 2, 0.8f) {
    z80_.set_memory_handlers([this](uint16_t a) { return read_byte(a); },
                             [this](uint16_t a, uint8_t v) { write_byte(a, v); });
    z80_.set_io_handlers([this](uint16_t p) { return read_port(p); },
                         [this](uint16_t p, uint8_t v) { write_port(p, v); });
    z80_.set_cycle_handler([this](int cycles) { on_main_cycles(cycles); });
    z80_.set_instruction_hook([this](uint16_t pc) { on_instruction(pc); });
    ay8910_.set_port_handlers([this] { return joy1_; }, nullptr, nullptr, nullptr);
}

bool Msx2::init(const std::string& rom_path, std::string* error) {
    if (!load_bios(rom_path, error)) return false;
    reset();
    return true;
}

void Msx2::reset() {
    ram_.fill(0);
    keyboard_.fill(0xff);
    joy1_ = 0x3f;
    ppi_c_ = 0;
    primary_sel_ = 0xf0;
    secondary_sel_.fill(0);
    expanded_ = {true, false, false, true};
    mapper_reg_ = {3, 2, 1, 0};
    rtc_addr_ = 0;
    rtc_mode_ = 0;
    std::memset(rtc_ram_, 0, sizeof(rtc_ram_));
    vdp_.reset();
    ay8910_.reset();
    audio_.clear();
    audio_accumulator_ = 0;
    update_pages();
    z80_.reset();
}

void Msx2::update_pages() {
    for (int page = 0; page < 4; page++) {
        const int ps = (primary_sel_ >> (page * 2)) & 3;
        int ss = 0;
        if (expanded_[size_t(ps)]) ss = (secondary_sel_[size_t(ps)] >> (page * 2)) & 3;
        rd_[size_t(page)] = ram_.data();
        wr_[size_t(page)] = nullptr;

        if (ps == 0 && ss == 0) {
            if (page < 2) rd_[size_t(page)] = bios_.data() + page * 0x4000;
        } else if (ps == 0 && ss == 1) {
            if (page == 0) rd_[size_t(page)] = subrom_.data();
        } else if (ps == 1) {
            if (!cart_.empty() && cart_mapper_ == Mapper::None) {
                if (cart_.size() <= 0x4000) {
                    if (page == 1) rd_[size_t(page)] = cart_.data();
                } else if (cart_.size() <= 0x8000) {
                    if (page == 1) rd_[size_t(page)] = cart_.data();
                    else if (page == 2) rd_[size_t(page)] = cart_.data() + 0x4000;
                } else {
                    const uint32_t offset = uint32_t(page) * 0x4000;
                    if (offset < cart_.size()) rd_[size_t(page)] = cart_.data() + offset;
                }
            } else if (cart_.empty() || cart_mapper_ != Mapper::None) {
                if (cart_.empty()) {
                    // empty cartridge slot falls through to RAM for reads
                } else {
                    rd_[size_t(page)] = nullptr;  // MegaROM
                }
            }
        } else if (ps == 3) {
            auto map_ram = [&] {
                const int ram_page = mapper_reg_[size_t(page)] % kRamPages;
                rd_[size_t(page)] = ram_.data() + ram_page * 0x4000;
                wr_[size_t(page)] = rd_[size_t(page)];
            };
            if (!expanded_[3] || ss == 0 || ss >= 2) {
                map_ram();
            } else if (ss == 1) {
                if (page == 0) rd_[size_t(page)] = subrom_.data();
                else if (page == 1 && has_diskrom_) rd_[size_t(page)] = diskrom_.data();
                else map_ram();
            }
        }
    }
}

uint8_t Msx2::megarom_read(uint16_t address) const {
    if (cart_mapper_ == Mapper::Ascii16) {
        const int page16 = (address >> 14) & 1;
        const uint32_t base = uint32_t(cart_bank_[size_t(page16)]) * 0x4000;
        const uint32_t offset = address & 0x3fff;
        if (base + offset < cart_.size()) return cart_[base + offset];
        return 0xff;
    }
    const int bank_idx = int(address >> 13) - 2;
    if (bank_idx < 0 || bank_idx > 3) return 0xff;
    const uint32_t base = uint32_t(cart_bank_[size_t(bank_idx)]) * 0x2000;
    const uint32_t offset = address & 0x1fff;
    if (base + offset < cart_.size()) return cart_[base + offset];
    return 0xff;
}

void Msx2::megarom_write(uint16_t address, uint8_t value) {
    switch (cart_mapper_) {
        case Mapper::Ascii8:
            if (address >= 0x6000 && address < 0x6800) cart_bank_[0] = value;
            else if (address >= 0x6800 && address < 0x7000) cart_bank_[1] = value;
            else if (address >= 0x7000 && address < 0x7800) cart_bank_[2] = value;
            else if (address >= 0x7800 && address < 0x8000) cart_bank_[3] = value;
            break;
        case Mapper::Ascii16:
            if (address >= 0x6000 && address < 0x6800) cart_bank_[0] = value;
            else if (address >= 0x7000 && address < 0x7800) cart_bank_[1] = value;
            break;
        case Mapper::Konami:
            if (address >= 0x6000 && address < 0x8000) cart_bank_[1] = value;
            else if (address >= 0x8000 && address < 0xa000) cart_bank_[2] = value;
            else if (address >= 0xa000 && address < 0xc000) cart_bank_[3] = value;
            break;
        case Mapper::KonamiScc:
            if (address >= 0x5000 && address < 0x5800) cart_bank_[0] = value;
            else if (address >= 0x7000 && address < 0x7800) cart_bank_[1] = value;
            else if (address >= 0x9000 && address < 0x9800) cart_bank_[2] = value;
            else if (address >= 0xb000 && address < 0xb800) cart_bank_[3] = value;
            break;
        default:
            break;
    }
}

uint8_t Msx2::read_byte(uint16_t address) {
    if (address == 0xffff) {
        const int ps = (primary_sel_ >> 6) & 3;
        if (expanded_[size_t(ps)]) return uint8_t(~secondary_sel_[size_t(ps)]);
    }
    const int page = address >> 14;
    const int ps = (primary_sel_ >> (page * 2)) & 3;
    if (ps == 1 && cart_mapper_ != Mapper::None && !cart_.empty() && address >= 0x4000 &&
        address < 0xc000) {
        return megarom_read(address);
    }
    if (rd_[size_t(page)]) return rd_[size_t(page)][address & 0x3fff];
    return 0xff;
}

void Msx2::write_byte(uint16_t address, uint8_t value) {
    if (address == 0xffff) {
        const int ps = (primary_sel_ >> 6) & 3;
        if (expanded_[size_t(ps)]) {
            secondary_sel_[size_t(ps)] = value;
            update_pages();
            return;
        }
    }
    const int page = address >> 14;
    const int ps = (primary_sel_ >> (page * 2)) & 3;
    if (ps == 1 && cart_mapper_ != Mapper::None && !cart_.empty() && address >= 0x4000 &&
        address < 0xc000) {
        megarom_write(address, value);
        return;
    }
    if (wr_[size_t(page)]) wr_[size_t(page)][address & 0x3fff] = value;
}

uint8_t Msx2::read_port(uint16_t port) {
    switch (port & 0xff) {
        case 0x98:
        case 0x99:
        case 0x9a:
        case 0x9b:
            return vdp_.port_read((port & 0xff) - 0x98);
        case 0xa2:
            return ay8910_.read();
        case 0xa8:
            return primary_sel_;
        case 0xa9: {
            const int row = ppi_c_ & 0x0f;
            return row < 11 ? keyboard_[size_t(row)] : 0xff;
        }
        case 0xaa:
            return ppi_c_;
        case 0xb5:
            return rtc_read();
        case 0xfc:
        case 0xfd:
        case 0xfe:
        case 0xff:
            return mapper_reg_[(port & 0xff) - 0xfc];
        default:
            return 0xff;
    }
}

void Msx2::write_port(uint16_t port, uint8_t value) {
    switch (port & 0xff) {
        case 0x98:
            if (vdp_.command_busy() && (vdp_.command_op() == 0x0b || vdp_.command_op() == 0x0f)) {
                vdp_.command_write_byte(value);
            } else {
                vdp_.port_write(0, value);
            }
            break;
        case 0x99:
        case 0x9a:
        case 0x9b:
            vdp_.port_write((port & 0xff) - 0x98, value);
            break;
        case 0xa0:
            ay8910_.control(value);
            break;
        case 0xa1:
            ay8910_.write(value);
            break;
        case 0xa8:
            primary_sel_ = value;
            update_pages();
            break;
        case 0xaa:
            ppi_c_ = value;
            break;
        case 0xab:
            if ((value & 0x80) == 0) {
                const int bit = (value >> 1) & 7;
                if (value & 1) ppi_c_ = uint8_t(ppi_c_ | (1 << bit));
                else ppi_c_ = uint8_t(ppi_c_ & ~(1 << bit));
            }
            break;
        case 0xb4:
            rtc_addr_ = value & 0x0f;
            break;
        case 0xb5:
            if (rtc_addr_ == 13) {
                rtc_mode_ = value & 0x0f;
            } else if (rtc_addr_ < 13 && (rtc_mode_ & 3) >= 2) {
                rtc_ram_[rtc_mode_ & 3][rtc_addr_] = value & 0x0f;
            }
            break;
        case 0xfc:
        case 0xfd:
        case 0xfe:
        case 0xff:
            mapper_reg_[(port & 0xff) - 0xfc] = value;
            update_pages();
            break;
        default:
            break;
    }
}

uint8_t Msx2::rtc_read() const {
    const int block = rtc_mode_ & 3;
    const int reg = rtc_addr_ & 0x0f;
    if (reg == 13) return rtc_mode_;
    if (reg >= 13) return 0x0f;
    if (block == 0) {
        const std::time_t now = std::time(nullptr);
        const std::tm* t = std::localtime(&now);
        if (t == nullptr) return 0;
        switch (reg) {
            case 0: return uint8_t(t->tm_sec % 10);
            case 1: return uint8_t(t->tm_sec / 10);
            case 2: return uint8_t(t->tm_min % 10);
            case 3: return uint8_t(t->tm_min / 10);
            case 4: return uint8_t(t->tm_hour % 10);
            case 5: return uint8_t(t->tm_hour / 10);
            case 6: return uint8_t(t->tm_wday);
            case 7: return uint8_t(t->tm_mday % 10);
            case 8: return uint8_t(t->tm_mday / 10);
            case 9: return uint8_t((t->tm_mon + 1) % 10);
            case 10: return uint8_t((t->tm_mon + 1) / 10);
            case 11: return uint8_t((t->tm_year % 100) % 10);
            case 12: return uint8_t(((t->tm_year % 100) / 10) & 0x0f);
            default: return 0;
        }
    }
    return rtc_ram_[block][reg];
}

void Msx2::bios_ret() {
    const uint16_t ret = uint16_t(read_byte(z80_.sp) | (read_byte(uint16_t(z80_.sp + 1)) << 8));
    z80_.sp = uint16_t(z80_.sp + 2);
    z80_.set_pc(ret);
}

void Msx2::on_instruction(uint16_t pc) { trap_bios(pc); }

bool Msx2::trap_bios(uint16_t pc) {
    if ((primary_sel_ & 3) == 0 && !cas_.empty()) {
        if (pc == 0x00e1 || pc == 0x00e4 || pc == 0x00e7) {
            if (pc == 0x00e1) {
                bool found = false;
                while (cas_pos_ + 8 <= cas_.size()) {
                    if (cas_pos_ & 7) {
                        cas_pos_ = (cas_pos_ + 7) & ~7u;
                        continue;
                    }
                    if (std::memcmp(cas_.data() + cas_pos_, kCasHeader, 8) == 0) {
                        cas_pos_ += 8;
                        found = true;
                        break;
                    }
                    cas_pos_ += 8;
                }
                if (found) z80_.f = uint8_t(z80_.f & ~Z80::CF);
                else z80_.f = uint8_t(z80_.f | Z80::CF);
                z80_.a = 0;
            } else if (pc == 0x00e4) {
                bool ok = false;
                uint8_t byte = 0;
                if (cas_pos_ < cas_.size()) {
                    if (!(cas_pos_ + 8 <= cas_.size() && (cas_pos_ & 7) == 0 &&
                          std::memcmp(cas_.data() + cas_pos_, kCasHeader, 8) == 0)) {
                        byte = cas_[cas_pos_++];
                        ok = true;
                    }
                }
                if (ok) {
                    z80_.a = byte;
                    z80_.f = uint8_t(z80_.f & ~Z80::CF);
                } else {
                    z80_.f = uint8_t(z80_.f | Z80::CF);
                }
            } else {
                z80_.f = uint8_t(z80_.f & ~Z80::CF);
            }
            bios_ret();
            return true;
        }
    }
    if (has_diskrom_ && !dsk_.empty()) {
        const int ps1 = (primary_sel_ >> 2) & 3;
        const int ss1 = expanded_[size_t(ps1)] ? (secondary_sel_[size_t(ps1)] >> 2) & 3 : 0;
        if (ps1 == 3 && ss1 == 1) {
            if (pc == 0x4010) {
                trap_dskio();
                bios_ret();
                return true;
            }
            if (pc == 0x4013) {
                z80_.b = 0;
                z80_.f = uint8_t(z80_.f & ~Z80::CF);
                bios_ret();
                return true;
            }
            if (pc == 0x4016) {
                trap_getdpb();
                bios_ret();
                return true;
            }
        }
    }
    return false;
}

void Msx2::trap_dskio() {
    const bool write = (z80_.f & Z80::CF) != 0;
    const int nsectors = z80_.b;
    const int sector = z80_.e | (z80_.d << 8);
    uint16_t buf = uint16_t((z80_.h << 8) | z80_.l);
    for (int i = 0; i < nsectors; i++) {
        const uint32_t offset = uint32_t(sector + i) * 512;
        if (offset + 512 > dsk_.size()) {
            z80_.a = 6;
            z80_.b = uint8_t(nsectors - i);
            z80_.f = uint8_t(z80_.f | Z80::CF);
            return;
        }
        if (write) {
            for (int j = 0; j < 512; j++) dsk_[offset + uint32_t(j)] = read_byte(uint16_t(buf + j));
        } else {
            for (int j = 0; j < 512; j++) write_byte(uint16_t(buf + j), dsk_[offset + uint32_t(j)]);
        }
        buf = uint16_t(buf + 512);
    }
    z80_.a = 0;
    z80_.b = 0;
    z80_.f = uint8_t(z80_.f & ~Z80::CF);
}

void Msx2::trap_getdpb() {
    const uint16_t addr = uint16_t(((z80_.h << 8) | z80_.l) + 1);
    for (int i = 0; i < int(sizeof(kDpb720)); i++) write_byte(uint16_t(addr + i), kDpb720[i]);
    z80_.f = uint8_t(z80_.f & ~Z80::CF);
}

void Msx2::on_main_cycles(int cycles) {
    audio_accumulator_ += int64_t(cycles) * AY8910::kSampleRate;
    while (audio_accumulator_ >= kMainClock) {
        audio_accumulator_ -= kMainClock;
        int32_t sample = ay8910_.update();
        if (ppi_c_ & 0x80) sample += 2000;
        if (sample > 32767) sample = 32767;
        if (sample < -32768) sample = -32768;
        audio_.push_back(int16_t(sample));
    }
}

void Msx2::run_frame() {
    vdp_.begin_frame();
    if (vdp_.irq_pending()) z80_.set_irq(IrqLine::Hold);
    else z80_.set_irq(IrqLine::Clear);
    for (int line = 0; line < kScanlines; line++) {
        z80_.run(kCyclesPerLine);
        if (line < V9938::kScreenHeight) vdp_.render_line(line);
        vdp_.check_line_irq(line);
        if (vdp_.irq_pending()) z80_.set_irq(IrqLine::Hold);
    }
}

void Msx2::drain_audio(std::vector<int16_t>& out) {
    out.swap(audio_);
    audio_.clear();
}

void Msx2::set_dip_switch(int, uint8_t) {}

void Msx2::set_inputs(const MachineInputs& inputs) {
    keyboard_.fill(0xff);
    const bool rshift = inputs.key(Key::RightShift);
    if (inputs.key(Key::Num0)) keyboard_[0] &= 0xfe;
    if (inputs.key(Key::Num1) && !rshift) keyboard_[0] &= 0xfd;
    if (inputs.key(Key::Num2) && !rshift) keyboard_[0] &= 0xfb;
    if (inputs.key(Key::Num3) && !rshift) keyboard_[0] &= 0xf7;
    if (inputs.key(Key::Num4) && !rshift) keyboard_[0] &= 0xef;
    if (inputs.key(Key::Num5) && !rshift) keyboard_[0] &= 0xdf;
    if (inputs.key(Key::Num6)) keyboard_[0] &= 0xbf;
    if (inputs.key(Key::Num7)) keyboard_[0] &= 0x7f;
    if (inputs.key(Key::Num8)) keyboard_[1] &= 0xfe;
    if (inputs.key(Key::Num9)) keyboard_[1] &= 0xfd;
    if (inputs.key(Key::Minus)) keyboard_[1] &= 0xfb;
    if (inputs.key(Key::Slash)) keyboard_[1] &= 0xef;
    if (inputs.key(Key::A)) keyboard_[2] &= 0xbf;
    if (inputs.key(Key::B)) keyboard_[2] &= 0x7f;
    if (inputs.key(Key::C)) keyboard_[3] &= 0xfe;
    if (inputs.key(Key::D)) keyboard_[3] &= 0xfd;
    if (inputs.key(Key::E)) keyboard_[3] &= 0xfb;
    if (inputs.key(Key::F)) keyboard_[3] &= 0xf7;
    if (inputs.key(Key::G)) keyboard_[3] &= 0xef;
    if (inputs.key(Key::H)) keyboard_[3] &= 0xdf;
    if (inputs.key(Key::I)) keyboard_[3] &= 0xbf;
    if (inputs.key(Key::J)) keyboard_[3] &= 0x7f;
    if (inputs.key(Key::K)) keyboard_[4] &= 0xfe;
    if (inputs.key(Key::L)) keyboard_[4] &= 0xfd;
    if (inputs.key(Key::M)) keyboard_[4] &= 0xfb;
    if (inputs.key(Key::N)) keyboard_[4] &= 0xf7;
    if (inputs.key(Key::O)) keyboard_[4] &= 0xef;
    if (inputs.key(Key::P)) keyboard_[4] &= 0xdf;
    if (inputs.key(Key::Q)) keyboard_[4] &= 0xbf;
    if (inputs.key(Key::R)) keyboard_[4] &= 0x7f;
    if (inputs.key(Key::S)) keyboard_[5] &= 0xfe;
    if (inputs.key(Key::T)) keyboard_[5] &= 0xfd;
    if (inputs.key(Key::U)) keyboard_[5] &= 0xfb;
    if (inputs.key(Key::V)) keyboard_[5] &= 0xf7;
    if (inputs.key(Key::W)) keyboard_[5] &= 0xef;
    if (inputs.key(Key::X)) keyboard_[5] &= 0xdf;
    if (inputs.key(Key::Y)) keyboard_[5] &= 0xbf;
    if (inputs.key(Key::Z)) keyboard_[5] &= 0x7f;
    if (inputs.key(Key::LeftShift) || rshift) keyboard_[6] &= 0xfe;
    if (inputs.key(Key::LeftCtrl) || inputs.key(Key::RightCtrl)) keyboard_[6] &= 0xfd;
    if (inputs.key(Key::CapsLock)) keyboard_[6] &= 0xf7;
    if (inputs.key(Key::F1)) keyboard_[6] &= 0xdf;
    if (inputs.key(Key::F2)) keyboard_[6] &= 0xbf;
    if (inputs.key(Key::F3)) keyboard_[6] &= 0x7f;
    if (inputs.key(Key::F4)) keyboard_[7] &= 0xfe;
    if (inputs.key(Key::F5)) keyboard_[7] &= 0xfd;
    if (inputs.key(Key::Escape)) keyboard_[7] &= 0xfb;
    if (inputs.key(Key::Tab)) keyboard_[7] &= 0xf7;
    if (inputs.key(Key::Backspace)) keyboard_[7] &= 0xdf;
    if (inputs.key(Key::Enter)) keyboard_[7] &= 0x7f;
    if (inputs.key(Key::Space)) keyboard_[8] &= 0xfe;
    if (inputs.key(Key::Left)) keyboard_[8] &= 0xef;
    if (inputs.key(Key::Up)) keyboard_[8] &= 0xdf;
    if (inputs.key(Key::Down)) keyboard_[8] &= 0xbf;
    if (inputs.key(Key::Right)) keyboard_[8] &= 0x7f;

    joy1_ = 0x3f;
    const InputState& p = inputs.player1;
    if (p.up) joy1_ &= 0xfe;
    if (p.down) joy1_ &= 0xfd;
    if (p.left) joy1_ &= 0xfb;
    if (p.right) joy1_ &= 0xf7;
    if (p.button1) joy1_ &= 0xef;
    if (p.button2) joy1_ &= 0xdf;
}

bool Msx2::load_media(const std::string& path, std::string* error) {
    if (ends_ci(path, ".dsk") || ends_ci(path, ".img")) return load_dsk(path, error);
    if (ends_ci(path, ".cas")) return load_cas(path, error);
    if (ends_ci(path, ".rom") || ends_ci(path, ".mx1") || ends_ci(path, ".mx2") ||
        ends_ci(path, ".zip")) {
        return load_cartridge(path, error);
    }
    if (error) *error = "unsupported MSX2 media (use .rom/.dsk/.cas)";
    return false;
}

bool Msx2::load_cas(const std::string& path, std::string* error) {
    if (!load_file(path, cas_)) {
        if (error) *error = "cannot read CAS " + path;
        return false;
    }
    cas_pos_ = 0;
    return true;
}

bool Msx2::load_dsk(const std::string& path, std::string* error) {
    if (!load_file(path, dsk_)) {
        if (error) *error = "cannot read DSK " + path;
        return false;
    }
    if (dsk_.size() >= 8 && std::memcmp(dsk_.data(), "MV - CPC", 8) == 0) {
        if (error) *error = path + " is a CPC disk, not an MSX .dsk";
        dsk_.clear();
        return false;
    }
    return true;
}

bool Msx2::load_cartridge(const std::string& path, std::string* error) {
    RomLoader loader;
    std::string ignored;
    std::vector<uint8_t> blob;
    if (loader.open(path, &ignored) && loader.load_first_file(blob, error) && !blob.empty()) {
        cart_ = std::move(blob);
    } else if (!load_file(path, cart_)) {
        if (error) *error = "cannot read cartridge " + path;
        return false;
    }
    if (cart_.size() > kMaxCartridge) cart_.resize(kMaxCartridge);
    cart_mapper_ = detect_mapper(cart_.data(), uint32_t(cart_.size()));
    cart_bank_ = {0, 1, 2, 3};
    update_pages();
    return true;
}

Msx2::Mapper Msx2::detect_mapper(const uint8_t* data, uint32_t size) {
    if (size <= 65536) return Mapper::None;
    int ascii8 = 0, ascii16 = 0, konami = 0, scc = 0;
    for (uint32_t i = 0; i + 2 < size; i++) {
        if (data[i] != 0x32) continue;
        const uint16_t addr = uint16_t(data[i + 1] | (data[i + 2] << 8));
        if (addr >= 0x6000 && addr < 0x8000) ascii8++;
        if (addr >= 0x7000 && addr < 0x7800) ascii16++;
        if (addr >= 0x5000 && addr < 0x5800) scc++;
        if (addr >= 0x7000 && addr < 0x7800) scc++;
        if (addr >= 0x9000 && addr < 0x9800) scc++;
        if (addr >= 0xb000 && addr < 0xb800) scc++;
        if (addr >= 0x6000 && addr < 0xc000) konami++;
    }
    if (scc > ascii8 && scc > konami) return Mapper::KonamiScc;
    if (konami > ascii8 && konami > ascii16) return Mapper::Konami;
    if (ascii16 > ascii8) return Mapper::Ascii16;
    return Mapper::Ascii8;
}

bool Msx2::load_bios(const std::string& path, std::string* error) {
    namespace fs = std::filesystem;
    std::unordered_map<std::string, fs::path> files;
    auto remember = [&](const fs::path& file) {
        files.emplace(ascii_lower(file.filename().string()), file);
    };
    std::error_code ec;
    fs::path root = path;
    if (ends_ci(path, ".dsk") || ends_ci(path, ".rom") || ends_ci(path, ".cas") ||
        ends_ci(path, ".zip")) {
        root = fs::path(path).parent_path();
        if (root.empty()) root = ".";
    }
    if (fs::is_regular_file(root, ec)) remember(root);
    else if (fs::is_directory(root, ec)) {
        fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec);
        int visited = 0;
        for (; it != fs::recursive_directory_iterator(); it.increment(ec)) {
            if (ec) {
                ec.clear();
                continue;
            }
            if (++visited > 20000) break;
            if (it->is_directory(ec)) {
                if (skip_dir(it->path().filename().string()) || it.depth() >= 6) {
                    it.disable_recursion_pending();
                }
                continue;
            }
            if (!it->is_regular_file(ec)) continue;
            const std::string name = it->path().filename().string();
            if (ends_ci(name, ".rom") || ends_ci(name, ".bin")) remember(it->path());
        }
    }

    auto take = [&](std::initializer_list<const char*> names, uint8_t* dest, size_t max) -> bool {
        for (const char* name : names) {
            const auto it = files.find(ascii_lower(name));
            if (it == files.end()) continue;
            std::vector<uint8_t> blob;
            if (!load_file(it->second.string(), blob) || blob.empty()) continue;
            const size_t n = std::min(max, blob.size());
            std::memcpy(dest, blob.data(), n);
            return true;
        }
        RomLoader loader;
        std::string ignored;
        if (loader.open(path, &ignored)) {
            for (const char* name : names) {
                std::vector<uint8_t> blob;
                if (loader.try_read(name, blob) && blob.size() >= max / 2) {
                    std::memcpy(dest, blob.data(), std::min(max, blob.size()));
                    return true;
                }
            }
        }
        return false;
    };

    bios_.fill(0);
    subrom_.fill(0);
    diskrom_.fill(0);
    has_diskrom_ = false;
    const bool have_main = take({"MSX2.ROM", "msx2bios.rom", "msx2_bios.rom", "cbios_main_msx2.rom",
                                 "cbios_main_msx2_eu.rom"},
                                bios_.data(), bios_.size());
    const bool have_sub = take({"MSX2EXT.ROM", "MSX2SUB.ROM", "msx2_ext.rom", "cbios_sub.rom"},
                               subrom_.data(), subrom_.size());
    has_diskrom_ = take({"DISK.ROM", "diskrom.rom"}, diskrom_.data(), diskrom_.size());
    if (!has_diskrom_) {
        bool cbios = false;
        for (size_t i = 0; i + 6 < bios_.size(); i++) {
            if (std::memcmp(bios_.data() + i, "C-BIOS", 6) == 0) {
                cbios = true;
                break;
            }
        }
        if (cbios) has_diskrom_ = take({"cbios_disk.rom"}, diskrom_.data(), diskrom_.size());
    }
    if (!have_main || !have_sub) {
        if (error) {
            *error = "MSX2 BIOS not found in " + path +
                     " (need MSX2.ROM/msx2_bios.rom and MSX2EXT.ROM/msx2_ext.rom)";
        }
        return false;
    }
    return true;
}

}  // namespace dsp
