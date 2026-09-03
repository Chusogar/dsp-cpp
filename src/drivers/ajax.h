#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "core/machine.h"
#include "cpu/konami.h"
#include "cpu/m6809.h"
#include "cpu/z80.h"
#include "sound/k007232.h"
#include "sound/ym2151.h"
#include "video/k051316.h"
#include "video/k051960.h"
#include "video/k052109.h"

namespace dsp {

// Ajax / Typhoon (Konami, 1987), ported from ajax_hw.pas.
//
// Main CPU: Konami-1 @ 12 MHz (ported from konami.pas). Sub CPU is HD6309E in
// 6809 emulation mode → M6809 @ 3 MHz. Sound is Z80 + YM2151 + dual K007232.
//
// Video: K052109 tiles, K051960 sprites, K051316 zoom. Screen is rotated 90°.
class Ajax : public Machine {
public:
    static constexpr int kScreenWidth = 224;   // after 90° rotation of 304×224
    static constexpr int kScreenHeight = 304;
    static constexpr int kNativeWidth = 304;
    static constexpr int kNativeHeight = 224;
    static constexpr double kFramesPerSecond = 59.185606;
    static constexpr int kScanlines = 256;
    static constexpr uint32_t kMainClock = 12000000;
    static constexpr uint32_t kSubClock = 3000000;
    static constexpr uint32_t kSoundClock = 3579545;

    Ajax();

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
    // Debug accessors
    uint16_t main_pc() const { return main_cpu_.pc(); }
    uint16_t main_x() const { return main_cpu_.x; }
    uint16_t main_u() const { return main_cpu_.u; }
    uint16_t main_s() const { return main_cpu_.s; }
    uint16_t sub_pc() const { return sub_cpu_.pc(); }
    uint16_t sound_pc() const { return sound_cpu_.pc(); }
    uint8_t rom_bank1() const { return rom_bank1_; }
    uint8_t main_read_pub(uint16_t a) { return main_read(a); }
    uint32_t palette_at(int i) const { return (i>=0 && i<0x800) ? palette_[size_t(i)] : 0; }
    const uint16_t* zoom_layer() const {
        return k051316_ ? k051316_->layer_data() : nullptr;
    }
    int zoom_layer_nonzero() const {
        if (!k051316_) return 0;
        const uint16_t* p = k051316_->layer_data();
        int n = 0;
        for (int i = 0; i < 512*512; i++) if (p[i]) n++;
        return n;
    }
    void zoom_ctrl(uint8_t out[16]) const {
        if (k051316_) k051316_->control_snapshot(out);
        else for (int i = 0; i < 16; i++) out[i] = 0;
    }
    bool zoom_wrap() const { return k051316_ ? k051316_->wraparound() : false; }

    const char* title() const override { return "Ajax"; }

    const std::vector<std::string>& warnings() const { return warnings_; }

private:
    uint8_t main_read(uint16_t address);
    void main_write(uint16_t address, uint8_t value);
    uint8_t sub_read(uint16_t address);
    void sub_write(uint16_t address, uint8_t value);
    uint8_t sound_read(uint16_t address);
    void sound_write(uint16_t address, uint8_t value);

    void update_video();
    void update_palette_entry(int index);
    void on_sound_cycles(int cycles);
    bool load_roms(const std::string& rom_path, std::string* error);

    // Main: Konami-1 @ 12 MHz (core runs clock/4), Sub: HD6309E → M6809, Sound: Z80
    KonamiCpu main_cpu_{kMainClock};
    M6809 sub_cpu_{kSubClock};
    Z80 sound_cpu_{kSoundClock};
    YM2151 ym2151_{kSoundClock};
    std::unique_ptr<K007232> k007232_0_;
    std::unique_ptr<K007232> k007232_1_;
    std::unique_ptr<K052109> k052109_;
    std::unique_ptr<K051960> k051960_;
    std::unique_ptr<K051316> k051316_;

    std::array<uint8_t, 0x10000> memoria_{};
    std::array<uint8_t, 0x10000> mem_misc_{};
    std::array<uint8_t, 0x10000> mem_snd_{};
    std::array<std::array<uint8_t, 0x2000>, 12> rom_bank_{};
    std::array<std::array<uint8_t, 0x2000>, 9> rom_sub_bank_{};
    std::array<uint8_t, 0x1000> palette_ram_{};
    std::array<uint32_t, 0x800> palette_{};
    std::vector<uint16_t> pens_;
    std::vector<uint32_t> framebuffer_;

    uint8_t sound_latch_ = 0;
    uint8_t rom_bank1_ = 0;
    uint8_t rom_bank2_ = 0;
    bool sub_firq_enable_ = false;
    uint8_t gun_rand_ = 0;
    int watchdog_ = 0;
    bool priority_ = false;

    uint8_t in0_ = 0xff, in1_ = 0xff, in2_ = 0xff;
    uint8_t dsw_a_ = 0xff, dsw_b_ = 0x5a, dsw_c_ = 0xff;

    // Pascal-style residual cycle budgets (frame_main/sub/snd)
    double frame_main_ = 0;
    double frame_sub_ = 0;
    double frame_snd_ = 0;
    int64_t audio_accumulator_ = 0;
    std::vector<int16_t> audio_;
    std::vector<std::string> warnings_;
};

}  // namespace dsp
