#include "machine/spectrum_tape.h"

#include <cstdio>

namespace dsp {

bool SpectrumTape::load(const std::string& path, std::string* error) {
    std::FILE* file = std::fopen(path.c_str(), "rb");
    if (file == nullptr) {
        if (error != nullptr) *error = "cannot open tape " + path;
        return false;
    }
    std::vector<uint8_t> data;
    uint8_t buffer[4096];
    size_t read = 0;
    while ((read = std::fread(buffer, 1, sizeof(buffer), file)) > 0) {
        data.insert(data.end(), buffer, buffer + read);
    }
    std::fclose(file);
    return load_from_memory(std::move(data), error);
}

bool SpectrumTape::load_from_memory(std::vector<uint8_t> data, std::string* error) {
    if (data.size() < 3) {
        if (error != nullptr) *error = "the tape image is empty";
        return false;
    }
    data_ = std::move(data);
    rewind();
    return true;
}

void SpectrumTape::rewind() {
    level_ = false;
    playing_ = false;
    finished_ = data_.empty();
    phase_ = Phase::Done;
    if (!data_.empty()) start_block(0);
}

bool SpectrumTape::start_block(size_t offset) {
    if (offset + 2 > data_.size()) {
        phase_ = Phase::Done;
        finished_ = true;
        playing_ = false;
        return false;
    }
    const size_t length = size_t(data_[offset]) | (size_t(data_[offset + 1]) << 8);
    block_start_ = offset + 2;
    block_end_ = block_start_ + length;
    if (length == 0 || block_end_ > data_.size()) {
        phase_ = Phase::Done;
        finished_ = true;
        playing_ = false;
        return false;
    }
    byte_index_ = block_start_;
    bit_mask_ = 0x80;
    phase_ = Phase::Pilot;
    pulses_left_ = data_[block_start_] < 0x80 ? kHeaderPilotPulses : kDataPilotPulses;
    pulse_cycles_ = kPilotPulse;
    remaining_ = pulse_cycles_;
    finished_ = false;
    return true;
}

void SpectrumTape::next_pulse() {
    switch (phase_) {
        case Phase::Pilot:
            level_ = !level_;
            if (--pulses_left_ > 0) {
                remaining_ = kPilotPulse;
            } else {
                phase_ = Phase::Sync1;
                remaining_ = kSync1Pulse;
            }
            break;
        case Phase::Sync1:
            level_ = !level_;
            phase_ = Phase::Sync2;
            remaining_ = kSync2Pulse;
            break;
        case Phase::Sync2:
            level_ = !level_;
            phase_ = Phase::BitFirst;
            pulse_cycles_ = (data_[byte_index_] & bit_mask_) != 0 ? kBit1Pulse : kBit0Pulse;
            remaining_ = pulse_cycles_;
            break;
        case Phase::BitFirst:
            level_ = !level_;
            phase_ = Phase::BitSecond;
            remaining_ = pulse_cycles_;
            break;
        case Phase::BitSecond:
            level_ = !level_;
            bit_mask_ >>= 1;
            if (bit_mask_ == 0) {
                bit_mask_ = 0x80;
                byte_index_++;
            }
            if (byte_index_ >= block_end_) {
                // The block ends with a short pulse so that the loader sees the
                // closing edge of its last bit before the silence.
                phase_ = Phase::Tail;
                remaining_ = kTailPulse;
            } else {
                phase_ = Phase::BitFirst;
                pulse_cycles_ = (data_[byte_index_] & bit_mask_) != 0 ? kBit1Pulse : kBit0Pulse;
                remaining_ = pulse_cycles_;
            }
            break;
        case Phase::Tail:
            level_ = false;
            phase_ = Phase::Pause;
            remaining_ = kPauseCycles;
            break;
        case Phase::Pause:
            start_block(block_end_);
            break;
        case Phase::Done:
            remaining_ = kPauseCycles;
            break;
    }
}

void SpectrumTape::advance(int cycles) {
    if (!playing_ || phase_ == Phase::Done) return;
    remaining_ -= cycles;
    while (remaining_ <= 0 && phase_ != Phase::Done) {
        const int leftover = remaining_;
        next_pulse();
        remaining_ += leftover;
    }
}

}  // namespace dsp
