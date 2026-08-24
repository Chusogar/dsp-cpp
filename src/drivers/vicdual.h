#pragma once

#include "core/machine.h"
#include "cpu/z80.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace dsp {

// Sega / Gremlin VIC Dual family (MAME sega/vicdual).
class VicDual : public Machine {
public:
    enum class Game {
        DepthCharge,
        Safari,
        Frogs,
        SpaceAttack,
        SpaceAttackHeadOn,
        HeadOn,
        HeadOn2,
        HeadOn2Slim,
        InvincoHeadOn2,
        NSub,
        Samurai,
        Invinco,
        InvincoDeepScan,
        TranqGun,
        SpaceTrek,
        Carnival,
        Borderline,
        Digger,
        Pulsar,
        Heiankyo,
        AlphaFighter,
    };

    enum class Layout {
        DualGame,
        HeadOn,
        HeadOn2,
        VramC000,
        Safari,
    };

    static constexpr int kScreenWidth = 256;
    static constexpr int kScreenHeight = 224;
    static constexpr int kVTotal = 0x106;
    static constexpr int kHTotal = 0x148;
    static constexpr uint32_t kMasterClock = 15468480;
    static constexpr uint32_t kCpuClock = kMasterClock / 8;
    static constexpr double kFramesPerSecond =
        double(kMasterClock / 3) / (double(kHTotal) * double(kVTotal));
    static constexpr int kSampleRate = 44100;

    explicit VicDual(Game game);

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
    int sample_rate() const override { return kSampleRate; }
    const char* title() const override;

private:
    Layout layout() const;
    bool is_color() const;

    uint8_t read_byte(uint16_t address);
    void write_byte(uint16_t address, uint8_t value);
    uint8_t read_port(uint16_t port);
    void write_port(uint16_t port, uint8_t value);
    bool load_roms(const std::string& rom_path, std::string* error);
    void update_video();
    bool timer_value() const;

    // Discrete-style audio (bit-driven tones + noise; samples not required)
    void audio_port1_w(uint8_t data);
    void audio_port2_w(uint8_t data);
    void mix_audio(int samples);

    Game game_;
    Z80 cpu_;
    std::array<uint8_t, 0x10000> memory_{};
    std::array<uint8_t, 0x400> videoram_{};
    std::array<uint8_t, 0x800> characterram_{};
    std::array<uint8_t, 0x20> color_prom_{};
    bool has_prom_ = false;
    std::vector<uint32_t> framebuffer_;
    uint8_t in0_ = 0xff, in1_ = 0xff, in2_ = 0xff, in3_ = 0xff;
    uint8_t dsw_ = 0;
    uint8_t coin_status_ = 0;
    int coin_clear_counter_ = 0;
    uint8_t palette_bank_ = 0;
    int scanline_ = 0;
    uint64_t frame_count_ = 0;

    // Audio state
    uint8_t audio_port1_ = 0;
    uint8_t audio_port2_ = 0;
    struct Voice {
        double phase = 0;
        double freq_hz = 0;
        float amp = 0;
        float target_amp = 0;
        int oneshot = 0;  // remaining samples for one-shot
        bool noise = false;
    };
    std::array<Voice, 8> voices_{};
    uint32_t lfsr_ = 0x1ffff;
    std::vector<int16_t> audio_;
};

}  // namespace dsp
