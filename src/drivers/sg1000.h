#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "core/machine.h"
#include "cpu/z80.h"
#include "sound/sn76496.h"
#include "video/tms9918.h"

namespace dsp {

// Sega SG-1000 (1983), ported from sg1000.pas.
//
// The simplest of the three TMS9918-based consoles ported so far (see also
// ColecoVision and MSX1): no BIOS, the cartridge is mapped straight in at
// $0000, 8 KiB of RAM mirrored across $c000-$ffff, and the VDP drives the
// Z80's regular /INT line (unlike the ColecoVision's NMI). Reuses the Z80,
// TMS9918 and SN76489 chips as-is; no new chips needed.
class Sg1000 : public Machine {
public:
    static constexpr uint32_t kMainClock = 3579545;
    static constexpr int kCyclesPerLine = 228;  // Z80 cycles per scanline
    static constexpr int kScanlines = 262;      // NTSC
    static constexpr double kFramesPerSecond =
        double(kMainClock) / kCyclesPerLine / kScanlines;
    static constexpr int kMaxCartridge = 0xc000;  // 48 KiB, clamped like abrir_sg

    Sg1000();

    bool init(const std::string& rom_path, std::string* error) override;
    void reset() override;
    void run_frame() override;

    void set_inputs(const MachineInputs& inputs) override;
    void set_dip_switch(int bank, uint8_t value) override;

    const uint32_t* framebuffer() const override { return vdp_.framebuffer(); }
    int screen_width() const override { return TMS9918::kScreenWidth; }
    int screen_height() const override { return TMS9918::kScreenHeight; }
    double frames_per_second() const override { return kFramesPerSecond; }

    void drain_audio(std::vector<int16_t>& out) override;
    int sample_rate() const override { return SN76496::kSampleRate; }

    const char* title() const override { return "SG-1000"; }

    // Attaches a cartridge (.sg/.bin, plain or zipped). Mirrors abrir_sg,
    // this machine has no BIOS: init() just resets, cartridges are always
    // loaded through this instead.
    bool load_media(const std::string& path, std::string* error) override;

private:
    uint8_t read_byte(uint16_t address);
    void write_byte(uint16_t address, uint8_t value);
    uint8_t read_port(uint16_t port);
    void write_port(uint16_t port, uint8_t value);
    void on_vdp_interrupt(bool asserted);
    void on_main_cycles(int cycles);

    Z80 z80_;
    TMS9918 vdp_;
    SN76496 sn76489_;

    std::array<uint8_t, 0x10000> memory_{};
    bool ram_8k_ = false;      // some carts (King's Valley, Knightmare...)
                               // physically added extra RAM at $2000-$3fff
    bool mid_8k_ram_ = false;  // others (Othello, Castle) added it at $8000-$9fff

    std::array<uint8_t, 2> keys_{0xff, 0xff};
    bool push_pause_ = false;

    uint64_t audio_accumulator_ = 0;
    std::vector<int16_t> audio_;
};

}  // namespace dsp
