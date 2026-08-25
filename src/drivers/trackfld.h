#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "core/machine.h"
#include "cpu/m6809.h"
#include "cpu/z80.h"
#include "sound/dac.h"
#include "sound/sn76496.h"
#include "sound/vlm5030.h"
#include "video/gfx.h"

namespace dsp {

// Track & Field / Hyper Olympic (Konami, 1983), ported from trackandfield_hw.pas.
// Main CPU: Konami-1 (encrypted M6809). Sound CPU: Z80 driving SN76496, DAC
// and a VLM5030 speech chip.
class TrackFld : public Machine {
public:
    static constexpr int kScreenWidth = 256;
    static constexpr int kScreenHeight = 224;
    static constexpr double kFramesPerSecond = 60.0;
    static constexpr int kScanlines = 256;
    static constexpr uint32_t kMainClock = 18432000 / 12;
    static constexpr uint32_t kSoundClock = 14318180 / 4;
    static constexpr uint32_t kSnClock = 14318180 / 8;
    static constexpr uint32_t kVlmClock = 3579545;

    TrackFld();

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

    const char* title() const override { return "Track & Field"; }

private:
    uint8_t main_read(uint16_t address);
    uint8_t main_opcode_read(uint16_t address);
    void main_write(uint16_t address, uint8_t value);
    uint8_t sound_read(uint16_t address);
    void sound_write(uint16_t address, uint8_t value);
    void on_sound_cycles(int cycles);
    void update_scroll(uint16_t address);

    void decode_graphics(const std::vector<uint8_t>& char_rom,
                         const std::vector<uint8_t>& sprite_rom);
    void build_palette(const std::vector<uint8_t>& prom);
    void update_video();
    void draw_tile(int offset);
    void draw_sprite(int index);

    M6809 main_cpu_;
    Z80 sound_cpu_;
    SN76496 sn_;
    Vlm5030 vlm_;
    Dac dac_;

    std::array<uint8_t, 0x10000> memory_{};
    std::array<uint8_t, 0xa000> opcodes_{};
    std::array<uint8_t, 0x10000> sound_memory_{};
    std::array<bool, 0x800> dirty_{};
    std::array<uint32_t, 32> palette_{};
    std::array<uint8_t, 0x100> char_lut_{};
    std::array<uint8_t, 0x100> sprite_lut_{};
    std::array<uint16_t, 32> scroll_x_{};

    GfxSet chars_;
    GfxSet sprites_;

    std::array<uint32_t, 512 * 256> tilemap_{};
    std::array<uint32_t, 256 * 256> composite_{};
    std::vector<uint32_t> framebuffer_;

    uint8_t sound_latch_ = 0;
    uint8_t chip_latch_ = 0;
    uint8_t sound_irq_trigger_ = 0;
    uint16_t last_addr_ = 0;
    bool irq_enable_ = false;
    bool flip_screen_ = false;

    uint8_t in0_ = 0xff;
    uint8_t in1_ = 0xff;
    uint8_t in2_ = 0xff;
    uint8_t dsw_a_ = 0xff;
    uint8_t dsw_b_ = 0x59;

    uint64_t sound_cycles_ = 0;
    int vlm_cycle_acc_ = 0;
    int64_t audio_accumulator_ = 0;
    std::vector<int16_t> audio_;
};

}  // namespace dsp
