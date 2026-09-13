#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "core/machine.h"
#include "cpu/m6502.h"
#include "sound/pokey.h"
#include "video/antic.h"
#include "video/gtia.h"

namespace dsp {

// Atari 8-bit home computers: the original 400/800 (1979) and the later
// XL/XE line. 6502B at 1.79 MHz with the ANTIC display processor, GTIA
// colour/sprite chip, POKEY for sound, keyboard and serial I/O, and a
// 6520 PIA for the joystick ports (and, on XL/XE, memory banking).
class Atari8 : public Machine {
public:
    enum class Model { A800, A800XL, A800XE };

    static constexpr uint32_t kClock = 1789790;  // NTSC 6502 clock
    static constexpr int kSampleRate = 44100;

    explicit Atari8(Model model = Model::A800);

    bool init(const std::string& rom_path, std::string* error) override;
    void reset() override;
    void run_frame() override;
    void set_inputs(const MachineInputs& inputs) override;
    void set_dip_switch(int bank, uint8_t value) override;
    const uint32_t* framebuffer() const override { return framebuffer_.data(); }
    int screen_width() const override { return Antic::kScreenWidth; }
    int screen_height() const override { return Antic::kScreenHeight; }
    double frames_per_second() const override {
        return double(kClock) / double(Antic::kLinesPerFrame * Antic::kCyclesPerLine);
    }
    void drain_audio(std::vector<int16_t>& out) override;
    int sample_rate() const override { return kSampleRate; }
    const char* title() const override;
    bool uses_keyboard() const override { return true; }
    bool load_media(const std::string& path, std::string* error) override;

private:
    uint8_t cpu_read(uint16_t address);
    void cpu_write(uint16_t address, uint8_t value);
    uint8_t antic_read(uint16_t address);  // ANTIC always sees RAM
    void on_cycles(int cycles);

    uint8_t pia_read(uint16_t offset);
    void pia_write(uint16_t offset, uint8_t value);
    void update_banking();

    // Services one SIOV call straight from the mounted disk image instead
    // of emulating the serial protocol cycle by cycle.
    void service_sio();
    bool disk_sector(int sector, std::vector<uint8_t>& out) const;
    bool disk_write_sector(int sector, const uint8_t* data, size_t len);

    void apply_keys(const MachineInputs& in);

    M6502 cpu_;
    Gtia gtia_;
    Antic antic_;
    Pokey pokey_;

    Model model_;
    std::array<uint8_t, 0x10000> ram_{};
    std::vector<uint8_t> os_;     // 10K (800) or 16K (XL/XE)
    std::vector<uint8_t> basic_;  // 8K, XL/XE only
    bool has_basic_ = false;

    // PIA 6520.
    uint8_t porta_ = 0xff, portb_ = 0xff;
    uint8_t pactl_ = 0, pbctl_ = 0;
    uint8_t porta_dir_ = 0, portb_dir_ = 0;

    // Derived from PORTB on XL/XE.
    bool os_enabled_ = true, basic_enabled_ = false, selftest_enabled_ = false;

    // Joysticks (two ports, active low nibbles) and triggers.
    uint8_t stick_[2] = {0x0f, 0x0f};
    uint8_t trig_[2] = {1, 1};
    uint8_t console_ = 0x07;  // START/SELECT/OPTION, active low
    // Counts down while OPTION is force-held over a disk cold start (see
    // the console handler in the .cpp).
    int boot_option_frames_ = 0;

    std::vector<uint8_t> disk_;  // raw .ATR image
    int disk_sector_size_ = 128;
    int disk_sectors_ = 0;
    bool disk_loaded_ = false;

    std::array<uint32_t, size_t(Antic::kScreenWidth) * Antic::kScreenHeight> framebuffer_{};
    std::vector<int16_t> audio_;
    int64_t audio_acc_ = 0;
    bool irq_line_ = false;
};

}  // namespace dsp
