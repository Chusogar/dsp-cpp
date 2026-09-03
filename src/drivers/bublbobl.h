#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "core/machine.h"
#include "cpu/hd63701.h"
#include "cpu/z80.h"
#include "sound/ym2203.h"
#include "sound/ym3812.h"
#include "video/gfx.h"

namespace dsp {

// Bubble Bobble (Taito, 1986) — port of bubblebobble_hw.pas (tipo 46).
// Main Z80 @ 6 MHz, sub Z80 @ 6 MHz, sound Z80 @ 3 MHz, M6801U4 MCU @ 4 MHz
// (HD63701Y with the 4K MCU ROM mapped at $F000), YM2203 + YM3526 @ 3 MHz.
// Visible area 256×224 (internal 256×256, crop y=16), 59.185606 Hz, 264 lines.
class BublBobl : public Machine {
public:
    static constexpr int kScreenWidth = 256;
    static constexpr int kScreenHeight = 224;
    static constexpr double kFramesPerSecond = 59.185606;
    static constexpr int kScanlines = 264;
    static constexpr uint32_t kMainClock = 6000000;
    static constexpr uint32_t kSubClock = 6000000;
    static constexpr uint32_t kSoundClock = 3000000;
    static constexpr uint32_t kMcuClock = 4000000;
    static constexpr uint32_t kYmClock = 3000000;

    BublBobl();

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
    int sample_rate() const override { return YM2203::kSampleRate; }

    const char* title() const override { return "Bubble Bobble"; }

    uint16_t debug_main_pc() const { return main_cpu_.pc(); }
    uint16_t debug_sub_pc() const { return sub_cpu_.pc(); }
    uint16_t debug_sound_pc() const { return sound_cpu_.pc(); }
    uint16_t debug_mcu_pc() const { return mcu_.pc(); }
    bool debug_video_enable() const { return video_enable_; }

private:
    uint8_t main_read(uint16_t address);
    void main_write(uint16_t address, uint8_t value);
    uint8_t sub_read(uint16_t address);
    void sub_write(uint16_t address, uint8_t value);
    uint8_t sound_read(uint16_t address);
    void sound_write(uint16_t address, uint8_t value);

    void mcu_port1_write(uint8_t value);
    void mcu_port2_write(uint8_t value);
    void update_sound_irq();
    void set_sub_reset(bool held);
    void set_sound_reset(bool held);
    void set_mcu_reset(bool held);

    bool load_roms(const std::string& rom_path, std::string* error);
    void decode_graphics(const std::vector<uint8_t>& gfx_rom);
    void set_color(int index);
    void update_video();
    void draw_tile(int nchar, int color, bool flipx, bool flipy, int x, int y);

    Z80 main_cpu_{kMainClock};
    Z80 sub_cpu_{kSubClock};
    Z80 sound_cpu_{kSoundClock};
    HD63701 mcu_{kMcuClock, HD63701::Type::HD63701Y};
    YM2203 ym2203_{kYmClock};
    YM3812 ym3526_{kYmClock, YM3812::kYM3526};

    std::array<uint8_t, 0x10000> memory_{};
    std::array<uint8_t, 0x10000> sub_rom_{};
    std::array<uint8_t, 0x10000> sound_mem_{};
    std::array<std::array<uint8_t, 0x4000>, 8> bank_rom_{};
    std::array<uint8_t, 0x200> palette_ram_{};
    std::array<uint32_t, 0x100> palette_{};
    std::array<uint8_t, 0x100> prom_{};

    GfxSet gfx_;
    std::vector<uint32_t> bitmap_;
    std::vector<uint32_t> framebuffer_;
    std::vector<int16_t> audio_buffer_;

    uint8_t bank_ = 0;
    uint8_t sound_stat_ = 0;
    uint8_t sound_latch_ = 0;
    bool sound_nmi_ = false;
    bool video_enable_ = false;
    bool flip_screen_ = false;
    bool sub_reset_ = true;
    bool sound_reset_ = false;
    bool mcu_held_ = true;

    uint8_t in0_ = 0xb3;
    uint8_t in1_ = 0xff;
    uint8_t in2_ = 0xff;
    uint8_t dsw_a_ = 0xfe;
    uint8_t dsw_b_ = 0xff;

    uint8_t mcu_port1_out_ = 0;
    uint8_t mcu_port2_out_ = 0;
    uint8_t mcu_port3_in_ = 0;
    uint8_t mcu_port3_out_ = 0;
    uint8_t mcu_port4_out_ = 0;

    bool ym2203_irq_ = false;
    bool ym3526_irq_ = false;

    double frame_main_ = 0;
    double frame_sub_ = 0;
    double frame_snd_ = 0;
    double frame_mcu_ = 0;
};

}  // namespace dsp
