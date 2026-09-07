#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "core/machine.h"
#include "cpu/z80.h"
#include "machine/taito_68705.h"
#include "sound/ay8910.h"
#include "video/gfx.h"

namespace dsp {

// Taito Arkanoid (1986): Z80 + AY-3-8910 + MC68705 (Arkanoid protocol).
// Vertical monitor 224×256; paddle via analog / mouse.
class Arkanoid : public Machine {
public:
    static constexpr int kScreenWidth = 224;
    static constexpr int kScreenHeight = 256;
    static constexpr int kScanlines = 264;
    static constexpr double kFramesPerSecond = 59.185608;
    static constexpr uint32_t kCpuClock = 12000000 / 2;
    static constexpr uint32_t kMcuClock = 3000000;
    static constexpr uint32_t kAyClock = 3000000;

    Arkanoid();

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

    const char* title() const override { return "Arkanoid"; }
    bool uses_pointer() const override { return true; }

private:
    uint8_t cpu_read(uint16_t address);
    void cpu_write(uint16_t address, uint8_t value);
    uint8_t arkanoid_paddle() const;
    void update_video();
    void on_cpu_cycles(int cycles);

    Z80 cpu_;
    Taito68705 mcu_;
    AY8910 ay_;

    std::array<uint8_t, 0x10000> memory_{};
    GfxSet tiles_;
    std::array<uint32_t, 0x200> palette_{};
    std::vector<uint32_t> layer_;       // 256×256 tile layer
    std::vector<uint32_t> sprite_layer_;
    std::vector<uint32_t> framebuffer_;

    uint8_t in0_ = 0x0f;
    uint8_t in1_ = 0xff;
    uint8_t dswa_ = 0xfe;
    uint8_t palette_bank_ = 0;
    uint8_t gfx_bank_ = 0;
    uint8_t paddle_select_ = 0;
    uint8_t paddle_x_[2] = {0x7f, 0x7f};

    int64_t audio_accum_ = 0;
    std::vector<int16_t> audio_;
};

}  // namespace dsp
