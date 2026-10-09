#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "core/machine.h"
#include "cpu/m68000.h"
#include "cpu/mcs51.h"
#include "cpu/z80.h"
#include "sound/ym2151.h"
#include "video/gfx.h"

namespace dsp {

// Bionic Commando (Capcom, 1987) — port of dsp-emulator bioniccommando_hw.pas,
// with the layer priorities, tile attributes and palette of MAME's bionicc.cpp.
//
// 68000 @ 12 MHz, Z80 @ 3.58 MHz with a YM2151, and an i8751 MCU that copies
// data between the main RAM and the sound CPU while the 68000 is halted
// ("DMA"). Video: 8x8 text layer, 8x8 background and 16x16 foreground
// scrolling tilemaps (the foreground split in front/behind halves) and 160
// buffered 16x16 sprites; 1024 colours in RRRRGGGGBBBBIIII format.
class BionicCommando : public Machine {
public:
    MachineType machine_type() const override { return MachineType::Arcade; }
    static constexpr int kScreenWidth = 256;
    static constexpr int kScreenHeight = 224;
    static constexpr int kScanlines = 260;
    static constexpr double kFramesPerSecond = 6000000.0 / 384.0 / 260.0;  // 60.096 Hz
    static constexpr uint32_t kMainClock = 12000000;
    static constexpr uint32_t kSoundClock = 3579545;
    static constexpr uint32_t kMcuClock = 6000000;

    BionicCommando();
    ~BionicCommando() override;

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

    const char* title() const override { return "Bionic Commando"; }

    // Test hooks.
    uint32_t main_pc() const { return main_cpu_.pc(); }
    uint32_t palette_entry(int i) const { return palette_[size_t(i) & 0x3ff]; }
    uint8_t main_byte(uint32_t address) { return read8(address); }
    int sound_nmis() const { return sound_nmis_; }
    int mcu_dma_count() const { return dma_count_; }

private:
    uint8_t read8(uint32_t address);
    void write8(uint32_t address, uint8_t value);
    uint16_t read16(uint32_t address);
    void write16(uint32_t address, uint16_t value);
    void set_palette(int index);
    uint8_t sound_read(uint16_t address);
    void sound_write(uint16_t address, uint8_t value);
    void mcu_p3_write(uint8_t value);
    void on_sound_cycles(int cycles);
    void update_video();

    M68000 main_cpu_;
    Z80 sound_cpu_;
    Mcs51 mcu_;
    YM2151 ym_;

    std::vector<uint8_t> rom_;                 // 256 KB, big-endian
    std::array<uint8_t, 0x1000> ram_{};        // e0000 (mirrored), includes sprite RAM
    std::array<uint8_t, 0x4000> wram_{};       // fc000
    std::array<uint8_t, 0x1000> tx_ram_{};     // ec000 (mirrored)
    std::array<uint8_t, 0x4000> fg_ram_{};     // f0000
    std::array<uint8_t, 0x4000> bg_ram_{};     // f4000
    std::array<uint8_t, 0x800> pal_ram_{};     // f8000
    std::array<uint16_t, 0x280> sprite_buf_{};
    std::array<uint8_t, 0x8000> sound_rom_{};
    std::array<uint8_t, 0x800> sound_ram_{};
    std::array<uint32_t, 0x400> palette_{};
    std::array<uint16_t, 4> scroll_{};

    GfxSet chars_, bg_tiles_, fg_tiles_, sprites_;
    std::vector<uint32_t> framebuffer_;
    std::vector<uint32_t> screen_;  // 256x256 composition (visible rows 16..239)

    bool flip_screen_ = false;
    uint8_t audiocpu_to_mcu_ = 0, mcu_to_audiocpu_ = 0, mcu_p1_ = 0, mcu_p3_ = 0;
    uint16_t inputs_ = 0xffff;
    uint16_t dsw_ = 0xdfff;

    double main_debt_ = 0, sound_debt_ = 0, mcu_debt_ = 0;
    int64_t audio_accum_ = 0;
    std::vector<int16_t> audio_;
    int sound_nmis_ = 0;
    int dma_count_ = 0;
};

}  // namespace dsp
