#pragma once

#include <array>
#include <cstdint>

namespace dsp {

// MOS 6581 / 8580 SID: envelopes, waveforms (combined), ring mod, hard sync,
// multi-mode filter. Register-compatible with sid_sound.pas.
class Sid6581 {
public:
    static constexpr int kSampleRate = 44100;
    enum class Model { Mos6581, Mos8580 };

    explicit Sid6581(uint32_t clock = 985248, Model model = Model::Mos6581);

    void reset();
    void write(uint8_t reg, uint8_t value);
    uint8_t read(uint8_t reg);
    int32_t update();
    void set_model(Model m) { model_ = m; }

private:
    struct Voice {
        uint32_t freq = 0;
        uint16_t pw = 0;
        uint8_t ctrl = 0;
        uint8_t ad = 0, sr = 0;
        uint32_t acc = 0;
        uint32_t noise = 0x7FFFF8;
        uint32_t prev_acc = 0;
        int env_state = 3;
        int env_vol = 0;
        int env_cnt = 0;
        int env_rate = 0;
        int sustain_level = 0;
        bool filter_enable = false;
    };

    void write_voice(int v, int offset, uint8_t value);
    void gate(Voice& v, bool on);
    void tick_envelope(Voice& v);
    int wave_output(Voice& v, Voice& mod_source);
    void clock_noise(Voice& v);
    int apply_filter(int input);

    uint32_t clock_;
    Model model_;
    std::array<Voice, 3> voice_{};
    std::array<uint8_t, 0x20> regs_{};
    uint16_t fc_ = 0;
    uint8_t res_filt_ = 0;
    uint8_t mode_vol_ = 0;
    float vlp_ = 0, vbp_ = 0, vhp_ = 0;
};

}  // namespace dsp
