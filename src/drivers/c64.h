#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "core/machine.h"
#include "cpu/m6502.h"
#include "machine/mos6526.h"
#include "machine/mos6566.h"
#include "sound/sid6581.h"
#include "machine/tape_tzx.h"
#include "machine/d64_image.h"
#include "machine/c1541.h"

namespace dsp {

// Commodore 64 (PAL), ported from leniad/dsp-emulator
// src/ordenadores/commodore64.pas.
//
// Chips: M6502 (6510 port @ $00/$01), MOS 6569 VIC-II, MOS 6581 SID,
// dual MOS 6526 CIA. PLA banking matches the Pascal actualiza_mem table.
class C64 : public Machine {
public:
    static constexpr int kScreenWidth = Mos6566::kScreenWidth;
    static constexpr int kScreenHeight = Mos6566::kScreenHeight;
    static constexpr uint32_t kCpuClock = 985248;
    static constexpr int kScanlines = 312;
    static constexpr int kCyclesPerLine = 63;
    static constexpr double kFramesPerSecond =
        double(kCpuClock) / (kScanlines * kCyclesPerLine);
    static constexpr int kSampleRate = Sid6581::kSampleRate;

    C64();

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

    const char* title() const override { return "Commodore 64"; }
    bool uses_keyboard() const override { return true; }

    bool load_roms(const std::string& dir, std::string* error);
    bool load_1541_rom(const std::string& path, std::string* error);
    C1541& drive() { return drive_; }
    bool load_media(const std::string& path, std::string* error) override;

private:
    uint8_t read_byte(uint16_t addr);
    void write_byte(uint16_t addr, uint8_t value);
    void on_cycles(int cycles);
    void update_pla();
    void update_irq();
    uint8_t cia1_portb_r();
    bool inject_prg(const std::vector<uint8_t>& data, std::string* error);

    M6502 cpu_;
    Mos6566 vic_;
    Sid6581 sid_;
    Mos6526 cia1_;
    Mos6526 cia2_;

    std::array<uint8_t, 0x10000> ram_{};
    std::array<uint8_t, 0x2000> kernel_rom_{};
    std::array<uint8_t, 0x2000> basic_rom_{};
    std::array<uint8_t, 0x1000> char_rom_{};
    std::array<uint8_t, 0x400> color_ram_{};

    // 6510 on-chip I/O
    uint8_t port_bits_ = 0x2F;
    uint8_t port_val_ = 0x37;
    uint8_t tape_control_ = 0x10;  // sense high = no button
    bool tape_motor_ = false;

    // PLA
    // These values must describe the power-on C64 banking before reset().
    // M6502::reset() reads $FFFC/$FFFD immediately, so KERNAL must already
    // be visible in the $E000-$FFFF window at that moment.
    //
    // write_ram_ is always true for the 8 LORAM/HIRAM/CHAREN configurations
    // modeled here: on real hardware a CPU write always reaches the RAM
    // chip underneath whatever ROM/I-O is currently banked in for reads
    // (only cartridge-driven Ultimax mode can disconnect RAM, which this
    // no-cartridge table does not represent). See update_pla().
    bool write_ram_ = true;
    bool read_ram_a_ = false;
    bool read_ram_e_ = false;
    uint8_t read_ram_d_ = 2;  // 0=RAM 1=CHAR 2=IO

    bool cia_irq_ = false, vic_irq_ = false, cia_nmi_ = false;

    std::array<uint8_t, 8> keyboard_{};

    TapeTzx tape_;
    D64Image disk_;  // legacy autoload helper
    C1541 drive_;
    bool tape_play_ = false;
    bool iec_enabled_ = true;

    // Number of C64 CPU cycles still owed/available at the raster scheduler.
    // M6502::run() executes complete instructions and may overshoot its
    // requested budget. Carrying that overshoot into the next raster line is
    // essential; throwing it away makes CPU/VIC phase drift every line and
    // produces a visibly unstable/rolling display.
    int cpu_cycle_debt_ = 0;

    int64_t audio_acc_ = 0;
    std::vector<uint32_t> framebuffer_;
    std::vector<int16_t> audio_;
};

}  // namespace dsp
