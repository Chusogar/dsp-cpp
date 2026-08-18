#include "drivers/apple2.h"

#include "cpu/irq_line.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>

#include "core/rom_loader.h"

namespace dsp {
namespace {

const std::vector<RomEntry> kIiPlusChips = {
    {"341-0011.d0", 0x0800, 0x0000, 0x6f05f949},
    {"341-0012.d8", 0x0800, 0x0800, 0x1f08087c},
    {"341-0013.e0", 0x0800, 0x1000, 0x2b8d9a89},
    {"341-0014.e8", 0x0800, 0x1800, 0x5719871a},
    {"341-0015.f0", 0x0800, 0x2000, 0x9a04eecf},
    {"341-0020-00.f8|341-0020.f8", 0x0800, 0x2800, 0x079589c4},
};
const std::vector<RomEntry> kIiPlusConcat = {
    {"apple2-asoft-auto.rom|apple2.rom|apple2plus.rom|apple2p.rom", 0x3000, 0x0000, 0},
};
const std::vector<RomEntry> kIiInteger = {
    {"apple2-int-auto.rom|apple2integer.rom", 0x2000, 0x0000, 0x2dcec5cb},
};
const std::vector<RomEntry> kIIeChips = {
    {"342-0135-b.64|342-0135-b.bin", 0x2000, 0x0000, 0xe248835e},
    {"342-0134-a.64|342-0134-a.bin", 0x2000, 0x2000, 0xfc3d59d8},
};
const std::vector<RomEntry> kIIeEnhancedChips = {
    {"342-0304-a.e10|342-0304-a.bin", 0x2000, 0x0000, 0x443aa7c4},
    {"342-0303-a.e8|342-0303-a.bin", 0x2000, 0x2000, 0x95e10034},
};
const std::vector<RomEntry> kIIeConcat = {
    {"AppleIIe.rom|apple2ee.rom|apple2e_enhanced.rom", 0x4000, 0x0000, 0},
};
const std::vector<RomEntry> kChargenIi = {
    {"341-0036.chr|apple2-character.rom|apple2.chr", 0x0800, 0x0000, 0x64f415c6},
};
const std::vector<RomEntry> kChargenIIe = {
    {"342-0133-a.chr|apple2e-character.rom|apple2e.chr", 0x1000, 0x0000, 0xb081df66},
};
const std::vector<RomEntry> kChargenIIeEnhanced = {
    {"342-0265-a.chr|apple2ee-character.rom|apple2eu-character.rom", 0x1000, 0x0000, 0x2651014d},
};
const std::vector<RomEntry> kDiskProm = {
    {"341-0027-a.p5|disk2-16boot.rom|AppleIIe_DiskII.rom|diskii.rom|p5.bin", 0x0100, 0x0000,
     0xce7144f6},
};

std::string to_lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

bool read_whole_file(const std::string& path, std::vector<uint8_t>& out) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        return false;
    }
    stream.seekg(0, std::ios::end);
    const std::streamoff size = stream.tellg();
    if (size < 0) {
        return false;
    }
    stream.seekg(0, std::ios::beg);
    out.resize(size_t(size));
    stream.read(reinterpret_cast<char*>(out.data()), size);
    return bool(stream);
}

bool load_optional(RomLoader& loader, const std::vector<RomEntry>& entries, std::vector<uint8_t>& dest) {
    std::string ignored;
    return loader.load(entries, dest, &ignored);
}

}  // namespace

Apple2::Apple2(Model model) : model_(model), cpu_(kClock) {
    cpu_.set_memory_handlers([this](uint16_t a) { return read_byte(a); },
                             [this](uint16_t a, uint8_t v) { write_byte(a, v); });
    cpu_.set_cycle_handler([this](int cycles) { on_cpu_cycles(cycles); });
}

const char* Apple2::title() const {
    switch (model_) {
        case Model::II:
            return "Apple II";
        case Model::IIPlus:
            return "Apple II+";
        case Model::IIe:
            return "Apple IIe";
        case Model::IIeEnhanced:
            return "Apple IIe Enhanced";
    }
    return "Apple II";
}

