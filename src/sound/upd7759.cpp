#include "sound/upd7759.h"

#include <algorithm>

#include "sound/ym2151.h"

namespace dsp {
namespace {

const int kStep[16][16] = {
    {0, 0, 1, 2, 3, 5, 7, 10, 0, 0, -1, -2, -3, -5, -7, -10},
    {0, 1, 2, 3, 4, 6, 8, 13, 0, -1, -2, -3, -4, -6, -8, -13},
    {0, 1, 2, 4, 5, 7, 10, 15, 0, -1, -2, -4, -5, -7, -10, -15},
    {0, 1, 3, 4, 6, 9, 13, 19, 0, -1, -3, -4, -6, -9, -13, -19},
    {0, 2, 3, 5, 8, 11, 15, 23, 0, -2, -3, -5, -8, -11, -15, -23},
    {0, 2, 4, 7, 10, 14, 19, 29, 0, -2, -4, -7, -10, -14, -19, -29},
    {0, 3, 5, 8, 12, 16, 22, 33, 0, -3, -5, -8, -12, -16, -22, -33},
    {1, 4, 7, 10, 15, 20, 29, 43, -1, -4, -7, -10, -15, -20, -29, -43},
    {1, 4, 8, 13, 18, 25, 35, 53, -1, -4, -8, -13, -18, -25, -35, -53},
    {1, 6, 10, 16, 22, 31, 43, 64, -1, -6, -10, -16, -22, -31, -43, -64},
    {2, 7, 12, 19, 27, 37, 51, 76, -2, -7, -12, -19, -27, -37, -51, -76},
    {2, 9, 16, 24, 34, 46, 64, 96, -2, -9, -16, -24, -34, -46, -64, -96},
    {3, 11, 19, 29, 41, 57, 79, 117, -3, -11, -19, -29, -41, -57, -79, -117},
    {4, 13, 24, 36, 50, 69, 96, 143, -4, -13, -24, -36, -50, -69, -96, -143},
    {4, 16, 29, 44, 62, 85, 118, 175, -4, -16, -29, -44, -62, -85, -118, -175},
    {6, 20, 36, 54, 76, 104, 144, 214, -6, -20, -36, -54, -76, -104, -144, -214},
};

const int kStateTable[16] = {-1, -1, 0, 0, 1, 2, 2, 3, -1, -1, 0, 0, 1, 2, 2, 3};

}  // namespace

Upd7759::Upd7759(float amp, bool slave) : amp_(amp), slave_(slave) {
    resample_inc_ = float(kClock) / 4.0f / float(YM2151::kSampleRate);
    reset();
}

void Upd7759::reset() {
    pos_ = 0;
    state_ = kIdle;
    clocks_left_ = 0;
    nibbles_left_ = 0;
    repeat_count_ = 0;
    post_drq_state_ = kIdle;
    post_drq_clocks_ = 0;
    req_sample_ = 0;
    last_sample_ = 0;
    block_header_ = 0;
    sample_rate_ = 0;
    first_valid_header_ = 0;
    offset_ = 0;
    repeat_offset_ = 0;
    adpcm_state_ = 0;
    adpcm_data_ = 0;
    sample_ = 0;
    drq_ = 0;
}

uint8_t Upd7759::rom_byte(uint32_t offset) const {
    if (rom_.empty()) return fifo_in_;
    return rom_[offset % rom_.size()];
}

void Upd7759::update_adpcm(int data) {
    sample_ += kStep[adpcm_state_][data];
    adpcm_state_ += kStateTable[data];
    if (adpcm_state_ < 0) adpcm_state_ = 0;
    if (adpcm_state_ > 15) adpcm_state_ = 15;
}

void Upd7759::advance_state() {
    switch (state_) {
        case kIdle:
            clocks_left_ = 4;
            break;
        case kDropDrq:
            drq_ = 0;
            clocks_left_ = post_drq_clocks_;
            state_ = post_drq_state_;
            break;
        case kStart:
            req_sample_ = rom_.empty() ? 0x10 : fifo_in_;
            clocks_left_ = 70;
            state_ = kFirstReq;
            break;
        case kFirstReq:
            drq_ = 1;
            clocks_left_ = 44;
            state_ = kLastSample;
            break;
        case kLastSample:
            last_sample_ = rom_.empty() ? fifo_in_ : rom_byte(0);
            drq_ = 1;
            clocks_left_ = 28;
            state_ = (req_sample_ > last_sample_) ? kIdle : kDummy1;
            break;
        case kDummy1:
            drq_ = 1;
            clocks_left_ = 32;
            state_ = kAddrMsb;
            break;
        case kAddrMsb:
            offset_ = uint32_t(rom_.empty() ? fifo_in_ : rom_byte(uint32_t(req_sample_) * 2 + 5))
                      << 9;
            drq_ = 1;
            clocks_left_ = 44;
            state_ = kAddrLsb;
            break;
        case kAddrLsb:
            offset_ |= uint32_t(rom_.empty() ? fifo_in_ : rom_byte(uint32_t(req_sample_) * 2 + 6))
                       << 1;
            drq_ = 1;
            clocks_left_ = 36;
            state_ = kDummy2;
            break;
        case kDummy2:
            offset_ += 1;
            first_valid_header_ = 0;
            drq_ = 1;
            clocks_left_ = 36;
            state_ = kBlockHeader;
            break;
        case kBlockHeader:
            if (repeat_count_ != 0) {
                repeat_count_--;
                offset_ = repeat_offset_;
            }
            block_header_ = rom_.empty() ? fifo_in_ : rom_byte(offset_ & 0x1ffff);
            offset_++;
            drq_ = 1;
            switch (block_header_ & 0xc0) {
                case 0x00:
                    clocks_left_ = 1024 * ((block_header_ & 0x3f) + 1);
                    state_ = (block_header_ == 0 && first_valid_header_) ? kIdle : kBlockHeader;
                    sample_ = 0;
                    adpcm_state_ = 0;
                    break;
                case 0x40:
                    sample_rate_ = uint8_t((block_header_ & 0x3f) + 1);
                    nibbles_left_ = 256;
                    clocks_left_ = 36;
                    state_ = kNibbleMsn;
                    break;
                case 0x80:
                    sample_rate_ = uint8_t((block_header_ & 0x3f) + 1);
                    clocks_left_ = 36;
                    state_ = kNibbleCount;
                    break;
                default:
                    repeat_count_ = uint8_t((block_header_ & 7) + 1);
                    repeat_offset_ = offset_;
                    clocks_left_ = 36;
                    state_ = kBlockHeader;
                    break;
            }
            if (block_header_ != 0) first_valid_header_ = 1;
            break;
        case kNibbleCount:
            nibbles_left_ = uint16_t((rom_.empty() ? fifo_in_ : rom_byte(offset_ & 0x1ffff)) + 1);
            offset_++;
            drq_ = 1;
            clocks_left_ = 36;
            state_ = kNibbleMsn;
            break;
        case kNibbleMsn:
            adpcm_data_ = rom_.empty() ? fifo_in_ : rom_byte(offset_ & 0x1ffff);
            offset_++;
            update_adpcm(adpcm_data_ >> 4);
            drq_ = 1;
            clocks_left_ = sample_rate_ * 4;
            nibbles_left_--;
            state_ = (nibbles_left_ == 0) ? kBlockHeader : kNibbleLsn;
            break;
        case kNibbleLsn:
            update_adpcm(adpcm_data_ & 15);
            clocks_left_ = sample_rate_ * 4;
            nibbles_left_--;
            state_ = (nibbles_left_ == 0) ? kBlockHeader : kNibbleMsn;
            break;
        default:
            break;
    }
    if (drq_ & 1) {
        post_drq_state_ = state_;
        post_drq_clocks_ = clocks_left_ - 21;
        state_ = kDropDrq;
        clocks_left_ = 21;
    }
}

void Upd7759::reset_w(uint8_t data) {
    const uint8_t old = reset_pin_;
    reset_pin_ = data ? 1 : 0;
    if (old && !reset_pin_) reset();
}

void Upd7759::start_w(uint8_t data) {
    const uint8_t old = start_;
    start_ = data ? 1 : 0;
    if (state_ == kIdle && !old && start_ && reset_pin_) state_ = kStart;
}

void Upd7759::port_w(uint8_t data) { fifo_in_ = data; }

int32_t Upd7759::update() {
    int clocks_left = clocks_left_;
    uint32_t pos = pos_;
    int out = 0;
    resample_pos_ += resample_inc_;
    int num_samples = int(resample_pos_);
    resample_pos_ -= float(num_samples);
    if (state_ != kIdle) {
        while (num_samples != 0) {
            out = sample_ << 7;
            pos += step_;
            while (pos >= kFracOne) {
                int clocks_this = int(pos >> kFracBits);
                if (clocks_this > clocks_left) clocks_this = clocks_left;
                pos -= uint32_t(clocks_this) * kFracOne;
                clocks_left -= clocks_this;
                if (clocks_left == 0) {
                    const uint8_t old_drq = drq_;
                    advance_state();
                    if (old_drq != drq_ && drq_handler_) drq_handler_(drq_);
                    if (state_ == kIdle) break;
                    clocks_left = clocks_left_;
                    out = (out + sample_ * 64) / 2;
                }
            }
            num_samples--;
        }
    }
    clocks_left_ = clocks_left;
    pos_ = pos;
    out = int(float(out) * amp_);
    return std::clamp(out, -0x7fff, 0x7fff);
}

}  // namespace dsp
