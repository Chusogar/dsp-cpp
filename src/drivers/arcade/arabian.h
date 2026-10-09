#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "core/machine.h"
#include "cpu/mb88xx.h"
#include "cpu/z80.h"
#include "sound/ay8910.h"

namespace dsp {

// Arabian (Sun Electronics, 1983) — port of dsp-emulator arabian_hw.pas.
//
// Z80 @ 3 MHz with a 256x256 4+4-bit bitmap written through a blitter, an
// AY-3-8910 whose ports select the palette bank and control the MB8841 MCU,
// and the MCU itself, which multiplexes the inputs and DIP switches and
// shares the Z80's 2 KB of work RAM. The monitor is vertical: 234x256.
class Arabian : public Machine {
public:
    MachineType machine_type() const override { return MachineType::Arcade; }
    static constexpr int kScreenWidth = 234;
    static constexpr int kScreenHeight = 256;
    static constexpr int kScanlines = 256;
    static constexpr double kFramesPerSecond = 60.0;
    static constexpr uint32_t kCpuClock = 3000000;
    static constexpr uint32_t kAyClock = 1500000;
    static constexpr uint32_t kMcuClock = 2000000;  // MB8841, 6 clocks per instruction cycle

    Arabian();

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
    int sample_rate() const override { return AY8910::kSampleRate; }

    const char* title() const override { return "Arabian"; }

    // Test hooks.
    uint8_t video_ram(int index) const { return video_ram_[size_t(index) & 0xffff]; }
    uint8_t work_ram(int index) const { return ram_[size_t(index) & 0x7ff]; }
    uint32_t palette_entry(int index) const { return palette_[size_t(index) & 0x1fff]; }
    uint16_t main_pc() const { return cpu_.pc(); }
    bool mcu_running() const { return !mcu_.reset_asserted(); }

private:
    uint8_t main_read(uint16_t address);
    void main_write(uint16_t address, uint8_t value);
    void main_out(uint16_t port, uint8_t value);
    void video_ram_write(uint16_t address, uint8_t value);
    void blit_area(uint8_t plane, uint16_t src, uint8_t x, uint8_t y, uint8_t sx, uint8_t sy);
    void create_palette();
    void update_video();
    void on_cycles(int cycles);

    // MCU ports
    uint8_t mcu_k_read();
    uint8_t mcu_r_read(int port);
    void mcu_r_write(int port, uint8_t value);

    Z80 cpu_;
    Mb88 mcu_;
    AY8910 ay_;

    std::array<uint8_t, 0x8000> rom_{};
    std::array<uint8_t, 0x800> ram_{};
    std::array<uint8_t, 0x10000> video_ram_{};
    std::array<uint8_t, 0x10000> gfx_{};  // blitter source, one 4-bit pixel per byte
    std::array<uint8_t, 8> blitter_{};
    std::array<uint32_t, 0x2000> palette_{};
    std::vector<uint32_t> framebuffer_;

    uint8_t video_control_ = 0;
    bool flip_screen_ = false;
    uint8_t mcu_port_p_ = 0;
    uint8_t mcu_port_o_ = 0;
    std::array<uint8_t, 4> mcu_port_r_{};

    uint8_t in0_ = 1, in1_ = 0, in2_ = 0, in3_ = 0;
    uint8_t dsw_a_ = 0x06;
    uint8_t dsw_b_ = 0x0f;

    double mcu_acc_ = 0;
    int64_t audio_accum_ = 0;
    std::vector<int16_t> audio_;
};

}  // namespace dsp
