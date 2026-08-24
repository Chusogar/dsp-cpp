#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "core/machine.h"
#include "cpu/m68000.h"
#include "cpu/z80.h"
#include "machine/opwolf_cchip.h"
#include "machine/tc0140syt.h"
#include "sound/msm5205.h"
#include "sound/ym2151.h"
#include "video/gfx.h"

namespace dsp {

// Operation Wolf (Taito, 1987), ported from operationwolf_hw.pas.
// 8 MHz 68000, 4 MHz Z80 sound CPU (TC0140SYT mailbox), YM2151, two MSM5205,
// software C-Chip, 320×240 light-gun cabinet.
class OpWolf : public Machine {
public:
    static constexpr int kScreenWidth = 320;
    static constexpr int kScreenHeight = 240;
    static constexpr int kWorkWidth = 512;
    static constexpr int kWorkHeight = 512;
    static constexpr double kFramesPerSecond = 60.0;
    static constexpr int kScanlines = 256;
    static constexpr uint32_t kMainClock = 8000000;
    static constexpr uint32_t kSoundClock = 4000000;
    static constexpr uint32_t kYmClock = 4000000;
    static constexpr uint32_t kMsmClock = 384000;

    OpWolf();

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

    const char* title() const override { return "Operation Wolf"; }
    bool uses_pointer() const override { return true; }

private:
    uint16_t main_read(uint32_t address);
    void main_write(uint32_t address, uint16_t value);
    uint8_t sound_read(uint16_t address);
    void sound_write(uint16_t address, uint8_t value);
    void on_sound_cycles(int cycles);
    void reset_sound();
    void start_adpcm(MSM5205& chip, const uint8_t* regs);

    bool load_roms(const std::string& rom_path, std::string* error);
    void decode_graphics(const std::vector<uint8_t>& tile_rom,
                         const std::vector<uint8_t>& sprite_rom);
    void set_palette(int index, uint16_t value);
    void update_video();
    void draw_tilemap(bool foreground);
    void blit_layer(const std::vector<uint32_t>& layer, uint16_t scroll_x, uint16_t scroll_y,
                    bool transparent);
    void draw_sprites();

    M68000 main_cpu_;
    Z80 sound_cpu_;
    YM2151 ym_;
    MSM5205 msm0_;
    MSM5205 msm1_;
    Tc0140Syt syt_;
    OpWolfCChip cchip_;

    std::vector<uint16_t> rom_;
    std::array<uint16_t, 0x4000> ram1_{};
    std::array<uint16_t, 0x8000> ram2_{};
    std::array<uint16_t, 0x2000> ram3_{};
    std::array<uint16_t, 0x800> palette_ram_{};
    std::array<uint8_t, 0x1000> sound_ram_{};
    std::array<uint8_t, 0x4000> sound_rom_{};
    std::array<std::array<uint8_t, 0x4000>, 4> sound_bank_{};
    uint8_t sound_bank_index_ = 0;
    std::array<uint8_t, 8> adpcm_b_{};
    std::array<uint8_t, 8> adpcm_c_{};

    GfxSet tiles_;
    GfxSet sprites_;
    std::array<uint32_t, 0x800> palette_{};
    std::vector<uint32_t> background_;
    std::vector<uint32_t> foreground_;
    std::vector<uint32_t> composite_;
    std::vector<uint32_t> framebuffer_;

    uint16_t scroll_x1_ = 0;
    uint16_t scroll_y1_ = 0;
    uint16_t scroll_x2_ = 0;
    uint16_t scroll_y2_ = 0;
    uint8_t sprite_bank_ = 0;

    uint8_t in0_ = 0xfc;
    uint8_t in1_ = 0xff;
    uint8_t dsw_a_ = 0xff;
    uint8_t dsw_b_ = 0x7f;
    uint16_t gun_x_ = 175;
    uint16_t gun_y_ = 120;

    int64_t audio_accumulator_ = 0;
    int64_t msm_accumulator_ = 0;
    std::vector<int16_t> audio_;
};

}  // namespace dsp
