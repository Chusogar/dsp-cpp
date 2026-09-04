#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "core/machine.h"
#include "cpu/hd63701.h"
#include "cpu/m6809.h"
#include "cpu/z80.h"
#include "sound/msm5205.h"
#include "sound/okim6295.h"
#include "sound/ym2151.h"
#include "video/gfx.h"

namespace dsp {

// Double Dragon (Technos, 1987) and Double Dragon II, ported from
// doubledragon_hw.pas.
//   ddragon:  HD6309 main CPU, HD63701Y sub CPU, M6809 sound CPU driving a
//             YM2151 and two MSM5205 ADPCM chips.
//   ddragon2: HD6309 main CPU, Z80 sub CPU, Z80 sound CPU driving a YM2151 and
//             an OKI MSM6295.
// The HD6309 runs the 6809 instruction set in emulation mode, so the main CPU
// reuses the M6809 core.
class DoubleDragon : public Machine {
public:
    enum class Variant { DDragon, DDragon2 };

    static constexpr int kScreenWidth = 256;
    static constexpr int kScreenHeight = 240;
    static constexpr double kFramesPerSecond = 6000000.0 / 384.0 / 272.0;
    static constexpr int kScanlines = 272;
    static constexpr int kCpuSync = 4;
    static constexpr uint32_t kMainClock = 12000000 / 4;
    static constexpr uint32_t kSubClock = 6000000 / 4;
    static constexpr uint32_t kSoundClock = 1500000;
    static constexpr uint32_t kSub2Clock = 4000000;
    static constexpr uint32_t kSound2Clock = 3579545;
    static constexpr uint32_t kYmClock = 3579545;
    static constexpr uint32_t kMsmClock = 375000;
    static constexpr uint32_t kOkiClock = 1056000;

    explicit DoubleDragon(Variant variant);

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

    const char* title() const override {
        return variant_ == Variant::DDragon ? "Double Dragon" : "Double Dragon II";
    }

private:
    bool is_dd2() const { return variant_ == Variant::DDragon2; }
    // The shared RAM is only visible to the main CPU while the sub CPU is stopped.
    bool sub_stopped() const { return sub_halt_ || sub_reset_; }

    uint8_t main_read(uint16_t address);
    void main_write(uint16_t address, uint8_t value);
    void write_control(uint8_t value);
    uint8_t sub_read(uint16_t address);
    void sub_write(uint16_t address, uint8_t value);
    uint8_t sub2_read(uint16_t address);
    void sub2_write(uint16_t address, uint8_t value);
    uint8_t sound_read(uint16_t address);
    void sound_write(uint16_t address, uint8_t value);
    uint8_t sound2_read(uint16_t address);
    void sound2_write(uint16_t address, uint8_t value);
    void on_sound_cycles(int cycles);
    void on_sound2_cycles(int cycles);

    bool load_roms(const std::string& rom_path, std::string* error);
    void decode_graphics(const std::vector<uint8_t>& char_rom,
                         const std::vector<uint8_t>& tile_rom,
                         const std::vector<uint8_t>& sprite_rom, int sprite_banks);
    void update_palette(uint16_t offset);
    void update_video();
    void draw_background();
    void draw_sprites();
    void draw_foreground();
    void draw_sprite_element(int code, int color, bool flip_x, bool flip_y, int pos_x, int pos_y);

    Variant variant_;

    M6809 main_cpu_;
    HD63701 sub_cpu_;
    M6809 sound_cpu_;
    Z80 sub_cpu2_;
    Z80 sound_cpu2_;
    YM2151 ym_;
    MSM5205 msm0_;
    MSM5205 msm1_;
    OKIM6295 oki_;

    std::array<uint8_t, 0x10000> memory_{};       // main CPU address space
    std::vector<uint8_t> banked_rom_;             // six 16 KB banks at $4000
    std::array<uint8_t, 0x10000> sound_memory_{};  // sound CPU ROM and RAM
    std::array<uint8_t, 0x10000> sub_memory_{};    // Double Dragon II sub CPU
    std::array<uint8_t, 0x200> shared_ram_{};      // main <-> sub CPU
    std::array<uint8_t, 0x400> palette_ram_{};

    GfxSet chars_;
    GfxSet tiles_;
    GfxSet sprites_;

    std::array<uint32_t, 512> palette_{};
    std::array<uint32_t, 512 * 512> background_{};
    std::array<uint32_t, 256 * 256> composite_{};
    std::vector<uint32_t> framebuffer_;

    uint8_t rom_bank_ = 0;
    uint8_t sound_latch_ = 0;
    uint8_t sub_port_ = 0;
    uint16_t scroll_x_ = 0;
    uint16_t scroll_y_ = 0;
    bool flip_screen_ = false;
    bool sub_halt_ = false;
    bool sub_reset_ = false;
    // Double Dragon II keeps the Z80 sub CPU NMI pending while it is stopped.

    uint8_t in0_ = 0xff;
    uint8_t in1_ = 0xff;
    uint8_t in2_ = 0xe7;
    uint8_t dsw_a_ = 0xff;
    uint8_t dsw_b_ = 0xff;

    int64_t audio_accumulator_ = 0;
    int64_t msm_accumulator_ = 0;
    std::vector<int16_t> audio_;
};

}  // namespace dsp
