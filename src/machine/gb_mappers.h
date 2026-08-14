#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace dsp {

// Cartridge mappers for the Game Boy, ported from
// leniad/dsp-emulator src/consolas/gb_mappers.pas.
//
// Supported cart types (header $0147):
//   $00          ROM only
//   $01..$03     MBC1 (+RAM / +Battery)
//   $05..$06     MBC2 (+Battery)
//   $08..$09     ROM + RAM (+Battery)
//   $0F..$13     MBC3 (+Timer / +RAM / +Battery)  — RTC latched, host clock
//   $19..$1E     MBC5 (+Rumble / +RAM / +Battery)
//   $FF          HuC-1
//
// Exotic chips (MMM01, MBC6, MBC7, Wisdom Tree, M161) are recognised but
// fall back to ROM-only behaviour until ported.
class GbMapper {
public:
    // Maximum sizes used by the Pascal core.
    static constexpr int kMaxRomBanks = 512;  // 8 MiB (MBC5)
    static constexpr int kMaxRamBanks = 16;   // 128 KiB
    static constexpr int kRomBankSize = 0x4000;
    static constexpr int kRamBankSize = 0x2000;

    GbMapper() = default;

    // Load a raw cartridge image (already read by the driver). Computes the
    // number of ROM/RAM banks from the header and wires the mapper callbacks.
    // `crc32` may be 0 to skip the special-case CRC overrides.
    bool load(const std::vector<uint8_t>& image, uint32_t crc32 = 0);

    void reset();

    // Optional battery-backed RAM persistence.
    bool has_battery() const { return has_battery_; }
    bool load_ram(const std::string& path);
    bool save_ram(const std::string& path) const;

    // Memory accessors used by the GameBoy driver.
    uint8_t read_rom(uint16_t address) const;
    void write_rom(uint16_t address, uint8_t value);  // mapper control
    uint8_t read_ram(uint16_t address) const;         // $A000-$BFFF
    void write_ram(uint16_t address, uint8_t value);

    uint8_t cart_type() const { return cart_type_; }
    uint16_t rom_banks() const { return rom_banks_; }
    uint8_t ram_banks() const { return ram_banks_; }
    uint8_t ram_size_code() const { return ram_size_code_; }
    bool is_gbc() const { return is_gbc_; }
    const std::string& title() const { return title_; }

private:
    enum class Kind {
        None,
        Mbc1,
        Mbc2,
        Mbc3,
        Mbc5,
        Huc1,
        RomRam,  // $08/$09
    };

    void configure(uint8_t cart_type, uint32_t crc32, uint16_t rom_banks,
                   uint8_t ram_size_code);

    void write_mbc1(uint16_t address, uint8_t value);
    void write_mbc2(uint16_t address, uint8_t value);
    void write_mbc3(uint16_t address, uint8_t value);
    void write_mbc5(uint16_t address, uint8_t value);
    void write_huc1(uint16_t address, uint8_t value);

    uint8_t read_ram_mbc1(uint16_t address) const;
    void write_ram_mbc1(uint16_t address, uint8_t value);
    uint8_t read_ram_mbc2(uint16_t address) const;
    void write_ram_mbc2(uint16_t address, uint8_t value);
    uint8_t read_ram_mbc3(uint16_t address) const;
    void write_ram_mbc3(uint16_t address, uint8_t value);
    uint8_t read_ram_mbc5(uint16_t address) const;
    void write_ram_mbc5(uint16_t address, uint8_t value);
    uint8_t read_ram_huc1(uint16_t address) const;
    void write_ram_huc1(uint16_t address, uint8_t value);

    void latch_rtc();

    Kind kind_ = Kind::None;
    uint8_t cart_type_ = 0;
    uint16_t rom_banks_ = 2;
    uint8_t ram_banks_ = 0;
    uint8_t ram_size_code_ = 0;  // header $0149
    bool has_battery_ = false;
    bool is_gbc_ = false;
    std::string title_;

    // Flat ROM storage (bank * 0x4000 + offset).
    std::vector<uint8_t> rom_;
    // Flat RAM storage (bank * 0x2000 + offset). MBC2 only uses 512 nybbles.
    std::vector<uint8_t> ram_;

    // Banking state.
    bool ram_enable_ = false;
    bool rom_mode_ = false;  // MBC1 mode flag
    uint16_t rom_bank_ = 1;  // bank mapped at $4000
    uint16_t rom_bank0_ = 0; // bank mapped at $0000 (MBC1 mode 1)
    uint8_t ram_bank_ = 0;
    uint8_t reg0_ = 1;  // MBC1 low bank
    uint8_t reg1_ = 0;  // MBC1 high / RAM bank
    uint8_t mbc1_mask_ = 0x1F;
    uint8_t mbc1_shift_ = 5;

    // MBC3 RTC (registers 0..4 = S, M, H, DL, DH). Latched on write 0->1 to $6000.
    uint8_t rtc_regs_[5] = {};
    uint8_t rtc_latched_[5] = {};
    bool rtc_ready_ = false;
    uint8_t rtc_latch_prev_ = 0xFF;
};

}  // namespace dsp
