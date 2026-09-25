#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>
#include "core/machine.h"
#include "cpu/m6800.h"
#include "cpu/m6809.h"
#include "machine/pia6821.h"
#include "sound/dac.h"

namespace dsp {

// Williams 6809 hardware (Defender, Mayday, Colony7, Joust, Robotron, Stargate)
// Ported from dsp-emulator williams_hw.pas — same clocks, map, PIA wiring and frame loop.
class Williams : public Machine {
public:
    enum class Game { Defender, Mayday, Colony7, Joust, Robotron, Stargate };

    // Pascal: screen_init(1,304,247) then iniciar_video(292,240), crop (xoff,7,292,240)
    static constexpr int kFbWidth = 304;
    static constexpr int kFbHeight = 247;
    static constexpr int kVisWidth = 292;
    static constexpr int kVisHeight = 240;
    static constexpr double kFramesPerSecond = 60.096154;
    static constexpr int kScanlines = 260;
    static constexpr int kCpuSync = 8;  // Pascal CPU_SYNC
    // Pascal: m6809 Create(12000000 div 3 div 4) = 1 MHz; m6800 Create(3579545)
    static constexpr uint32_t kMainClock = 1000000;
    static constexpr uint32_t kSoundClock = 3579545;
    static constexpr int kSampleRate = 44100;

    explicit Williams(Game game = Game::Defender);
    ~Williams() override;
    // Where the CMOS is kept; call before init() (default: the ROM path with
    // ".nv" in place of its extension).
    void set_nvram_path(const std::string& path) { nvram_path_ = path; }

    bool init(const std::string& rom_path, std::string* error) override;
    void reset() override;
    void run_frame() override;
    void set_inputs(const MachineInputs& inputs) override;
    void set_dip_switch(int bank, uint8_t value) override;
    const uint32_t* framebuffer() const override {
        return rotated() ? rot_fb_.data() : vis_fb_.data();
    }
    int screen_width() const override { return rotated() ? kVisHeight : kVisWidth; }
    int screen_height() const override { return rotated() ? kVisWidth : kVisHeight; }
    double frames_per_second() const override { return kFramesPerSecond; }
    void drain_audio(std::vector<int16_t>& out) override;
    int sample_rate() const override { return kSampleRate; }
    const char* title() const override;

private:
    bool has_blitter() const { return game_ >= Game::Joust; }
    // Colony 7 has a vertical monitor (MAME ROT270).
    bool rotated() const { return game_ == Game::Colony7; }

    uint8_t main_read(uint16_t a);
    void main_write(uint16_t a, uint8_t v);
    uint8_t sound_read(uint16_t a);
    void sound_write(uint16_t a, uint8_t v);
    void update_video_line(int line);
    void present_frame();
    void update_main_irq();
    void update_sound_irq();
    void sound_latch_w(uint8_t v);
    void build_palette_lookup();
    void blitter_w(uint8_t reg, uint8_t v);
    void blit_pixel(uint16_t dst, uint8_t srcdata);
    int blitter_core(uint16_t sstart, uint16_t dstart, uint8_t w, uint8_t h);
    uint8_t defender_read(uint16_t a);
    void defender_write(uint16_t a, uint8_t v);
    uint8_t joust_read(uint16_t a);
    void joust_write(uint16_t a, uint8_t v);

    Game game_;
    M6809 main_{kMainClock};
    M6800 sound_{kSoundClock};
    Pia6821 pia0_, pia1_, pia2_;
    Dac dac_;

    std::array<uint8_t, 0x10000> mem_{};
    std::array<uint8_t, 0x10000> snd_mem_{};
    std::array<std::array<uint8_t, 0x1000>, 16> rom_bank_{};
    std::array<uint8_t, 0x400> nvram_{};
    std::array<uint8_t, 16> palette_{};
    std::array<uint32_t, 256> pal_lookup_{};

    uint8_t ram_bank_ = 0;
    uint8_t sound_latch_ = 0;
    uint8_t dsw_a_ = 0;
    int scanline_ = 0;
    int xoff_ = 12;  // Defender; Joust-family uses 6

    uint8_t in0_ = 0, in1_ = 0, in2_ = 0, in3_ = 0;
    bool ram_rom_set_ = false;
    bool player_select_ = false;
    uint8_t blit_xor_ = 4;
    std::array<uint8_t, 8> blit_ram_{};
    std::vector<uint8_t> blit_remap_;  // 256*256 remap from decoder PROMs
    int blit_remap_bank_ = 0;
    void build_blit_remap(const uint8_t* prom, size_t prom_len);

    // Full Williams bitmap then crop like actualiza_trozo_final
    std::array<uint32_t, size_t(kFbWidth) * kFbHeight> full_fb_{};
    std::array<uint32_t, size_t(kVisWidth) * kVisHeight> vis_fb_{};
    std::array<uint32_t, size_t(kVisWidth) * kVisHeight> rot_fb_{};
    std::vector<int16_t> audio_;

    // Battery-backed CMOS persistence and the operator "Advance" button.
    std::string nvram_path_;
    bool nvram_loaded_ = false;
    bool nvram_dirty_ = false;
    int nvram_idle_ = 0;
    bool service_ = false;
    bool advance_ = false;
    int auto_advance_ = -1;
    int frame_count_ = 0;
    int cmos_burst_ = 0;   // CMOS bytes changed during the current frame
    void load_nvram();
    void save_nvram();

    // Pascal residual cycle budgets (tframes)
    double frame_main_ = 0;
    double frame_snd_ = 0;
    double tframes_main_ = 0;
    double tframes_snd_ = 0;
};

}  // namespace dsp
