#include "drivers/computers/atari8.h"

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

bool find_rom(const std::string& dir, const char* name, std::vector<uint8_t>& out) {
    namespace fs = std::filesystem;
    if (load_file((fs::path(dir) / name).string(), out)) return true;
    std::string upper = name;
    for (char& c : upper) c = char(std::toupper(static_cast<unsigned char>(c)));
    return load_file((fs::path(dir) / upper).string(), out);
}

// Atari keyboard scan codes as they appear in POKEY's KBCODE register.
// Index is the code, value the host key it corresponds to.
struct KeyEntry { Key host; uint8_t code; };
constexpr KeyEntry kKeyTable[] = {
    {Key::L, 0x00}, {Key::J, 0x01}, {Key::Semicolon, 0x02},
    {Key::K, 0x05}, {Key::Plus, 0x06},
    {Key::O, 0x08}, {Key::P, 0x0a}, {Key::U, 0x0b},
    {Key::Enter, 0x0c}, {Key::I, 0x0d}, {Key::Minus, 0x0e}, {Key::Equals, 0x0f},
    {Key::V, 0x10}, {Key::C, 0x12}, {Key::B, 0x15}, {Key::X, 0x16}, {Key::Z, 0x17},
    {Key::Num4, 0x18}, {Key::Num3, 0x1a}, {Key::Num6, 0x1b},
    {Key::Escape, 0x1c}, {Key::Num5, 0x1d}, {Key::Num2, 0x1e}, {Key::Num1, 0x1f},
    {Key::Comma, 0x20}, {Key::Space, 0x21}, {Key::Period, 0x22}, {Key::N, 0x23},
    {Key::M, 0x25}, {Key::Slash, 0x26},
    {Key::R, 0x28}, {Key::E, 0x2a}, {Key::Y, 0x2b},
    {Key::Tab, 0x2c}, {Key::T, 0x2d}, {Key::W, 0x2e}, {Key::Q, 0x2f},
    {Key::Num9, 0x30}, {Key::Num0, 0x32}, {Key::Num7, 0x33},
    {Key::Backspace, 0x34}, {Key::Num8, 0x35},
    {Key::F, 0x38}, {Key::H, 0x39}, {Key::D, 0x3a},
    {Key::CapsLock, 0x3c}, {Key::G, 0x3d}, {Key::S, 0x3e}, {Key::A, 0x3f},
};

constexpr uint16_t kSiovVector = 0xe459;

}  // namespace

Atari8::Atari8(Model model)
    : cpu_(kClock), antic_(gtia_), pokey_(kClock), model_(model) {}

const char* Atari8::title() const {
    switch (model_) {
        case Model::A800XL: return "Atari 800XL";
        case Model::A800XE: return "Atari 800XE";
        default: return "Atari 800";
    }
}

