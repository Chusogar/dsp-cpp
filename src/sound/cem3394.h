#pragma once

#include <cstdint>

namespace dsp {

// Curtis Electromusic Specialties CEM3394 synthesizer voice, ported from the
// classic MAME 0.160-era cem3394 driver. Crude pulse / triangle / sawtooth
// waveform generation; filter resonance and true VCF are not modelled.
class Cem3394 {
public:
    // Control-voltage inputs (match classic MAME set_voltage indices).
    enum Input {
        VCO_FREQUENCY = 0,
        MODULATION_AMOUNT,
        WAVE_SELECT,
        PULSE_WIDTH,
        MIXER_BALANCE,
        FILTER_RESONANCE,
        FILTER_FREQUENCY,
        FINAL_GAIN
    };

    static constexpr int kSampleRate = 44100;

    // `clock` is unused (the chip is analog); kept for API consistency with
    // other sound devices. Defaults match Bally/Sente (VCO 431.894 Hz,
    // filter 1300 Hz at 0.0 V).
    explicit Cem3394(uint32_t clock = 0, double vco_zero_freq = 431.894,
                     double filter_zero_freq = 1300.0);

    void reset();

    // Set the voltage going to a particular parameter.
    void set_voltage(int input, double voltage);

    // Translated parameter for the given input:
    //   VCO_FREQUENCY:     frequency in Hz
    //   MODULATION_AMOUNT: scale factor, 0.0 to 2.0
    //   WAVE_SELECT:       voltage from this line
    //   PULSE_WIDTH:       width fraction, from 0.0 to 1.0
    //   MIXER_BALANCE:     balance, from -1.0 to 1.0
    //   FILTER_RESONANCE:  resonance, from 0.0 to 1.0
    //   FILTER_FREQUENCY:  frequency, in Hz
    //   FINAL_GAIN:        gain, in dB
    double get_parameter(int input) const;

    // Generates the next mixed sample (sample rate is kSampleRate).
    int16_t update();

private:
    static double compute_db(double voltage);
    static uint32_t compute_db_volume(double voltage);

    double vco_zero_freq_ = 431.894;
    double filter_zero_freq_ = 1300.0;
    double inv_sample_rate_ = 1.0 / double(kSampleRate);

    double values_[8] = {};
    uint8_t wave_select_ = 0;

    uint32_t volume_ = 0;
    uint32_t mixer_internal_ = 0;
    uint32_t mixer_external_ = 0;

    uint32_t position_ = 0;
    uint32_t step_ = 0;

    uint32_t filter_step_ = 0;
    uint32_t modulation_depth_ = 0;

    uint32_t pulse_width_ = 0;
};

}  // namespace dsp
