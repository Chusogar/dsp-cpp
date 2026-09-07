#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "core/machine.h"
#include "cpu/m6809.h"
#include "sound/sn76496.h"
#include "video/gfx.h"

namespace dsp {

// Shaolin's Road / Kicker (Konami, 1985), ported from shaolinsroad_hw.pas.
// Main CPU: M6809E @ 1.536 MHz, two SN76496 (1.536 / 3.072 MHz).
// Vertical screen 224x256.
class ShaolinsRoad : public Machine {
public:
    static constexpr int kScreenWidth = 224;
    static constexpr int kScreenHeight = 256;
    static constexpr double kFramesPerSecond = 60.0;
    static constexpr int kScanlines = 256;
    static constexpr uint32_t kMainClock = 18432000 / 12;  // 1.536 MHz
    static constexpr uint32_t kSn0Clock = 18432000 / 12;   // 1.536 MHz
    static constexpr uint32_t kSn1Clock = 18432000 / 6;    // 3.072 MHz

    ShaolinsRoad();

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

    const char* title() const override { return "Shaolin's Road"; }

private:
    uint8_t main_read(uint16_t address);
    void main_write(uint16_t address, uint8_t value);
    void on_cpu_cycles(int cycles);

    void decode_graphics(const std::vector<uint8_t>& char_rom,
                         const std::vector<uint8_t>& sprite_rom);
    void build_palette(const std::vector<uint8_t>& prom);
    void update_video();
    void draw_tile(int offset);
    void draw_sprite(int index);

    M6809 main_cpu_;
    SN76496 sn0_;
    SN76496 sn1_;

    std::array<uint8_t, 0x10000> memory_{};
    std::array<bool, 0x400> dirty_{};
    std::array<uint32_t, 256> palette_{};
    std::array<uint8_t, 0x800> char_lut_{};
    std::array<uint8_t, 0x800> sprite_lut_{};

    GfxSet chars_;
    GfxSet sprites_;

    std::vector<uint32_t> tilemap_;
    std::vector<uint32_t> composite_;
    std::vector<uint32_t> framebuffer_;

    uint8_t palette_bank_ = 0;
    uint8_t scroll_ = 0;
    bool nmi_enable_ = false;
    bool flip_screen_ = false;

    uint8_t in0_ = 0xff;
    uint8_t in1_ = 0xff;
    uint8_t in2_ = 0xff;
    uint8_t dsw_a_ = 0x5a;
    uint8_t dsw_b_ = 0x0f;
    uint8_t dsw_c_ = 0xff;

    int64_t audio_accumulator_ = 0;
    std::vector<int16_t> audio_;
};

}  // namespace dsp