bool Atari8::init(const std::string& rom_path, std::string* error) {
    namespace fs = std::filesystem;
    std::string dir = rom_path;
    std::error_code ec;
    if (!fs::is_directory(fs::path(rom_path), ec)) {
        dir = fs::path(rom_path).parent_path().string();
        if (dir.empty()) dir = ".";
    }

    if (model_ == Model::A800) {
        // The 400/800 OS lives in three chips: 4K + 4K + 2K covering
        // $D800-$FFFF. Prefer the OS-B revision when both are present.
        std::vector<uint8_t> a, b, c;
        const bool osb = find_rom(dir, "co12499b.rom", a) &&
                         find_rom(dir, "co14599b.rom", b);
        if (!osb) {
            if (!find_rom(dir, "co12499a.rom", a) || !find_rom(dir, "co14599a.rom", b)) {
                if (error) *error = "Atari 800 OS ROMs (co12499*/co14599*) not found in " + dir;
                return false;
            }
        }
        if (!find_rom(dir, "co12399b.rom", c) && !find_rom(dir, "co12399a.rom", c)) {
            if (error) *error = "Atari 800 OS ROM co12399b.rom not found in " + dir;
            return false;
        }
        if (a.size() < 0x1000 || b.size() < 0x1000 || c.size() < 0x800) {
            if (error) *error = "Atari 800 OS ROM parts have unexpected sizes";
            return false;
        }
        // Chip order by address, not by part number: CO12399 is the 2K
        // floating-point package at $D800-$DFFF, CO12499 the 4K at
        // $E000-$EFFF (which opens with the character set -- the blank
        // glyph for code 0 is the eight zero bytes at $E000), and CO14599
        // the 4K at $F000-$FFFF holding the reset vector.
        os_.clear();
        os_.insert(os_.end(), c.begin(), c.begin() + 0x800);
        os_.insert(os_.end(), a.begin(), a.begin() + 0x1000);
        os_.insert(os_.end(), b.begin(), b.begin() + 0x1000);
    } else {
        const char* os_name = (model_ == Model::A800XE) ? "c300717.rom" : "co61598b.rom";
        if (!find_rom(dir, os_name, os_) || os_.size() < 0x4000) {
            if (error) *error = std::string("Atari XL/XE OS ROM ") + os_name + " not found in " + dir;
            return false;
        }
        os_.resize(0x4000);
        const char* basic_name = (model_ == Model::A800XE) ? "co24947a.rom" : "co60302a.rom";
        if (find_rom(dir, basic_name, basic_) && basic_.size() >= 0x2000) {
            basic_.resize(0x2000);
            has_basic_ = true;
        }
    }

    cpu_.set_memory_handlers(
        [this](uint16_t a) { return cpu_read(a); },
        [this](uint16_t a, uint8_t v) { cpu_write(a, v); });
    antic_.set_memory_handler([this](uint16_t a) { return antic_read(a); });
    antic_.set_nmi_handler([this]() { cpu_.set_nmi(IrqLine::Pulse); });
    pokey_.set_irq_handler([this](uint8_t) {
        irq_line_ = true;
        cpu_.set_irq(IrqLine::Assert);
    });
    gtia_.set_console_handler([this]() {
        // Holding OPTION through cold start is how you tell the XL/XE OS
        // not to page in the built-in BASIC. Disk software regularly needs
        // that: a 128-sector boot loads $7000-$AFFF, which runs straight
        // through BASIC's $A000-$BFFF window, so with BASIC left enabled
        // the game's own code reads back as cartridge ROM and it crashes
        // as soon as execution reaches that far. Real users hold the key;
        // do it for them while a disk is mounted, until the OS has
        // finished sampling it.
        uint8_t c = console_;
        if (disk_loaded_ && boot_option_frames_ > 0) c = uint8_t(c & ~0x04);
        return c;
    });
    gtia_.set_trigger_handler([this](int n) { return (n < 2) ? trig_[n] : uint8_t(1); });

    reset();
    return true;
}

void Atari8::reset() {
    ram_.fill(0);
    porta_ = portb_ = 0xff;
    pactl_ = pbctl_ = 0;
    porta_dir_ = portb_dir_ = 0;
    // XL/XE come up with the OS mapped in; the 800 has no banking at all.
    os_enabled_ = true;
    basic_enabled_ = false;
    selftest_enabled_ = false;
    stick_[0] = stick_[1] = 0x0f;
    trig_[0] = trig_[1] = 1;
    console_ = 0x07;
    boot_option_frames_ = 120;  // ~2 s, plenty for the OS to sample OPTION
    irq_line_ = false;
    audio_.clear();
    audio_acc_ = 0;
    framebuffer_.fill(0xff000000u);
    gtia_.reset();
    antic_.reset();
    pokey_.reset();
    cpu_.reset();
}

void Atari8::update_banking() {
    if (model_ == Model::A800) return;  // no PORTB banking on the 800
    os_enabled_ = (portb_ & 0x01) != 0;
    basic_enabled_ = has_basic_ && ((portb_ & 0x02) == 0);
    selftest_enabled_ = (portb_ & 0x80) == 0;
}

uint8_t Atari8::antic_read(uint16_t address) {
    // ANTIC shares the address/data bus with the CPU, so display data can
    // come from ROM as well as RAM -- and normally does: the default
    // character set the OS points CHBASE at ($E000) lives inside the OS
    // ROM. Reading only RAM here made every character fetch return 0, so
    // the display list rendered correctly but every glyph came out blank.
    //
    // The one region deliberately not mirrored is the $D000-$D7FF hardware
    // window: those reads have side effects (clearing latches, triggering
    // pot scans) that a DMA fetch must not cause.
    if (address >= 0xd000 && address < 0xd800) return ram_[address];

    if (model_ == Model::A800) {
        if (address >= 0xd800) return os_[address - 0xd800];
    } else {
        if (os_enabled_ && (address >= 0xc000 && address < 0xd000)) return os_[address - 0xc000];
        if (os_enabled_ && address >= 0xd800) return os_[address - 0xc000];
        if (basic_enabled_ && address >= 0xa000 && address < 0xc000) return basic_[address - 0xa000];
    }
    return ram_[address];
}

