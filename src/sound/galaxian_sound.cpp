#include "sound/galaxian_sound.h"

#include <algorithm>

namespace dsp {
namespace {

// Background 555 periods (Hz) for FS1/FS2/FS3 at lfo DAC = 0, scaled up as
// the 4-bit DAC shorts timing resistors (MAME GAL_R15..R18 network).
constexpr int kLfoBaseHz[3] = {8, 16, 32};

}  // namespace

void GalaxianSound::reset() {
    pitch_ = 0xff;
    lfo_ = 0;
    fs_[0] = fs_[1] = fs_[2] = false;
    hit_ = fire_ = vol1_ = vol2_ = false;
    pitch_acc_ = 0;
    pitch_div_ = 0;
    lfo_acc_[0] = lfo_acc_[1] = lfo_acc_[2] = 0;
    lfo_out_[0] = lfo_out_[1] = lfo_out_[2] = false;
    noise_ = 0x1;
    hit_env_ = 0;
}

void GalaxianSound::pitch_w(uint8_t data) { pitch_ = data; }

void GalaxianSound::lfo_freq_w(int bit, uint8_t data) {
    if (bit < 0 || bit > 3) return;
    if (data & 1)
        lfo_ |= uint8_t(1 << bit);
    else
        lfo_ = uint8_t(lfo_ & ~(1 << bit));
}

void GalaxianSound::sound_w(int offset, uint8_t data) {
    const bool on = (data & 1) != 0;
    switch (offset & 7) {
        case 0:
        case 1:
        case 2:
            fs_[offset & 7] = on;
            break;
        case 3:
            if (on && !hit_) hit_env_ = 18000;
            hit_ = on;
            break;
        case 5:
            fire_ = on;
            break;
        case 6:
            vol1_ = on;
            break;
        case 7:
            vol2_ = on;
            break;
        default:
            break;
    }
}

int16_t GalaxianSound::update() {
    // Two cascaded LS164s reload from the pitch latch: freq = SOUND_CLOCK / (256-pitch).
    const int reload = std::max(1, 256 - int(pitch_));
    pitch_acc_ += kSoundClock;
    while (pitch_acc_ >= uint32_t(reload) * uint32_t(kSampleRate)) {
        pitch_acc_ -= uint32_t(reload) * uint32_t(kSampleRate);
        pitch_div_ = uint8_t((pitch_div_ + 1) & 0x0f);
    }

    // 74393: QA, QC, QD feed the mixer (MAME NODE_133). VOL1/VOL2 (4066
    // switches) select which ladder legs reach the op-amp — this is the
    // attract march / alien-hit oscillator, not just the FIRE path.
    int32_t tone = 0;
    if (pitch_div_ & 0x01) tone += 2800;
    if (pitch_div_ & 0x04) tone += 4200;
    if (pitch_div_ & 0x08) tone += 5600;

    int32_t melody = 0;
    if (pitch_ != 0xff) {
        if (vol1_) melody += tone;
        if (vol2_) melody += tone * 2 / 3;
        melody += tone / 6;
    }

    int32_t bg = melody;
    const int lfo_scale = 1 + int(lfo_);
    for (int i = 0; i < 3; i++) {
        const int hz = std::max(1, kLfoBaseHz[i] * lfo_scale);
        lfo_acc_[i] += uint32_t(hz);
        while (lfo_acc_[i] >= uint32_t(kSampleRate)) {
            lfo_acc_[i] -= uint32_t(kSampleRate);
            lfo_out_[i] = !lfo_out_[i];
        }
        if (fs_[i] && lfo_out_[i]) bg += tone;
    }

    int32_t shot = 0;
    if (fire_) {
        shot = tone;
        if (vol1_) shot += tone / 2;
        if (vol2_) shot += tone / 2;
    }

    int32_t noise = 0;
    noise_ = (noise_ >> 1) ^ ((noise_ & 1) ? 0xD400u : 0);
    if (hit_ || hit_env_ > 0) {
        const int32_t level = hit_ ? 12000 : hit_env_;
        noise = (int32_t(noise_ & 0xffff) - 0x8000) * level / 32768;
        if (hit_env_ > 0) hit_env_ -= 3;
    }

    const int32_t sample = bg / 3 + shot / 2 + noise;
    return int16_t(std::clamp(sample, int32_t(-24000), int32_t(24000)));
}

}  // namespace dsp