bool Apple2::try_load_disk_prom(const std::string& rom_path) {
    auto try_path = [&](const std::string& path) {
        RomLoader loader;
        std::string ignored;
        if (!loader.open(path, &ignored)) {
            return false;
        }
        std::vector<uint8_t> prom;
        if (!load_optional(loader, kDiskProm, prom) || prom.size() < 256) {
            return false;
        }
        std::memcpy(disk_prom_.data(), prom.data(), 256);
        disk_prom_loaded_ = true;
        return true;
    };

    if (try_path(rom_path)) {
        return true;
    }
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path path(rom_path);
    const std::string lower = path.filename().string();
    const bool zip = lower.size() >= 4 && to_lower(lower).compare(lower.size() - 4, 4, ".zip") == 0;
    if (!fs::is_regular_file(path, ec) || zip) {
        return false;
    }
    const fs::path base = path.parent_path();
    const char* names[] = {"disk2-16boot.rom", "341-0027-a.p5", "AppleIIe_DiskII.rom",
                           "a2diskiing.zip"};
    for (const char* name : names) {
        const fs::path candidate = base / name;
        if (fs::exists(candidate, ec) && try_path(candidate.string())) {
            return true;
        }
    }
    return false;
}

bool Apple2::load_roms(const std::string& rom_path, std::string* error) {
    namespace fs = std::filesystem;
    std::error_code ec;
    std::vector<uint8_t> firmware;
    std::vector<uint8_t> chargen;

    auto load_from_loader = [&](RomLoader& loader) -> bool {
        firmware.clear();
        chargen.clear();
        std::string ignored;
        switch (model_) {
            case Model::II:
                if (!loader.load(kIiInteger, firmware, &ignored)) {
                    return false;
                }
                load_optional(loader, kChargenIi, chargen);
                break;
            case Model::IIPlus:
                if (!loader.load(kIiPlusChips, firmware, &ignored)) {
                    ignored.clear();
                    if (!loader.load(kIiPlusConcat, firmware, &ignored)) {
                        return false;
                    }
                }
                load_optional(loader, kChargenIi, chargen);
                break;
            case Model::IIe:
                if (!loader.load(kIIeChips, firmware, &ignored)) {
                    ignored.clear();
                    if (!loader.load(kIIeConcat, firmware, &ignored) || firmware.size() < 0x4000) {
                        return false;
                    }
                }
                if (!load_optional(loader, kChargenIIe, chargen)) {
                    load_optional(loader, kChargenIIeEnhanced, chargen);
                }
                break;
            case Model::IIeEnhanced:
                if (!loader.load(kIIeEnhancedChips, firmware, &ignored)) {
                    ignored.clear();
                    if (!loader.load(kIIeConcat, firmware, &ignored) || firmware.size() < 0x4000) {
                        return false;
                    }
                }
                if (!load_optional(loader, kChargenIIeEnhanced, chargen)) {
                    load_optional(loader, kChargenIIe, chargen);
                }
                break;
        }
        return !firmware.empty();
    };

    RomLoader loader;
    std::string open_error;
    const bool opened = loader.open(rom_path, &open_error);
    bool loaded = opened && load_from_loader(loader);
    if (loaded) {
        warnings_ = loader.warnings();
    }

    if (!loaded && fs::is_regular_file(rom_path, ec)) {
        std::vector<uint8_t> raw;
        if (read_whole_file(rom_path, raw)) {
            if ((model_ == Model::IIPlus && raw.size() >= 0x3000) ||
                (model_ == Model::II && raw.size() >= 0x2000) ||
                (is_iie() && (raw.size() == 0x4000 || raw.size() == 0x3000 || raw.size() >= 0x4000))) {
                firmware = std::move(raw);
                loaded = true;
                const fs::path parent = fs::path(rom_path).parent_path();
                RomLoader sibling;
                std::string ignored;
                if (sibling.open(parent.string(), &ignored)) {
                    if (is_iie()) {
                        if (!load_optional(sibling, kChargenIIeEnhanced, chargen)) {
                            load_optional(sibling, kChargenIIe, chargen);
                        }
                    } else {
                        load_optional(sibling, kChargenIi, chargen);
                    }
                }
            }
        }
    }

    if (!loaded) {
        if (error) {
            *error = open_error.empty() ? ("Apple II firmware not found in " + rom_path) : open_error;
        }
        return false;
    }

    rom_.fill(0);
    if (is_iie()) {
        const size_t n = std::min(firmware.size(), rom_.size());
        std::memcpy(rom_.data(), firmware.data(), n);
    } else if (model_ == Model::II) {
        const size_t n = std::min(firmware.size(), size_t(0x2000));
        std::memcpy(rom_.data() + 0x2000, firmware.data(), n);
    } else {
        const size_t n = std::min(firmware.size(), size_t(0x3000));
        std::memcpy(rom_.data() + 0x1000, firmware.data(), n);
    }

    chargen_.fill(0);
    chargen_size_ = 0;
    if (!chargen.empty()) {
        chargen_size_ = int(std::min(chargen.size(), chargen_.size()));
        std::memcpy(chargen_.data(), chargen.data(), size_t(chargen_size_));
    } else {
        // Missing character ROM: fill with a block so text is still visible.
        chargen_.fill(0x7F);
        chargen_size_ = is_iie() ? 0x1000 : 0x0800;
        warnings_.push_back("Apple II character ROM missing; using a fallback font");
    }

    disk_prom_loaded_ = false;
    disk_prom_.fill(0);
    try_load_disk_prom(rom_path);
    return true;
}