uint8_t Atari8::cpu_read(uint16_t address) {
    // Serial I/O trap: rather than emulating the SIO bus bit by bit, catch
    // the OS entry point and fulfil the request from the disk image, then
    // hand the CPU an RTS so it returns to the caller. This is the same
    // "SIO patch" approach other Atari emulators offer, and it is what
    // makes disk software load without a cycle-exact drive model.
    //
    // Only intercept an actual instruction fetch. The XL/XE OS checksums
    // its own ROM during startup, which reads every byte including this
    // one; answering those data reads with $60 corrupts the checksum, the
    // OS decides the ROM is bad and drops into self-test instead of
    // booting. After `read(pc_++)` the program counter sits one past the
    // byte being fetched, so that is what distinguishes the two cases.
    // Note this must intercept even with no disk mounted. The patch is what
    // answers SIO at all -- the serial interrupts the OS's own SIO code
    // waits on are never generated here -- so leaving it off for a
    // diskless boot left the OS spinning in its timeout loop forever
    // instead of failing the call and starting the cartridge. With no
    // image loaded service_sio() reports $8A (device does not respond),
    // which is what a machine with no drive attached looks like.
    if (address == kSiovVector && cpu_.pc() == uint16_t(kSiovVector + 1) &&
        os_enabled_) {
        service_sio();
        return 0x60;  // RTS
    }

    if (address < 0xc000) {
        if (model_ != Model::A800 && basic_enabled_ && address >= 0xa000)
            return basic_[address - 0xa000];
        if (selftest_enabled_ && address >= 0x5000 && address < 0x5800)
            return os_[0x1000 + (address - 0x5000)];  // self-test mirrors OS $D000 area
        return ram_[address];
    }

    if (model_ == Model::A800) {
        if (address >= 0xd800) return os_[address - 0xd800];
    } else {
        if (address >= 0xc000 && address < 0xd000)
            return os_enabled_ ? os_[address - 0xc000] : ram_[address];
        if (address >= 0xd800)
            return os_enabled_ ? os_[address - 0xc000] : ram_[address];
    }

    // $D000-$D7FF hardware.
    switch (address & 0xff00) {
        case 0xd000: return gtia_.read(address & 0x1f);
        case 0xd200: return pokey_.read(address & 0x0f);
        case 0xd300: return pia_read(address & 3);
        case 0xd400: return antic_.read(address & 0x0f);
        default: return 0xff;
    }
}

void Atari8::cpu_write(uint16_t address, uint8_t value) {
    if (address < 0xc000) {
        // Writes always land in RAM even where ROM is banked over it.
        ram_[address] = value;
        return;
    }
    if (address >= 0xd800 || (model_ != Model::A800 && address < 0xd000)) {
        if (model_ != Model::A800 && !os_enabled_) ram_[address] = value;
        return;  // ROM is read-only
    }
    switch (address & 0xff00) {
        case 0xd000: gtia_.write(address & 0x1f, value); break;
        case 0xd200: pokey_.write(address & 0x0f, value); break;
        case 0xd300: pia_write(address & 3, value); break;
        case 0xd400: antic_.write(address & 0x0f, value); break;
        default: break;
    }
}

uint8_t Atari8::pia_read(uint16_t offset) {
    switch (offset & 3) {
        case 0:  // PORTA: joystick directions, or the data-direction register
            if (!(pactl_ & 0x04)) return porta_dir_;
            return uint8_t((stick_[0] & 0x0f) | uint8_t((stick_[1] & 0x0f) << 4));
        case 1:
            if (!(pbctl_ & 0x04)) return portb_dir_;
            if (model_ == Model::A800) return 0xff;  // 800: joysticks 3/4, none attached
            return portb_;
        case 2: return pactl_;
        default: return pbctl_;
    }
}

void Atari8::pia_write(uint16_t offset, uint8_t value) {
    switch (offset & 3) {
        case 0:
            if (pactl_ & 0x04) porta_ = value; else porta_dir_ = value;
            break;
        case 1:
            if (pbctl_ & 0x04) {
                portb_ = value;
                update_banking();
            } else {
                portb_dir_ = value;
            }
            break;
        case 2: pactl_ = value; break;
        default: pbctl_ = value; break;
    }
}

