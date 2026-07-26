#pragma once

#include <cstdint>
#include <functional>

namespace dsp {

// AY-3-8910 PSG, ported from ay_8910.pas (MAME's classic AY core).
class AY8910 {
public:
    using PortRead = std::function<uint8_t()>;
    using PortWrite = std::function<void(uint8_t)>;

    static constexpr int kSampleRate = 44100;

    AY8910(uint32_t clock, float amplitude = 2.0f);

    void set_port_handlers(PortRead port_a_read, PortRead port_b_read, PortWrite port_a_write,
                           PortWrite port_b_write);

    void reset();
    void control(uint8_t value) { latch_ = uint8_t(value & 0x0f); }
    void write(uint8_t value) { write_reg(latch_, value); }
    uint8_t read() { return read_reg(latch_); }

    // Generates the next mixed sample (sample rate is kSampleRate).
    int32_t update();

private:
    void write_reg(uint8_t reg, uint8_t value);
    uint8_t read_reg(uint8_t reg);

    uint8_t regs_[16] = {};
    int32_t period_a_ = 0, period_b_ = 0, period_c_ = 0, period_n_ = 0, period_e_ = 0;
    int32_t count_a_ = 0, count_b_ = 0, count_c_ = 0, count_n_ = 0, count_e_ = 0;
    int32_t vol_a_ = 0, vol_b_ = 0, vol_c_ = 0, vol_e_ = 0;
    int32_t envelope_a_ = 0, envelope_b_ = 0, envelope_c_ = 0;
    int32_t output_a_ = 0, output_b_ = 0, output_c_ = 0, output_n_ = 0xff;
    int32_t hold_ = 0, alternate_ = 0, attack_ = 0, holding_ = 0, rng_ = 1, update_step_ = 0;
    int8_t count_env_ = 0;
    int16_t last_enable_ = -1;
    uint8_t latch_ = 0;
    uint32_t clock_;
    float amplitude_;

    PortRead port_a_read_, port_b_read_;
    PortWrite port_a_write_, port_b_write_;
};

}  // namespace dsp
