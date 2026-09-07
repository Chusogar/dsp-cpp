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

// Toaplan / Taito Slap Fight (1986): dual Z80 + MC68705 + 2×AY8910.
// Also covers Tiger Heli hardware with different ROMs/MCU type.
class SlapFight : public Machine {
public:
    enum class Variant { SlapFight, TigerHeli };

    static constexpr int kScreenWidth = 239;
    static constexpr int kScreenHeight = 280;
    static constexpr int kScanlines = 270;
    static constexpr double kFramesPerSecond = 36000000.0 / 6.0 / 388.0 / 270.0;
    static constexpr uint32_t kMainClock = 6000000;
    static constexpr uint32_t kSoundClock = 3000000;
    static constexpr uint32_t kMcuClock = 3000000;
    static constexpr uint32_t kAyClock = 1500000;

    explicit SlapFight(Variant v = Variant::SlapFight);

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

    const char* title() const override {
        return variant_ == Variant::TigerHeli ? "Tiger Heli" : "Slap Fight";
    }

private:
    uint8_t main_read(uint16_t address);
    void main_write(uint16_t address, uint8_t value);
    uint8_t main_in(uint16_t port);
    void main_out(uint16_t port, uint8_t value);
    uint8_t sound_read(uint16_t address);
    void sound_write(uint16_t address, uint8_t value);
    void update_video();
    void on_sound_cycles(int cycles);
    void mcu_scroll_y(int pos, uint8_t value);

    Variant variant_;
    Z80 main_cpu_;
    Z80 sound_cpu_;
    Taito68705 mcu_;
    AY8910 ay0_;
    AY8910 ay1_;

    std::array<uint8_t, 0x10000> memory_{};
    std::array<std::array<uint8_t, 0x4000>, 2> rom_bank_{};
    std::array<uint8_t, 0x10000> sound_mem_{};

    GfxSet chars_;
    GfxSet tiles_;
    GfxSet sprites_;
    std::array<uint32_t, 256> palette_{};

    std::vector<uint32_t> fg_layer_;   // 256×512
    std::vector<uint32_t> bg_layer_;
    std::vector<uint32_t> sprite_layer_;
    std::vector<uint32_t> framebuffer_;

    uint8_t in0_ = 0xff;
    uint8_t in1_ = 0xff;
    uint8_t dswa_ = 0x7f;
    uint8_t dswb_ = 0xff;
    uint8_t rom_bank_sel_ = 0;
    uint8_t scroll_x_ = 0;
    uint16_t scroll_y_ = 0;
    bool ena_irq_ = false;
    bool sound_nmi_ = false;
    bool sound_reset_ = true;
    int status_state_ = 0;
    int sound_nmi_counter_ = 0;

    int64_t audio_accum_ = 0;
    std::vector<int16_t> audio_;
};

}  // namespace dsp
