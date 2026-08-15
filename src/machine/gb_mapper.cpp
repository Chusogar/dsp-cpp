#include "machine/gb_mapper.h"

#include <algorithm>
#include <chrono>
#include <ctime>

namespace dsp {
namespace {
size_t ram_size_for_code(uint8_t code) {
    switch (code) {
        case 1: return 0x800;
        case 2: return 0x2000;
        case 3: return 0x8000;
        case 4: return 0x20000;
        case 5: return 0x10000;
        default: return 0;
    }
}
}  // namespace

bool GbMapper::configure(uint8_t header_type, uint32_t rom_crc32, size_t rom_size_bytes,
                         uint8_t ram_size_code) {
    rom_bank_count_ = rom_size_bytes / 0x4000;
    if (rom_bank_count_ < 2) rom_bank_count_ = 2;
    size_t ram_bytes = ram_size_for_code(ram_size_code);
    has_battery_ = false;
    has_rtc_ = false;

    switch (header_type) {
        case 0x00: type_ = Type::None; break;
        case 0x01: type_ = Type::Mbc1; break;
        case 0x02: type_ = Type::Mbc1; break;
        case 0x03: type_ = Type::Mbc1; has_battery_ = true; break;
        case 0x05: type_ = Type::Mbc2; ram_bytes = 0x200; break;
        case 0x06: type_ = Type::Mbc2; ram_bytes = 0x200; has_battery_ = true; break;
        case 0x0f: type_ = Type::Mbc3; has_battery_ = true; has_rtc_ = true; break;
        case 0x10: type_ = Type::Mbc3; has_battery_ = true; has_rtc_ = true; break;
        case 0x11: type_ = Type::Mbc3; break;
        case 0x12: type_ = Type::Mbc3; break;
        case 0x13: type_ = Type::Mbc3; has_battery_ = true; break;
        case 0x19: type_ = Type::Mbc5; break;
        case 0x1a: type_ = Type::Mbc5; break;
        case 0x1b: type_ = Type::Mbc5; has_battery_ = true; break;
        case 0x1c: type_ = Type::Mbc5; break;  // + rumble, motor not emulated
        case 0x1d: type_ = Type::Mbc5; break;
        case 0x1e: type_ = Type::Mbc5; has_battery_ = true; break;
        default: type_ = Type::Unsupported; return false;
    }

    ram_.assign(ram_bytes, 0xff);
    // Matches gb_mappers.pas's set_mapper: a handful of known multicart
    // MBC1 CRCs use a narrower 4-bit ROM-bank-low field; everything else
    // uses the normal 5.
    switch (rom_crc32) {
        case 0xb91d6c8d: case 0x509a6b73: case 0xf724b5ce: case 0xb1a8dfd0:
        case 0x339f1694: case 0xad376905: case 0x7d1d8fdc: case 0x018b4a02:
            mbc1_mask_ = 0x0f;
            mbc1_shift_ = 4;
            break;
        default:
            mbc1_mask_ = 0x1f;
            mbc1_shift_ = 5;
            break;
    }
    reset();
    return true;
}

void GbMapper::reset() {
    ram_enable_ = false;
    rom_mode_ = false;
    rom_bank_ = 1;
    ram_bank_ = 0;
    mbc1_reg0_ = 1;
    mbc1_reg1_ = 0;
    rtc_regs_.fill(0);
    rtc_latch_armed_ = false;
}

uint8_t GbMapper::read_rom(uint16_t address) const {
    if (rom_.empty()) return 0xff;
    if (address < 0x4000) {
        size_t bank0 = 0;
        if (type_ == Type::Mbc1 && rom_mode_) {
            // In mode 1, the RAM-bank register also selects the ROM bank
            // mapped at $0000-$3fff (used by multicart-style large ROMs).
            bank0 = size_t((ram_bank_ << mbc1_shift_) % rom_bank_count_);
        }
        size_t offset = bank0 * 0x4000 + address;
        return offset < rom_.size() ? rom_[offset] : 0xff;
    }
    size_t bank = rom_bank_ % rom_bank_count_;
    size_t offset = bank * 0x4000 + (address - 0x4000);
    return offset < rom_.size() ? rom_[offset] : 0xff;
}

void GbMapper::write_register(uint16_t address, uint8_t value) {
    switch (type_) {
        case Type::Mbc1: write_mbc1(address, value); break;
        case Type::Mbc2: write_mbc2(address, value); break;
        case Type::Mbc3: write_mbc3(address, value); break;
        case Type::Mbc5: write_mbc5(address, value); break;
        default: break;
    }
}

void GbMapper::write_mbc1(uint16_t address, uint8_t value) {
    if (address <= 0x1fff) {
        ram_enable_ = (value & 0xf) == 0xa;
        return;
    } else if (address <= 0x3fff) {
        mbc1_reg0_ = value & 0x1f;
        if (mbc1_reg0_ == 0) mbc1_reg0_ = 1;
    } else if (address <= 0x5fff) {
        mbc1_reg1_ = value & 3;
    } else if (address <= 0x7fff) {
        rom_mode_ = (value & 1) != 0;
    }
    rom_bank_ = ((mbc1_reg0_ & mbc1_mask_) | (mbc1_reg1_ << mbc1_shift_)) % uint32_t(rom_bank_count_);
    ram_bank_ = rom_mode_ ? mbc1_reg1_ : 0;
}

void GbMapper::write_mbc2(uint16_t address, uint8_t value) {
    if (address <= 0x3fff) {
        if ((address & 0x100) == 0) {
            ram_enable_ = (value & 0xf) == 0xa;
        } else {
            rom_bank_ = value & 0xf;
            if (rom_bank_ == 0) rom_bank_ = 1;
        }
    }
}

void GbMapper::write_mbc3(uint16_t address, uint8_t value) {
    if (address <= 0x1fff) {
        ram_enable_ = (value & 0xf) == 0xa;
    } else if (address <= 0x3fff) {
        rom_bank_ = value & 0x7f;
        if (rom_bank_ == 0) rom_bank_ = 1;
    } else if (address <= 0x5fff) {
        ram_bank_ = value;  // 0-3 selects RAM bank, 8-12 selects an RTC register
    } else if (address <= 0x7fff) {
        if (!has_rtc_) return;
        if (rtc_latch_armed_ && value == 1) {
            rtc_latch_armed_ = false;
            std::time_t now = std::time(nullptr);
            std::tm local{};
#if defined(_WIN32)
            localtime_s(&local, &now);
#else
            localtime_r(&now, &local);
#endif
            rtc_regs_[0] = uint8_t(local.tm_sec);
            rtc_regs_[1] = uint8_t(local.tm_min);
            rtc_regs_[2] = uint8_t(local.tm_hour);
        } else if (value == 0) {
            rtc_latch_armed_ = true;
        }
    }
}

void GbMapper::write_mbc5(uint16_t address, uint8_t value) {
    if (address <= 0x1fff) {
        ram_enable_ = (value & 0xf) == 0xa;
    } else if (address <= 0x2fff) {
        rom_bank_ = (rom_bank_ & 0x100) | value;
    } else if (address <= 0x3fff) {
        rom_bank_ = (rom_bank_ & 0xff) | uint32_t((value & 1) << 8);
    } else if (address <= 0x5fff) {
        ram_bank_ = value & 0xf;
    }
}

uint8_t GbMapper::read_ram(uint16_t address) const {
    uint16_t off = address & 0x1fff;
    switch (type_) {
        case Type::Mbc1:
            if (!ram_enable_ || ram_.empty()) return 0xff;
            if (ram_.size() <= 0x2000) return ram_[off % ram_.size()];
            return ram_[(size_t(ram_bank_) * 0x2000 + off) % ram_.size()];
        case Type::Mbc2:
            if (!ram_enable_) return 0xff;
            return uint8_t(0xf0 | (ram_[off & 0x1ff] & 0xf));
        case Type::Mbc3:
            if (ram_bank_ <= 3) {
                if (!ram_enable_ || ram_.empty()) return 0xff;
                return ram_[(size_t(ram_bank_) * 0x2000 + off) % ram_.size()];
            }
            if (ram_bank_ >= 8 && ram_bank_ <= 0xc) return rtc_regs_[ram_bank_ - 8];
            return 0xff;
        case Type::Mbc5:
            if (!ram_enable_ || ram_.empty()) return 0xff;
            if (ram_.size() <= 0x2000) return ram_[off % ram_.size()];
            return ram_[(size_t(ram_bank_) * 0x2000 + off) % ram_.size()];
        default: return 0xff;
    }
}

void GbMapper::write_ram(uint16_t address, uint8_t value) {
    uint16_t off = address & 0x1fff;
    switch (type_) {
        case Type::Mbc1:
            if (!ram_enable_ || ram_.empty()) return;
            if (ram_.size() <= 0x2000) ram_[off % ram_.size()] = value;
            else ram_[(size_t(ram_bank_) * 0x2000 + off) % ram_.size()] = value;
            break;
        case Type::Mbc2:
            if (ram_enable_) ram_[off & 0x1ff] = uint8_t(0xf0 | (value & 0xf));
            break;
        case Type::Mbc3:
            if (ram_bank_ <= 3) {
                if (ram_enable_ && !ram_.empty())
                    ram_[(size_t(ram_bank_) * 0x2000 + off) % ram_.size()] = value;
            } else if (ram_bank_ >= 8 && ram_bank_ <= 0xc) {
                rtc_regs_[ram_bank_ - 8] = value;
            }
            break;
        case Type::Mbc5:
            if (!ram_enable_ || ram_.empty()) return;
            if (ram_.size() <= 0x2000) ram_[off % ram_.size()] = value;
            else ram_[(size_t(ram_bank_) * 0x2000 + off) % ram_.size()] = value;
            break;
        default: break;
    }
}

void GbMapper::load_ram(const std::vector<uint8_t>& data) {
    size_t n = std::min(data.size(), ram_.size());
    std::copy(data.begin(), data.begin() + long(n), ram_.begin());
}

}  // namespace dsp
