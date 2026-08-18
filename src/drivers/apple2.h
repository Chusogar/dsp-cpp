#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "core/machine.h"
#include "cpu/m6502.h"
#include "machine/diskii.h"
#include "video/apple2_video.h"

namespace dsp {

// Apple II family: original ][ (Integer BASIC), II+, IIe and IIe Enhanced
// ("IIe+"), with a Disk II controller in slot 6.
class Apple2 : public Machine {
public:
    enum class Model { II, IIPlus, IIe, IIeEnhanced };

    static constexpr uint32_t kClock = 1020484;
    static constexpr int kScanlines = 262;
    static constexpr int kCyclesPerLine = 65;
    static constexpr double kFramesPerSecond =
        double(kClock) / double(kScanlines * kCyclesPerLine);
    static constexpr int kSampleRate = 44100;

    explicit Apple2(Model model = Model::IIPlus);

    bool init(const std::string& rom_path, std::string* error) override;
    void reset() override;
    void run_frame() override;

    void set_inputs(const MachineInputs& inputs) override;
    void set_dip_switch(int bank, uint8_t value) override;

    const uint32_t* framebuffer() const override { return framebuffer_.data(); }
    int screen_width() const override { return Apple2Video::kWidth; }
    int screen_height() const override { return Apple2Video::kHeight; }
    double frames_per_second() const override { return kFramesPerSecond; }

    void drain_audio(std::vector<int16_t>& out) override;
    int sample_rate() const override { return kSampleRate; }

    const char* title() const override;
    bool uses_keyboard() const override { return true; }

    bool load_media(const std::string& path, std::string* error) override;

    void poke(uint16_t address, uint8_t value) { write_byte(address, value); }
    uint8_t peek(uint16_t address) { return read_byte(address); }
    void set_pc(uint16_t value) { cpu_.set_pc(value); }
    uint16_t pc() const { return cpu_.pc(); }

    // Unit tests that do not ship copyrighted firmware.
    void init_synthetic_roms();
    bool disk_prom_loaded() const { return disk_prom_loaded_; }
    bool disk_loaded() const { return disk_.loaded(); }
    DiskIi& disk() { return disk_; }
    Apple2Video& video() { return video_; }

private:
    bool is_iie() const { return model_ == Model::IIe || model_ == Model::IIeEnhanced; }

    uint8_t read_byte(uint16_t address);
    void write_byte(uint16_t address, uint8_t value);
    uint8_t read_io(uint16_t address);
    void write_io(uint16_t address, uint8_t value);
    uint8_t read_cx(uint16_t address);
    void access_language_card(uint16_t address);
    uint8_t* lc_ptr(uint16_t address, bool aux);
    void on_cpu_cycles(int cycles);
    void apply_keyboard(const MachineInputs& inputs);
    uint8_t ascii_from_keys(const MachineInputs& inputs) const;
    bool load_roms(const std::string& rom_path, std::string* error);
    bool try_load_disk_prom(const std::string& rom_path);

    Model model_;
    M6502 cpu_;
    DiskIi disk_;
    Apple2Video video_;

    std::array<uint8_t, 0x10000> main_{};
    std::array<uint8_t, 0x10000> aux_{};
    std::array<uint8_t, 0x1000> lc_bank2_{};
    std::array<uint8_t, 0x1000> aux_lc_bank2_{};
    std::array<uint8_t, 0x4000> rom_{};
    std::array<uint8_t, 0x1000> chargen_{};
    std::array<uint8_t, 0x100> disk_prom_{};
    int chargen_size_ = 0;
    bool disk_prom_loaded_ = false;

    bool text_ = true;
    bool mixed_ = false;
    bool page2_ = false;
    bool hires_ = false;
    bool store80_ = false;
    bool ramrd_ = false;
    bool ramwrt_ = false;
    bool intcxrom_ = false;
    bool altzp_ = false;
    bool slotc3rom_ = false;
    bool col80_ = false;
    bool altcharset_ = false;
    bool an3_ = true;
    bool c8rom_ = false;
    bool lc_read_ram_ = false;
    bool lc_write_ram_ = false;
    bool lc_bank2_ = true;
    bool lc_prewrite_ = false;
    bool caps_lock_ = true;
    bool speaker_ = false;
    bool open_apple_ = false;
    bool closed_apple_ = false;

    uint8_t keyboard_ = 0;
    bool any_key_ = false;
    std::array<bool, size_t(Key::Count)> prev_keys_{};

    int scanline_ = 0;
    std::array<uint32_t, Apple2Video::kWidth * Apple2Video::kHeight> framebuffer_{};
    std::vector<int16_t> audio_;
    int64_t audio_acc_ = 0;
};

}  // namespace dsp
