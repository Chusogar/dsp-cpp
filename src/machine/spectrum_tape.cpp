#include "machine/spectrum_tape.h"

#include <cstdio>
#include <cstring>

namespace dsp {
namespace {

// Header of a .tzx image: "ZXTape!" $1a, major, minor.
const char kTzxMagic[] = "ZXTape!\x1a";

}  // namespace

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
    blocks_.clear();
    const bool tzx = data_.size() > 10 && std::memcmp(data_.data(), kTzxMagic, 8) == 0;
    if (!(tzx ? parse_tzx(error) : parse_tap(error))) {
        data_.clear();
        blocks_.clear();
        rewind();
        return false;
    }
    rewind();
    return true;
}

void SpectrumTape::add_standard_block(size_t start, size_t end) {
    Block block;
    block.kind = Block::Kind::Data;
    block.pilot_pulse = kPilotPulse;
    block.pilot_pulses = data_[start] < 0x80 ? kHeaderPilotPulses : kDataPilotPulses;
    block.sync1 = kSync1Pulse;
    block.sync2 = kSync2Pulse;
    block.zero = kBit0Pulse;
    block.one = kBit1Pulse;
    block.pause_cycles = kPauseCycles;
    block.data_start = start;
    block.data_end = end;
    blocks_.push_back(std::move(block));
}

bool SpectrumTape::parse_tap(std::string* error) {
    size_t offset = 0;
    while (offset + 2 <= data_.size()) {
        const size_t length = size_t(data_[offset]) | (size_t(data_[offset + 1]) << 8);
        const size_t start = offset + 2;
        if (length == 0 || start + length > data_.size()) break;
        add_standard_block(start, start + length);
        offset = start + length;
    }
    if (blocks_.empty()) {
        if (error != nullptr) *error = "the .tap image has no blocks";
        return false;
    }
    return true;
}

