#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "core/machine.h"
#include "cpu/z80.h"
#include "sound/ay8910.h"
#include "video/gfx.h"

namespace dsp {

// Mini Invaders, ported from minivadr_hw.pas.
class Minivadr : public Machine {
public:
    static constexpr int kScreenWidth = 256;
    static constexpr int kScreenHeight = 256;
    static constexpr double kFramesPerSecond = 60.0;
    static constexpr int kScanlines = 256;
    static constexpr uint32_t kCpuClock = 24000000 / 6;
    
    Minivadr();

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
    int sample_rate() const override { return 0; }

    const char* title() const override { return "Mini Invaders"; }

private:
    uint8_t read_byte(uint16_t address);
    void write_byte(uint16_t address, uint8_t value);
    void on_cycles(int cycles);

    void decode_graphics(const std::vector<uint8_t>& gfx_rom);
    void build_palette(const std::vector<uint8_t>& prom);
    void update_video();

    Z80 cpu_;
    
    std::array<uint8_t, 0x10000> memory_{};
    std::array<uint32_t, 256> palette_{};
    //GfxSet chars_;
    std::vector<uint32_t> framebuffer_;

    uint8_t in0_ = 0xff;
    uint8_t in1_ = 0xff;
    uint8_t dsw_ = 0xff;

    //int64_t audio_accumulator_ = 0;
    //std::vector<int16_t> audio_;
};

}  // namespace dsp
