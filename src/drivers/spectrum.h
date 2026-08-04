#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "core/machine.h"
#include "cpu/z80.h"
#include "machine/spectrum_tape.h"

namespace dsp {

// Sinclair ZX Spectrum 48K, ported from spectrum_48k.pas.
class Spectrum48 : public Machine {
public:
    static constexpr int kBorderLeft = 48;
    static constexpr int kBorderTop = 48;
    static constexpr int kBorderBottom = 40;
    static constexpr int kScreenWidth = kBorderLeft + 256 + kBorderLeft;
    static constexpr int kScreenHeight = kBorderTop + 192 + kBorderBottom;

    static constexpr uint32_t kCpuClock = 14000000 / 4;  // 3.5 MHz
    static constexpr int kScanlines = 312;
    static constexpr int kCyclesPerLine = 224;
    static constexpr int kCyclesPerFrame = kScanlines * kCyclesPerLine;  // 69888
    static constexpr int kIrqCycles = 32;
    static constexpr int kSampleRate = 44100;

    Spectrum48();

    bool init(const std::string& rom_path, std::string* error) override;
    void reset() override;
    void run_frame() override;

    void set_inputs(const MachineInputs& inputs) override;
    void set_dip_switch(int bank, uint8_t value) override;

    const uint32_t* framebuffer() const override { return framebuffer_.data(); }
    int screen_width() const override { return kScreenWidth; }
    int screen_height() const override { return kScreenHeight; }
    double frames_per_second() const override { return double(kCpuClock) / kCyclesPerFrame; }

    void drain_audio(std::vector<int16_t>& out) override;
    int sample_rate() const override { return kSampleRate; }

    const char* title() const override { return "ZX Spectrum 48K"; }
    bool uses_keyboard() const override { return true; }
    bool load_media(const std::string& path, std::string* error) override;

    // Exposed for the tests: the ULA ports and the keyboard matrix.
    uint8_t read_port(uint16_t port);
    void write_port(uint16_t port, uint8_t value);

private:
    uint8_t read_byte(uint16_t address) const { return memory_[address]; }
    void write_byte(uint16_t address, uint8_t value);
    void on_cycles(int cycles);

    void render_line(int line);
    void update_tape();

    Z80 cpu_;
    SpectrumTape tape_;

    std::array<uint8_t, 0x10000> memory_{};
    std::array<uint8_t, 0x4000> rom_{};
    std::array<uint8_t, 8> keys_{};  // one byte per half row, active low
    std::array<uint32_t, 16> palette_{};
    std::vector<uint32_t> framebuffer_;

    uint8_t border_ = 0;
    uint8_t speaker_ = 0;   // bit 4 of port $fe
    uint8_t mic_ = 0;       // bit 3 of port $fe
    uint8_t joystick_ = 0;  // Kempston, active high
    uint8_t flash_counter_ = 0;
    bool flash_ = false;
    bool issue2_ = false;

    int64_t audio_accumulator_ = 0;
    int64_t audio_level_ = 0;
    std::vector<int16_t> audio_;
};

}  // namespace dsp