bool Apple2::init(const std::string& rom_path, std::string* error) {
    if (!load_roms(rom_path, error)) {
        return false;
    }
    reset();
    return true;
}

void Apple2::init_synthetic_roms() {
    rom_.fill(0xEA);
    chargen_.fill(0x7F);
    chargen_size_ = is_iie() ? 0x1000 : 0x0800;
    disk_prom_loaded_ = false;
    disk_prom_.fill(0);
    // Reset vector at $FFFC -> $F000.
    rom_[0x3FFC] = 0x00;
    rom_[0x3FFD] = 0xF0;
    rom_[0x3FFE] = 0x00;
    rom_[0x3FFF] = 0xF0;
    // JMP $F000
    rom_[0x3000] = 0x4C;
    rom_[0x3001] = 0x00;
    rom_[0x3002] = 0xF0;
    reset();
}

void Apple2::reset() {
    main_.fill(0);
    aux_.fill(0);
    lc_bank2_ram_.fill(0);
    aux_lc_bank2_ram_.fill(0);
    text_ = true;
    mixed_ = false;
    page2_ = false;
    hires_ = false;
    store80_ = false;
    ramrd_ = false;
    ramwrt_ = false;
    intcxrom_ = false;
    altzp_ = false;
    slotc3rom_ = false;
    col80_ = false;
    altcharset_ = false;
    an3_ = true;
    c8rom_ = false;
    lc_read_ram_ = false;
    lc_write_ram_ = false;
    lc_use_bank2_ = true;
    lc_prewrite_ = false;
    caps_lock_ = true;
    speaker_ = false;
    keyboard_ = 0;
    any_key_ = false;
    prev_keys_.fill(false);
    scanline_ = 0;
    audio_.clear();
    audio_acc_ = 0;
    disk_.reset();
    cpu_.set_cmos(model_ == Model::IIeEnhanced);
    cpu_.set_irq(IrqLine::Clear);
    cpu_.set_nmi(IrqLine::Clear);
    cpu_.reset();
}

void Apple2::access_language_card(uint16_t address) {
    const int n = address & 0x0F;
    lc_use_bank2_ = (n & 0x08) == 0;
    const int mode = n & 3;
    const bool odd = (mode & 1) != 0;
    lc_read_ram_ = (mode == 0 || mode == 3);
    if (odd) {
        if (lc_prewrite_) {
            lc_write_ram_ = true;
        }
        lc_prewrite_ = true;
    } else {
        lc_write_ram_ = false;
        lc_prewrite_ = false;
    }
}

uint8_t* Apple2::lc_ptr(uint16_t address, bool aux) {
    if (address < 0xE000) {
        uint8_t* bank = aux ? aux_lc_bank2_ram_.data() : lc_bank2_ram_.data();
        if (!lc_use_bank2_) {
            bank = (aux ? aux_ : main_).data() + 0xD000;
        }
        return bank + (address - 0xD000);
    }
    return (aux ? aux_ : main_).data() + address;
}

