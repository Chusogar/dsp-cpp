#pragma once

#include <array>
#include <cstdint>

namespace dsp {

struct Sid;
struct SidOperator;

using SidOutFn = int8_t (*)(SidOperator&);
using SidEnveFn = uint16_t (*)(SidOperator&);
using SidWaveFn = void (*)(SidOperator&);

struct SidWavePre {
    uint16_t len = 0;
    uint32_t pnt = 0;
    int16_t stp = 0;
};

struct SidOperator {
    std::array<uint8_t, 7> reg{};
    uint32_t sid_freq = 0;
    uint16_t sid_pulse_width = 0;
    uint8_t sid_ctrl = 0;
    uint8_t sid_ad = 0;
    uint8_t sid_sr = 0;
    SidOperator* carrier = nullptr;
    SidOperator* modulator = nullptr;
    Sid* chip = nullptr;
    bool sync = false;
    uint16_t pulse_index = 0;
    uint16_t new_pulse_index = 0;
    uint16_t cur_sid_freq = 0;
    uint16_t cur_noise_freq = 0;
    uint8_t output = 0;
    uint8_t filt_voice_mask = 0;
    bool filt_enabled = false;
    float filt_low = 0;
    float filt_ref = 0;
    int8_t filt_io = 0;
    int32_t cycle_len_count = 0;
    uint32_t cycle_add_len_pnt = 0;
    uint16_t cycle_len = 0;
    uint16_t cycle_len_pnt = 0;
    uint16_t wave_step = 0;
    uint16_t wave_step_add = 0;
    uint32_t wave_step_pnt = 0;
    uint32_t wave_step_add_pnt = 0;
    uint16_t wave_step_old = 0;
    std::array<SidWavePre, 2> wave_pre{};
    uint32_t noise_reg = 0;
    uint32_t noise_step = 0;
    uint32_t noise_step_add = 0;
    uint8_t noise_output = 0;
    bool noise_is_locked = false;
    uint8_t adsr_ctrl = 0;
    float fenve_step = 0;
    float fenve_step_add = 0;
    uint32_t enve_step = 0;
    uint8_t enve_vol = 0;
    uint8_t enve_sus_vol = 0;
    uint16_t enve_short_attack_count = 0;
    SidOutFn out_proc = nullptr;
    SidEnveFn adsr_proc = nullptr;
    SidWaveFn wave_proc = nullptr;

    void clear();
    void set();
    void set2();
    void wave_calc_cycle_len();
};

// MOS 6581/8580 SID, ported from sid_sound.pas. `update()` produces one
// sample at 44100 Hz, same as AY8910/SN76496 in this tree.
class Sid {
public:
    static constexpr int kSampleRate = 44100;
    enum Type { Type8580 = 0, Type6581 = 1 };

    explicit Sid(uint32_t clock, Type type = Type6581);
    void reset();
    void write(uint8_t address, uint8_t value);
    uint8_t read(uint8_t address);
    int16_t update();

    uint32_t pcm_sid = 0;
    uint32_t pcm_sid_noise = 0;
    uint8_t master_volume = 0;
    uint16_t master_volume_ampl_index = 0;
    int optr3_output_mask = -1;
    bool filter_enabled = true;
    uint8_t filter_type = 0;
    uint8_t filter_cur_type = 0;
    float filter_dy = 0;
    float filter_res_dy = 0;
    uint16_t filter_value = 0;
    std::array<uint8_t, 0x20> reg{};
    std::array<SidOperator, 3> optr{};
    std::array<SidWaveFn, 16> mode_normal{};
    std::array<SidWaveFn, 16> mode_ring{};
    const uint8_t* waveform30 = nullptr;
    const uint8_t* waveform50 = nullptr;
    const uint8_t* waveform60 = nullptr;
    const uint8_t* waveform70 = nullptr;

    std::array<float, 0x800> filter_table{};
    std::array<float, 0x800> band_pass_param{};
    std::array<float, 16> filter_res_table{};

private:
    void mixer_init(int three_voice_amplify);
    void filter_table_init();
    void sync_em();
    void init_waveforms(Type type);
    void enve_emu_init(uint32_t update_freq, bool measured);

    Type type_ = Type6581;
    uint32_t clock_ = 0;
    uint16_t zero16bit_ = 0;
    std::array<uint16_t, 256 * 4> mix16mono_{};
};

void sid_wave_calc_filter(SidOperator& voice);

}  // namespace dsp
