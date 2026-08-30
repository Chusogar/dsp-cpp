#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "core/machine.h"
#include "cpu/konami.h"
#include "cpu/z80.h"
#include "machine/eeprom_er5911.h"
#include "sound/k053260.h"
#include "sound/ym2151.h"
#include "video/k052109.h"
#include "video/k053246.h"
#include "video/k053251.h"

namespace dsp {

// The Simpsons (Konami GX072, 1991) — port of simpsons_hw.pas
// Main: Konami-1 (053248) @ 12 MHz. Sound: Z80 + YM2151 + K053260.
// Video: K052109 tiles + K053246/247 sprites + K053251 priority.
// Screen 288×224 (crop 112,16), ROT0.
class Simpsons : public Machine {
public:
    static constexpr int kScreenWidth = 288;
    static constexpr int kScreenHeight = 224;
    static constexpr double kFramesPerSecond = 59.185606;
    static constexpr int kScanlines = 264;
    static constexpr uint32_t kMainClock = 12000000;
    static constexpr uint32_t kSoundClock = 3579545;

    Simpsons();

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

    const char* title() const override { return "The Simpsons"; }
    uint16_t main_pc() const { return main_cpu_.pc(); }
    uint16_t sound_pc() const { return sound_cpu_.pc(); }

private:
    uint8_t main_read(uint16_t address);
    void main_write(uint16_t address, uint8_t value);
    uint8_t sound_read(uint16_t address);
    void sound_write(uint16_t address, uint8_t value);

    void update_video();
    void update_palette_entry(int index);
    void objdma();
    void on_sound_cycles(int cycles);
    void mix_audio_line(int sound_cycles);
    bool load_roms(const std::string& rom_path, std::string* error);

    KonamiCpu main_cpu_{kMainClock};
    Z80 sound_cpu_{kSoundClock};
    YM2151 ym2151_{kSoundClock};
    std::unique_ptr<K053260> k053260_;
    std::unique_ptr<K052109> k052109_;
    std::unique_ptr<K053246> k053246_;
    K053251 k053251_;
    EepromEr5911 eeprom_;

    std::array<uint8_t, 0x10000> memoria_{};
    std::array<uint8_t, 0x10000> mem_snd_{};
    std::array<std::array<uint8_t, 0x2000>, 0x40> rom_bank_{};
    std::array<std::array<uint8_t, 0x4000>, 8> sound_rom_bank_{};
    std::array<uint8_t, 0x1000> buffer_paleta_{};
    std::array<uint16_t, 0x800> sprite_ram_{};
    std::array<uint32_t, 0x800> palette_{};

    uint8_t bank0_bank_ = 0;
    uint8_t bank2000_bank_ = 0;
    uint8_t rom_bank1_ = 0;
    uint8_t sound_bank_ = 0;
    int nmi_timer_ = 0;  // cycles until NMI clear
    bool ym_irq_ = false;
    bool main_snd_irq_ = false;
    void update_sound_irq();
    bool firq_enabled_ = false;

    uint8_t in0_ = 0xff, in1_ = 0xff, in2_ = 0xff;
    uint8_t sprite_colorbase_ = 0;
    std::array<uint8_t, 3> layer_colorbase_{};
    std::array<uint8_t, 3> layerpri_{};

    // DMA FIRQ timing
    int sprite_dma_timer_ = 0;  // >0 counting down to FIRQ assert/clear

    double frame_main_ = 0;
    double frame_snd_ = 0;

    std::vector<uint16_t> pens_;
    std::vector<uint32_t> framebuffer_;
    std::vector<int16_t> audio_buffer_;
};

}  // namespace dsp