// Builds the block list of a .tzx image, the blocks abrir_tzx() understands.
bool SpectrumTape::parse_tzx(std::string* error) {
    size_t offset = 10;
    auto left = [&](size_t bytes) { return offset + bytes <= data_.size(); };
    auto byte = [&](size_t index) { return int(data_[offset + index]); };
    auto word = [&](size_t index) { return byte(index) | (byte(index + 1) << 8); };
    auto triple = [&](size_t index) { return size_t(word(index)) | (size_t(byte(index + 2)) << 16); };

    while (offset < data_.size()) {
        const int id = data_[offset++];
        switch (id) {
            case 0x10: {  // standard speed data
                if (!left(4)) return blocks_.empty() ? false : true;
                const int pause = word(0);
                const size_t length = size_t(word(2));
                if (!left(4 + length) || length == 0) return !blocks_.empty();
                add_standard_block(offset + 4, offset + 4 + length);
                blocks_.back().pause_cycles = pause * kCyclesPerMs;
                offset += 4 + length;
                break;
            }
            case 0x11: {  // turbo speed data
                if (!left(18)) return !blocks_.empty();
                Block block;
                block.pilot_pulse = word(0);
                block.sync1 = word(2);
                block.sync2 = word(4);
                block.zero = word(6);
                block.one = word(8);
                block.pilot_pulses = word(10);
                block.last_byte_bits = byte(12);
                block.pause_cycles = word(13) * kCyclesPerMs;
                const size_t length = triple(15);
                if (!left(18 + length) || length == 0) return !blocks_.empty();
                block.data_start = offset + 18;
                block.data_end = block.data_start + length;
                blocks_.push_back(std::move(block));
                offset += 18 + length;
                break;
            }
            case 0x12: {  // pure tone
                if (!left(4)) return !blocks_.empty();
                Block block;
                block.kind = Block::Kind::Tone;
                block.pilot_pulse = word(0);
                block.pilot_pulses = word(2);
                blocks_.push_back(std::move(block));
                offset += 4;
                break;
            }
            case 0x13: {  // sequence of pulses
                if (!left(1)) return !blocks_.empty();
                const size_t count = size_t(byte(0));
                if (!left(1 + count * 2)) return !blocks_.empty();
                Block block;
                block.kind = Block::Kind::Pulses;
                for (size_t index = 0; index < count; index++) {
                    block.pulses.push_back(word(1 + index * 2));
                }
                blocks_.push_back(std::move(block));
                offset += 1 + count * 2;
                break;
            }
            case 0x14: {  // pure data, no pilot and no sync
                if (!left(10)) return !blocks_.empty();
                Block block;
                block.zero = word(0);
                block.one = word(2);
                block.last_byte_bits = byte(4);
                block.pause_cycles = word(5) * kCyclesPerMs;
                const size_t length = triple(7);
                if (!left(10 + length) || length == 0) return !blocks_.empty();
                block.data_start = offset + 10;
                block.data_end = block.data_start + length;
                blocks_.push_back(std::move(block));
                offset += 10 + length;
                break;
            }
            case 0x15: {  // direct recording, one sample per bit
                if (!left(8)) return !blocks_.empty();
                Block block;
                block.kind = Block::Kind::Direct;
                block.sample_cycles = word(0);
                block.pause_cycles = word(2) * kCyclesPerMs;
                block.last_byte_bits = byte(4);
                const size_t length = triple(5);
                if (!left(8 + length) || length == 0) return !blocks_.empty();
                block.data_start = offset + 8;
                block.data_end = block.data_start + length;
                blocks_.push_back(std::move(block));
                offset += 8 + length;
                break;
            }
            case 0x18:    // CSW recording
            case 0x19: {  // generalized data
                if (!left(4)) return !blocks_.empty();
                const size_t length = size_t(word(0)) | (size_t(word(2)) << 16);
                if (!left(4 + length)) return !blocks_.empty();
                offset += 4 + length;  // not supported, skipped
                break;
            }
            case 0x20: {  // pause, zero means "stop the tape"
                if (!left(2)) return !blocks_.empty();
                Block block;
                block.kind = Block::Kind::Pause;
                block.pause_cycles = word(0) * kCyclesPerMs;
                blocks_.push_back(std::move(block));
                offset += 2;
                break;
            }
            case 0x21: {  // group start
                if (!left(1)) return !blocks_.empty();
                offset += 1 + size_t(byte(0));
                break;
            }
            case 0x22:  // group end
            case 0x27:  // return from call sequence
                break;
            case 0x23: {  // jump to another block
                if (!left(2)) return !blocks_.empty();
                Block block;
                block.kind = Block::Kind::Jump;
                block.target = int16_t(word(0));
                blocks_.push_back(std::move(block));
                offset += 2;
                break;
            }
            case 0x24: {  // loop start
                if (!left(2)) return !blocks_.empty();
                Block block;
                block.kind = Block::Kind::LoopStart;
                block.count = word(0);
                blocks_.push_back(std::move(block));
                offset += 2;
                break;
            }
            case 0x25: {  // loop end
                Block block;
                block.kind = Block::Kind::LoopEnd;
                blocks_.push_back(std::move(block));
                break;
            }
            case 0x26: {  // call sequence
                if (!left(2)) return !blocks_.empty();
                offset += 2 + size_t(word(0)) * 2;
                break;
            }
            case 0x28: {  // select block
                if (!left(2)) return !blocks_.empty();
                offset += 2 + size_t(word(0));
                break;
            }
            case 0x2a: {  // stop the tape if the machine is a 48K
                if (!left(4)) return !blocks_.empty();
                Block block;
                block.kind = Block::Kind::Pause;
                block.pause_cycles = 0;
                blocks_.push_back(std::move(block));
                offset += 4;
                break;
            }
            case 0x2b: {  // set signal level
                if (!left(5)) return !blocks_.empty();
                Block block;
                block.kind = Block::Kind::SetLevel;
                block.level = byte(4) != 0;
                blocks_.push_back(std::move(block));
                offset += 5;
                break;
            }
            case 0x30: {  // text description
                if (!left(1)) return !blocks_.empty();
                offset += 1 + size_t(byte(0));
                break;
            }
            case 0x31: {  // message
                if (!left(2)) return !blocks_.empty();
                offset += 2 + size_t(byte(1));
                break;
            }
            case 0x32: {  // archive info
                if (!left(2)) return !blocks_.empty();
                offset += 2 + size_t(word(0));
                break;
            }
            case 0x33: {  // hardware type
                if (!left(1)) return !blocks_.empty();
                offset += 1 + size_t(byte(0)) * 3;
                break;
            }
            case 0x35: {  // custom info
                if (!left(20)) return !blocks_.empty();
                offset += 20 + (size_t(word(16)) | (size_t(word(18)) << 16));
                break;
            }
            case 0x5a:  // glue block
                offset += 9;
                break;
            default:
                if (error != nullptr) {
                    char message[64];
                    std::snprintf(message, sizeof(message), "unknown .tzx block $%02x", id);
                    *error = message;
                }
                return !blocks_.empty();
        }
    }
    if (blocks_.empty()) {
        if (error != nullptr) *error = "the .tzx image has no playable blocks";
        return false;
    }
    return true;
}

