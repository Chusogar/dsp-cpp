#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace dsp {

// Game Boy cartridge mapper, ported from gb_mappers.pas. Supports mapper 0
// (none), MBC1, MBC2, MBC3 (with a simplified real-time-clock, matching the
// original: it latches the host's wall-clock seconds/minutes/hours, it does
// not track a day counter) and MBC5 -- together these cover the large
// majority of real cartridges. MMM01, MBC6, MBC7, the Wisdom Tree scheme
// and M161 and HuC-1 are not implemented, same as the source (it shows an
// "unimplemented mapper" dialog for those; this port surfaces that as an
// error from ROM loading instead).
//
// Unlike gb_mappers.pas, which keeps the two active 16 KiB windows as
// copies inside the shared `memoria` array and memcpy's a fresh 16 KiB
// whenever the bank register changes, this computes the bank offset
// directly on every access. Functionally identical, but avoids a 16 KiB
// copy on every bank switch and the risk of a stale copy after a snapshot
// load; more idiomatic for a driver that isn't built around one shared
// blit-target array.
class GbMapper {
public:
    enum class Type { None, Mbc1, Mbc2, Mbc3, Mbc5, Unsupported };

    // `header_type` is the cartridge header byte at $0147, `ram_size_code`
    // the byte at $0149. Returns false (Type::Unsupported) for mappers this
    // port doesn't implement.
    bool configure(uint8_t header_type, uint32_t rom_crc32, size_t rom_size_bytes,
                   uint8_t ram_size_code);
    void set_rom(std::vector<uint8_t> rom) { rom_ = std::move(rom); }
    void reset();

    uint8_t read_rom(uint16_t address) const;             // $0000-$7fff
    void write_register(uint16_t address, uint8_t value);  // any write in $0000-$7fff
    uint8_t read_ram(uint16_t address) const;              // $a000-$bfff
    void write_ram(uint16_t address, uint8_t value);

    Type type() const { return type_; }
    bool has_battery() const { return has_battery_; }
    const std::vector<uint8_t>& ram() const { return ram_; }
    void load_ram(const std::vector<uint8_t>& data);

private:
    void write_mbc1(uint16_t address, uint8_t value);
    void write_mbc2(uint16_t address, uint8_t value);
    void write_mbc3(uint16_t address, uint8_t value);
    void write_mbc5(uint16_t address, uint8_t value);

    Type type_ = Type::None;
    bool has_battery_ = false;
    bool has_rtc_ = false;

    std::vector<uint8_t> rom_;
    std::vector<uint8_t> ram_;
    size_t rom_bank_count_ = 2;

    bool ram_enable_ = false;
    bool rom_mode_ = false;  // MBC1 mode select (banking vs RAM-banking mode)
    uint32_t rom_bank_ = 1;
    uint32_t ram_bank_ = 0;
    uint32_t mbc1_reg0_ = 1, mbc1_reg1_ = 0;  // raw MBC1 bank-select registers
    uint8_t mbc1_mask_ = 0x1f;
    int mbc1_shift_ = 5;

    // MBC3 RTC: latched seconds/minutes/hours only (day counter not
    // implemented, matching gb_mappers.pas exactly).
    std::array<uint8_t, 5> rtc_regs_{};
    bool rtc_latch_armed_ = false;
};

}  // namespace dsp