bool Atari8::load_media(const std::string& path, std::string* error) {
    std::vector<uint8_t> data;
    if (!load_file(path, data)) {
        if (error) *error = "cannot open disk image: " + path;
        return false;
    }
    if (data.size() < 16 || data[0] != 0x96 || data[1] != 0x02) {
        if (error) *error = "not an Atari .ATR disk image: " + path;
        return false;
    }
    disk_sector_size_ = int(data[4] | (uint16_t(data[5]) << 8));
    if (disk_sector_size_ != 128 && disk_sector_size_ != 256) {
        if (error) *error = "unsupported ATR sector size";
        return false;
    }
    const uint32_t paragraphs = uint32_t(data[2]) | (uint32_t(data[3]) << 8) |
                                (uint32_t(data[6]) << 16);
    const uint32_t bytes = paragraphs * 16;
    disk_sectors_ = (disk_sector_size_ == 128)
                        ? int(bytes / 128)
                        : int(3 + (bytes > 384 ? (bytes - 384) / 256 : 0));
    disk_ = std::move(data);
    disk_loaded_ = true;
    return true;
}

bool Atari8::disk_sector(int sector, std::vector<uint8_t>& out) const {
    if (!disk_loaded_ || sector < 1) return false;
    // Double-density images keep the first three sectors at 128 bytes,
    // which is how the boot loader expects to find them.
    size_t off, len;
    if (disk_sector_size_ == 128) {
        off = 16 + size_t(sector - 1) * 128;
        len = 128;
    } else if (sector <= 3) {
        off = 16 + size_t(sector - 1) * 128;
        len = 128;
    } else {
        off = 16 + 384 + size_t(sector - 4) * 256;
        len = 256;
    }
    if (off + len > disk_.size()) return false;
    out.assign(disk_.begin() + long(off), disk_.begin() + long(off + len));
    return true;
}

bool Atari8::disk_write_sector(int sector, const uint8_t* data, size_t len) {
    std::vector<uint8_t> tmp;
    if (!disk_sector(sector, tmp)) return false;
    size_t off;
    if (disk_sector_size_ == 128 || sector <= 3) off = 16 + size_t(sector - 1) * 128;
    else off = 16 + 384 + size_t(sector - 4) * 256;
    const size_t n = std::min(len, tmp.size());
    if (off + n > disk_.size()) return false;
    std::copy(data, data + n, disk_.begin() + long(off));
    return true;
}

void Atari8::service_sio() {
    // Device Control Block at $0300.
    const uint8_t device = ram_[0x0300];
    const uint8_t command = ram_[0x0302];
    const uint16_t buffer = uint16_t(ram_[0x0304] | (uint16_t(ram_[0x0305]) << 8));
    const uint16_t count = uint16_t(ram_[0x0308] | (uint16_t(ram_[0x0309]) << 8));
    const int sector = int(ram_[0x030a] | (uint16_t(ram_[0x030b]) << 8));

    uint8_t status = 0x8a;  // device does not respond
    // With no disk image mounted there is no drive on the bus at all, so
    // every command has to time out with $8A. Answering $90 ("record not
    // found") instead tells the OS a drive *is* present but the read
    // failed, and its boot code then retries forever rather than giving
    // up and starting the cartridge -- which is why a diskless boot never
    // reached BASIC's READY prompt.
    if (device == 0x31 && disk_loaded_) {
        switch (command) {
            case 0x52: {  // 'R' read sector
                std::vector<uint8_t> sec;
                if (disk_sector(sector, sec)) {
                    const size_t n = std::min<size_t>(count ? count : sec.size(), sec.size());
                    for (size_t i = 0; i < n; ++i) ram_[uint16_t(buffer + i)] = sec[i];
                    status = 0x01;
                } else {
                    status = 0x90;  // record not found
                }
                break;
            }
            case 0x50:    // 'P' put sector
            case 0x57: {  // 'W' write sector (with verify)
                std::vector<uint8_t> tmp(count ? count : size_t(disk_sector_size_));
                for (size_t i = 0; i < tmp.size(); ++i) tmp[i] = ram_[uint16_t(buffer + i)];
                status = disk_write_sector(sector, tmp.data(), tmp.size()) ? 0x01 : 0x90;
                break;
            }
            case 0x53: {  // 'S' status
                const uint8_t dd = (disk_sector_size_ == 256) ? 0x20 : 0x00;
                ram_[uint16_t(buffer + 0)] = uint8_t(0x10 | dd);
                ram_[uint16_t(buffer + 1)] = 0xff;
                ram_[uint16_t(buffer + 2)] = 0xe0;
                ram_[uint16_t(buffer + 3)] = 0x00;
                status = 0x01;
                break;
            }
            case 0x4e: {  // 'N' read percom block
                for (int i = 0; i < 12; ++i) ram_[uint16_t(buffer + i)] = 0;
                ram_[uint16_t(buffer + 0)] = 40;   // tracks
                ram_[uint16_t(buffer + 2)] = 0;    // sectors per track (hi)
                ram_[uint16_t(buffer + 3)] = 18;   // sectors per track (lo)
                ram_[uint16_t(buffer + 6)] = uint8_t(disk_sector_size_ >> 8);
                ram_[uint16_t(buffer + 7)] = uint8_t(disk_sector_size_ & 0xff);
                status = 0x01;
                break;
            }
            default: status = 0x8b; break;  // unsupported command
        }
    }

    ram_[0x0303] = status;
    cpu_.y = status;
    cpu_.p.n = (status & 0x80) != 0;
    cpu_.p.z = (status == 0);
}

