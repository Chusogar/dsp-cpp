#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "core/machine.h"
#include "cpu/z80.h"
#include "machine/i8255.h"
#include "machine/tape_tzx.h"
#include "sound/ay8910.h"
#include "video/tms9918.h"

namespace dsp {

// MSX1 (1983), ported from msx1.pas. Modeled after the Panasonic/Misubishi
// "mpc100bios.rom" MSX1 machines the original targeted: 2 cartridge slots
// (BIOS in slot 0, cartridge in slot 1) plus a RAM-only slot 3, an
// i8255 PPI driving the slot selector/keyboard matrix/tape motor, a
// TMS9918A VDP and an AY-3-8910 (whose own I/O ports read the joysticks and
// the cassette input, on top of making sound).
//
// Chips reused as-is from dsp-cpp: Z80, TMS9918 (already ported for the
// ColecoVision driver), AY8910, I8255, TapeTzx. Nothing new needed writing.
class Msx1 : public Machine {
public:
    static constexpr uint32_t kMainClock = 3579545;
    static constexpr int kCyclesPerLine = 228;  // Z80 cycles per scanline
    static constexpr int kScanlines = 313;      // PAL
    static constexpr double kFramesPerSecond =
        double(kMainClock) / kCyclesPerLine / kScanlines;
    static constexpr int kMaxCartridge = 0x80000;  // 512 KiB

    Msx1();

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
    int sample_rate() const override { return AY8910::kSampleRate; }

    const char* title() const override { return "MSX1"; }
    bool uses_keyboard() const override { return true; }

    // Cartridge (.rom, plain or zipped), mirrors abrir_msx1.
    bool load_media(const std::string& path, std::string* error) override;
    // Tape (.tzx/.tsx/.cas/.wav), mirrors msx_tapes.
    bool load_tape(const std::string& path, std::string* error);
    void tape_play();
    void tape_stop();
    bool tape_playing() const { return tape_.is_playing(); }

private:
    uint8_t read_byte(uint16_t address);
    void write_byte(uint16_t address, uint8_t value);
    uint8_t read_port(uint16_t port);
    void write_port(uint16_t port, uint8_t value);
    void on_vdp_interrupt(bool asserted);
    void on_main_cycles(int cycles);

    // i8255 ports.
    uint8_t port_a_read() { return port_a_; }
    uint8_t port_b_read();
    void port_a_write(uint8_t value);
    void port_c_write(uint8_t value);

    // AY8910's own I/O ports (joystick + cassette input on port A, joystick
    // select + general purpose data on port B).
    uint8_t ay_port_a_read();
    uint8_t ay_port_b_read() { return port_b_ay_; }
    void ay_port_b_write(uint8_t value);

    void start_auto_type(int key_type);
    void auto_type_step();

    Z80 z80_;
    TMS9918 vdp_;
    AY8910 ay8910_;
    I8255 ppi_;
    TapeTzx tape_;

    struct MemBank {
        std::array<uint8_t, 0x4000> mem{};
        bool rom = false;
        bool ena = false;
    };
    std::array<std::array<MemBank, 4>, 4> slot_{};  // slot_[slot][page]
    std::array<int, 4> page_slot_{};
    std::array<bool, 4> pag_ena_{};
    std::array<bool, 4> pag_rom_{};

    uint8_t teclado_ = 0;
    bool last_irq_ = false;
    std::array<uint8_t, 10> keypad_{};
    uint8_t port_a_ = 0;
    uint8_t port_c_ = 0x7f;
    uint8_t port_b_ay_ = 0;
    std::array<uint8_t, 2> joystick_{0x3f, 0x3f};
    uint8_t joy_select_ = 0;

    // Auto-type: pokes CLOAD/RUN keystrokes into the matrix after a tape
    // loads, mirroring key_press/run_key_N (driven by timers.init in the
    // original; here it is driven from run_frame() instead).
    bool auto_type_active_ = false;
    int auto_type_pos_ = 0;
    int auto_type_key_type_ = 0;
    int auto_type_frame_counter_ = 0;

    uint64_t audio_accumulator_ = 0;
    std::vector<int16_t> audio_;
};

}  // namespace dsp
