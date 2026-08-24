#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "core/machine.h"
#include "cpu/hu6280.h"
#include "sound/huc6280_psg.h"
#include "video/huc6260.h"
#include "video/huc6270.h"

namespace dsp {

// NEC PC Engine / TurboGrafx-16. Same split as the NES and Game Boy ports: the
// driver owns the HuCard mapping through the HuC6280 MPR banks, the frame loop
// and the I/O port, while the HuC6270 (VDC), HuC6260 (VCE) and the PSG live in
// video/ and sound/.
class PcEngine : public Machine {
public:
    static constexpr uint32_t kMasterClock = 21477270;
    static constexpr uint32_t kClock = kMasterClock / 3;  // 7.16 MHz CPU
    static constexpr int kScanlines = 262;
    static constexpr int kCyclesPerLine = 1365 / 3;
    static constexpr double kFramesPerSecond =
        double(kMasterClock) / (1365.0 * double(kScanlines));
    // Fixed output: 256 wide modes are doubled and 512 wide modes fit exactly,
    // with the lines doubled so the pixels stay roughly square.
    static constexpr int kScreenWidth = 512;
    static constexpr int kScreenHeight = 484;
    static constexpr int kMaxCartridge = 0x400000;  // 4 MiB, enough for SF2

    PcEngine();

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
    int sample_rate() const override { return HuC6280Psg::kSampleRate; }

    const char* title() const override { return "PC Engine"; }

    bool load_media(const std::string& path, std::string* error) override;
    bool load_hucard(const std::vector<uint8_t>& data, std::string* error);

    uint8_t debug_read(uint32_t address) { return read_byte(address); }
    void debug_write(uint32_t address, uint8_t value) { write_byte(address, value); }
    uint16_t debug_pc() const { return cpu_.pc(); }
    HuC6270& debug_vdc() { return vdc_; }
    HuC6260& debug_vce() { return vce_; }

private:
    uint8_t read_byte(uint32_t address);
    void write_byte(uint32_t address, uint8_t value);
    uint8_t io_read(uint16_t offset);
    void io_write(uint16_t offset, uint8_t value);
    uint8_t joypad_read() const;
    void joypad_write(uint8_t value);
    uint32_t rom_offset(uint32_t bank) const;
    void on_cpu_cycles(int cycles);
    void blit_line(int display_line, int width);

    HuC6280 cpu_{kClock};
    HuC6270 vdc_;
    HuC6260 vce_;
    HuC6280Psg psg_{HuC6280Psg::kClock};

    std::vector<uint8_t> rom_;
    std::array<uint8_t, 0x2000> ram_{};
    std::array<uint8_t, 0x800> backup_ram_{};
    std::array<uint32_t, size_t(kScreenWidth) * size_t(kScreenHeight)> framebuffer_{};
    std::array<uint16_t, HuC6270::kMaxWidth> line_{};
    std::vector<int16_t> audio_;

    uint8_t pad_ = 0xff;
    bool pad_select_ = false;
    bool pad_clear_ = false;
    uint8_t sf2_bank_ = 0;
    uint64_t audio_accumulator_ = 0;
};

}  // namespace dsp