void Atari8::apply_keys(const MachineInputs& in) {
    // Joystick 1 from the arrow keys, fire from left control; joystick
    // directions are active low in the low nibble (right/left/down/up).
    uint8_t s = 0x0f;
    if (in.key(Key::Up)) s = uint8_t(s & ~0x01);
    if (in.key(Key::Down)) s = uint8_t(s & ~0x02);
    if (in.key(Key::Left)) s = uint8_t(s & ~0x04);
    if (in.key(Key::Right)) s = uint8_t(s & ~0x08);
    stick_[0] = s;
    trig_[0] = in.key(Key::LeftCtrl) ? 0 : 1;

    console_ = 0x07;
    if (in.key(Key::F2)) console_ = uint8_t(console_ & ~0x01);  // START
    if (in.key(Key::F3)) console_ = uint8_t(console_ & ~0x02);  // SELECT
    if (in.key(Key::F4)) console_ = uint8_t(console_ & ~0x04);  // OPTION

    const bool shift = in.key(Key::LeftShift) || in.key(Key::RightShift);
    const bool ctrl = in.key(Key::RightCtrl);
    bool any = false;
    for (const auto& e : kKeyTable) {
        if (!in.key(e.host)) continue;
        uint8_t code = e.code;
        if (shift) code = uint8_t(code | 0x40);
        if (ctrl) code = uint8_t(code | 0x80);
        pokey_.set_key(code, true, shift);
        any = true;
        break;
    }
    if (!any) pokey_.set_key(0, false, shift);
}

void Atari8::set_inputs(const MachineInputs& inputs) { apply_keys(inputs); }

void Atari8::set_dip_switch(int, uint8_t) {}

void Atari8::on_cycles(int cycles) {
    pokey_.run(cycles);
    audio_acc_ += int64_t(cycles) * kSampleRate;
    while (audio_acc_ >= int64_t(kClock)) {
        audio_acc_ -= int64_t(kClock);
        const int32_t s = pokey_.update();
        audio_.push_back(int16_t(std::clamp<int32_t>(s, -32768, 32767)));
    }
}

void Atari8::run_frame() {
    if (boot_option_frames_ > 0) --boot_option_frames_;
    antic_.begin_frame();
    std::array<uint32_t, size_t(Antic::kScreenWidth)> offscreen{};

    for (int line = 0; line < Antic::kLinesPerFrame; ++line) {
        const int fb_line = line - Antic::kFirstVisibleLine;
        uint32_t* dst = (fb_line >= 0 && fb_line < Antic::kScreenHeight)
                            ? framebuffer_.data() + size_t(fb_line) * Antic::kScreenWidth
                            : offscreen.data();
        const int stolen = antic_.scanline(line, dst);

        antic_.clear_wsync();
        int budget = Antic::kCyclesPerLine - stolen;
        int done = 0;
        while (done < budget) {
            if (antic_.wsync_pending()) {
                // WSYNC parks the CPU until the next scanline begins.
                antic_.clear_wsync();
                break;
            }
            const int ran = cpu_.run(1);
            if (ran <= 0) break;
            done += ran;
        }
        on_cycles(Antic::kCyclesPerLine);
        if (irq_line_) {
            irq_line_ = false;
            cpu_.set_irq(IrqLine::Clear);
        }
    }
}

void Atari8::drain_audio(std::vector<int16_t>& out) {
    out.swap(audio_);
    audio_.clear();
}

}  // namespace dsp
