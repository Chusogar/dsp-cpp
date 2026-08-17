#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "core/machine.h"
#include "cpu/m6502.h"
#include "machine/lynx_mikey.h"
#include "machine/lynx_suzy.h"

namespace dsp {

// Atari Lynx (1989). 65C02 inside Mikey at 16 MHz with wait states (effective
// 4 MHz), 64 KiB shared DRAM, Suzy sprite/math coprocessor at $FC00 and Mikey
// timers/LCD/sound/UART at $FD00. MAPCTL at $FFF9 overlays those windows and
// the 512-byte bootstrap ROM at $FE00 (`lynxboot.img`, the same dump Handy and
// Mednafen use). Cartridges are sequential ROM shifted through Suzy RCART0
// ($FCB2) with a 74HC164/4040 address generator clocked from Mikey SYSCTL1.
class AtariLynx : public Machine {
public:
    static constexpr uint32_t kSystemClock = 16000000;
    static constexpr uint32_t kCpuClock = 4000000;
    static constexpr int kScanlines = 105;
    static constexpr int kCyclesPerLine = 636;  // 4 MHz / ~59.8 Hz / 105
    static constexpr double kFramesPerSecond =
        double(kCpuClock) / kCyclesPerLine / kScanlines;
    static constexpr int kScreenWidth = LynxSuzy::kScreenWidth;
    static constexpr int kScreenHeight = LynxSuzy::kScreenHeight;
    static constexpr size_t kMaxCartridge = 1024 * 1024;

    AtariLynx();

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
    int sample_rate() const override { return LynxMikey::kSampleRate; }

    const char* title() const override { return "Atari Lynx"; }

    bool load_media(const std::string& path, std::string* error) override;

    // Handy/Mednafen `lynxboot.img` (512 bytes, CRC 0d973c9d or e1ffecb6).
    bool load_bios(const std::string& path, std::string* error);
    bool bios_loaded() const { return bios_loaded_; }

    uint16_t debug_pc() const { return cpu_.pc(); }
    uint8_t debug_iodir() const { return mikey_.iodir(); }

private:
    uint8_t read_byte(uint16_t address);
    void write_byte(uint16_t address, uint8_t value);
    void on_cpu_cycles(int cycles);
    void present_frame();
    uint8_t cart_read();
    void cart_strobe(uint8_t sysctl, uint8_t iodat);
    void install_fallback_bios();
    bool install_bios(const std::vector<uint8_t>& data, std::string* error);
    void search_bios(const std::string& rom_path);
    bool load_cart_from_directory(const std::string& directory, std::string* error);

    M6502 cpu_;
    LynxSuzy suzy_;
    LynxMikey mikey_;

    std::array<uint8_t, 0x10000> ram_{};
    std::array<uint8_t, 0x200> boot_rom_{};
    bool bios_loaded_ = false;
    std::array<uint32_t, kScreenWidth * kScreenHeight> framebuffer_{};

    std::vector<uint8_t> cart_;
    uint16_t granularity_ = 0x400;
    uint32_t audin_offset_ = 0;
    uint8_t cart_block_ = 0;
    uint16_t cart_counter_ = 0;
    uint8_t last_sysctl_ = 0;
    uint8_t mapctl_ = 0;

    uint64_t audio_accumulator_ = 0;
    std::vector<int16_t> audio_;
};

}  // namespace dsp
