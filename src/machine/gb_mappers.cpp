#include "machine/gb_mappers.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>

namespace dsp {
namespace {

uint16_t rom_banks_from_header(uint8_t code) {
    // Header $0148.
    if (code <= 0x08) return uint16_t(2u << code);  // 32KB .. 8MB
    switch (code) {
        case 0x52: return 72;
        case 0x53: return 80;
        case 0x54: return 96;
        default: return 2;
    }
}

uint8_t ram_banks_from_header(uint8_t code) {
    // Header $0149. Returns number of 8 KiB banks (MBC2 is special-cased).
    switch (code) {
        case 0x00: return 0;
        case 0x01: return 1;  // 2 KiB, still one bank slot
        case 0x02: return 1;  // 8 KiB
        case 0x03: return 4;  // 32 KiB
        case 0x04: return 16; // 128 KiB
        case 0x05: return 8;  // 64 KiB
        default: return 0;
    }
}

bool type_has_battery(uint8_t t) {
    switch (t) {
        case 0x03:
        case 0x06:
        case 0x09:
        case 0x0D:
        case 0x0F:
        case 0x10:
        case 0x13:
        case 0x1B:
        case 0x1E:
        case 0xFF:
            return true;
        default:
            return false;
    }
}

}  // namespace

bool GbMapper::load(const std::vector<uint8_t>& image, uint32_t crc32) {
    if (image.size() < 0x150) return false;

    rom_ = image;
    // Pad to a whole number of 16 KiB banks.
    if (rom_.size() % kRomBankSize) {
        rom_.resize(((rom_.size() / kRomBankSize) + 1) * kRomBankSize, 0xFF);
    }

    const uint8_t cgb = rom_[0x143];
    is_gbc_ = (cgb == 0x80 || cgb == 0xC0);

    // Title: $0134-$0143, ASCII, null-padded.
    title_.clear();
    for (int i = 0x134; i <= 0x143; i++) {
        const char ch = char(rom_[i]);
        if (ch == 0) break;
        if (ch >= 32 && ch < 127) title_.push_back(ch);
    }

    uint8_t cart_type = rom_[0x147];
    uint16_t rom_banks = rom_banks_from_header(rom_[0x148]);
    // Prefer the actual file size when it is larger than the header claim
    // (some dumps are over-dumped or the header is wrong).
    const uint16_t file_banks = uint16_t(rom_.size() / kRomBankSize);
    if (file_banks > rom_banks) rom_banks = file_banks;
    if (rom_banks < 2) rom_banks = 2;

    const uint8_t ram_code = rom_[0x149];
    configure(cart_type, crc32, rom_banks, ram_code);
    reset();
    return true;
}

void GbMapper::configure(uint8_t cart_type, uint32_t crc32, uint16_t rom_banks,
                         uint8_t ram_size_code) {
    // CRC overrides from the Pascal set_mapper.
    if (crc32 == 0x0C38A775) cart_type = 0xC1;  // M161
    if (crc32 == 0x5BFC3EF5 || crc32 == 0x6DBAA5E8) {
        cart_type = uint8_t(cart_type + 10);
        rom_banks = 32;
    }

    cart_type_ = cart_type;
    rom_banks_ = rom_banks;
    ram_size_code_ = ram_size_code;
    has_battery_ = type_has_battery(cart_type);

    // Default MBC1 bank mask/shift; a few known dumps use 4-bit / shift-4.
    mbc1_mask_ = 0x1F;
    mbc1_shift_ = 5;
    switch (crc32) {
        case 0xB91D6C8D:
        case 0x509A6B73:
        case 0xF724B5CE:
        case 0xB1A8DFD0:
        case 0x339F1694:
        case 0xAD376905:
        case 0x7D1D8FDC:
        case 0x018B4A02:
            mbc1_mask_ = 0x0F;
            mbc1_shift_ = 4;
            break;
        default:
            break;
    }

    kind_ = Kind::None;
    ram_banks_ = 0;

    switch (cart_type) {
        case 0x00:
            kind_ = Kind::None;
            break;
        case 0x01:
        case 0x02:
        case 0x03:
            kind_ = Kind::Mbc1;
            if (cart_type >= 0x02) ram_banks_ = ram_banks_from_header(ram_size_code);
            break;
        case 0x05:
        case 0x06:
            kind_ = Kind::Mbc2;
            ram_banks_ = 1;  // 512 x 4-bit built-in
            break;
        case 0x08:
        case 0x09:
            kind_ = Kind::RomRam;
            ram_banks_ = ram_banks_from_header(ram_size_code);
            break;
        case 0x0F:
        case 0x10:
        case 0x11:
        case 0x12:
        case 0x13:
            kind_ = Kind::Mbc3;
            if (cart_type == 0x10 || cart_type == 0x12 || cart_type == 0x13)
                ram_banks_ = ram_banks_from_header(ram_size_code);
            // Timer-only ($0F/$11) still needs the RTC regs.
            break;
        case 0x19:
        case 0x1A:
        case 0x1B:
        case 0x1C:
        case 0x1D:
        case 0x1E:
            kind_ = Kind::Mbc5;
            if (cart_type == 0x1A || cart_type == 0x1B || cart_type == 0x1D ||
                cart_type == 0x1E)
                ram_banks_ = ram_banks_from_header(ram_size_code);
            break;
        case 0xFF:
            kind_ = Kind::Huc1;
            ram_banks_ = ram_banks_from_header(ram_size_code);
            break;
        default:
            // Unimplemented exotic mapper: behave as ROM-only.
            kind_ = Kind::None;
            break;
    }

    if (ram_banks_ > kMaxRamBanks) ram_banks_ = kMaxRamBanks;
    ram_.assign(size_t(ram_banks_) * kRamBankSize, 0xFF);
    // MBC2: only 512 bytes of 4-bit RAM matter; keep a full bank for simplicity.
    if (kind_ == Kind::Mbc2) ram_.assign(0x200, 0xFF);
}

void GbMapper::reset() {
    ram_enable_ = false;
    rom_mode_ = false;
    rom_bank_ = 1;
    rom_bank0_ = 0;
    ram_bank_ = 0;
    reg0_ = 1;
    reg1_ = 0;
    rtc_ready_ = false;
    rtc_latch_prev_ = 0xFF;
    std::memset(rtc_regs_, 0, sizeof(rtc_regs_));
    std::memset(rtc_latched_, 0, sizeof(rtc_latched_));
}

bool GbMapper::load_ram(const std::string& path) {
    if (ram_.empty()) return false;
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    in.read(reinterpret_cast<char*>(ram_.data()),
            std::streamsize(ram_.size()));
    return true;
}

bool GbMapper::save_ram(const std::string& path) const {
    if (!has_battery_ || ram_.empty()) return false;
    std::ofstream out(path, std::ios::binary);
    if (!out) return false;
    out.write(reinterpret_cast<const char*>(ram_.data()),
              std::streamsize(ram_.size()));
    return bool(out);
}

// ---------------------------------------------------------------------------
// ROM read / write
// ---------------------------------------------------------------------------

uint8_t GbMapper::read_rom(uint16_t address) const {
    if (rom_.empty()) return 0xFF;
    if (address < 0x4000) {
        const size_t bank = rom_bank0_ % std::max<uint16_t>(rom_banks_, 1);
        const size_t off = bank * kRomBankSize + address;
        return off < rom_.size() ? rom_[off] : 0xFF;
    }
    if (address < 0x8000) {
        const size_t bank = rom_bank_ % std::max<uint16_t>(rom_banks_, 1);
        const size_t off = bank * kRomBankSize + (address & 0x3FFF);
        return off < rom_.size() ? rom_[off] : 0xFF;
    }
    return 0xFF;
}

void GbMapper::write_rom(uint16_t address, uint8_t value) {
    switch (kind_) {
        case Kind::Mbc1:
            write_mbc1(address, value);
            break;
        case Kind::Mbc2:
            write_mbc2(address, value);
            break;
        case Kind::Mbc3:
            write_mbc3(address, value);
            break;
        case Kind::Mbc5:
            write_mbc5(address, value);
            break;
        case Kind::Huc1:
            write_huc1(address, value);
            break;
        case Kind::RomRam:
            // No bank switching; RAM enable lives at $0000-$1FFF on some carts
            // but ROM+RAM usually has RAM always accessible. Match Pascal: no
            // rom_putbyte for type 0/8/9.
            break;
        case Kind::None:
        default:
            break;
    }
}

// ---------------------------------------------------------------------------
// External RAM
// ---------------------------------------------------------------------------

uint8_t GbMapper::read_ram(uint16_t address) const {
    switch (kind_) {
        case Kind::Mbc1:
        case Kind::RomRam:
            return read_ram_mbc1(address);
        case Kind::Mbc2:
            return read_ram_mbc2(address);
        case Kind::Mbc3:
            return read_ram_mbc3(address);
        case Kind::Mbc5:
            return read_ram_mbc5(address);
        case Kind::Huc1:
            return read_ram_huc1(address);
        default:
            return 0xFF;
    }
}

void GbMapper::write_ram(uint16_t address, uint8_t value) {
    switch (kind_) {
        case Kind::Mbc1:
        case Kind::RomRam:
            write_ram_mbc1(address, value);
            break;
        case Kind::Mbc2:
            write_ram_mbc2(address, value);
            break;
        case Kind::Mbc3:
            write_ram_mbc3(address, value);
            break;
        case Kind::Mbc5:
            write_ram_mbc5(address, value);
            break;
        case Kind::Huc1:
            write_ram_huc1(address, value);
            break;
        default:
            break;
    }
}

// ---- MBC1 ----------------------------------------------------------------

void GbMapper::write_mbc1(uint16_t address, uint8_t value) {
    switch (address & 0xE000) {
        case 0x0000:
            ram_enable_ = (value & 0x0F) == 0x0A;
            break;
        case 0x2000:
            reg0_ = value & 0x1F;
            if (reg0_ == 0) reg0_ = 1;
            break;
        case 0x4000:
            reg1_ = value & 0x03;
            break;
        case 0x6000:
            rom_mode_ = (value & 1) != 0;
            break;
        default:
            break;
    }
    // Combined bank number.
    uint16_t bank = uint16_t((reg0_ & mbc1_mask_) | (reg1_ << mbc1_shift_));
    if (rom_banks_ > 0) bank = uint16_t(bank % rom_banks_);
    rom_bank_ = bank == 0 ? 1 : bank;

    if (rom_mode_) {
        // Advanced ROM banking: bank 0 area also switches.
        rom_bank0_ = uint16_t((reg1_ << mbc1_shift_) % std::max<uint16_t>(rom_banks_, 1));
        ram_bank_ = reg1_;
    } else {
        rom_bank0_ = 0;
        ram_bank_ = 0;
    }
}

uint8_t GbMapper::read_ram_mbc1(uint16_t address) const {
    if (kind_ == Kind::RomRam) {
        // ROM+RAM: always accessible when RAM is present.
        if (ram_.empty()) return 0xFF;
        return ram_[address & 0x1FFF];
    }
    if (!ram_enable_ || ram_.empty()) return 0xFF;
    switch (ram_size_code_) {
        case 0:
            return 0xFF;
        case 1:  // 2 KiB
            return ram_[address & 0x7FF];
        case 2:  // 8 KiB
            return ram_[address & 0x1FFF];
        default: {  // banked
            const size_t off =
                size_t(ram_bank_ % std::max<uint8_t>(ram_banks_, 1)) * kRamBankSize +
                (address & 0x1FFF);
            return off < ram_.size() ? ram_[off] : 0xFF;
        }
    }
}

void GbMapper::write_ram_mbc1(uint16_t address, uint8_t value) {
    if (kind_ == Kind::RomRam) {
        if (ram_.empty()) return;
        ram_[address & 0x1FFF] = value;
        return;
    }
    if (!ram_enable_ || ram_.empty()) return;
    switch (ram_size_code_) {
        case 0:
            break;
        case 1:
            ram_[address & 0x7FF] = value;
            break;
        case 2:
            ram_[address & 0x1FFF] = value;
            break;
        default: {
            const size_t off =
                size_t(ram_bank_ % std::max<uint8_t>(ram_banks_, 1)) * kRamBankSize +
                (address & 0x1FFF);
            if (off < ram_.size()) ram_[off] = value;
            break;
        }
    }
}

// ---- MBC2 ----------------------------------------------------------------

void GbMapper::write_mbc2(uint16_t address, uint8_t value) {
    if (address > 0x3FFF) return;
    value &= 0x0F;
    if ((address & 0x100) == 0) {
        ram_enable_ = (value == 0x0A);
    } else {
        uint8_t bank = value;
        if (bank == 0) bank = 1;
        rom_bank_ = bank % std::max<uint16_t>(rom_banks_, 1);
        if (rom_bank_ == 0) rom_bank_ = 1;
    }
}

uint8_t GbMapper::read_ram_mbc2(uint16_t address) const {
    if (!ram_enable_ || ram_.empty()) return 0xFF;
    return uint8_t(0xF0 | (ram_[address & 0x1FF] & 0x0F));
}

void GbMapper::write_ram_mbc2(uint16_t address, uint8_t value) {
    if (!ram_enable_ || ram_.empty()) return;
    ram_[address & 0x1FF] = uint8_t(0xF0 | (value & 0x0F));
}

// ---- MBC3 ----------------------------------------------------------------

void GbMapper::latch_rtc() {
    std::time_t now = std::time(nullptr);
    std::tm* t = std::localtime(&now);
    if (!t) return;
    rtc_regs_[0] = uint8_t(t->tm_sec);
    rtc_regs_[1] = uint8_t(t->tm_min);
    rtc_regs_[2] = uint8_t(t->tm_hour);
    // Day counter is not fully accurate (would need a persistent epoch);
    // expose day-of-year low bits as a stand-in.
    const int day = t->tm_yday;
    rtc_regs_[3] = uint8_t(day & 0xFF);
    rtc_regs_[4] = uint8_t((day >> 8) & 0x01);
    std::memcpy(rtc_latched_, rtc_regs_, sizeof(rtc_latched_));
}

void GbMapper::write_mbc3(uint16_t address, uint8_t value) {
    switch (address & 0xE000) {
        case 0x0000:
            ram_enable_ = (value & 0x0F) == 0x0A;
            break;
        case 0x2000: {
            uint8_t bank = value & 0x7F;
            if (bank == 0) bank = 1;
            rom_bank_ = bank % std::max<uint16_t>(rom_banks_, 1);
            if (rom_bank_ == 0) rom_bank_ = 1;
            break;
        }
        case 0x4000:
            // 0-3 = RAM bank, 8-0C = RTC register select.
            ram_bank_ = value;
            break;
        case 0x6000:
            // Latch RTC on 0 -> 1 transition.
            if (rtc_latch_prev_ == 0x00 && value == 0x01) {
                latch_rtc();
                rtc_ready_ = true;
            }
            rtc_latch_prev_ = value;
            break;
        default:
            break;
    }
}

uint8_t GbMapper::read_ram_mbc3(uint16_t address) const {
    if (!ram_enable_) return 0xFF;
    if (ram_bank_ < 4) {
        if (ram_.empty()) return 0xFF;
        const size_t off =
            size_t(ram_bank_ % std::max<uint8_t>(std::max<uint8_t>(ram_banks_, 1), 1)) *
                kRamBankSize +
            (address & 0x1FFF);
        return off < ram_.size() ? ram_[off] : 0xFF;
    }
    if (ram_bank_ >= 0x08 && ram_bank_ <= 0x0C) {
        return rtc_latched_[ram_bank_ - 0x08];
    }
    return 0xFF;
}

void GbMapper::write_ram_mbc3(uint16_t address, uint8_t value) {
    if (!ram_enable_) return;
    if (ram_bank_ < 4) {
        if (ram_.empty()) return;
        const size_t off =
            size_t(ram_bank_ % std::max<uint8_t>(std::max<uint8_t>(ram_banks_, 1), 1)) *
                kRamBankSize +
            (address & 0x1FFF);
        if (off < ram_.size()) ram_[off] = value;
        return;
    }
    if (ram_bank_ >= 0x08 && ram_bank_ <= 0x0C) {
        rtc_regs_[ram_bank_ - 0x08] = value;
        rtc_latched_[ram_bank_ - 0x08] = value;
    }
}

// ---- MBC5 ----------------------------------------------------------------

void GbMapper::write_mbc5(uint16_t address, uint8_t value) {
    if (address <= 0x1FFF) {
        ram_enable_ = (value & 0x0F) == 0x0A;
    } else if (address <= 0x2FFF) {
        rom_bank_ = uint16_t((rom_bank_ & 0x100) | value);
        if (rom_banks_ > 0) rom_bank_ = uint16_t(rom_bank_ % rom_banks_);
    } else if (address <= 0x3FFF) {
        rom_bank_ = uint16_t((rom_bank_ & 0xFF) | ((value & 1) << 8));
        if (rom_banks_ > 0) rom_bank_ = uint16_t(rom_bank_ % rom_banks_);
    } else if (address <= 0x5FFF) {
        ram_bank_ = value & 0x0F;
    }
}

uint8_t GbMapper::read_ram_mbc5(uint16_t address) const {
    if (!ram_enable_ || ram_.empty()) return 0xFF;
    if (ram_size_code_ <= 1) {
        return ram_[address & 0x1FFF];
    }
    const size_t off =
        size_t(ram_bank_ % std::max<uint8_t>(ram_banks_, 1)) * kRamBankSize +
        (address & 0x1FFF);
    return off < ram_.size() ? ram_[off] : 0xFF;
}

void GbMapper::write_ram_mbc5(uint16_t address, uint8_t value) {
    if (!ram_enable_ || ram_.empty()) return;
    if (ram_size_code_ <= 1) {
        ram_[address & 0x1FFF] = value;
        return;
    }
    const size_t off =
        size_t(ram_bank_ % std::max<uint8_t>(ram_banks_, 1)) * kRamBankSize +
        (address & 0x1FFF);
    if (off < ram_.size()) ram_[off] = value;
}

// ---- HuC-1 ---------------------------------------------------------------

void GbMapper::write_huc1(uint16_t address, uint8_t value) {
    switch (address & 0xE000) {
        case 0x0000:
            // HuC-1 uses $0E to select IR mode (treated as ram_enable here).
            ram_enable_ = (value & 0x0F) == 0x0E;
            break;
        case 0x2000: {
            uint8_t bank = value & 0x3F;
            if (bank == 0) bank = 1;
            rom_bank_ = bank % std::max<uint16_t>(rom_banks_, 1);
            if (rom_bank_ == 0) rom_bank_ = 1;
            break;
        }
        case 0x4000:
            ram_bank_ = value & 0x03;
            break;
        default:
            break;
    }
}

uint8_t GbMapper::read_ram_huc1(uint16_t address) const {
    // When "IR mode" is active Pascal returns $C0; otherwise normal RAM.
    if (ram_enable_) return 0xC0;
    if (ram_.empty()) return 0xFF;
    switch (ram_size_code_) {
        case 0:
            return 0xFF;
        case 1:
            return ram_[address & 0x7FF];
        case 2:
            return ram_[address & 0x1FFF];
        default: {
            const size_t off =
                size_t(ram_bank_ % std::max<uint8_t>(ram_banks_, 1)) * kRamBankSize +
                (address & 0x1FFF);
            return off < ram_.size() ? ram_[off] : 0xFF;
        }
    }
}

void GbMapper::write_ram_huc1(uint16_t address, uint8_t value) {
    if (ram_enable_) return;  // IR mode ignores writes
    if (ram_.empty()) return;
    switch (ram_size_code_) {
        case 0:
            break;
        case 1:
            ram_[address & 0x7FF] = value;
            break;
        case 2:
            ram_[address & 0x1FFF] = value;
            break;
        default: {
            const size_t off =
                size_t(ram_bank_ % std::max<uint8_t>(ram_banks_, 1)) * kRamBankSize +
                (address & 0x1FFF);
            if (off < ram_.size()) ram_[off] = value;
            break;
        }
    }
}

}  // namespace dsp
