#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "core/machine.h"
#include "cpu/m68000.h"
#include "cpu/z80.h"
#include "machine/i8255.h"
#include "machine/sega_315_5195.h"
#include "sound/sega_pcm.h"
#include "sound/ym2151.h"
#include "video/sega16.h"

namespace dsp {

// OutRun (Sega, 1986), ported from outrun_hw.pas.
class Outrun : public Machine {
public:
    static constexpr int kScreenWidth = 320;
    static constexpr int kScreenHeight = 224;
    static constexpr double kFramesPerSecond = 60.0;
    static constexpr int kScanlines = 262;
    static constexpr int kCpuSync = 4;
    static constexpr uint32_t kMainClock = 10000000;
    static constexpr uint32_t kSoundClock = 4000000;

    Outrun();

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

    const char* title() const override { return "OutRun"; }

    uint32_t debug_pc() const { return main_cpu_.pc(); }
    uint32_t debug_sub_pc() const { return sub_cpu_.pc(); }

private:
    uint16_t main_read(uint32_t address);
    void main_write(uint32_t address, uint16_t value);
    uint16_t sub_read(uint32_t address);
    void sub_write(uint32_t address, uint16_t value);
    uint8_t sub_read_byte(uint32_t address);
    void sub_write_byte(uint32_t address, uint8_t value);
    uint8_t sound_read(uint16_t address);
    void sound_write(uint16_t address, uint8_t value);
    uint8_t sound_in(uint16_t port);
    void sound_out(uint16_t port, uint8_t value);
    void on_sound_cycles(int cycles);
    void swap_road();
    void update_video();
    bool load_roms(const std::string& rom_path, std::string* error);
    uint16_t io_read(uint16_t address);
    void io_write(uint16_t address, uint16_t value);

    M68000 main_cpu_;
    M68000 sub_cpu_;
    Z80 sound_cpu_;
    YM2151 ym_;
    SegaPcm pcm_;
    I8255 ppi_;
    Sega3155195 mapper_;
    Sega16Video video_;

    std::vector<uint16_t> rom_;
    std::vector<uint16_t> rom2_;
    // Shared 68k work RAM at $60000-$67fff (main + sub, MAME share("share1")).
    std::array<uint16_t, 0x4000> ram_{};
    std::array<uint16_t, 0x800> road_ram_{};
    std::array<uint16_t, 0x800> road_buffer_{};
    std::vector<uint32_t> sprite_rom_;
    std::vector<uint8_t> pcm_rom_;
    std::vector<uint8_t> road_gfx_;
    std::array<uint8_t, 0x10000> sound_rom_{};

    std::vector<uint32_t> framebuffer_;
    std::vector<uint32_t> bg_low_, bg_high_, fg_low_, fg_high_, text_low_, text_high_;

    uint8_t adc_select_ = 0;
    uint8_t sound_latch_ = 0;
    uint8_t analog_x_ = 0x80;
    uint8_t analog_gas_ = 0;
    uint8_t analog_brake_ = 0;
    uint16_t in0_ = 0x00ef;
    uint8_t dsw_a_ = 0xff;
    uint8_t dsw_b_ = 0xfb;
    bool gear_hi_ = false;
    bool push_gear_ = false;
    bool z80_reset_ = false;
    uint8_t road_control_ = 0;
    int sprite_banks_ = 4;

    int64_t audio_acc_ = 0;
    int64_t pcm_acc_ = 0;
    std::vector<int16_t> audio_;
};

}  // namespace dsp
