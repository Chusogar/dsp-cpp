#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "core/machine.h"
#include "cpu/z80.h"
#include "machine/i2cmem.h"
#include "sound/ay8910.h"
#include "sound/sn76496.h"
#include "video/tms9918.h"

namespace dsp {

// Coleco ColecoVision (1982), ported from coleco.pas.
//
// Main CPU: Z80 @ 3.579545 MHz. Video: TMS9918A. Sound: SN76489 always
// present, plus an AY-3-8910 on the Super Game Module cartridges.
//
// `init()` loads the fixed 8 KiB "coleco.rom" BIOS (a directory or zip
// holding it, same convention as Spectrum48k::init loading 48.rom).
// `load_media()` attaches the cartridge (arbitrary name/size, like a tape),
// matching abrir_coleco/abrir_cartucho. Save-state snapshots (.dsp/.csn in
// the original) are not part of dsp-cpp and are intentionally not ported;
// only the hardware itself is.
class ColecoVision : public Machine {
public:
    static constexpr uint32_t kMainClock = 3579545;
    static constexpr int kCyclesPerLine = 228;   // Z80 cycles per scanline
    static constexpr int kScanlines = 262;       // NTSC
    static constexpr double kFramesPerSecond =
        double(kMainClock) / kCyclesPerLine / kScanlines;
    static constexpr int kMaxCartridge = 0x80000;  // 512 KiB (Wizard of Wor)

    ColecoVision();
    ~ColecoVision() override;

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

    const char* title() const override { return "ColecoVision"; }
    bool uses_keyboard() const override { return true; }

    // Attaches a cartridge (.rom/.col, plain or zipped). Mirrors abrir_coleco.
    bool load_media(const std::string& path, std::string* error) override;

private:
    uint8_t read_byte(uint16_t address);
    void write_byte(uint16_t address, uint8_t value);
    uint8_t read_port(uint16_t port);
    void write_port(uint16_t port, uint8_t value);
    void on_vdp_interrupt(bool asserted);
    void on_main_cycles(int cycles);

    bool open_cartridge(const std::vector<uint8_t>& data, std::string* error);
    void load_eeprom_save();
    void save_eeprom();

    Z80 z80_;
    TMS9918 vdp_;
    SN76496 sn76489_;
    AY8910 ay8910_;

    std::array<uint8_t, 0x2000> bios_{};        // coleco.rom, $0000-$1fff
    std::array<uint8_t, 0x10000> memory_{};      // Z80 address space, $2000 and up used
    std::vector<std::array<uint8_t, 0x4000>> mega_cart_rom_;  // 16 KiB banks
    int mega_cart_size_ = 0;                     // bank count - 1, used as an index mask
    bool mega_cart_ = false;

    bool joymode_ = false;
    bool rom_enabled_ = true;
    bool sgm_ram_ = false;
    bool last_nmi_ = false;
    std::array<uint8_t, 2> joystick_{0xff, 0xff};
    std::array<uint16_t, 2> keypad_{0xffff, 0xffff};

    enum class EepromType { None, C08, C256 };
    EepromType eeprom_type_ = EepromType::None;
    I2CMem* i2cmem_ = nullptr;
    std::string cartridge_path_;
    std::string eeprom_save_name_;  // "black_onix" or "boxxle", empty otherwise

    uint64_t audio_accumulator_ = 0;
    std::vector<int16_t> audio_;
};

}  // namespace dsp