void SpectrumTape::rewind() {
    level_ = false;
    playing_ = false;
    loops_.clear();
    finished_ = blocks_.empty();
    phase_ = Phase::Done;
    remaining_ = kPauseCycles;
    if (!blocks_.empty()) begin_block(0);
}

void SpectrumTape::begin_block(size_t index) {
    // Control blocks take no time, so several of them can be chained.
    for (int guard = 0; guard < 4096; guard++) {
        if (index >= blocks_.size()) {
            phase_ = Phase::Done;
            finished_ = true;
            playing_ = false;
            return;
        }
        block_index_ = index;
        const Block& block = blocks_[index];
        switch (block.kind) {
            case Block::Kind::Data:
                finished_ = false;
                if (block.pilot_pulses > 0) {
                    phase_ = Phase::Pilot;
                    pulses_left_ = block.pilot_pulses;
                    remaining_ = block.pilot_pulse;
                } else if (block.sync1 > 0) {
                    phase_ = Phase::Sync1;
                    remaining_ = block.sync1;
                } else {
                    begin_data_bits();
                }
                return;
            case Block::Kind::Tone:
                finished_ = false;
                if (block.pilot_pulses <= 0) break;
                phase_ = Phase::Pilot;
                pulses_left_ = block.pilot_pulses;
                remaining_ = block.pilot_pulse;
                return;
            case Block::Kind::Pulses:
                finished_ = false;
                if (block.pulses.empty()) break;
                phase_ = Phase::PulseList;
                pulse_index_ = 1;
                remaining_ = block.pulses[0];
                return;
            case Block::Kind::Direct:
                finished_ = false;
                byte_index_ = block.data_start;
                bit_mask_ = 0x80;
                if (byte_index_ >= block.data_end) break;
                phase_ = Phase::Direct;
                level_ = (data_[byte_index_] & bit_mask_) != 0;
                remaining_ = block.sample_cycles;
                return;
            case Block::Kind::Pause:
                finished_ = false;
                begin_pause(block.pause_cycles > 0 ? block.pause_cycles : kStopCycles);
                return;
            case Block::Kind::SetLevel:
                level_ = block.level;
                break;
            case Block::Kind::Jump:
                index = size_t(int(index) + (block.target != 0 ? block.target : 1));
                continue;
            case Block::Kind::LoopStart:
                if (block.count > 0) loops_.emplace_back(index + 1, block.count);
                break;
            case Block::Kind::LoopEnd:
                if (!loops_.empty()) {
                    auto& loop = loops_.back();
                    if (--loop.second > 0) {
                        index = loop.first;
                        continue;
                    }
                    loops_.pop_back();
                }
                break;
        }
        index++;
    }
    phase_ = Phase::Done;
    finished_ = true;
    playing_ = false;
}

