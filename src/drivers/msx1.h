#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "core/machine.h"
#include "cpu/z80.h"
#include "machine/i8255.h"
#include "machine/msx_mapper.h"
#include "machine/tape_tzx.h"
#include "sound/ay8910.h"
#include "video/tms9918.h"

namespace dsp {

class Msx1 : public Machine {
public:
    static constexpr uint32_t kMainClock = 3579545;
    static constexpr int kCyclesPerLine = 228;
    static constexpr int kScanlines = 313;
    static constexpr double kFramesPerSecond =
        double(kMainClock) / kCyclesPerLine / kScanlines;
    static constexpr int kMaxCartridge = 0x200000;

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
    bool load_media(const std::string& path, std::string* error) override;
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
    uint8_t port_a_read() { return port_a_; }
    uint8_t port_b_read();
    void port_a_write(uint8_t value);
    void port_c_write(uint8_t value);
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
    std::array<std::array<MemBank, 4>, 4> slot_{};
    std::array<int, 4> page_slot_{};
    std::array<bool, 4> pag_ena_{};
    std::array<bool, 4> pag_rom_{};
    MsxCartridgeMapper cartridge_;

    uint8_t teclado_ = 0;
    bool last_irq_ = false;
    std::array<uint8_t, 10> keypad_{};
    uint8_t port_a_ = 0;
    uint8_t port_c_ = 0x7f;
    uint8_t port_b_ay_ = 0;
    std::array<uint8_t, 2> joystick_{0x3f, 0x3f};
    uint8_t joy_select_ = 0;
    bool auto_type_active_ = false;
    int auto_type_pos_ = 0;
    int auto_type_key_type_ = 0;
    int auto_type_frame_counter_ = 0;
    uint64_t audio_accumulator_ = 0;
    std::vector<int16_t> audio_;
};

}  // namespace dsp
