#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "core/machine.h"
#include "cpu/hu6280.h"
#include "cpu/m6502.h"
#include "sound/okim6295.h"
#include "sound/ym2203.h"
#include "sound/ym3812.h"
#include "video/deco_bac06.h"
#include "video/gfx.h"

namespace dsp {

// Act-Fancer (Data East, 1989), ported from actfancer_hw.pas.
//
// Main CPU: HuC6280 @ 21.47727/3 MHz
// Sound CPU: M6502 @ 1.5 MHz with YM2203, YM3812 and OKI6295
// Video: DECO BAC06 (tile_1 = 16x16 BG, tile_2 = 8x8 text) + MXC06 sprites
class ActFancer : public Machine {
public:
    static constexpr int kScreenWidth = 256;
    static constexpr int kScreenHeight = 240;
    static constexpr double kFramesPerSecond = 57.444885;
    static constexpr int kScanlines = 256;
    static constexpr uint32_t kMainClock = 21477200 / 3;
    static constexpr uint32_t kSoundClock = 1500000;
    static constexpr uint32_t kYm3812Clock = 3000000;
    static constexpr uint32_t kYm2203Clock = 1500000;
    static constexpr uint32_t kOkiClock = 1024188;

    ActFancer();

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
    int sample_rate() const override { return YM3812::kSampleRate; }

    const char* title() const override { return "Act-Fancer"; }

    const std::vector<std::string>& warnings() const { return warnings_; }

private:
    uint8_t main_read(uint32_t address);
    void main_write(uint32_t address, uint8_t value);
    uint8_t sound_read(uint16_t address);
    void sound_write(uint16_t address, uint8_t value);

    void update_video();
    void update_palette_entry(int index);
    void on_sound_cycles(int cycles);

    // 8-bit BAC06 helpers (HuC6280 byte bus with swapped word access).
    static void write_control0_8b(Bac06Layer& layer, uint32_t address, uint8_t value);
    static void write_control1_8b_swap(Bac06Layer& layer, uint32_t address, uint8_t value);
    static void write_tile_data_8b_swap(Bac06Layer& layer, uint32_t address, uint8_t value,
                                        uint32_t mask);

    bool load_roms(const std::string& rom_path, std::string* error);
    void decode_graphics(const std::vector<uint8_t>& char_rom, const std::vector<uint8_t>& tile_rom,
                         const std::vector<uint8_t>& sprite_rom);

    HuC6280 main_cpu_{kMainClock};
    M6502 sound_cpu_{kSoundClock, M6502::Type::Nmos};
    YM3812 ym3812_{kYm3812Clock};
    YM2203 ym2203_{kYm2203Clock};
    OKIM6295 okim_{kOkiClock, true};

    // tile_1: 16x16 BG colour base 0x100, mult 2; tile_2: 8x8 text base 0; sprites base 0x200.
    Bac06Chip bac06_{0x100, 0x000, 0x000, 2, 1, 1, 0x200, 0x0f};

    GfxSet gfx_char_;
    GfxSet gfx_tiles_;
    GfxSet gfx_sprites_;

    std::vector<uint8_t> rom_;
    std::array<uint8_t, 0x4000> ram_{};
    std::array<uint8_t, 0x10000> mem_snd_{};
    std::array<uint8_t, 0x800> buffer_sprites_{};
    std::array<uint8_t, 0x800> palette_ram_{};
    std::array<uint32_t, 0x400> palette_{};
    std::array<uint16_t, Bac06Layer::kScreenWidth * Bac06Layer::kScreenHeight> pens_{};
    std::vector<uint32_t> framebuffer_;

    uint8_t sound_latch_ = 0;
    uint8_t in0_ = 0xff;
    uint8_t in1_ = 0x7f;
    uint8_t in2_ = 0xff;
    uint8_t dsw_a_ = 0x7f;
    uint8_t dsw_b_ = 0xff;
    uint32_t frames_ = 0;

    int64_t audio_accumulator_ = 0;
    std::vector<int16_t> audio_;
    std::vector<std::string> warnings_;
};

}  // namespace dsp
