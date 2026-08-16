#include "sound/sid.h"

#include "sound/sid_tables.h"

#include <cmath>
#include <cstdlib>
#include <cstring>

namespace dsp {
namespace {

constexpr int kMaxLogicalVoices = 4;
constexpr int kMix16monoMiddleIndex = 256 * kMaxLogicalVoices / 2;
constexpr uint32_t kNoiseSeed = 0x7ffff8;
constexpr uint8_t kEnveStartAttack = 0;
constexpr uint8_t kEnveStartRelease = 2;
constexpr uint8_t kEnveAttack = 4;
constexpr uint8_t kEnveDecay = 6;
constexpr uint8_t kEnveSustain = 8;
constexpr uint8_t kEnveRelease = 10;
constexpr uint8_t kEnveSustainDecay = 12;
constexpr uint8_t kEnveMute = 14;
constexpr uint8_t kEnveStartShortAttack = 16;
constexpr uint8_t kEnveShortAttack = 16;
constexpr uint8_t kEnveAlter = 32;
constexpr int kAttackTabLen = 255;

const uint8_t kMasterVolumeLevels[16] = {0,  17,  34,  51,  68,  85,  102, 119,
                                         136, 153, 170, 187, 204, 221, 238, 255};
const float kAttackTimes[16] = {2.2528606f,   8.0099577f,  15.7696042f, 23.7795619f, 37.2963655f, 55.0684591f,
                                66.8330845f,  78.3473987f, 98.1219818f, 244.554021f, 489.108042f, 782.472742f,
                                977.715461f,  2933.64701f, 4889.07793f, 7822.72493f};
const float kDecayReleaseTimes[16] = {8.91777693f, 24.594051f,  48.4185907f, 73.0116639f, 114.512475f,
                                      169.078356f, 205.199432f, 240.551975f, 301.266125f, 750.858245f,
                                      1501.71551f, 2402.43682f, 3001.89298f, 9007.21405f, 15010.998f, 24018.2111f};

int8_t amp_mod_1x8[256 * 256];
uint8_t triangle_table[4096];
uint8_t sawtooth_table[4096];
uint8_t square_table[2 * 4096];
uint8_t noise_table_msb[256];
uint8_t noise_table_lsb[65536];
uint16_t master_ampl_mod_table[16 * 256];
float attack_rates[16];
float decay_release_rates[16];
uint32_t release_pos[256];
uint32_t release_tab_len = 0;
bool mixer_ready = false;

int sshr(int num, uint8_t fac) {
    if (num < 0) return -(std::abs(num) >> fac);
    return num >> fac;
}

void sid_init_mixer_engine() {
    if (mixer_ready) return;
    constexpr float kFilterAmpl = 0.7f;
    int uk = 0;
    for (int si = 0; si < 256; si++) {
        for (int sj = -128; sj <= 127; sj++) {
            amp_mod_1x8[uk++] = int8_t(std::trunc(((float(si) * float(sj)) / 255.0f) * kFilterAmpl));
        }
    }
    mixer_ready = true;
}

uint16_t enve_emu_alter_attack(SidOperator& voice);
uint16_t enve_emu_alter_decay(SidOperator& voice);
uint16_t enve_emu_alter_sustain(SidOperator& voice);
uint16_t enve_emu_alter_release(SidOperator& voice);
uint16_t enve_emu_alter_sustain_decay(SidOperator& voice);
uint16_t enve_emu_attack(SidOperator& voice);
uint16_t enve_emu_decay(SidOperator& voice);
uint16_t enve_emu_sustain(SidOperator& voice);
uint16_t enve_emu_release(SidOperator& voice);
uint16_t enve_emu_sustain_decay(SidOperator& voice);
uint16_t enve_emu_mute(SidOperator& voice);
uint16_t enve_emu_start_attack(SidOperator& voice);
uint16_t enve_emu_start_release(SidOperator& voice);
uint16_t enve_emu_start_short_attack(SidOperator& voice);

void wave_advance(SidOperator& voice) {
    voice.wave_step_pnt += voice.wave_step_add_pnt;
    voice.wave_step = uint16_t(voice.wave_step + voice.wave_step_add);
    if (voice.wave_step_pnt > 65535) voice.wave_step = uint16_t(voice.wave_step + 1);
    voice.wave_step_pnt &= 0xffff;
    voice.wave_step = uint16_t(voice.wave_step & 0xfff);
}

void noise_advance(SidOperator& voice) {
    voice.noise_step += voice.noise_step_add;
    if (voice.noise_step >= (1u << 20)) {
        voice.noise_step -= (1u << 20);
        voice.noise_reg = (voice.noise_reg << 1) |
                          (((voice.noise_reg >> 22) ^ (voice.noise_reg >> 17)) & 1);
        voice.noise_output =
            uint8_t(noise_table_lsb[voice.noise_reg & 0xffff] | noise_table_msb[(voice.noise_reg >> 16) & 0xff]);
    }
}

void noise_advance_hp(SidOperator& voice) {
    uint32_t tmp = voice.noise_step_add;
    while (tmp >= (1u << 20)) {
        tmp -= (1u << 20);
        voice.noise_reg =
            (voice.noise_reg << 1) | (((voice.noise_reg >> 22) ^ (voice.noise_reg >> 17)) & 1);
    }
    voice.noise_step += tmp;
    if (voice.noise_step >= (1u << 20)) {
        voice.noise_step -= (1u << 20);
        voice.noise_reg =
            (voice.noise_reg << 1) | (((voice.noise_reg >> 22) ^ (voice.noise_reg >> 17)) & 1);
    }
    voice.noise_output =
        uint8_t(noise_table_lsb[voice.noise_reg & 0xffff] | noise_table_msb[(voice.noise_reg >> 16) & 0xff]);
}

void sid_mode00(SidOperator& voice) {
    voice.output = uint8_t(voice.filt_io - 0x80);
    wave_advance(voice);
}
void sid_mode10(SidOperator& voice) {
    voice.output = triangle_table[voice.wave_step];
    wave_advance(voice);
}
void sid_mode20(SidOperator& voice) {
    voice.output = sawtooth_table[voice.wave_step];
    wave_advance(voice);
}
void sid_mode30(SidOperator& voice) {
    voice.output = voice.chip->waveform30[voice.wave_step];
    wave_advance(voice);
}
void sid_mode40(SidOperator& voice) {
    voice.output = square_table[(voice.wave_step + voice.pulse_index) & 0x1fff];
    wave_advance(voice);
}
void sid_mode50(SidOperator& voice) {
    const uint16_t tword = uint16_t(voice.wave_step + voice.sid_pulse_width);
    voice.output = (tword > 4095) ? uint8_t(0) : voice.chip->waveform50[tword];
    wave_advance(voice);
}
void sid_mode60(SidOperator& voice) {
    const uint16_t tword = uint16_t(voice.wave_step + voice.sid_pulse_width);
    voice.output = (tword > 4095) ? uint8_t(0) : voice.chip->waveform60[tword];
    wave_advance(voice);
}
void sid_mode70(SidOperator& voice) {
    const uint16_t tword = uint16_t(voice.wave_step + voice.sid_pulse_width);
    voice.output = (tword > 4095) ? uint8_t(0) : voice.chip->waveform70[tword];
    wave_advance(voice);
}
void sid_mode80(SidOperator& voice) {
    voice.output = voice.noise_output;
    wave_advance(voice);
    noise_advance(voice);
}
void sid_mode80hp(SidOperator& voice) {
    voice.output = voice.noise_output;
    wave_advance(voice);
    noise_advance_hp(voice);
}
void sid_mode_lock(SidOperator& voice) {
    voice.noise_is_locked = true;
    voice.output = uint8_t(voice.filt_io - 0x80);
    wave_advance(voice);
}
void sid_mode14(SidOperator& voice) {
    if (voice.modulator->wave_step < 2048) {
        voice.output = triangle_table[voice.wave_step & 0xfff];
    } else {
        voice.output = uint8_t(0xff ^ triangle_table[voice.wave_step & 0xfff]);
    }
    wave_advance(voice);
}
void sid_mode34(SidOperator& voice) {
    if (voice.modulator->wave_step < 2048) {
        voice.output = voice.chip->waveform30[voice.wave_step & 0xfff];
    } else {
        voice.output = uint8_t(0xff ^ voice.chip->waveform30[voice.wave_step & 0xfff]);
    }
    wave_advance(voice);
}
void sid_mode54(SidOperator& voice) {
    uint16_t tword = uint16_t(voice.wave_step + voice.sid_pulse_width);
    tword = (tword > 4095) ? uint16_t(0) : uint16_t(voice.chip->waveform50[tword]);
    if (voice.modulator->wave_step < 2048) {
        voice.output = uint8_t(tword);
    } else {
        voice.output = uint8_t(0xff ^ tword);
    }
    wave_advance(voice);
}
void sid_mode74(SidOperator& voice) {
    uint16_t tword = uint16_t(voice.wave_step + voice.sid_pulse_width);
    tword = (tword > 4095) ? uint16_t(0) : uint16_t(voice.chip->waveform70[tword]);
    if (voice.modulator->wave_step < 2048) {
        voice.output = uint8_t(tword);
    } else {
        voice.output = uint8_t(0xff ^ tword);
    }
    wave_advance(voice);
}

int8_t wave_calc_mute(SidOperator& voice) {
    if (voice.adsr_proc) voice.adsr_proc(voice);
    return voice.filt_io;
}

int8_t wave_calc_normal(SidOperator& voice) {
    if (voice.cycle_len_count <= 0) {
        voice.wave_calc_cycle_len();
        if (voice.sid_ctrl & 0x40) {
            voice.pulse_index = voice.new_pulse_index;
            if (voice.pulse_index > 2048) voice.wave_step = 0;
        }
    }
    if (voice.wave_proc) voice.wave_proc(voice);
    const uint16_t adsr = voice.adsr_proc ? voice.adsr_proc(voice) : uint16_t(0);
    voice.filt_io = amp_mod_1x8[adsr | voice.output];
    sid_wave_calc_filter(voice);
    return voice.filt_io;
}

int8_t wave_calc_range_check(SidOperator& voice) {
    voice.wave_step_old = voice.wave_step;
    if (voice.wave_proc) voice.wave_proc(voice);
    if (voice.wave_step < voice.wave_step_old) {
        voice.cycle_len_count = 0;
        voice.out_proc = wave_calc_normal;
        voice.wave_step = 4095;
    }
    const uint16_t adsr = voice.adsr_proc ? voice.adsr_proc(voice) : uint16_t(0);
    voice.filt_io = amp_mod_1x8[adsr | voice.output];
    sid_wave_calc_filter(voice);
    return voice.filt_io;
}

void enve_emu_enve_advance(SidOperator& voice) { voice.fenve_step += voice.fenve_step_add; }

uint16_t enve_index(SidOperator& voice) {
    return uint16_t((voice.chip->master_volume_ampl_index + voice.enve_vol) & 0xfff);
}

uint16_t enve_emu_start_decay(SidOperator& voice) {
    voice.adsr_ctrl = kEnveDecay;
    voice.fenve_step = 0;
    return enve_emu_alter_decay(voice);
}

uint16_t enve_emu_start_attack(SidOperator& voice) {
    voice.adsr_ctrl = kEnveAttack;
    voice.fenve_step = float(voice.enve_vol);
    return enve_emu_alter_attack(voice);
}

uint16_t enve_emu_start_release(SidOperator& voice) {
    voice.adsr_ctrl = kEnveRelease;
    voice.fenve_step = float(release_pos[voice.enve_vol]);
    return enve_emu_alter_release(voice);
}

uint16_t enve_emu_attack(SidOperator& voice) {
    voice.enve_step = uint32_t(std::trunc(voice.fenve_step)) & 0xffff;
    if (voice.enve_step >= uint32_t(kAttackTabLen)) return enve_emu_start_decay(voice);
    voice.enve_vol = uint8_t(voice.enve_step);
    enve_emu_enve_advance(voice);
    return master_ampl_mod_table[enve_index(voice)];
}

uint16_t enve_emu_decay(SidOperator& voice) {
    voice.enve_step = uint32_t(std::trunc(voice.fenve_step)) & 0xffff;
    if (voice.enve_step >= release_tab_len) {
        voice.enve_vol = voice.enve_sus_vol;
        return enve_emu_alter_sustain(voice);
    }
    voice.enve_vol = sid_tables::releaseTab[voice.enve_step];
    if (voice.enve_vol <= voice.enve_sus_vol) {
        voice.enve_vol = voice.enve_sus_vol;
        return enve_emu_alter_sustain(voice);
    }
    enve_emu_enve_advance(voice);
    return master_ampl_mod_table[enve_index(voice)];
}

uint16_t enve_emu_sustain(SidOperator& voice) { return master_ampl_mod_table[enve_index(voice)]; }

uint16_t enve_emu_release(SidOperator& voice) {
    voice.enve_step = uint32_t(std::trunc(voice.fenve_step)) & 0xffff;
    if (voice.enve_step >= release_tab_len) {
        voice.enve_vol = sid_tables::releaseTab[release_tab_len - 1];
        return master_ampl_mod_table[enve_index(voice)];
    }
    voice.enve_vol = sid_tables::releaseTab[voice.enve_step];
    enve_emu_enve_advance(voice);
    return master_ampl_mod_table[enve_index(voice)];
}

uint16_t enve_emu_sustain_decay(SidOperator& voice) {
    voice.enve_step = uint32_t(std::trunc(voice.fenve_step)) & 0xffff;
    if (voice.enve_step >= release_tab_len) {
        voice.enve_vol = sid_tables::releaseTab[release_tab_len - 1];
        return enve_emu_alter_sustain(voice);
    }
    voice.enve_vol = sid_tables::releaseTab[voice.enve_step];
    if (voice.enve_vol <= voice.enve_sus_vol) {
        voice.enve_vol = voice.enve_sus_vol;
        return enve_emu_alter_sustain(voice);
    }
    enve_emu_enve_advance(voice);
    return master_ampl_mod_table[enve_index(voice)];
}

uint16_t enve_emu_mute(SidOperator&) { return 0; }

uint16_t enve_emu_short_attack(SidOperator& voice) {
    voice.enve_step = uint32_t(std::trunc(voice.fenve_step)) & 0xffff;
    if ((voice.enve_step >= uint32_t(kAttackTabLen)) || (voice.enve_short_attack_count == 0)) {
        return enve_emu_start_decay(voice);
    }
    voice.enve_vol = uint8_t(voice.enve_step);
    voice.enve_short_attack_count = uint16_t(voice.enve_short_attack_count - 1);
    enve_emu_enve_advance(voice);
    return master_ampl_mod_table[enve_index(voice)];
}

uint16_t enve_emu_alter_short_attack(SidOperator& voice) {
    voice.fenve_step_add = attack_rates[voice.sid_ad >> 4];
    voice.adsr_proc = enve_emu_short_attack;
    return enve_emu_short_attack(voice);
}

uint16_t enve_emu_start_short_attack(SidOperator& voice) {
    voice.adsr_ctrl = kEnveShortAttack;
    voice.fenve_step = float(voice.enve_vol);
    voice.enve_short_attack_count = 65535;
    return enve_emu_alter_short_attack(voice);
}

uint16_t enve_emu_alter_attack(SidOperator& voice) {
    voice.fenve_step_add = attack_rates[voice.sid_ad >> 4];
    voice.adsr_proc = enve_emu_attack;
    return enve_emu_attack(voice);
}

uint16_t enve_emu_alter_decay(SidOperator& voice) {
    voice.fenve_step_add = decay_release_rates[voice.sid_ad & 0x0f];
    voice.adsr_proc = enve_emu_decay;
    return enve_emu_decay(voice);
}

uint16_t enve_emu_alter_sustain(SidOperator& voice) {
    if (voice.enve_vol > voice.enve_sus_vol) {
        voice.adsr_ctrl = kEnveSustainDecay;
        voice.adsr_proc = enve_emu_sustain_decay;
        return enve_emu_alter_sustain_decay(voice);
    }
    voice.adsr_ctrl = kEnveSustain;
    voice.adsr_proc = enve_emu_sustain;
    return enve_emu_sustain(voice);
}

uint16_t enve_emu_alter_release(SidOperator& voice) {
    voice.fenve_step_add = decay_release_rates[voice.sid_sr & 0x0f];
    voice.adsr_proc = enve_emu_release;
    return enve_emu_release(voice);
}

uint16_t enve_emu_alter_sustain_decay(SidOperator& voice) {
    voice.fenve_step_add = decay_release_rates[voice.sid_ad & 0x0f];
    voice.adsr_proc = enve_emu_sustain_decay;
    return enve_emu_sustain_decay(voice);
}

const SidEnveFn kEnveModeTable[32] = {
    enve_emu_start_attack,       enve_emu_start_release, enve_emu_attack,     enve_emu_decay,
    enve_emu_sustain,            enve_emu_release,       enve_emu_sustain_decay, enve_emu_mute,
    enve_emu_start_short_attack, enve_emu_mute,          enve_emu_mute,       enve_emu_mute,
    enve_emu_mute,               enve_emu_mute,          enve_emu_mute,       enve_emu_mute,
    enve_emu_start_attack,       enve_emu_start_release, enve_emu_alter_attack, enve_emu_alter_decay,
    enve_emu_alter_sustain,      enve_emu_alter_release, enve_emu_alter_sustain_decay, enve_emu_mute,
    enve_emu_start_short_attack, enve_emu_mute,          enve_emu_mute,       enve_emu_mute,
    enve_emu_mute,               enve_emu_mute,          enve_emu_mute,       enve_emu_mute,
};

void enve_emu_reset_operator(SidOperator& voice) {
    voice.adsr_ctrl = kEnveMute;
    voice.fenve_step = 0;
    voice.fenve_step_add = 0;
    voice.enve_step = 0;
    voice.enve_sus_vol = 0;
    voice.enve_vol = 0;
    voice.enve_short_attack_count = 0;
}

}  // namespace

void sid_wave_calc_filter(SidOperator& voice) {
    if (!voice.filt_enabled) return;
    Sid& sid = *voice.chip;
    if (sid.filter_type != 0) {
        if (sid.filter_type == 0x20) {
            voice.filt_low += voice.filt_ref * sid.filter_dy;
            float tmp = float(voice.filt_io) - voice.filt_low;
            tmp -= voice.filt_ref * sid.filter_res_dy;
            voice.filt_ref += tmp * sid.filter_dy;
            voice.filt_io = int8_t(std::trunc(voice.filt_ref - voice.filt_low / 4.0f));
        } else if (sid.filter_type == 0x40) {
            voice.filt_low += voice.filt_ref * sid.filter_dy * 0.1f;
            float tmp = float(voice.filt_io) - voice.filt_low;
            tmp -= voice.filt_ref * sid.filter_res_dy;
            voice.filt_ref += tmp * sid.filter_dy;
            float tmp2 = voice.filt_ref - float(voice.filt_io) / 8.0f;
            if (tmp2 < -128) tmp2 = -128;
            if (tmp2 > 127) tmp2 = 127;
            voice.filt_io = int8_t(std::trunc(tmp2));
        } else {
            voice.filt_low += voice.filt_ref * sid.filter_dy;
            const float sample = float(voice.filt_io);
            float sample2 = sample - voice.filt_low;
            const int tmpint = int(std::trunc(sample2));
            sample2 -= voice.filt_ref * sid.filter_res_dy;
            voice.filt_ref += sample2 * sid.filter_dy;
            if (sid.filter_type == 0x10 || sid.filter_type == 0x30) {
                voice.filt_io = int8_t(std::trunc(voice.filt_low));
            } else if (sid.filter_type == 0x50 || sid.filter_type == 0x70) {
                voice.filt_io = int8_t(std::trunc(sample - float(sshr(tmpint, 1))));
            } else if (sid.filter_type == 0x60) {
                voice.filt_io = int8_t(tmpint);
            }
        }
    } else {
        voice.filt_io = 0;
    }
}

void SidOperator::wave_calc_cycle_len() {
    cycle_add_len_pnt += cycle_len_pnt;
    cycle_len_count = cycle_len;
    if (cycle_add_len_pnt > 65535) cycle_len_count += 1;
    cycle_add_len_pnt &= 0xffff;
    if (cycle_len_count <= 0) cycle_len_count = 1;
    const uint16_t diff = uint16_t(cycle_len_count - int32_t(cycle_len));
    SidWavePre& pre = wave_pre[diff & 1];
    if (pre.len != uint16_t(cycle_len_count)) {
        pre.len = uint16_t(cycle_len_count);
        wave_step_add = uint16_t(4096 / cycle_len_count);
        pre.stp = int16_t(wave_step_add);
        wave_step_add_pnt = uint32_t(((4096 % cycle_len_count) * 65536) / cycle_len_count);
        pre.pnt = wave_step_add_pnt;
    } else {
        wave_step_add = uint16_t(pre.stp);
        wave_step_add_pnt = pre.pnt;
    }
}

void SidOperator::clear() {
    sid_freq = 0;
    sid_ctrl = 0;
    sid_ad = 0;
    sid_sr = 0;
    sync = false;
    pulse_index = 0;
    new_pulse_index = 0;
    sid_pulse_width = 0;
    cur_sid_freq = 0;
    cur_noise_freq = 0;
    output = 0;
    noise_output = 0;
    filt_io = 0;
    filt_enabled = false;
    filt_low = 0;
    filt_ref = 0;
    cycle_len_count = 0;
    cycle_len = 0;
    cycle_len_pnt = 0;
    cycle_add_len_pnt = 0;
    out_proc = wave_calc_mute;
    wave_step_add = 0;
    wave_step_add_pnt = 0;
    wave_step = 0;
    wave_step_pnt = 0;
    wave_pre[0] = {};
    wave_pre[1] = {};
    wave_step_old = 0;
    noise_reg = kNoiseSeed;
    noise_step_add = 0;
    noise_step = 0;
    noise_is_locked = false;
}

void SidOperator::set() {
    sid_freq = uint32_t(reg[0] | (uint32_t(reg[1]) << 8));
    sid_pulse_width = uint16_t((reg[2] | (uint16_t(reg[3]) << 8)) & 0x0fff);
    new_pulse_index = uint16_t(4096 - sid_pulse_width);
    if (((wave_step + pulse_index) >= 0x1000) && ((wave_step + new_pulse_index) >= 0x1000)) {
        pulse_index = new_pulse_index;
    } else if (((wave_step + pulse_index) < 0x1000) && ((wave_step + new_pulse_index) < 0x1000)) {
        pulse_index = new_pulse_index;
    }
    const uint8_t old_wave = sid_ctrl;
    const uint8_t new_wave = reg[4];
    uint8_t enve_temp = adsr_ctrl;
    sid_ctrl = new_wave;
    if ((new_wave & 1) == 0) {
        if (old_wave & 1) enve_temp = kEnveStartRelease;
    } else if ((old_wave & 1) == 0) {
        enve_temp = kEnveStartAttack;
    }
    if ((old_wave ^ new_wave) & 0xf0) cycle_len_count = 0;
    const uint8_t ad_temp = reg[5];
    const uint8_t sr_temp = reg[6];
    if (sid_ad != ad_temp) {
        enve_temp = uint8_t(enve_temp | kEnveAlter);
    } else if (sid_sr != sr_temp) {
        enve_temp = uint8_t(enve_temp | kEnveAlter);
    }
    sid_ad = ad_temp;
    sid_sr = sr_temp;
    const uint8_t tmp_sus_vol = kMasterVolumeLevels[sr_temp >> 4];
    if (adsr_ctrl != kEnveSustain) {
        enve_sus_vol = tmp_sus_vol;
    } else if (enve_sus_vol > enve_vol) {
        enve_sus_vol = 0;
    } else {
        enve_sus_vol = tmp_sus_vol;
    }
    adsr_proc = kEnveModeTable[(enve_temp >> 1) & 0x1f];
    adsr_ctrl = uint8_t(enve_temp & (255 - kEnveAlter - 1));
    filt_enabled = chip && chip->filter_enabled && ((chip->reg[0x17] & filt_voice_mask) != 0);
}

void SidOperator::set2() {
    out_proc = wave_calc_normal;
    sync = false;
    if ((sid_freq < 16) || (sid_ctrl & 8)) {
        out_proc = wave_calc_mute;
        if (sid_freq == 0) {
            cycle_len = 0;
            cycle_len_pnt = 0;
            cycle_add_len_pnt = 0;
            wave_step = 0;
            wave_step_pnt = 0;
            cur_sid_freq = 0;
            cur_noise_freq = 0;
            noise_step_add = 0;
            cycle_len_count = 0;
        }
        if (sid_ctrl & 8) {
            if (noise_is_locked) {
                noise_is_locked = false;
                noise_reg = kNoiseSeed;
            }
        }
    } else {
        if (cur_sid_freq != sid_freq) {
            cur_sid_freq = uint16_t(sid_freq);
            cycle_len = uint16_t(chip->pcm_sid / sid_freq);
            cycle_len_pnt = uint16_t(((chip->pcm_sid % sid_freq) * 65536) / sid_freq);
            if (cycle_len_count > 0) {
                wave_calc_cycle_len();
                out_proc = wave_calc_range_check;
            }
        }
        if ((sid_ctrl & 0x80) && (cur_noise_freq != sid_freq)) {
            cur_noise_freq = uint16_t(sid_freq);
            noise_step_add = (chip->pcm_sid_noise * sid_freq) >> 8;
            if (noise_step_add >= (1u << 21)) {
                chip->mode_normal[8] = sid_mode80hp;
            } else {
                chip->mode_normal[8] = sid_mode80;
            }
        }
        if (sid_ctrl & 2) {
            if ((modulator->sid_freq == 0) || (modulator->sid_ctrl & 8)) {
            } else if ((carrier->sid_ctrl & 2) && (modulator->sid_freq >= (sid_freq << 1))) {
            } else {
                sync = true;
            }
        }
        if (((sid_ctrl & 0x14) == 0x14) && (modulator->sid_freq != 0)) {
            wave_proc = chip->mode_ring[sid_ctrl >> 4];
        } else {
            wave_proc = chip->mode_normal[sid_ctrl >> 4];
        }
    }
}

void Sid::init_waveforms(Type type) {
    int k = 0;
    for (int i = 0; i < 256; i++) {
        for (int j = 0; j < 8; j++) triangle_table[k++] = uint8_t(i);
    }
    for (int i = 255; i >= 0; i--) {
        for (int j = 0; j < 8; j++) triangle_table[k++] = uint8_t(i);
    }
    k = 0;
    for (int i = 0; i < 256; i++) {
        for (int j = 0; j < 16; j++) sawtooth_table[k++] = uint8_t(i);
    }
    k = 0;
    for (int i = 0; i < 4096; i++) square_table[k++] = 255;
    for (int i = 0; i < 4096; i++) square_table[k++] = 0;
    if (type == Type8580) {
        waveform30 = sid_tables::waveform30_8580;
        waveform50 = sid_tables::waveform50_8580;
        waveform60 = sid_tables::waveform60_8580;
        waveform70 = sid_tables::waveform70_8580;
        mode_normal[7] = sid_mode70;
        mode_ring[7] = sid_mode74;
    } else {
        waveform30 = sid_tables::waveform30_6581;
        waveform50 = sid_tables::waveform50_6581;
        waveform60 = sid_tables::waveform60_6581;
        waveform70 = sid_tables::waveform70_6581;
        mode_normal[7] = sid_mode00;
        mode_ring[7] = sid_mode00;
    }
    mode_normal[0] = sid_mode00;
    mode_normal[1] = sid_mode10;
    mode_normal[2] = sid_mode20;
    mode_normal[3] = sid_mode30;
    mode_normal[4] = sid_mode40;
    mode_normal[5] = sid_mode50;
    mode_normal[6] = sid_mode60;
    mode_normal[8] = sid_mode80;
    for (int i = 9; i < 16; i++) mode_normal[size_t(i)] = sid_mode_lock;
    mode_ring[0] = sid_mode00;
    mode_ring[1] = sid_mode14;
    mode_ring[2] = sid_mode00;
    mode_ring[3] = sid_mode34;
    mode_ring[4] = sid_mode00;
    mode_ring[5] = sid_mode54;
    mode_ring[6] = sid_mode00;
    for (int i = 8; i < 16; i++) mode_ring[size_t(i)] = sid_mode_lock;
    for (uint32_t ni = 0; ni < 65536; ni++) {
        noise_table_lsb[ni] = uint8_t(((ni >> (13 - 4)) & 0x10) | ((ni >> (11 - 3)) & 0x08) |
                                      ((ni >> (7 - 2)) & 0x04) | ((ni >> (4 - 1)) & 0x02) |
                                      ((ni >> (2 - 0)) & 0x01));
    }
    for (uint32_t ni = 0; ni < 256; ni++) {
        noise_table_msb[ni] = uint8_t(((ni << (7 - (22 - 16))) & 0x80) | ((ni << (6 - (20 - 16))) & 0x40) |
                                      ((ni << (5 - (16 - 16))) & 0x20));
    }
}

void Sid::enve_emu_init(uint32_t update_freq, bool measured) {
    release_tab_len = uint32_t(sizeof(sid_tables::releaseTab));
    for (int i = 0; i < 256; i++) {
        uint32_t j = 0;
        while ((j < release_tab_len) && (sid_tables::releaseTab[j] > i)) j++;
        release_pos[i] = (j < release_tab_len) ? j : (release_tab_len - 1);
    }
    int k = 0;
    for (int i = 0; i < 16; i++) {
        for (int j = 0; j < 256; j++) {
            uint16_t tmp_vol = uint16_t(j);
            if (measured) {
                tmp_vol = uint16_t(std::trunc((293.0 * (1.0 - std::exp(j / -130.0))) + 4.0));
                if (j == 0) tmp_vol = 0;
                if (tmp_vol > 255) tmp_vol = 255;
            }
            master_ampl_mod_table[k++] =
                uint16_t(std::lround((double(tmp_vol) * kMasterVolumeLevels[i]) / 255.0) << 8);
        }
    }
    for (int i = 0; i < 16; i++) {
        uint32_t scaled = uint32_t(std::floor((kAttackTimes[i] * float(update_freq)) / 1000.0f));
        if (scaled == 0) scaled = 1;
        attack_rates[i] = float(kAttackTabLen) / float(scaled);
        scaled = uint32_t(std::floor((kDecayReleaseTimes[i] * float(update_freq)) / 1000.0f));
        if (scaled == 0) scaled = 1;
        decay_release_rates[i] = float(release_tab_len) / float(scaled);
    }
}

Sid::Sid(uint32_t clock, Type type) : type_(type), clock_(clock) {
    const int rev[3] = {2, 1, 0};
    for (int v = 0; v < 3; v++) {
        optr[size_t(v)].chip = this;
        optr[size_t(v)].modulator = &optr[size_t(rev[v])];
        optr[size_t(rev[v])].carrier = &optr[size_t(v)];
        optr[size_t(v)].filt_voice_mask = uint8_t(1 << v);
    }
    pcm_sid = uint32_t(std::trunc(double(kSampleRate) * (16777216.0 / double(clock))));
    pcm_sid_noise = uint32_t(std::trunc((double(clock) * 256.0) / double(kSampleRate)));
    filter_enabled = true;
    sid_init_mixer_engine();
    filter_table_init();
    init_waveforms(type);
    enve_emu_init(kSampleRate, true);
    zero16bit_ = 0;
    mixer_init(0);
}

void Sid::reset() {
    for (int v = 0; v < 3; v++) {
        optr[size_t(v)].clear();
        enve_emu_reset_operator(optr[size_t(v)]);
    }
    optr3_output_mask = -1;
    filter_type = 0;
    filter_cur_type = 0;
    filter_value = 0;
    filter_dy = 0;
    filter_res_dy = 0;
    master_volume = 0;
    master_volume_ampl_index = 0;
    reg.fill(0);
    for (int v = 0; v < 3; v++) {
        optr[size_t(v)].set();
        optr[size_t(v)].set2();
    }
}

void Sid::mixer_init(int three_voice_amplify) {
    uint32_t amp_div = kMaxLogicalVoices;
    if (three_voice_amplify != 0) amp_div = kMaxLogicalVoices - 1;
    int si = (-128 * kMaxLogicalVoices) * 256;
    for (size_t ui = 0; ui < mix16mono_.size(); ui++) {
        mix16mono_[ui] = uint16_t(int(si / int(amp_div)) + int(zero16bit_));
        si += 256;
    }
}

void Sid::filter_table_init() {
    for (int rk = 0; rk < 0x800; rk++) {
        filter_table[size_t(rk)] =
            float((((std::exp(rk / 2048.0 * std::log(400.0)) / 60.0) + 0.05) * 44100.0) / kSampleRate);
        if (filter_table[size_t(rk)] < 0.01f) filter_table[size_t(rk)] = 0.01f;
        if (filter_table[size_t(rk)] > 1.0f) filter_table[size_t(rk)] = 1.0f;
    }
    float y_tmp = 0.05f;
    const float y_add = (0.22f - 0.05f) / 2048.0f;
    for (int rk = 0; rk < 0x800; rk++) {
        band_pass_param[size_t(rk)] = (y_tmp * 44100.0f) / float(kSampleRate);
        y_tmp += y_add;
    }
    float res_dy = 2.0f;
    for (int rk = 0; rk < 16; rk++) {
        filter_res_table[size_t(rk)] = res_dy;
        res_dy -= (2.0f - 1.0f) / 15.0f;
    }
    filter_res_table[0] = 2.0f;
    filter_res_table[15] = 1.0f;
}

void Sid::sync_em() {
    bool sync[3];
    for (int v = 0; v < 3; v++) {
        sync[v] = optr[size_t(v)].modulator->cycle_len_count <= 0;
        optr[size_t(v)].cycle_len_count -= 1;
    }
    for (int v = 0; v < 3; v++) {
        if (optr[size_t(v)].sync && sync[v]) {
            optr[size_t(v)].cycle_len_count = 0;
            optr[size_t(v)].out_proc = wave_calc_normal;
            optr[size_t(v)].wave_step = 0;
            optr[size_t(v)].wave_step_pnt = 0;
        }
    }
}

int16_t Sid::update() {
    const int v0 = optr[0].out_proc ? optr[0].out_proc(optr[0]) : 0;
    const int v1 = optr[1].out_proc ? optr[1].out_proc(optr[1]) : 0;
    const int v2 = (optr[2].out_proc ? optr[2].out_proc(optr[2]) : 0) & optr3_output_mask;
    int index = kMix16monoMiddleIndex + v0 + v1 + v2 + (int(master_volume) << 2);
    if (index < 0) index = -index;
    if (index >= int(mix16mono_.size())) index = int(mix16mono_.size()) - 1;
    sync_em();
    return int16_t(mix16mono_[size_t(index)]);
}

uint8_t Sid::read(uint8_t address) {
    address = uint8_t(address & 0x1f);
    if (address >= 0x1d) return 0xff;
    if (address == 0x1b) return optr[2].output;
    if (address == 0x1c) return optr[2].enve_vol;
    return reg[address];
}

void Sid::write(uint8_t address, uint8_t value) {
    address = uint8_t(address & 0x1f);
    if (address >= 0x19) return;
    if (address >= 0x15) {
        reg[address] = value;
        master_volume = uint8_t(reg[0x18] & 15);
        master_volume_ampl_index = uint16_t(master_volume << 8);
        if ((reg[0x18] & 0x80) && ((reg[0x17] & optr[2].filt_voice_mask) == 0)) {
            optr3_output_mask = 0;
        } else {
            optr3_output_mask = -1;
        }
        filter_type = uint8_t(reg[0x18] & 0x70);
        if (filter_type != filter_cur_type) {
            filter_cur_type = filter_type;
            for (int v = 0; v < 3; v++) {
                optr[size_t(v)].filt_low = 0;
                optr[size_t(v)].filt_ref = 0;
            }
        }
        if (filter_enabled) {
            filter_value = uint16_t(0x7ff & ((reg[0x15] & 7) | (uint16_t(reg[0x16]) << 3)));
            if (filter_type == 0x20) {
                filter_dy = band_pass_param[filter_value];
            } else {
                filter_dy = filter_table[filter_value];
            }
            filter_res_dy = filter_res_table[reg[0x17] >> 4] - filter_dy;
            if (filter_res_dy < 1.0f) filter_res_dy = 1.0f;
        }
        for (int v = 0; v < 3; v++) {
            optr[size_t(v)].set();
            optr[size_t(v)].set2();
        }
        return;
    }
    reg[address] = value;
    if (address < 7) {
        optr[0].reg[address] = value;
    } else if (address < 14) {
        optr[1].reg[address - 7] = value;
    } else if (address < 21) {
        optr[2].reg[address - 14] = value;
    }
    for (int v = 0; v < 3; v++) {
        optr[size_t(v)].set();
        optr[size_t(v)].set2();
    }
}

}  // namespace dsp
