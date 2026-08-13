#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "core/machine.h"
#include "cpu/z80.h"
#include "cpu/z80ctc.h"
#include "sound/ay8910.h"

namespace dsp {

// Midway MCR-II / MCR-III hardware (Tapper, Tron, Satan's Hollow, …)
// from mcr_hw.pas — dual Z80, CTC, SSIO (2×AY-8910), tiles + sprites.
class Mcr : public Machine {
public:
    static constexpr int kScreenW = 512;
    static constexpr int kScreenH = 480;
    static constexpr uint32_t kMainClock = 5000000;
    static constexpr uint32_t kSoundClock = 2000000;
    static constexpr int kScanlines = 480;
    static constexpr double kFps = 30.0;
    static constexpr int kSampleRate = AY8910::kSampleRate;

    enum class Game { Tapper, Tron, Shollow, Domino, Wacko, Dotron, Timber };

    explicit Mcr(Game game = Game::Tapper);

    bool init(const std::string& rom_path, std::string* error) override;
    void reset() override;
    void run_frame() override;
    void set_inputs(const MachineInputs& inputs) override;
    void set_dip_switch(int bank, uint8_t value) override;
    const uint32_t* framebuffer() const override { return framebuffer_.data(); }
    int screen_width() const override { return kScreenW; }
    int screen_height() const override { return kScreenH; }
    double frames_per_second() const override { return kFps; }
    void drain_audio(std::vector<int16_t>& out) override;
    int sample_rate() const override { return kSampleRate; }
    const char* title() const override;

private:
    uint8_t main_read(uint16_t addr);
    void main_write(uint16_t addr, uint8_t value);
    uint8_t main_in(uint16_t port);
    void main_out(uint16_t port, uint8_t value);
    uint8_t sound_read(uint16_t addr);
    void sound_write(uint16_t addr, uint8_t value);
    void on_main_cycles(int cycles);
    void on_sound_cycles(int cycles);
    void update_video();
    void set_color(int index, uint16_t value);
    bool load_roms(const std::string& path, std::string* error);

    Game game_;
    Z80 main_cpu_;
    Z80 sound_cpu_;
    Z80Ctc ctc_;
    AY8910 ay0_;
    AY8910 ay1_;

    std::array<uint8_t, 0x10000> mem_{};
    std::array<uint8_t, 0x10000> sound_mem_{};
    std::array<uint8_t, 0x800> nvram_{};
    std::array<uint32_t, 256> palette_{};
    std::array<uint32_t, kScreenW * kScreenH> framebuffer_{};

    // GFX: 8x8 chars 4bpp, 32x32 sprites 4bpp
    std::vector<uint8_t> chars_;   // n * 64
    std::vector<uint8_t> sprites_; // n * 1024
    int char_count_ = 0;
    int sprite_count_ = 0;

    uint8_t in0_ = 0xff, in1_ = 0xff, in2_ = 0xff, in3_ = 0xff, dsw_ = 0xc0;
    uint8_t ssio_status_ = 0;
    std::array<uint8_t, 4> ssio_data_{};
    int ssio_count_ = 0;

    std::vector<int16_t> audio_;
    int64_t audio_acc_ = 0;
    int sound_div_ = 0;
};

}  // namespace dsp
