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

// Casio PV-2000 (1983), ported from leniad/dsp-emulator src/consolas/pv2000.pas.
//
// Z80 @ 3.579545 MHz, TMS9918A VDP (NMI on vblank), SN76489 PSG, 16 KiB BIOS,
// 4 KiB RAM at $7000 and a 16 KiB cartridge window at $c000. The console has a
// full keyboard plus a joystick; cassette I/O is stubbed like the Pascal driver.
class Pv2000 : public Machine {
public:
    static constexpr uint32_t kMainClock = 3579545;  // 7159090 / 2
    static constexpr int kCyclesPerLine = 228;
    static constexpr int kScanlines = 262;  // NTSC
    static constexpr double kFramesPerSecond =
        double(kMainClock) / kCyclesPerLine / kScanlines;
    static constexpr int kMaxCartridge = 0x4000;  // 16 KiB window at $c000

    Pv2000();

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

    const char* title() const override { return "Casio PV-2000"; }
    bool uses_keyboard() const override { return true; }

    // Attaches a cartridge (.bin/.rom, plain or zipped), matching abrir_pv2000.
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
    std::array<uint8_t, 11> keys_{};
    uint8_t keyb_column_ = 0;
    uint8_t last_key_ = 0;
    bool last_nmi_ = false;

    uint64_t audio_accumulator_ = 0;
    std::vector<int16_t> audio_;
};

}  // namespace dsp
