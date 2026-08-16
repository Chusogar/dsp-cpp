#pragma once

#include <array>
#include <cstdint>
#include <functional>

#include "cpu/irq_line.h"
#include "video/nes_ppu.h"

namespace dsp {

// NES cartridge mapper, ported from nes_mappers.pas. Banking is the same
// memcpy-into-window scheme as the original (`set_prg_16/32/8`, `set_chr_8/4/2/1`)
// so MMC1/MMC3/UxROM/CNROM/AxROM and the handful of simpler boards behave the
// same way. Unsupported mapper numbers fail at load time, matching the Pascal
// "Mapper unknown" dialog.
class NesMapper {
public:
    static constexpr int kMaxPrgBanks = 32;  // 16 KiB pages
    static constexpr int kMaxChrBanks = 64;  // 8 KiB pages

    void attach(uint8_t* cpu_mem, NesPpu* ppu, std::function<void(IrqLine)> irq);
    void reset();
    bool set_mapper(int mapper, int submapper);

    void add_cycles(int cycles);  // MMC1 write-ignore delay

    uint8_t read_prg_ram(uint16_t address) const;
    void write_prg_ram(uint16_t address, uint8_t value);
    uint8_t read_expansion(uint16_t address) const;
    void write_expansion(uint16_t address, uint8_t value);
    uint8_t read_rom(uint16_t address) const;
    void write_rom(uint16_t address, uint8_t value);
    void line_ack(bool force);
    void ppu_read(uint16_t address);

    bool has_write_rom() const { return write_rom_ != nullptr; }
    bool has_read_rom() const { return read_rom_ != nullptr; }
    bool has_line_ack() const { return line_ack_ != nullptr; }
    bool has_ppu_read() const { return ppu_read_ != nullptr; }
    bool has_read_expansion() const { return read_expansion_ != nullptr; }
    bool has_write_expansion() const { return write_expansion_ != nullptr; }

    std::array<std::array<uint8_t, 0x4000>, kMaxPrgBanks> prg{};
    std::array<std::array<uint8_t, 0x2000>, kMaxChrBanks> chr{};
    std::array<uint8_t, 2> chr_map{0, 1};
    int mapper = 0;
    int submapper = 0;
    int last_prg = 1;
    int last_chr = 0;
    bool prg_ram_enable = false;
    bool prg_ram_writable = false;

private:
    using ReadFn = uint8_t (NesMapper::*)(uint16_t) const;
    using WriteFn = void (NesMapper::*)(uint16_t, uint8_t);
    using LineFn = void (NesMapper::*)(bool);
    using PpuFn = void (NesMapper::*)(uint16_t);

    void set_prg_16(uint16_t pos, int bank);
    void set_prg_32(int bank);
    void set_prg_8(uint16_t pos, int bank);
    void set_chr_8(int bank);
    void set_chr_4(uint16_t pos, int bank);
    void set_chr_2(uint16_t pos, int bank);
    void set_chr_1(uint16_t pos, int bank);

    uint8_t default_prg_ram_read(uint16_t address) const;
    void default_prg_ram_write(uint16_t address, uint8_t value);

    void mapper1_chr();
    void mapper1_prg();
    void mapper1_write(uint16_t address, uint8_t value);
    void mapper2_write(uint16_t address, uint8_t value);
    void mapper3_write(uint16_t address, uint8_t value);
    void mapper4_update_chr(uint8_t value);
    void mapper4_update_prg(uint8_t value);
    void mapper4_write(uint16_t address, uint8_t value);
    void mapper4_line(bool force);
    void mapper7_write(uint16_t address, uint8_t value);
    void mapper9_write(uint16_t address, uint8_t value);
    void mapper9_ppu(uint16_t address);
    void mapper10_write(uint16_t address, uint8_t value);
    void mapper11_write(uint16_t address, uint8_t value);
    void mapper13_write(uint16_t address, uint8_t value);
    void mapper15_write(uint16_t address, uint8_t value);
    void mapper34_write(uint16_t address, uint8_t value);
    void mapper66_write(uint16_t address, uint8_t value);
    void mapper68_write(uint16_t address, uint8_t value);
    void mapper70_write(uint16_t address, uint8_t value);
    void mapper71_write(uint16_t address, uint8_t value);
    void mapper76_write(uint16_t address, uint8_t value);
    void mapper79_write(uint16_t address, uint8_t value);
    void mapper87_write(uint16_t address, uint8_t value);
    void mapper88_write(uint16_t address, uint8_t value);
    void mapper93_write(uint16_t address, uint8_t value);
    void mapper94_write(uint16_t address, uint8_t value);
    void mapper95_write(uint16_t address, uint8_t value);
    void mapper113_write(uint16_t address, uint8_t value);
    void mapper180_write(uint16_t address, uint8_t value);
    void mapper184_write(uint16_t address, uint8_t value);
    void mapper185_write(uint16_t address, uint8_t value);
    void mapper206_write(uint16_t address, uint8_t value);
    void mapper_mmc6_write(uint16_t address, uint8_t value);
    uint8_t mapper_mmc6_ram_read(uint16_t address) const;
    void mapper_mmc6_ram_write(uint16_t address, uint8_t value);
    void mapper95_nametable();

    uint8_t* cpu_mem_ = nullptr;
    NesPpu* ppu_ = nullptr;
    std::function<void(IrqLine)> irq_;

    WriteFn write_rom_ = nullptr;
    ReadFn read_rom_ = nullptr;
    WriteFn write_prg_ram_ = nullptr;
    ReadFn read_prg_ram_ = nullptr;
    WriteFn write_expansion_ = nullptr;
    ReadFn read_expansion_ = nullptr;
    LineFn line_ack_ = nullptr;
    PpuFn ppu_read_ = nullptr;

    std::array<uint8_t, 0x31> regs_{};
    std::array<uint8_t, 16> dregs_{};
    uint8_t serial_cnt_ = 0;
    uint8_t valor_map_ = 0;
    uint8_t latch0_ = 0;
    uint8_t latch1_ = 0;
    int counter_ = 0;
    bool irq_ena_ = false;
    bool reload_ = false;
    bool chr_extra_ena_ = false;
};

}  // namespace dsp
