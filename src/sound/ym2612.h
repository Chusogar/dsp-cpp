#pragma once

#include <cstdint>
#include <functional>

#include "sound/fmopn.h"

namespace dsp {

// Yamaha YM2612 (OPN2): six FM channels plus a DAC, as used by the Sega
// Genesis / Mega Drive. Built on the existing OpnCore (the FM half of the
// YM2203/YM2612 family) with the second register bank mapped onto channels
// 3-5 and a PCM DAC that replaces channel 6 when enabled.
class YM2612 {
public:
    using IrqHandler = std::function<void(bool)>;

    static constexpr int kSampleRate = 44100;

    explicit YM2612(uint32_t clock, float amplitude = 1.0f);

    YM2612(const YM2612&) = delete;
    YM2612& operator=(const YM2612&) = delete;

    void set_irq_handler(IrqHandler handler) { opn_.set_irq_handler(std::move(handler)); }

    void reset();

    // Four-port interface: 0/1 = part 0 address/data, 2/3 = part 1 address/data.
    void write(int port, uint8_t value);
    uint8_t read(int port) const;

    uint8_t status() const { return opn_.status(); }

    // Next mixed sample at kSampleRate (mono, signed 16-bit range).
    int32_t update();

private:
    OpnCore opn_;
    uint8_t address_[2] = {};
    uint8_t dac_data_ = 0;
    bool dac_enable_ = false;
    float amplitude_;
};

}  // namespace dsp
