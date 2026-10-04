#include "machine/model3_sound.h"

#include <algorithm>
#include <cstring>

namespace dsp {
namespace {

constexpr uint32_t kRamMask = 0x7ffff;
constexpr int kCyclesPerSample = 256;            // 11.2896 MHz / 44100
constexpr double kDsbClock = 11000000.0;
constexpr int kDsbBlock = 44;                    // DSB2 scheduled every ~1 ms of output

// ROM_LOAD16_WORD_SWAP: the dumps hold little-endian words.
std::vector<uint8_t> word_swap(const std::vector<uint8_t>& in) {
    std::vector<uint8_t> out(in.size());
    for (size_t i = 0; i + 1 < in.size(); i += 2) {
        out[i] = in[i + 1];
        out[i + 1] = in[i];
    }
    return out;
}

}  // namespace

Model3Sound::Model3Sound() : cpu_(11289600), dsb_cpu_(11000000) {
    ram1_.assign(kRamMask + 1, 0);
    ram2_.assign(kRamMask + 1, 0);
    program_.assign(0x80000, 0xff);
    samples_.assign(0x800000, 0);
    dsb_program_.assign(0x20000, 0xff);
    dsb_ram_.assign(0x20000, 0);
    scsp1_.set_ram(ram1_.data(), kRamMask);
    scsp2_.set_ram(ram2_.data(), kRamMask);

    cpu_.set_memory_handlers([this](uint32_t a) { return read_word(a); },
                             [this](uint32_t a, uint16_t v) { write_word(a, v); });
    cpu_.set_byte_handlers([this](uint32_t a) { return read_byte(a); },
                           [this](uint32_t a, uint8_t v) { write_byte(a, v); });
    cpu_.set_address_mask(0xffffff);
    dsb_cpu_.set_memory_handlers([this](uint32_t a) { return dsb_read_word(a); },
                                 [this](uint32_t a, uint16_t v) { dsb_write_word(a, v); });
    dsb_cpu_.set_byte_handlers([this](uint32_t a) { return dsb_read_byte(a); },
                               [this](uint32_t a, uint8_t v) { dsb_write_byte(a, v); });
    dsb_cpu_.set_address_mask(0xffffff);
}

void Model3Sound::load(const std::vector<uint8_t>& program, const std::vector<uint8_t>& samples,
                       const std::vector<uint8_t>& dsb_program, const std::vector<uint8_t>& mpeg) {
    program_ = word_swap(program);
    program_.resize(0x80000, 0xff);
    samples_ = word_swap(samples);
    if (samples_.empty()) samples_.assign(0x800000, 0);
    dsb_program_ = word_swap(dsb_program);
    dsb_program_.resize(0x20000, 0xff);
    mpeg_rom_ = mpeg;
    if (mpeg_rom_.empty()) mpeg_rom_.assign(16, 0);
    decoder_ = std::make_unique<mpeg_audio>(mpeg_rom_.data(), mpeg_audio::L2, false, 0);
}

void Model3Sound::reset() {
    std::fill(ram1_.begin(), ram1_.end(), 0);
    std::fill(ram2_.begin(), ram2_.end(), 0);
    std::memcpy(ram1_.data(), program_.data(), 16);  // 68000 vectors
    sample_bank_ = 0;
    scsp1_.reset();
    scsp2_.reset();
    cycle_debt_ = 0;
    current_level_ = 0;
    cpu_.reset();

    std::fill(dsb_ram_.begin(), dsb_ram_.end(), 0);
    dsb_fifo_r_ = dsb_fifo_w_ = 0;
    cmd_latch_ = 0;
    dsb_state_ = 0;
    mp_start_ = mp_end_ = 0;
    mpeg_playing_ = false;
    loop_ = false;
    volume_[0] = volume_[1] = 0xff;
    stereo_ = 0;
    dsb_cycle_acc_ = dsb_timer_acc_ = 0;
    pcm_.clear();
    pcm_pos_ = 0;
    pcm_frac_ = 0;
    last_l_ = last_r_ = 0;
    if (decoder_) decoder_->clear();
    dsb_cpu_.reset();
}

void Model3Sound::midi_write(uint8_t data) {
    scsp1_.midi_in(data);
    dsb_fifo_[dsb_fifo_w_++] = data;
    if (dsb_fifo_w_ == dsb_fifo_r_) ++dsb_fifo_r_;
}

void Model3Sound::set_level(int level) {
    if (level == current_level_) return;
    current_level_ = level;
    for (int l = 1; l < 8; ++l) cpu_.set_irq(l, l == level ? IrqLine::Assert : IrqLine::Clear);
}

// ---------------------------------------------------------------------------
// Sound board bus

uint8_t Model3Sound::read_byte(uint32_t a) {
    a &= 0xffffff;
    switch ((a >> 20) & 0xf) {
        case 0x0: return ram1_[a & kRamMask];
        case 0x1: return scsp1_.read8(a & 0xfff);
        case 0x2: return ram2_[a & kRamMask];
        case 0x3: return scsp2_.read8(a & 0xfff);
        case 0x6: return program_[a & 0x7ffff];
        case 0x8: case 0x9: case 0xa: case 0xb:
        case 0xc: case 0xd: case 0xe: case 0xf:
            return samples_[(sample_bank_ + (a & 0x7fffff)) % samples_.size()];
        default: return 0;
    }
}

uint16_t Model3Sound::read_word(uint32_t a) {
    a &= 0xfffffe;
    switch ((a >> 20) & 0xf) {
        case 0x0: return uint16_t(ram1_[a & kRamMask] << 8 | ram1_[(a + 1) & kRamMask]);
        case 0x1: return scsp1_.read16(a & 0xfff);
        case 0x2: return uint16_t(ram2_[a & kRamMask] << 8 | ram2_[(a + 1) & kRamMask]);
        case 0x3: return scsp2_.read16(a & 0xfff);
        case 0x6: return uint16_t(program_[a & 0x7ffff] << 8 | program_[(a + 1) & 0x7ffff]);
        case 0x8: case 0x9: case 0xa: case 0xb:
        case 0xc: case 0xd: case 0xe: case 0xf: {
            const size_t o = (sample_bank_ + (a & 0x7fffff)) % samples_.size();
            return uint16_t(samples_[o] << 8 | samples_[(o + 1) % samples_.size()]);
        }
        default: return 0;
    }
}

void Model3Sound::write_byte(uint32_t a, uint8_t v) {
    a &= 0xffffff;
    switch ((a >> 20) & 0xf) {
        case 0x0: ram1_[a & kRamMask] = v; break;
        case 0x1: scsp1_.write8(a & 0xfff, v); break;
        case 0x2: ram2_[a & kRamMask] = v; break;
        case 0x3: scsp2_.write8(a & 0xfff, v); break;
        default:
            if (a == 0x400001) sample_bank_ = (v & 0x10) ? 0x800000u : 0u;
            break;
    }
}

void Model3Sound::write_word(uint32_t a, uint16_t v) {
    a &= 0xfffffe;
    switch ((a >> 20) & 0xf) {
        case 0x0:
            ram1_[a & kRamMask] = uint8_t(v >> 8);
            ram1_[(a + 1) & kRamMask] = uint8_t(v);
            break;
        case 0x1: scsp1_.write16(a & 0xfff, v); break;
        case 0x2:
            ram2_[a & kRamMask] = uint8_t(v >> 8);
            ram2_[(a + 1) & kRamMask] = uint8_t(v);
            break;
        case 0x3: scsp2_.write16(a & 0xfff, v); break;
        default:
            if (a == 0x400000) sample_bank_ = (v & 0x10) ? 0x800000u : 0u;
            break;
    }
}

// ---------------------------------------------------------------------------
// DSB2

uint8_t Model3Sound::dsb_read_byte(uint32_t a) {
    a &= 0xffffff;
    if (a < 0x20000) return dsb_program_[a];
    if (a == 0xc00001) return cmd_latch_;
    if (a == 0xc00003) return 1;     // command valid
    if (a == 0xe80001) return 0x01;  // MPEG decoder status
    if (a >= 0xf00000 && a < 0xf20000) return dsb_ram_[a & 0x1ffff];
    return 0;
}

uint16_t Model3Sound::dsb_read_word(uint32_t a) {
    a &= 0xfffffe;
    if (a < 0x20000) return uint16_t(dsb_program_[a] << 8 | dsb_program_[a + 1]);
    if (a >= 0xf00000 && a < 0xf20000) return uint16_t(dsb_ram_[a & 0x1ffff] << 8 | dsb_ram_[(a & 0x1ffff) + 1]);
    if (a == 0xc00000) return cmd_latch_;
    if (a == 0xc00002) return 1;
    if (a == 0xe80000) return 1;
    return 0;
}

void Model3Sound::dsb_write_byte(uint32_t a, uint8_t v) {
    a &= 0xffffff;
    if (a >= 0xf00000 && a < 0xf20000) {
        dsb_ram_[a & 0x1ffff] = v;
    } else if (a == 0xe00003) {
        mpeg_fifo_write(v);
    }
}

void Model3Sound::dsb_write_word(uint32_t a, uint16_t v) {
    a &= 0xfffffe;
    if (a >= 0xf00000 && a < 0xf20000) {
        dsb_ram_[a & 0x1ffff] = uint8_t(v >> 8);
        dsb_ram_[(a & 0x1ffff) + 1] = uint8_t(v);
    } else if (a == 0xe00002) {
        mpeg_fifo_write(uint8_t(v));
    }
}

void Model3Sound::mpeg_fifo_write(uint8_t b) {
    enum {
        kIdle, kStart0, kStart1, kStart2, kEnd0, kEnd1, kEnd2,
        kA0, kA1, kA3, kA4, kA5, kA7, kB0, kB1, kB2, kB4, kB5, kB6
    };
    auto start_play = [this] {
        loop_ = false;
        const uint32_t size = uint32_t(mpeg_rom_.size());
        if (mp_start_ >= size || mp_end_ > size || mp_end_ <= mp_start_) {
            mpeg_playing_ = false;
            return;
        }
        mp_pos_ = int(mp_start_ * 8);
        mp_limit_ = int(mp_end_ * 8);
        mpeg_playing_ = true;
        pcm_.clear();
        pcm_pos_ = 0;
        if (decoder_) decoder_->clear();
    };
    switch (dsb_state_) {
        case kIdle:
            switch (b) {
                case 0x14: case 0x15: dsb_state_ = kStart0; break;
                case 0x24: case 0x25: dsb_state_ = kEnd0; break;
                case 0x74: case 0x75: start_play(); break;
                case 0x84: case 0x85:
                    mpeg_playing_ = false;
                    pcm_.clear();
                    pcm_pos_ = 0;
                    break;
                case 0xa0: dsb_state_ = kA0; break;
                case 0xa1: dsb_state_ = kA1; break;
                case 0xa3: dsb_state_ = kA3; break;
                case 0xa4: dsb_state_ = kA4; break;
                case 0xa5: dsb_state_ = kA5; break;
                case 0xa7: dsb_state_ = kA7; break;
                case 0xb0: dsb_state_ = kB0; break;
                case 0xb1: dsb_state_ = kB1; break;
                case 0xb2: dsb_state_ = kB2; break;
                case 0xb4: dsb_state_ = kB4; break;
                case 0xb5: dsb_state_ = kB5; break;
                case 0xb6: dsb_state_ = kB6; break;
                default: break;
            }
            break;
        case kStart0: mp_start_ = (mp_start_ & 0x00ffff) | uint32_t(b) << 16; dsb_state_ = kStart1; break;
        case kStart1: mp_start_ = (mp_start_ & 0xff00ff) | uint32_t(b) << 8; dsb_state_ = kStart2; break;
        case kStart2:
            mp_start_ = (mp_start_ & 0xffff00) | b;
            dsb_state_ = kIdle;
            if (mpeg_playing_) {
                // A new start address while playing sets the loop segment.
                loop_ = true;
                loop_start_ = mp_start_;
                loop_end_ = mp_end_;
            }
            break;
        case kEnd0: mp_end_ = (mp_end_ & 0x00ffff) | uint32_t(b) << 16; dsb_state_ = kEnd1; break;
        case kEnd1: mp_end_ = (mp_end_ & 0xff00ff) | uint32_t(b) << 8; dsb_state_ = kEnd2; break;
        case kEnd2:
            mp_end_ = (mp_end_ & 0xffff00) | b;
            stereo_ = 0;
            dsb_state_ = kIdle;
            break;
        case kA0: stereo_ = b ? 1 : 0; dsb_state_ = kIdle; break;
        case kB1: stereo_ = b ? 2 : 0; dsb_state_ = kIdle; break;
        case kA4:
            dsb_state_ = kIdle;
            if (b == 0x75) start_play();
            break;
        case kB4:
            dsb_state_ = kIdle;
            if (b == 0x96) mpeg_playing_ = false;
            break;
        case kB0: case kB6: volume_[0] = b; dsb_state_ = kIdle; break;
        case kA1: case kA7: volume_[1] = b; dsb_state_ = kIdle; break;
        default: dsb_state_ = kIdle; break;
    }
}

void Model3Sound::run_dsb(int cycles) {
    // Pending command bytes: one IRQ 1 each, with time to read the latch.
    while (dsb_fifo_r_ != dsb_fifo_w_) {
        cmd_latch_ = dsb_fifo_[dsb_fifo_r_++];
        dsb_cpu_.set_irq(1, IrqLine::Hold);
        cycles -= dsb_cpu_.run(500);
    }
    dsb_cpu_.set_irq(2, IrqLine::Hold);  // 1 kHz timer
    if (cycles > 0) dsb_cpu_.run(cycles);
}

void Model3Sound::decode_mpeg_frame() {
    pcm_.clear();
    pcm_pos_ = 0;
    if (!mpeg_playing_ || !decoder_) return;
    short buf[1152 * 2];
    int samples = 0, rate = 0, channels = 0;
    for (int attempt = 0; attempt < 2; ++attempt) {
        int pos = mp_pos_;
        if (decoder_->decode_buffer(pos, mp_limit_, buf, samples, rate, channels)) {
            mp_pos_ = pos;
            if (rate > 0) pcm_rate_ = rate;
            for (int i = 0; i < samples; ++i) {
                const int16_t l = buf[channels == 2 ? i * 2 : i];
                const int16_t r = buf[channels == 2 ? i * 2 + 1 : i];
                pcm_.push_back(l);
                pcm_.push_back(r);
            }
            return;
        }
        if (!loop_ || attempt == 1) break;
        mp_pos_ = int(loop_start_ * 8);
        mp_limit_ = int(loop_end_ * 8);
    }
    mpeg_playing_ = false;
}

void Model3Sound::run(int samples, std::vector<int16_t>& out) {
    const double dsb_per_sample = kDsbClock / Scsp::kSampleRate;
    for (int s = 0; s < samples; ++s) {
        if (s % kDsbBlock == 0) {
            dsb_cycle_acc_ += dsb_per_sample * kDsbBlock;
            const int n = int(dsb_cycle_acc_);
            dsb_cycle_acc_ -= n;
            run_dsb(n);
        }

        int32_t l1, r1, l2, r2;
        scsp1_.sample(l1, r1);
        scsp2_.sample(l2, r2);
        set_level(scsp1_.irq_level());
        const int want = kCyclesPerSample - cycle_debt_;
        const int ran = want > 0 ? cpu_.run(want) : 0;
        cycle_debt_ = ran - want;
        if (cycle_debt_ > 4 * kCyclesPerSample) cycle_debt_ = 0;

        // MPEG music, resampled to 44.1 kHz (linear interpolation).
        int32_t ml = 0, mr = 0;
        pcm_frac_ += double(pcm_rate_) / Scsp::kSampleRate;
        while (pcm_frac_ >= 1.0) {
            pcm_frac_ -= 1.0;
            if (pcm_pos_ < pcm_.size() / 2) {
                last_l_ = pcm_[pcm_pos_ * 2];
                last_r_ = pcm_[pcm_pos_ * 2 + 1];
                ++pcm_pos_;
            } else {
                decode_mpeg_frame();
                if (!pcm_.empty()) {
                    last_l_ = pcm_[0];
                    last_r_ = pcm_[1];
                    pcm_pos_ = 1;
                } else {
                    last_l_ = last_r_ = 0;
                }
            }
        }
        if (mpeg_playing_ || pcm_pos_ < pcm_.size() / 2) {
            int32_t nl = last_l_, nr = last_r_;
            if (pcm_pos_ < pcm_.size() / 2) {
                nl = pcm_[pcm_pos_ * 2];
                nr = pcm_[pcm_pos_ * 2 + 1];
            }
            const int32_t il = int32_t(last_l_ + (nl - last_l_) * pcm_frac_);
            const int32_t ir = int32_t(last_r_ + (nr - last_r_) * pcm_frac_);
            int32_t src_l = il, src_r = ir;
            uint8_t vl = volume_[0], vr = volume_[1];
            if (stereo_ == 1) {
                src_r = il;
                vr = volume_[0];
            } else if (stereo_ == 2) {
                src_l = ir;
                vl = volume_[1];
            }
            ml = src_l * vl / 255;
            mr = src_r * vr / 255;
        }

        const int32_t left = std::clamp(l1 + l2 + ml, -32768, 32767);
        const int32_t right = std::clamp(r1 + r2 + mr, -32768, 32767);
        out.push_back(int16_t(left));
        out.push_back(int16_t(right));
    }
}

}  // namespace dsp
