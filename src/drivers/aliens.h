#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "core/machine.h"
#include "cpu/konami.h"
#include "cpu/z80.h"
#include "sound/k007232.h"
#include "sound/ym2151.h"
#include "video/k051960.h"
#include "video/k052109.h"

namespace dsp {

// Aliens (Konami, 1990), ported from aliens_hw.pas.
// Main: Konami-1 @ 12 MHz. Sound: Z80 + YM2151 + K007232.
// Video: K052109 tiles + K051960 sprites. Screen 288×224 (crop 112,16).
class Aliens : public Machine {
public:
    static constexpr int kScreenWidth = 288;
    static constexpr int kScreenHeight = 224;
    static constexpr double kFramesPerSecond = 59.185606;
    static constexpr int kScanlines = 256;
    static constexpr uint32_t kMainClock = 12000000;
    static constexpr uint32_t kSoundClock = 3579545;

    Aliens();

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
    int sample_rate() const override { return YM2151::kSampleRate; }

    const char* title() const override { return "Aliens"; }
    uint16_t main_pc() const { return main_cpu_.pc(); }
    uint16_t sound_pc() const { return sound_cpu_.pc(); }
    uint8_t bank() const { return rom_bank1_; }

private:
    uint8_t main_read(uint16_t address);
    void main_write(uint16_t address, uint8_t value);
    uint8_t sound_read(uint16_t address);
    void sound_write(uint16_t address, uint8_t value);

    void update_video();
    void update_palette_entry(int index);
    void on_sound_cycles(int cycles);
    void mix_audio_line(int sound_cycles);
    bool load_roms(const std::string& rom_path, std::string* error);

    KonamiCpu main_cpu_{kMainClock};
    Z80 sound_cpu_{kSoundClock};
    YM2151 ym2151_{kSoundClock};
    std::unique_ptr<K007232> k007232_;
    std::unique_ptr<K052109> k052109_;
    std::unique_ptr<K051960> k051960_;

    std::array<uint8_t, 0x10000> memoria_{};
    std::array<uint8_t, 0x10000> mem_snd_{};
    std::array<std::array<uint8_t, 0x2000>, 24> rom_bank_{};
    std::array<std::array<uint8_t, 0x400>, 2> ram_bank_{};
    std::array<uint8_t, 0x400> palette_ram_{};
    std::array<uint32_t, 0x200> palette_{};
    std::vector<uint16_t> pens_;
    std::vector<uint32_t> framebuffer_;

    uint8_t sound_latch_ = 0;
    uint8_t bank0_bank_ = 0;
    uint8_t rom_bank1_ = 0;
    bool rmrd_ = false;

    uint8_t in0_ = 0xff, in1_ = 0xff;
    uint8_t dsw_a_ = 0xff, dsw_b_ = 0x5e, dsw_c_ = 0xff;

    int64_t audio_accumulator_ = 0;
    std::vector<int16_t> audio_;
};

}  // namespace dsp
