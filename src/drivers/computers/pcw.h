#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "core/machine.h"
#include "cpu/z80.h"
#include "machine/nec765.h"
#include "sound/ay8910.h"

namespace dsp {

// Amstrad PCW8256 / PCW8512 ("Joyce"), from MAME amstrad/pcw.cpp + pcw_v.cpp.
//
// Z80 @ 3.4 MHz, 256 KiB (8256) or 512 KiB (8512) banked RAM, UPD765 FDC,
// 720×256 green-phosphor bitmap via roller RAM. No boot ROM: the printer MCU
// bootstrap is injected into RAM on reset (MAME hack). Printer/keyboard MCUs
// are stubbed.
class Pcw : public Machine {
public:
    enum class Model { PCW8256, PCW8512 };

    static constexpr int kBorderWidth = 8;
    static constexpr int kBorderHeight = 8;
    static constexpr int kDisplayWidth = 720;
    static constexpr int kDisplayHeight = 256;
    static constexpr int kScreenWidth = kDisplayWidth + (kBorderWidth << 1);
    static constexpr int kScreenHeight = kDisplayHeight + (kBorderHeight << 1);

    static constexpr uint32_t kCpuClock = 3400000;
    static constexpr int kFramesPerSecond = 50;
    static constexpr int kLinesPerFrame = 288;  // active + borders, approx PAL
    static constexpr int kCyclesPerFrame = int(kCpuClock / kFramesPerSecond);
    static constexpr int kSampleRate = 44100;

    // Green phosphor (MAME pcw_8xxx_palette).
    static constexpr uint32_t kPenBlack = 0xff000000u;
    static constexpr uint32_t kPenGreen = 0xff4aff00u;  // MAME pcw_8xxx_palette

    explicit Pcw(Model model = Model::PCW8256);

    bool init(const std::string& rom_path, std::string* error) override;
    void reset() override;
    void run_frame() override;

    void set_inputs(const MachineInputs& inputs) override;
    void set_dip_switch(int bank, uint8_t value) override;

    const uint32_t* framebuffer() const override { return framebuffer_.data(); }
    int screen_width() const override { return kScreenWidth; }
    int screen_height() const override { return kScreenHeight; }
    // PCW CRT pixels are roughly 2:1 tall (720×256 → ~4:3), same as MSX2/SAM.
    int display_width() const override { return kScreenWidth; }
    int display_height() const override { return kScreenHeight * 2; }
    double frames_per_second() const override { return kFramesPerSecond; }

    void drain_audio(std::vector<int16_t>& out) override;
    int sample_rate() const override { return kSampleRate; }

    const char* title() const override;
    bool uses_keyboard() const override { return true; }
    bool load_media(const std::string& path, std::string* error) override;

private:
    uint8_t read_byte(uint16_t address);
    void write_byte(uint16_t address, uint8_t value);
    uint8_t read_port(uint16_t port);
    void write_port(uint16_t port, uint8_t value);

    void update_mem(int block, uint8_t data);
    void update_irqs();
    void timer_tick();
    void render_screen();
    uint8_t system_status() const;
    void system_control(uint8_t data);
    void maybe_patch_blit_setup();
    void maybe_patch_abadia_keyboard();

    size_t ram_size() const { return size_t(ram_banks_) * 0x4000; }
    uint8_t* bank_ptr(int bank);

    Model model_;
    int ram_banks_ = 16;  // 256 KiB / 16K

    Z80 cpu_;
    Nec765Fdc fdc_;
    AY8910 ay_;
    uint8_t ay_latch_ = 0;

    std::vector<uint8_t> ram_;
    std::vector<uint8_t> printer_mcu_rom_;

    std::array<uint8_t, 4> banks_{0x80, 0x81, 0x82, 0x83};
    std::array<int, 4> read_bank_{0, 1, 2, 3};
    std::array<int, 4> write_bank_{0, 1, 2, 3};
    uint8_t bank_force_ = 0xf0;

    int interrupt_counter_ = 0;
    uint8_t system_status_bits_ = 0;  // bit5 = FDC INT latched
    int fdc_interrupt_code_ = 2;      // 0=NMI, 1=INT, 2=neither
    bool timer_irq_flag_ = false;
    bool nmi_flag_ = false;
    int timer_pulse_lines_ = 0;

    unsigned roller_ram_addr_ = 0;
    unsigned short roller_ram_offset_ = 0;
    uint8_t vdu_video_control_ = 0;

    bool beeper_on_ = false;
    bool disk_motor_ = false;
    bool frame_50hz_ = true;
    bool in_vblank_ = false;

    std::array<uint8_t, 16> keyboard_{};
    // DK'Tronics AY port A (reg 0x0E), active-low. Filmation titles (Knight Lore)
    // read this via OUT ($AA),$0E / IN A,($A9) when joystick mode is selected.
    uint8_t joystick_porta_ = 0xff;
    std::vector<uint32_t> framebuffer_;
    std::vector<int16_t> audio_;
    int64_t audio_accumulator_ = 0;
    int beeper_phase_ = 0;

    int timer_line_counter_ = 0;

    // Habisoft Abadia (and similar) blit: CALL $32BC skips LD IY/$3309 setup at
    // $32B3. Once phys banks 0/1 are mapped at $0000/$4000, retarget those CALLs.
    bool blit_setup_patched_ = false;
    // Habisoft Abadia: remap logical space ($2F) to the PCW matrix encoding.
    bool abadia_keyboard_patched_ = false;
    // After parchment → game, drop CP $09 so key scans see the real matrix.
    bool abadia_ingame_ = false;
};

}  // namespace dsp
