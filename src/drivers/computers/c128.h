#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "core/machine.h"
#include "cpu/m6502.h"
#include "machine/mos6526.h"
#include "machine/mos6566.h"
#include "machine/mos8722.h"
#include "sound/sid6581.h"

namespace dsp {

// Commodore 128 — C128 mode, 40-column VIC-II.
// MOS 8502 via M6502, MOS8722 MMU (simplified), VIC-II, SID, dual CIA.
class C128 : public Machine {
public:
    static constexpr int kScreenWidth = Mos6566::kScreenWidth;
    static constexpr int kScreenHeight = Mos6566::kScreenHeight;
    static constexpr uint32_t kCpuClock = 985248;
    static constexpr int kScanlines = 312;
    static constexpr int kCyclesPerLine = 63;
    static constexpr double kFramesPerSecond =
        double(kCpuClock) / (kScanlines * kCyclesPerLine);
    static constexpr int kSampleRate = Sid6581::kSampleRate;

    C128();

    bool init(const std::string& rom_path, std::string* error) override;
    void reset() override;
    void run_frame() override;

    void set_inputs(const MachineInputs& inputs) override;
    void set_dip_switch(int bank, uint8_t value) override;

    const uint32_t* framebuffer() const override { return framebuffer_.data(); }
    int screen_width() const override { return kScreenWidth; }
    int screen_height() const override { return kScreenHeight; }
    double frames_per_second() const override { return kFramesPerSecond; }

    void drain_audio(std::vector<int16_t>& out) override;
    int sample_rate() const override { return kSampleRate; }

    const char* title() const override { return "Commodore 128"; }
    bool uses_keyboard() const override { return true; }

    bool load_roms(const std::string& path, std::string* error);

    uint8_t debug_read_ram(uint32_t addr) const {
        return ram_[addr & 0x1ffff];
    }
    void debug_write_ram(uint32_t addr, uint8_t value) {
        ram_[addr & 0x1ffff] = value;
    }
    uint8_t debug_keyboard(int column) const {
        return keyboard_[size_t(column & 7)];
    }

private:
    uint8_t read_byte(uint16_t addr);
    void write_byte(uint16_t addr, uint8_t value);
    void on_cycles(int cycles);
    void update_irq();
    void update_nmi();
    uint8_t cia1_portb_r();
    uint8_t read_io(uint16_t addr);
    void write_io(uint16_t addr, uint8_t value);
    uint8_t* color_bank();

    M6502 cpu_;
    Mos6566 vic_;
    Sid6581 sid_;
    Mos6526 cia1_;
    Mos6526 cia2_;
    Mos8722 mmu_;

    // 128 KiB = two 64K banks.
    std::array<uint8_t, 0x20000> ram_{};
    std::array<uint8_t, 0x4000> basic_lo_{};
    std::array<uint8_t, 0x4000> basic_hi_{};
    std::array<uint8_t, 0x4000> kernal_rom_{};   // editor + z80 + kernal
    std::array<uint8_t, 0x4000> c64_rom_{};      // C64 BASIC+KERNAL (unused in C128 boot)
    std::array<uint8_t, 0x2000> char_rom_{};     // 8K CHAROM
    std::array<uint8_t, 0x2000> z80_bios_{};     // loaded / unused (Z80 stubbed)
    std::array<uint8_t, 0x800> color_ram_{};     // 2K (two 1K banks)

    // 8502 on-chip I/O (cassette / unused banking in C128 mode)
    uint8_t port_bits_ = 0x2F;
    uint8_t port_val_ = 0x37;

    bool cia_irq_ = false, vic_irq_ = false, cia_nmi_ = false;

    std::array<uint8_t, 8> keyboard_{};

    int cpu_cycle_debt_ = 0;
    int64_t audio_acc_ = 0;
    std::vector<uint32_t> framebuffer_;
    std::vector<int16_t> audio_;
};

}  // namespace dsp