uint8_t Apple2::read_cx(uint16_t address) {
    if (address == 0xCFFF) {
        c8rom_ = false;
        return is_iie() ? rom_[address - 0xC000] : 0x00;
    }
    if (is_iie() && intcxrom_) {
        if (address >= 0xC300 && address < 0xC400) {
            c8rom_ = true;
        }
        return rom_[address - 0xC000];
    }
    if (is_iie() && address >= 0xC300 && address < 0xC400 && !slotc3rom_) {
        c8rom_ = true;
        return rom_[address - 0xC000];
    }
    if (address >= 0xC800) {
        if (is_iie() && (intcxrom_ || c8rom_)) {
            return rom_[address - 0xC000];
        }
        return 0x00;
    }
    const int slot = (address >> 8) & 0x0F;
    if (slot == 6 && disk_prom_loaded_) {
        return disk_prom_[address & 0xFF];
    }
    return 0x00;
}

uint8_t Apple2::read_io(uint16_t address) {
    const uint16_t a = address & 0x00FF;
    if (a >= 0xE0 && a <= 0xEF) {
        return disk_.read_io(uint8_t(a));
    }
    if (a >= 0x80 && a <= 0x8F) {
        access_language_card(address);
        return 0x00;
    }

    switch (a) {
        case 0x00:
            return keyboard_;
        case 0x10: {
            const uint8_t strobe = keyboard_;
            keyboard_ &= 0x7F;
            if (is_iie()) {
                return uint8_t((any_key_ ? 0x80 : 0x00) | (strobe & 0x7F));
            }
            return strobe;
        }
        case 0x11:
            return lc_use_bank2_ ? 0x80 : 0x00;
        case 0x12:
            return lc_read_ram_ ? 0x80 : 0x00;
        case 0x13:
            return ramrd_ ? 0x80 : 0x00;
        case 0x14:
            return ramwrt_ ? 0x80 : 0x00;
        case 0x15:
            return intcxrom_ ? 0x80 : 0x00;
        case 0x16:
            return altzp_ ? 0x80 : 0x00;
        case 0x17:
            return slotc3rom_ ? 0x80 : 0x00;
        case 0x18:
            return store80_ ? 0x80 : 0x00;
        case 0x19:
            return (scanline_ >= 192) ? 0x80 : 0x00;
        case 0x1A:
            return text_ ? 0x80 : 0x00;
        case 0x1B:
            return mixed_ ? 0x80 : 0x00;
        case 0x1C:
            return page2_ ? 0x80 : 0x00;
        case 0x1D:
            return hires_ ? 0x80 : 0x00;
        case 0x1E:
            return altcharset_ ? 0x80 : 0x00;
        case 0x1F:
            return col80_ ? 0x80 : 0x00;
        case 0x30:
            speaker_ = !speaker_;
            return 0x00;
        case 0x50:
            text_ = false;
            return 0x00;
        case 0x51:
            text_ = true;
            return 0x00;
        case 0x52:
            mixed_ = false;
            return 0x00;
        case 0x53:
            mixed_ = true;
            return 0x00;
        case 0x54:
            page2_ = false;
            return 0x00;
        case 0x55:
            page2_ = true;
            return 0x00;
        case 0x56:
            hires_ = false;
            return 0x00;
        case 0x57:
            hires_ = true;
            return 0x00;
        case 0x5E:
            an3_ = false;
            return 0x00;
        case 0x5F:
            an3_ = true;
            return 0x00;
        case 0x61:
            return open_apple_ ? 0x80 : 0x00;
        case 0x62:
            return closed_apple_ ? 0x80 : 0x00;
        case 0x63:
        case 0x64:
        case 0x65:
        case 0x66:
        case 0x67:
            return 0x00;
        default:
            break;
    }
    return 0x00;
}

