#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "core/machine.h"
#include "cpu/z80.h"
#include "machine/i8255.h"
#include "machine/msx_dsk.h"
#include "machine/rp5c01.h"
#include "machine/tape_tzx.h"
#include "sound/ay8910.h"
#include "video/v9938.h"

namespace dsp {

// Generic MSX2 (Philips NMS 8250 layout): V9938, 256 KiB mapper RAM, sub-ROM,
// WD2793 disk ROM in slot 3-2, RP-5C01 RTC. Cartridges in slot 1; .dsk images
// via --disk / load_media().
class Msx2 : public Machine {
public:
    static constexpr uint32_t kMainClock = 3579545;
    static constexpr int kCyclesPerLine = 228;
    static constexpr int kScanlines = 313;  // PAL, like the NMS 8250
    static constexpr double kFramesPerSecond =
        double(kMainClock) / kCyclesPerLine / kScanlines;
    static constexpr int kMapperSegments = 16;  // 256 KiB
    static constexpr int kMaxCartridge = 0x200000;

    Msx2();

    bool init(const std::string& rom_path, std::string* error) override;
    void reset() override;
    void run_frame() override;

    void set_inputs(const MachineInputs& inputs) override;
    void set_dip_switch(int bank, uint8_t value) override;

    const uint32_t* framebuffer() const override { return vdp_.framebuffer(); }
    int screen_width() const override { return V9938::kScreenWidth; }
    int screen_height() const override { return V9938::kScreenHeight; }
    double frames_per_second() const override { return kFramesPerSecond; }

    void drain_audio(std::vector<int16_t>& out) override;
    int sample_rate() const override { return AY8910::kSampleRate; }

    const char* title() const override { return "MSX2"; }
    bool uses_keyboard() const override { return true; }

    bool load_media(const std::string& path, std::string* error) override;
    void tape_toggle_play() override;
    bool tape_loaded() const override { return tape_.is_loaded(); }

    uint16_t debug_pc() const { return z80_.pc(); }
    uint8_t debug_read_byte(uint16_t address) { return read_byte(address); }
    void debug_write_byte(uint16_t address, uint8_t value) { write_byte(address, value); }
    uint8_t debug_read_port(uint16_t port) { return read_port(port); }
    void debug_write_port(uint16_t port, uint8_t value) { write_port(port, value); }
    bool disk_rom_loaded() const { return disk_rom_loaded_; }

private:
    uint8_t read_byte(uint16_t address);
    void write_byte(uint16_t address, uint8_t value);
    uint8_t read_port(uint16_t port);
    void write_port(uint16_t port, uint8_t value);
    void on_vdp_interrupt(bool asserted);
    void on_main_cycles(int cycles);

    uint8_t port_a_read() { return port_a_; }
    uint8_t port_b_read();
    void port_a_write(uint8_t value);
    void port_c_write(uint8_t value);
    uint8_t ay_port_a_read();
    uint8_t ay_port_b_read() { return port_b_ay_; }
    void ay_port_b_write(uint8_t value);

    int primary_slot(int page) const { return (port_a_ >> (page * 2)) & 3; }
    int sub_slot(int prim, int page) const;
    bool slot_expanded(int prim) const { return prim == 3; }

    uint8_t read_slot(int prim, int sub, int page, uint16_t address);
    void write_slot(int prim, int sub, int page, uint16_t address, uint8_t value);
    uint8_t fdc_read(uint16_t address);
    void fdc_write(uint16_t address, uint8_t value);
    bool slot_is_disk(int page) const;
    bool fdc_mapped(uint16_t address) const;

    bool load_cartridge(const std::string& path, std::string* error);
    bool load_tape(const std::string& path, std::string* error);

    Z80 z80_;
    V9938 vdp_;
    AY8910 ay8910_;
    I8255 ppi_;
    Rp5c01 rtc_;
    MsxDisk disk_;
    MsxFdc fdc_;
    TapeTzx tape_;

    std::array<uint8_t, 0x8000> bios_{};
    std::array<uint8_t, 0x4000> subrom_{};
    std::array<uint8_t, 0x4000> diskrom_{};
    std::array<std::array<uint8_t, 0x4000>, kMapperSegments> ram_{};
    std::array<uint8_t, 4> mapper_{3, 2, 1, 0};
    std::vector<uint8_t> cart_;
    int cart_bank0_ = 0;
    int cart_bank1_ = 1;
    bool cart_ascii16_ = false;
    bool disk_rom_loaded_ = false;

    uint8_t subslot_[4] = {};
    uint8_t teclado_ = 0;
    bool last_irq_ = false;
    std::array<uint8_t, 11> keypad_{};
    uint8_t port_a_ = 0;
    uint8_t port_c_ = 0x7f;
    uint8_t port_b_ay_ = 0;
    std::array<uint8_t, 2> joystick_{0x3f, 0x3f};
    uint8_t joy_select_ = 0;
    uint8_t fdc_control_ = 0;

    uint64_t audio_accumulator_ = 0;
    std::vector<int16_t> audio_;
};

}  // namespace dsp
