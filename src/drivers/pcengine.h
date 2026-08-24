#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "core/machine.h"
#include "cpu/hu6280.h"
#include "sound/huc6280_psg.h"
#include "video/huc6260.h"
#include "video/huc6270.h"

namespace dsp {

class PcEngine : public Machine {
public:
    static constexpr uint32_t kCpuClock = 7159090;
    static constexpr int kLinesPerFrame = HuC6270::kLinesPerFrame;
    static constexpr double kFramesPerSecond = 59.94;
    static constexpr int kMaxWidth = HuC6270::kMaxWidth;
    static constexpr int kMaxHeight = 224;
    static constexpr int kSampleRate = HuC6280Psg::kSampleRate;
    static constexpr size_t kMaxRom = 0x100000;
    static constexpr size_t kRamSize = 0x8000;  // SuperGrafx has 32KB

    explicit PcEngine(bool supergrafx = false);

    bool init(const std::string& rom_path, std::string* error) override;
    void reset() override;
    void run_frame() override;

    void set_inputs(const MachineInputs& inputs) override;
    void set_dip_switch(int bank, uint8_t value) override;

    const uint32_t* framebuffer() const override { return framebuffer_.data(); }
    int screen_width() const override { return width_; }
    int screen_height() const override { return kMaxHeight; }
    double frames_per_second() const override { return kFramesPerSecond; }

    void drain_audio(std::vector<int16_t>& out) override;
    int sample_rate() const override { return kSampleRate; }

    const char* title() const override {
        return supergrafx_ ? "SuperGrafx" : "PC-Engine";
    }

    bool load_media(const std::string& path, std::string* error) override;

private:
    uint8_t read_physical(uint32_t address);
    void write_physical(uint32_t address, uint8_t value);
    void on_cycles(int cycles);
    bool load_hucard(const std::vector<uint8_t>& data, std::string* error);
    void update_vdc_irq();

    bool supergrafx_ = false;
    int width_ = 256;

    HuC6280 cpu_;
    HuC6270 vdc0_;
    HuC6270 vdc1_;   // SuperGrafx second VDC
    HuC6202 vpc_;
    HuC6260 vce_;
    HuC6280Psg psg_;

    std::vector<uint8_t> rom_;
    std::array<uint8_t, kRamSize> ram_{};
    std::array<uint32_t, kMaxWidth * kMaxHeight> framebuffer_{};
    std::array<uint16_t, kMaxWidth> line0_{};
    std::array<uint16_t, kMaxWidth> line1_{};

    uint8_t joy_data_ = 0xFF;
    uint8_t joy_sel_ = 0;
    uint8_t joy_clr_ = 0;
    bool vdc0_irq_ = false;
    bool vdc1_irq_ = false;
    int cycles_per_line_ = 0;
    std::vector<int16_t> audio_;
};

}  // namespace dsp