void Apple2::write_io(uint16_t address, uint8_t value) {
    const uint16_t a = address & 0x00FF;
    if (a >= 0xE0 && a <= 0xEF) {
        disk_.write_io(uint8_t(a), value);
        return;
    }
    if (a >= 0x80 && a <= 0x8F) {
        access_language_card(address);
        return;
    }
    if (is_iie() && a <= 0x0F) {
        switch (a) {
            case 0x00:
                store80_ = false;
                return;
            case 0x01:
                store80_ = true;
                return;
            case 0x02:
                ramrd_ = false;
                return;
            case 0x03:
                ramrd_ = true;
                return;
            case 0x04:
                ramwrt_ = false;
                return;
            case 0x05:
                ramwrt_ = true;
                return;
            case 0x06:
                intcxrom_ = false;
                return;
            case 0x07:
                intcxrom_ = true;
                return;
            case 0x08:
                altzp_ = false;
                return;
            case 0x09:
                altzp_ = true;
                return;
            case 0x0A:
                slotc3rom_ = false;
                return;
            case 0x0B:
                slotc3rom_ = true;
                return;
            case 0x0C:
                col80_ = false;
                return;
            case 0x0D:
                col80_ = true;
                return;
            case 0x0E:
                altcharset_ = false;
                return;
            case 0x0F:
                altcharset_ = true;
                return;
            default:
                break;
        }
    }
    switch (a) {
        case 0x10:
            keyboard_ &= 0x7F;
            break;
        case 0x30:
            speaker_ = !speaker_;
            break;
        case 0x50:
            text_ = false;
            break;
        case 0x51:
            text_ = true;
            break;
        case 0x52:
            mixed_ = false;
            break;
        case 0x53:
            mixed_ = true;
            break;
        case 0x54:
            page2_ = false;
            break;
        case 0x55:
            page2_ = true;
            break;
        case 0x56:
            hires_ = false;
            break;
        case 0x57:
            hires_ = true;
            break;
        case 0x5E:
            an3_ = false;
            break;
        case 0x5F:
            an3_ = true;
            break;
        default:
            break;
    }
}

uint8_t Apple2::read_byte(uint16_t address) {
    if (address < 0x0200) {
        const auto& ram = (is_iie() && altzp_) ? aux_ : main_;
        return ram[address];
    }
    if (address < 0xC000) {
        bool aux = is_iie() && ramrd_;
        if (is_iie() && store80_) {
            if (address >= 0x0400 && address < 0x0800) {
                aux = page2_;
            } else if (hires_ && address >= 0x2000 && address < 0x4000) {
                aux = page2_;
            }
        }
        return (aux ? aux_ : main_)[address];
    }
    if (address < 0xC100) {
        return read_io(address);
    }
    if (address < 0xD000) {
        return read_cx(address);
    }
    if (lc_read_ram_) {
        return *lc_ptr(address, is_iie() && altzp_);
    }
    return rom_[address - 0xC000];
}

void Apple2::write_byte(uint16_t address, uint8_t value) {
    if (address < 0x0200) {
        auto& ram = (is_iie() && altzp_) ? aux_ : main_;
        ram[address] = value;
        return;
    }
    if (address < 0xC000) {
        bool aux = is_iie() && ramwrt_;
        if (is_iie() && store80_) {
            if (address >= 0x0400 && address < 0x0800) {
                aux = page2_;
            } else if (hires_ && address >= 0x2000 && address < 0x4000) {
                aux = page2_;
            }
        }
        (aux ? aux_ : main_)[address] = value;
        return;
    }
    if (address < 0xC100) {
        write_io(address, value);
        return;
    }
    if (address < 0xD000) {
        if (address == 0xCFFF) {
            c8rom_ = false;
        }
        return;
    }
    if (lc_write_ram_) {
        *lc_ptr(address, is_iie() && altzp_) = value;
    }
}

void Apple2::on_cpu_cycles(int cycles) {
    disk_.tick(cycles);
    audio_acc_ += int64_t(cycles) * kSampleRate;
    while (audio_acc_ >= kClock) {
        audio_acc_ -= kClock;
        audio_.push_back(int16_t(speaker_ ? 4000 : -4000));
    }
}

