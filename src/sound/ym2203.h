#pragma once

#include <cstdint>
#include <functional>
#include <memory>

#include "sound/ay8910.h"
#include "sound/fmopn.h"

namespace dsp {

// Yamaha YM2203 (OPN): three FM channels plus an AY-3-8910 compatible PSG,
// ported from ym_2203.pas.
class YM2203 {
public:
    using IrqHandler = std::function<void(bool)>;

    static constexpr int kSampleRate = 44100;

    explicit YM2203(uint32_t clock, float amplitude = 1.0f, float ay_amplitude = 1.0f);

    YM2203(const YM2203&) = delete;
    YM2203& operator=(const YM2203&) = delete;

    void set_irq_handler(IrqHandler handler);
    void set_port_handlers(AY8910::PortRead port_a_read, AY8910::PortRead port_b_read,
                           AY8910::PortWrite port_a_write, AY8910::PortWrite port_b_write);

    void reset();

    void control(uint8_t value) { write_port(0, value); }
    void write(uint8_t value) { write_port(1, value); }
    uint8_t status() const { return opn_.status(); }
    uint8_t debug_reg(uint8_t address) const { return regs_[address]; }
    uint8_t read();

    // Generates the next mixed sample (sample rate is kSampleRate).
    int32_t update();
    void run_timers(int cycles);

private:
    void write_port(int port, uint8_t value);

    OpnCore opn_;
    AY8910 ay_;
    uint8_t regs_[256] = {};
    float amplitude_;
    bool external_timers_ = false;
};

}  // namespace dsp
