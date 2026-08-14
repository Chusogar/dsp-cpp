#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <vector>

namespace dsp {

// NEC µPD1771C sound chip (Epoch Super Cassette Vision).
// Ported from leniad/dsp-emulator src/snd/upd1771.pas.
class Upd1771 {
public:
    static constexpr int kSampleRate = 44100;
    using AckHandler = std::function<void(bool asserted)>;

    explicit Upd1771(uint32_t clock = 6000000, float amp = 10.0f);

    void reset();
    void set_ack_handler(AckHandler h) { ack_ = std::move(h); }

    void write(uint8_t value);          // $3600 data port
    void pcm_write(uint8_t pc3_state);  // PC3 bit from port C
    uint8_t read() const { return 0x80; }

    // Advance chip by `cpu_cycles` of the host CPU clock, generate samples.
    void run_cycles(int cpu_cycles, uint32_t cpu_clock);
    void take_samples(std::vector<int16_t>& out);

private:
    void internal_tick();
    void schedule_ack();

    enum State : uint8_t { Silence = 0, Noise = 1, Tone = 2, Adpcm = 3 };

    uint32_t clock_;
    float amp_;
    AckHandler ack_;

    State state_ = Silence;
    uint32_t index_ = 0;
    uint8_t pc3_ = 0;
    std::array<uint8_t, 0x100> packet_{};  // enough for command packets

    // Tone
    uint8_t t_timbre_ = 0, t_offset_ = 0, t_volume_ = 0, t_tpos_ = 0;
    uint16_t t_period_ = 0, t_ppos_ = 0;

    // Noise
    uint8_t nw_timbre_ = 0, nw_volume_ = 0, nw_tpos_ = 0;
    uint32_t nw_period_ = 0, nw_ppos_ = 0;
    std::array<uint8_t, 3> n_value_{};
    std::array<uint32_t, 3> n_ppos_{}, n_period_{};
    std::array<uint16_t, 3> n_volume_{};

    int16_t salida_ = 0;
    int ack_timer_ = -1;  // cycles until ACK

    double sample_acc_ = 0;
    std::vector<int16_t> samples_;

    static const int8_t kWaveforms[8][32];
    static const uint8_t kNoiseTbl[256];
};

}  // namespace dsp