uint8_t Apple2::ascii_from_keys(const MachineInputs& inputs) const {
    const bool shift = inputs.key(Key::LeftShift) || inputs.key(Key::RightShift);
    const bool ctrl = inputs.key(Key::LeftCtrl) || inputs.key(Key::RightCtrl);

    if (inputs.key(Key::Enter)) return 0x0D;
    if (inputs.key(Key::Escape)) return 0x1B;
    if (inputs.key(Key::Backspace) || inputs.key(Key::Left)) return 0x08;
    if (inputs.key(Key::Right)) return 0x15;
    if (inputs.key(Key::Up)) return 0x0B;
    if (inputs.key(Key::Down)) return 0x0A;
    if (inputs.key(Key::Space)) return 0x20;
    if (inputs.key(Key::Tab)) return 0x09;

    static const Key kLetters[] = {Key::A, Key::B, Key::C, Key::D, Key::E, Key::F, Key::G, Key::H,
                                   Key::I, Key::J, Key::K, Key::L, Key::M, Key::N, Key::O, Key::P,
                                   Key::Q, Key::R, Key::S, Key::T, Key::U, Key::V, Key::W, Key::X,
                                   Key::Y, Key::Z};
    for (int i = 0; i < 26; i++) {
        if (!inputs.key(kLetters[i])) {
            continue;
        }
        if (ctrl) {
            return uint8_t(i + 1);
        }
        if (!is_iie()) {
            return uint8_t('A' + i);
        }
        const bool upper = caps_lock_ ? !shift : shift;
        return uint8_t((upper ? 'A' : 'a') + i);
    }

    static const Key kDigits[] = {Key::Num0, Key::Num1, Key::Num2, Key::Num3, Key::Num4,
                                  Key::Num5, Key::Num6, Key::Num7, Key::Num8, Key::Num9};
    static const char kShiftDigit[] = {')', '!', '@', '#', '$', '%', '^', '&', '*', '('};
    for (int i = 0; i < 10; i++) {
        if (inputs.key(kDigits[i])) {
            return uint8_t(shift ? kShiftDigit[i] : ('0' + i));
        }
    }
    if (inputs.key(Key::Comma)) return uint8_t(shift ? '<' : ',');
    if (inputs.key(Key::Period)) return uint8_t(shift ? '>' : '.');
    if (inputs.key(Key::Semicolon)) return uint8_t(shift ? ':' : ';');
    if (inputs.key(Key::Quote)) return uint8_t(shift ? '"' : '\'');
    if (inputs.key(Key::Slash)) return uint8_t(shift ? '?' : '/');
    if (inputs.key(Key::Minus)) return uint8_t(shift ? '_' : '-');
    return 0;
}

void Apple2::apply_keyboard(const MachineInputs& inputs) {
    any_key_ = false;
    uint8_t found = 0;
    bool fresh = false;
    for (size_t i = 0; i < size_t(Key::Count); i++) {
        const bool down = inputs.keys[i];
        if (down) {
            any_key_ = true;
        }
        if (down && !prev_keys_[i]) {
            fresh = true;
        }
        prev_keys_[i] = down;
    }
    static bool prev_caps = false;
    const bool caps = inputs.key(Key::CapsLock);
    if (caps && !prev_caps) {
        caps_lock_ = !caps_lock_;
    }
    prev_caps = caps;

    found = ascii_from_keys(inputs);
    if (fresh && found != 0) {
        keyboard_ = uint8_t(found | 0x80);
    }
}

void Apple2::set_inputs(const MachineInputs& inputs) {
    apply_keyboard(inputs);
    open_apple_ = inputs.player1.button2 || inputs.player1.button1;
    closed_apple_ = inputs.player1.button3;
}

void Apple2::set_dip_switch(int, uint8_t) {}

void Apple2::run_frame() {
    for (int line = 0; line < kScanlines; line++) {
        scanline_ = line;
        cpu_.run(kCyclesPerLine);
    }
    video_.text = text_;
    video_.mixed = mixed_;
    video_.page2 = page2_ && !store80_ && !col80_;
    video_.hires = hires_;
    video_.col80 = col80_;
    video_.altcharset = altcharset_;
    video_.dhires = is_iie() && !an3_ && col80_;
    video_.iie = is_iie();
    video_.flash_phase++;
    video_.render(framebuffer_.data(), main_.data(), aux_.data(), chargen_.data(), chargen_size_);
}

void Apple2::drain_audio(std::vector<int16_t>& out) {
    out.swap(audio_);
    audio_.clear();
}

bool Apple2::load_media(const std::string& path, std::string* error) {
    if (!disk_.load_file(path, error)) {
        return false;
    }
    return true;
}

}  // namespace dsp