void SpectrumTape::next_block() {
    begin_block(block_index_ + 1);
}

void SpectrumTape::begin_data_bits() {
    const Block& block = blocks_[block_index_];
    byte_index_ = block.data_start;
    bit_mask_ = 0x80;
    if (byte_index_ >= block.data_end) {
        next_block();
        return;
    }
    phase_ = Phase::BitFirst;
    pulse_cycles_ = (data_[byte_index_] & bit_mask_) != 0 ? block.one : block.zero;
    remaining_ = pulse_cycles_;
}

void SpectrumTape::begin_pause(int cycles) {
    level_ = false;
    if (cycles <= 0) {
        next_block();
        return;
    }
    phase_ = Phase::Pause;
    remaining_ = cycles;
}

void SpectrumTape::next_pulse() {
    const Block& block = blocks_[block_index_];
    switch (phase_) {
        case Phase::Pilot:
            level_ = !level_;
            if (--pulses_left_ > 0) {
                remaining_ = block.pilot_pulse;
            } else if (block.kind == Block::Kind::Tone) {
                begin_pause(block.pause_cycles);
            } else if (block.sync1 > 0) {
                phase_ = Phase::Sync1;
                remaining_ = block.sync1;
            } else {
                begin_data_bits();
            }
            break;
        case Phase::Sync1:
            level_ = !level_;
            if (block.sync2 > 0) {
                phase_ = Phase::Sync2;
                remaining_ = block.sync2;
            } else {
                begin_data_bits();
            }
            break;
        case Phase::Sync2:
            level_ = !level_;
            begin_data_bits();
            break;
        case Phase::BitFirst:
            level_ = !level_;
            phase_ = Phase::BitSecond;
            remaining_ = pulse_cycles_;
            break;
        case Phase::BitSecond: {
            level_ = !level_;
            bit_mask_ >>= 1;
            const bool last_byte = byte_index_ + 1 >= block.data_end;
            const int used_bits = last_byte ? block.last_byte_bits : 8;
            if (bit_mask_ == 0 || (0x80 >> used_bits) >= bit_mask_) {
                bit_mask_ = 0x80;
                byte_index_++;
            }
            if (byte_index_ >= block.data_end) {
                if (block.sync1 > 0) {
                    // A ROM block ends with a short pulse: without that edge the
                    // loader never sees the end of the last bit.
                    phase_ = Phase::Tail;
                    remaining_ = kTailPulse;
                } else {
                    begin_pause(block.pause_cycles);
                }
            } else {
                phase_ = Phase::BitFirst;
                pulse_cycles_ = (data_[byte_index_] & bit_mask_) != 0 ? block.one : block.zero;
                remaining_ = pulse_cycles_;
            }
            break;
        }
        case Phase::Tail:
            begin_pause(block.pause_cycles);
            break;
        case Phase::PulseList:
            level_ = !level_;
            if (pulse_index_ < block.pulses.size()) {
                remaining_ = block.pulses[pulse_index_++];
            } else {
                begin_pause(block.pause_cycles);
            }
            break;
        case Phase::Direct: {
            bit_mask_ >>= 1;
            const bool last_byte = byte_index_ + 1 >= block.data_end;
            const int used_bits = last_byte ? block.last_byte_bits : 8;
            if (bit_mask_ == 0 || (0x80 >> used_bits) >= bit_mask_) {
                bit_mask_ = 0x80;
                byte_index_++;
            }
            if (byte_index_ >= block.data_end) {
                begin_pause(block.pause_cycles);
            } else {
                level_ = (data_[byte_index_] & bit_mask_) != 0;
                remaining_ = block.sample_cycles;
            }
            break;
        }
        case Phase::Pause:
            next_block();
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
