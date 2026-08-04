#pragma once

#include <cstdint>
#include <vector>

namespace dsp {

// OKI MSM5205 ADPCM speech chip, ported from msm5205.pas. The driver feeds the
// samples from its own ROM and clocks the chip with vclk().
class MSM5205 {
public:
    // `prescaler` is the S1/S2 divider (96, 64 or 48) and `bits` the data width.
    MSM5205(uint32_t clock, int prescaler, int bits);

    void set_rom(std::vector<uint8_t> rom) { rom_ = std::move(rom); }

    void reset();

    // VCLK edge: fetches the next nibble of the current sample and decodes it.
    void vclk();

    // Frequency of the VCLK signal, i.e. how often vclk() has to be called.
    uint32_t sample_frequency() const { return clock_ / uint32_t(prescaler_); }

    // Current output, in the same 16 bit range used by the rest of the chips.
    int32_t output() const { return signal_ * 16; }

    bool idle() const { return idle_; }

    void set_start(uint32_t address) { position_ = address; }
    void set_end(uint32_t address) { end_ = address; }
    void set_reset(bool state);

private:
    void decode(uint8_t nibble);

    uint32_t clock_;
    int prescaler_;
    int bits_;

    std::vector<uint8_t> rom_;
    uint32_t position_ = 0;
    uint32_t end_ = 0;
    int data_value_ = -1;  // -1 when the high nibble has still to be fetched
    bool reset_ = true;
    bool idle_ = true;
    int signal_ = 0;
    int step_ = 0;
};

}  // namespace dsp
