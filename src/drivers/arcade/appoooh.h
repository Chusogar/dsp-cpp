#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "core/machine.h"
#include "cpu/z80.h"
#include "sound/msm5205.h"
#include "sound/sn76496.h"
#include "video/gfx.h"

namespace dsp {

// Appoooh / Robo Wres 2001 (Sanritsu / Sega) — port of appoooh_hw.pas
class Appoooh : public Machine {
public:
    enum class Variant { Appoooh, RoboWres };

    static constexpr int kScreenWidth = 256;
    static constexpr int kScreenHeight = 224;
    static constexpr int kScanlines = 256;
    static constexpr double kFramesPerSecond = 60.0;
    static constexpr uint32_t kCpuClock = 18432000 / 6;  // 3_072_000
    static constexpr uint32_t kMsmClock = 384000;

    explicit Appoooh(Variant v = Variant::Appoooh);

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
    int sample_rate() const override { return SN76496::kSampleRate; }

    const char* title() const override {
        return variant_ == Variant::RoboWres ? "Robo Wres 2001" : "Appoooh";
    }

private:
    uint8_t main_read(uint16_t address);
    void main_write(uint16_t address, uint8_t value);
    uint8_t main_in(uint16_t port);
    void main_out(uint16_t port, uint8_t value);
    void update_video();
    void on_cycles(int cycles);
    void adpcm_vclk();
    void draw_sprites(int bank, uint32_t* dest);

    Variant variant_;
    Z80 cpu_;
    SN76496 sn0_, sn1_, sn2_;
    MSM5205 msm_;

    std::array<uint8_t, 0x10000> memory_{};
    std::array<std::array<uint8_t, 0x4000>, 2> rom_bank_{};
    std::vector<uint8_t> robowres_opcodes_;  // decrypted opcodes for 315-5179
    std::vector<uint8_t> adpcm_rom_;

    GfxSet chars0_, chars1_;      // FG / BG tiles
    GfxSet sprites0_, sprites1_;  // sprite banks
    std::array<uint32_t, 512> palette_{};

    std::vector<uint32_t> framebuffer_;

    uint8_t in0_ = 0, in1_ = 0, in2_ = 0;
    uint8_t dsw_ = 0x60;
    uint8_t priority_ = 0xff;
    uint8_t rom_bank_sel_ = 0;
    bool nmi_vblank_ = false;
    bool flip_screen_ = false;
    bool adpcm_playing_ = false;
    uint32_t adpcm_pos_ = 0;
    int sprite_base_ = 0;

    int64_t audio_accum_ = 0;
    int64_t msm_accum_ = 0;
    std::vector<int16_t> audio_;
};

}  // namespace dsp
