#include "sound/gba_apu.h"

#include <algorithm>

namespace dsp {
namespace {

const uint8_t kDuty[4] = {0x01, 0x81, 0x87, 0x7E};

}  // namespace

void GbaApu::reset() {
    regs_.fill(0);
    for (auto& bank : wave_ram_) bank.fill(0);
    sq_ = {};
    wave_ = {};
    noise_ = {};
    for (auto& f : fifo_) f = Fifo{};
    frame_counter_ = 0;
    frame_step_ = 0;
    regs_[0x28] = 0x00;
    regs_[0x29] = 0x02;  // SOUNDBIAS = $200
}

uint8_t GbaApu::read8(uint32_t offset) const {
    if (offset >= 0x30 && offset < 0x40) return wave_ram_[size_t(1 - wave_bank_playing())][offset - 0x30];
    if (offset >= 0x40) return 0;  // FIFOs are write-only
    static const uint16_t kReadMask[0x2A / 2] = {
        0x007F, 0xFFC0, 0x4000, 0x0000, 0xFFC0, 0x0000, 0x4000, 0x0000,  // 60-6E
        0x00E0, 0xE000, 0x4000, 0x0000, 0xFF00, 0x0000, 0x40FF, 0x0000,  // 70-7E
        0xFF77, 0x770F, 0x0080, 0x0000, 0xC3FF,                          // 80-88
    };
    if (offset >= 0x2A) return 0;
    if (offset == 0x24) {
        uint8_t v = regs_[0x24] & 0x80;
        if (sq_[0].enabled) v |= 1;
        if (sq_[1].enabled) v |= 2;
        if (wave_.enabled) v |= 4;
        if (noise_.enabled) v |= 8;
        return v;
    }
    const uint16_t mask = kReadMask[offset / 2];
    return uint8_t(regs_[offset] & ((offset & 1) ? (mask >> 8) : mask));
}

void GbaApu::write8(uint32_t offset, uint8_t value) {
    if (offset >= 0x40 && offset < 0x48) {
        Fifo& f = fifo_[(offset - 0x40) / 4];
        if (f.count < 32) {
            f.data[size_t(f.write)] = int8_t(value);
            f.write = (f.write + 1) & 31;
            f.count++;
        }
        return;
    }
    if (offset >= 0x30 && offset < 0x40) {
        wave_ram_[size_t(1 - wave_bank_playing())][offset - 0x30] = value;
        return;
    }
    if (offset >= 0x2A) return;
    // With the master switch off only SOUNDCNT_X (and the bias) are writable.
    if (!master_enabled() && offset < 0x24) return;
    regs_[offset] = value;
    switch (offset) {
        case 0x00:
            sq_[0].sweep_shift = value & 7;
            sq_[0].sweep_down = (value & 8) != 0;
            sq_[0].sweep_period = (value >> 4) & 7;
            break;
        case 0x02:
        case 0x08: {
            Square& s = sq_[offset == 0x02 ? 0 : 1];
            s.length = 64 - (value & 63);
            s.duty = value >> 6;
            break;
        }
        case 0x03:
        case 0x09: {
            Square& s = sq_[offset == 0x03 ? 0 : 1];
            if ((value & 0xF8) == 0) s.enabled = false;  // DAC off
            break;
        }
        case 0x04:
        case 0x0C: {
            Square& s = sq_[offset == 0x04 ? 0 : 1];
            s.freq = (s.freq & 0x700) | value;
            break;
        }
        case 0x05:
        case 0x0D: {
            const int ch = offset == 0x05 ? 0 : 1;
            Square& s = sq_[size_t(ch)];
            s.freq = (s.freq & 0xFF) | ((value & 7) << 8);
            s.length_enable = (value & 0x40) != 0;
            if (value & 0x80) trigger_square(ch);
            break;
        }
        case 0x10:
            if (!(value & 0x80)) wave_.enabled = false;
            break;
        case 0x12:
            wave_.length = 256 - value;
            break;
        case 0x14:
            wave_.freq = (wave_.freq & 0x700) | value;
            break;
        case 0x15:
            wave_.freq = (wave_.freq & 0xFF) | ((value & 7) << 8);
            wave_.length_enable = (value & 0x40) != 0;
            if (value & 0x80) trigger_wave();
            break;
        case 0x18:
            noise_.length = 64 - (value & 63);
            break;
        case 0x19:
            if ((value & 0xF8) == 0) noise_.enabled = false;
            break;
        case 0x1D:
            noise_.length_enable = (value & 0x40) != 0;
            if (value & 0x80) trigger_noise();
            break;
        case 0x23:
            if (value & 0x08) fifo_[0].clear();
            if (value & 0x80) fifo_[1].clear();
            regs_[0x23] = value & 0x77;
            break;
        case 0x24:
            if (!(value & 0x80)) {
                // Master off: the PSG registers clear and every channel stops.
                std::fill(regs_.begin(), regs_.begin() + 0x22, uint8_t(0));
                sq_ = {};
                wave_ = {};
                noise_ = {};
            }
            break;
        default:
            break;
    }
}

void GbaApu::trigger_square(int ch) {
    Square& s = sq_[size_t(ch)];
    const uint8_t env = regs_[ch == 0 ? 0x03 : 0x09];
    s.enabled = (env & 0xF8) != 0;
    if (s.length == 0) s.length = 64;
    s.volume = env >> 4;
    s.env_up = (env & 8) != 0;
    s.env_period = env & 7;
    s.env_timer = s.env_period;
    s.timer = (2048 - s.freq) * 16;
    if (ch == 0) {
        s.shadow = s.freq;
        s.sweep_timer = s.sweep_period ? s.sweep_period : 8;
        s.sweep_enabled = s.sweep_period != 0 || s.sweep_shift != 0;
        if (s.sweep_shift) sweep_calc(false);
    }
}

void GbaApu::trigger_wave() {
    wave_.enabled = (regs_[0x10] & 0x80) != 0;
    if (wave_.length == 0) wave_.length = 256;
    wave_.timer = (2048 - wave_.freq) * 8;
    wave_.pos = 0;
}

void GbaApu::trigger_noise() {
    const uint8_t env = regs_[0x19];
    noise_.enabled = (env & 0xF8) != 0;
    if (noise_.length == 0) noise_.length = 64;
    noise_.volume = env >> 4;
    noise_.env_up = (env & 8) != 0;
    noise_.env_period = env & 7;
    noise_.env_timer = noise_.env_period;
    noise_.lfsr = 0x7FFF;
    noise_.timer = 0;
}

int GbaApu::sweep_calc(bool update) {
    Square& s = sq_[0];
    const int delta = s.shadow >> s.sweep_shift;
    const int next = s.sweep_down ? s.shadow - delta : s.shadow + delta;
    if (next > 2047) {
        s.enabled = false;
    } else if (update && s.sweep_shift) {
        s.shadow = next;
        s.freq = next;
    }
    return next;
}

void GbaApu::frame_step() {
    const int step = frame_step_;
    frame_step_ = (frame_step_ + 1) & 7;
    if ((step & 1) == 0) {  // 256 Hz length
        auto clock_length = [](bool enable, int& length, bool& on) {
            if (enable && length > 0 && --length == 0) on = false;
        };
        clock_length(sq_[0].length_enable, sq_[0].length, sq_[0].enabled);
        clock_length(sq_[1].length_enable, sq_[1].length, sq_[1].enabled);
        clock_length(wave_.length_enable, wave_.length, wave_.enabled);
        clock_length(noise_.length_enable, noise_.length, noise_.enabled);
    }
    if (step == 2 || step == 6) {  // 128 Hz sweep
        Square& s = sq_[0];
        if (--s.sweep_timer <= 0) {
            s.sweep_timer = s.sweep_period ? s.sweep_period : 8;
            if (s.sweep_enabled && s.sweep_period) {
                sweep_calc(true);
                sweep_calc(false);
            }
        }
    }
    if (step == 7) {  // 64 Hz envelope
        auto clock_env = [](int period, int& timer, int& volume, bool up) {
            if (period == 0) return;
            if (--timer <= 0) {
                timer = period;
                if (up && volume < 15) volume++;
                else if (!up && volume > 0) volume--;
            }
        };
        clock_env(sq_[0].env_period, sq_[0].env_timer, sq_[0].volume, sq_[0].env_up);
        clock_env(sq_[1].env_period, sq_[1].env_timer, sq_[1].volume, sq_[1].env_up);
        clock_env(noise_.env_period, noise_.env_timer, noise_.volume, noise_.env_up);
    }
}

void GbaApu::clock_channels() {
    for (Square& s : sq_) {
        if (!s.enabled) continue;
        const int period = (2048 - s.freq) * 16;
        s.timer -= kCyclesPerSample;
        while (s.timer <= 0) {
            s.timer += period;
            s.duty_pos = (s.duty_pos + 1) & 7;
        }
    }
    if (wave_.enabled) {
        const int period = (2048 - wave_.freq) * 8;
        const int samples = (regs_[0x10] & 0x20) ? 64 : 32;
        wave_.timer -= kCyclesPerSample;
        while (wave_.timer <= 0) {
            wave_.timer += period;
            wave_.pos = (wave_.pos + 1) % samples;
        }
    }
    if (noise_.enabled) {
        const uint8_t cnt = regs_[0x1C];
        const int shift = cnt >> 4;
        if (shift < 14) {
            const int r = cnt & 7;
            const int period = ((r ? r * 16 : 8) << shift) * 4;
            noise_.timer -= kCyclesPerSample;
            while (noise_.timer <= 0) {
                noise_.timer += period;
                const uint16_t bit = (noise_.lfsr ^ (noise_.lfsr >> 1)) & 1;
                noise_.lfsr = uint16_t((noise_.lfsr >> 1) | (bit << 14));
                if (cnt & 8) noise_.lfsr = uint16_t((noise_.lfsr & ~0x40) | (bit << 6));
            }
        }
    }
}

void GbaApu::timer_overflow(int timer) {
    const uint8_t hi = regs_[0x23];
    for (int ch = 0; ch < 2; ch++) {
        const int selected = (hi >> (ch == 0 ? 2 : 6)) & 1;
        if (selected != timer) continue;
        Fifo& f = fifo_[size_t(ch)];
        if (f.count > 0) {
            f.current = f.data[size_t(f.read)];
            f.read = (f.read + 1) & 31;
            f.count--;
        }
        if (f.count <= 16 && request_fifo) request_fifo(ch);
    }
}

int16_t GbaApu::sample() {
    if (!master_enabled()) return 0;
    if (++frame_counter_ >= kSampleRate / 512) {
        frame_counter_ = 0;
        frame_step();
    }
    clock_channels();

    int out[4] = {0, 0, 0, 0};
    for (int ch = 0; ch < 2; ch++) {
        const Square& s = sq_[size_t(ch)];
        if (s.enabled) out[ch] = ((kDuty[s.duty] >> s.duty_pos) & 1) ? s.volume : -s.volume;
    }
    if (wave_.enabled) {
        const int bank = (wave_bank_playing() + wave_.pos / 32) & 1;
        const int idx = wave_.pos & 31;
        const uint8_t byte = wave_ram_[size_t(bank)][size_t(idx / 2)];
        const int nib = (idx & 1) ? (byte & 15) : (byte >> 4);
        const uint8_t vol = regs_[0x13];
        int v = nib * 2 - 15;
        if (vol & 0x80) v = v * 3 / 4;
        else switch ((vol >> 5) & 3) {
            case 0: v = 0; break;
            case 1: break;
            case 2: v /= 2; break;
            default: v /= 4; break;
        }
        out[2] = v;
    }
    if (noise_.enabled) out[3] = (noise_.lfsr & 1) ? -noise_.volume : noise_.volume;

    const uint8_t enables = regs_[0x21];
    const int vol_r = (regs_[0x20] & 7) + 1;
    const int vol_l = ((regs_[0x20] >> 4) & 7) + 1;
    int left = 0, right = 0;
    for (int ch = 0; ch < 4; ch++) {
        if (enables & (1 << ch)) right += out[ch];
        if (enables & (0x10 << ch)) left += out[ch];
    }
    left *= vol_l;
    right *= vol_r;
    const uint8_t lo = regs_[0x22];
    const int ratio = lo & 3;
    const int psg_shift = ratio == 0 ? 2 : ratio == 1 ? 1 : 0;
    left >>= psg_shift;
    right >>= psg_shift;

    const uint8_t hi = regs_[0x23];
    for (int ch = 0; ch < 2; ch++) {
        const int full = (lo >> (2 + ch)) & 1;
        const int v = int(fifo_[size_t(ch)].current) * (full ? 4 : 2);
        const int sh = ch * 4;
        if (hi & (1 << sh)) right += v;
        if (hi & (2 << sh)) left += v;
    }
    left = std::clamp(left, -512, 511);
    right = std::clamp(right, -512, 511);
    return int16_t((left + right) * 32);
}

}  // namespace dsp
