#pragma once

#include "core/machine.h"
#include "cpu/mcs48.h"
#include "cpu/z80.h"
#include "sound/ay8910.h"

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace dsp {

// Sega / Gremlin VIC Dual family (MAME sega/vicdual.cpp).
//
// Memory and I/O maps, input ports, coin handling (a coin resets the CPU and
// holds the coin status line for 70 ms), timing sources (64V, VBLANK, CBLANK,
// the 500 Hz timer) and monitor orientation follow MAME per game.
//
// Sound:
//  * Head On family: behavioural model of the Head On discrete board (555
//    engine VCOs with their divider chain, screeches, bonus, crash).
//  * Carnival: the i8035 music board with its AY-3-8912 (ROM epr-412.u5).
//  * Sample games (Depthcharge, Invinco, Pulsar, Carnival, N-Sub): MAME
//    sample WAVs are played when found in a "samples" folder next to the
//    ROMs (samples/<set>.zip or samples/<set>/); otherwise synthesized
//    effects stand in for each sample.
//  * Frogs, Borderline, Tranquillizer Gun: synthesized effects driven by the
//    same sound latch bits as MAME's netlists.
//  * Games without sound hardware emulation in MAME stay silent.
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

    static constexpr int kNativeWidth = 256;
    static constexpr int kNativeHeight = 224;
    static constexpr int kVTotal = 0x106;
    static constexpr int kHTotal = 0x148;
    static constexpr int kVBlankStart = 0xe0;
    static constexpr uint32_t kMasterClock = 15468480;
    static constexpr uint32_t kCpuClock = kMasterClock / 8;
    // 328 pixels at master/3 = 123 CPU cycles per line, 96 of them visible.
    static constexpr int kCyclesPerLine = kHTotal * 3 / 8;
    static constexpr int kVisibleCycles = 256 * 3 / 8;
    static constexpr double kFramesPerSecond =
        double(kMasterClock / 3) / (double(kHTotal) * double(kVTotal));
    static constexpr int kSampleRate = 44100;

    explicit VicDual(Game game);
    ~VicDual() override;

    bool init(const std::string& rom_path, std::string* error) override;
    void reset() override;
    void run_frame() override;
    void set_inputs(const MachineInputs& inputs) override;
    void set_dip_switch(int bank, uint8_t value) override;
    const uint32_t* framebuffer() const override { return output_.data(); }
    int screen_width() const override { return rotated() ? kNativeHeight : kNativeWidth; }
    int screen_height() const override { return rotated() ? kNativeWidth : kNativeHeight; }
    double frames_per_second() const override { return kFramesPerSecond; }
    void drain_audio(std::vector<int16_t>& out) override;
    int sample_rate() const override { return kSampleRate; }
    const char* title() const override;

    // Test hooks.
    bool rotated() const;
    bool samples_loaded() const { return samples_from_files_; }
    uint16_t debug_pc() const { return cpu_.pc(); }
    uint8_t debug_sound_port(int n) const { return n == 1 ? port1_state_ : port2_state_; }
    int debug_psg_writes() const { return psg_writes_; }
    // Writes the Head On sound latch and renders `samples` of audio.
    std::vector<int16_t> debug_headon_audio(uint8_t latch, int samples) {
        headon_audio_w(latch);
        generate_audio(samples);
        std::vector<int16_t> out;
        drain_audio(out);
        return out;
    }

    struct Sample {
        std::vector<float> data;  // at kSampleRate
    };

private:
    enum class Map { Vid8000, HeadOn, Vid8000Samurai, Invinco, Safari };
    enum class IoRead { Logic18, Logic148, Dual4 };
    enum class Audio { None, HeadOn, Samples, Frogs, Borderline, Carnival };

    Map map() const;
    IoRead io_read_type() const;
    uint8_t io_mask() const;
    void configure();

    uint8_t read_byte(uint16_t address);
    void write_byte(uint16_t address, uint8_t value);
    uint8_t read_port(uint16_t port);
    void write_port(uint16_t port, uint8_t value);
    uint8_t input_port(int n);
    bool load_roms(const std::string& rom_path, std::string* error);
    void load_samples(const std::string& rom_path);
    void render_line(int y);
    void rotate_output();

    int vcounter() const { return scanline_; }
    bool timer_value() const;
    void coin_in();
    void palette_bank_w(uint8_t data) { palette_bank_ = data & 3; }

    // Sound latches (MAME port1State / port2State semantics).
    void headon_audio_w(uint8_t data);
    void invho2_audio_w(uint8_t data);
    void depthch_audio_w(uint8_t data);
    void invinco_audio_w(uint8_t data);
    void pulsar_audio_1_w(uint8_t data);
    void pulsar_audio_2_w(uint8_t data);
    void carnival_audio_1_w(uint8_t data);
    void carnival_audio_2_w(uint8_t data);
    void nsub_audio_w(uint8_t data);
    void netlist_audio_w(uint8_t data);

    void play(int channel, int sample, bool loop);
    void stop(int channel);
    void generate_audio(int samples);
    float headon_sample();
    float netlist_sample();

    Game game_;
    Z80 cpu_;
    std::array<uint8_t, 0x4000> rom_{};
    std::array<uint8_t, 0x400> videoram_{};
    std::array<uint8_t, 0x400> ram_{};
    std::array<uint8_t, 0x1000> safari_ram_{};
    std::array<uint8_t, 0x800> characterram_{};
    std::array<uint8_t, 0x20> color_prom_{};
    bool has_prom_ = false;
    std::vector<uint32_t> native_;
    std::vector<uint32_t> output_;

    // Inputs as the host sees them this frame.
    MachineInputs host_{};
    bool prev_coin_ = false;
    bool prev_select_ = false;
    bool game_select_ = false;
    uint8_t dsw_ = 0;
    bool dsw_set_ = false;

    uint8_t coin_status_ = 0;
    int64_t coin_clear_at_ = -1;
    uint8_t palette_bank_ = 0;
    int scanline_ = 0;
    bool hblank_ = false;
    int64_t cycles_ = 0;
    uint8_t samurai_protection_ = 0;
    uint8_t tranqgun_prot_ = 0;
    int nsub_play_counter_ = 0;
    int64_t nsub_next_pulse_ = 0;

    // Sound.
    Audio sound_ = Audio::None;
    uint8_t port1_state_ = 0;
    uint8_t port2_state_ = 0;
    std::vector<Sample> samples_;
    bool samples_from_files_ = false;
    struct Channel {
        int sample = -1;
        double pos = 0;
        bool loop = false;
    };
    std::array<Channel, 16> channels_{};
    double audio_frac_ = 0;
    std::vector<int16_t> audio_;
    double hp_in_ = 0, hp_out_ = 0;

    // Head On discrete model.
    struct HeadOnCar {
        double ramp_car = 12.0;  // DISCRETE_RAMP on CAR_ON
        double ramp_hi = 0.0;    // DISCRETE_RAMP on HISPEED
        double cap = 4.0;        // 555 timing capacitor voltage
        bool charging = true;
        int div2 = 0, div4 = 0, div3 = 0;
    };
    struct HeadOn {
        bool car_on = false, hispeed_pc = false, hispeed_cc = false;
        bool crash = false, screech1 = false, screech2 = false, bonus = false;
        HeadOnCar player, computer;
        double screech_phase1 = 0, screech_phase2 = 0;
        double bonus_mod = 0, bonus_phase = 0;
        double crash1 = 0, crash2 = 0;  // 555 monostable time left (s)
        double bp_lp = 0, bp_hp = 0, sk_1 = 0, sk_2 = 0;
        uint32_t lfsr = 1;
        double noise_acc = 0;
        int noise = 0;
    } ho_;

    // Netlist-board stand-ins (Frogs, Borderline): one voice per input bit.
    struct NetVoice {
        double t = -1;     // seconds since trigger, <0 idle
        double phase = 0;
        bool held = false;
    };
    std::array<NetVoice, 8> net_{};
    uint32_t noise_lfsr_ = 0x1ffff;

    // Carnival music board: i8035 + AY-3-8912.
    std::unique_ptr<Mcs48> music_cpu_;
    std::unique_ptr<AY8910> psg_;
    uint8_t music_data_ = 0;
    uint8_t music_bus_ = 0;
    int psg_writes_ = 0;
    double music_cycle_acc_ = 0;
};

}  // namespace dsp
