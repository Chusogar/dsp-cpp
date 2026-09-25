#include "drivers/computers/exelv_tape.h"

#include <algorithm>
#include <string>

namespace dsp {

namespace {

constexpr int kLeaderBytes = 255;
constexpr uint8_t kLeader = 0x55;
constexpr uint8_t kSync = 0x70;
// Half periods shorter than this are bit 1 (the ROM reader's threshold is
// about the same point between 503 and 976 cycles).
constexpr int kHalfThreshold = (ExelTape::kHalfBit0 + ExelTape::kHalfBit1) / 2;

}  // namespace

bool ExelTape::load(std::vector<uint8_t> data) {
    if (data.empty()) return false;
    size_t leader = 0;
    while (leader < data.size() && data[leader] == kLeader) leader++;
    std::vector<uint8_t> tape;
    if (leader < 16) {
        tape.assign(kLeaderBytes, kLeader);
        tape.insert(tape.end(), data.begin() + std::ptrdiff_t(leader), data.end());
        // An image that starts with the name has no sync byte either.
        if (data[leader < data.size() ? leader : 0] != kSync) {
            tape.insert(tape.begin() + kLeaderBytes, kSync);
        }
    } else {
        tape = std::move(data);
    }
    bytes_ = std::move(tape);
    rewind();
    return true;
}

void ExelTape::rewind() {
    pos_ = 0;
    bit_ = 7;
    half_ = 0;
    level_ = true;
    remain_ = 0;
    if (!bytes_.empty()) start_bit();
}

bool ExelTape::seek_sync() {
    for (size_t k = pos_ + (bit_ == 7 && half_ == 0 ? 0 : 1); k < bytes_.size(); k++) {
        if (bytes_[k] == kSync && k > 0 && bytes_[k - 1] == kLeader) {
            pos_ = k + 1;
            bit_ = 7;
            half_ = 0;
            if (!at_end()) start_bit();
            return true;
        }
    }
    return false;
}

int ExelTape::next_byte() {
    if (bit_ != 7 || half_ != 0) pos_++;  // drop a partly played byte
    if (at_end()) return -1;
    const int value = bytes_[pos_++];
    bit_ = 7;
    half_ = 0;
    if (!at_end()) start_bit();
    return value;
}

bool ExelTape::load_wav(const std::vector<uint8_t>& f, std::string* error) {
    auto u16 = [&](size_t o) { return unsigned(f[o] | (f[o + 1] << 8)); };
    auto u32 = [&](size_t o) { return uint32_t(u16(o) | (u16(o + 2) << 16)); };
    if (f.size() < 44 || std::string(f.begin(), f.begin() + 4) != "RIFF" ||
        std::string(f.begin() + 8, f.begin() + 12) != "WAVE") {
        if (error) *error = "not a WAV file";
        return false;
    }
    unsigned channels = 0, bits = 0;
    uint32_t rate = 0;
    size_t data = 0, data_size = 0;
    for (size_t o = 12; o + 8 <= f.size();) {
        const std::string id(f.begin() + std::ptrdiff_t(o), f.begin() + std::ptrdiff_t(o + 4));
        const uint32_t size = u32(o + 4);
        if (id == "fmt " && o + 24 <= f.size()) {
            if (u16(o + 8) != 1) break;  // PCM only
            channels = u16(o + 10);
            rate = u32(o + 12);
            bits = u16(o + 22);
        } else if (id == "data") {
            data = o + 8;
            data_size = std::min<size_t>(size, f.size() - data);
            break;
        }
        o += 8 + size + (size & 1);
    }
    if (!data || !rate || !channels || (bits != 8 && bits != 16)) {
        if (error) *error = "unsupported WAV (PCM 8/16-bit needed)";
        return false;
    }
    const size_t frame = channels * (bits / 8);
    const size_t frames = data_size / frame;
    // Level with hysteresis around the mean, then half periods in CPU cycles.
    std::vector<int> samples(frames);
    int64_t sum = 0;
    for (size_t i = 0; i < frames; i++) {
        const size_t o = data + i * frame;
        const int v = bits == 8 ? int(f[o]) - 128 : int(int16_t(u16(o))) >> 8;
        samples[i] = v;
        sum += v;
    }
    const int mid = frames ? int(sum / int64_t(frames)) : 0;
    constexpr double kCpuClock = 2457600.0;
    std::vector<int> halves;
    bool high = !samples.empty() && samples[0] > mid;
    size_t last = 0;
    for (size_t i = 0; i < frames; i++) {
        const bool flip = high ? samples[i] < mid - 8 : samples[i] > mid + 8;
        if (!flip) continue;
        halves.push_back(int(double(i - last) * kCpuClock / rate));
        last = i;
        high = !high;
    }
    // Split at silences; each burst starts on an unknown half, so keep the
    // alignment that yields the most leader bytes.
    std::vector<uint8_t> out;
    std::vector<int> burst;
    auto flush = [&]() {
        std::vector<uint8_t> best;
        size_t best_leader = 0;
        for (size_t skip = 0; skip < 4 && skip < burst.size(); skip++) {
            std::vector<int> part(burst.begin() + std::ptrdiff_t(skip), burst.end());
            std::vector<uint8_t> bytes = decode_halves(part);
            const size_t leader = size_t(std::count(bytes.begin(), bytes.end(), kLeader));
            if (leader > best_leader) {
                best_leader = leader;
                best.swap(bytes);
            }
        }
        // Drop what precedes the leader (the tape start) if it is noise.
        size_t first = 0;
        while (first < best.size() && best[first] != kLeader) first++;
        out.insert(out.end(), best.begin() + std::ptrdiff_t(first), best.end());
        burst.clear();
    };
    for (int h : halves) {
        if (h > kHalfBit0 * 8) {
            if (burst.size() > 64) flush();
            burst.clear();
        } else {
            burst.push_back(h);
        }
    }
    if (burst.size() > 64) flush();
    if (out.empty()) {
        if (error) *error = "no Exelvision data found in the WAV";
        return false;
    }
    return load(std::move(out));
}

void ExelTape::start_bit() {
    const bool one = (bytes_[pos_] >> bit_) & 1;
    remain_ = one ? kHalfBit1 : kHalfBit0;
    // The writer drives the low half first (ANDP %>F7,P6), then the high one.
    level_ = (half_ & 1) != 0;
}

bool ExelTape::advance(int cycles) {
    if (at_end()) {
        level_ = true;
        return level_;
    }
    remain_ -= cycles;
    while (remain_ <= 0 && !at_end()) {
        const int carry = remain_;
        if (++half_ == 4) {
            half_ = 0;
            if (--bit_ < 0) {
                bit_ = 7;
                if (++pos_ >= bytes_.size()) {
                    level_ = true;
                    return level_;
                }
            }
        }
        start_bit();
        remain_ += carry;
    }
    return level_;
}

void ExelTape::record_edge(uint64_t cycle, bool) {
    if (have_edge_) {
        const uint64_t half = cycle - last_edge_;
        // A long silence ends a recording: decode what was captured.
        if (half > uint64_t(kHalfBit0) * 8) {
            auto bytes = decode_halves(halves_);
            recorded_.insert(recorded_.end(), bytes.begin(), bytes.end());
            halves_.clear();
        } else {
            halves_.push_back(int(half));
        }
    }
    last_edge_ = cycle;
    have_edge_ = true;
}

std::vector<uint8_t> ExelTape::take_recording() {
    if (!halves_.empty()) {
        // The last half has no closing edge; repeat the one before it.
        halves_.push_back(halves_.back());
        auto bytes = decode_halves(halves_);
        recorded_.insert(recorded_.end(), bytes.begin(), bytes.end());
        halves_.clear();
    }
    have_edge_ = false;
    std::vector<uint8_t> out;
    out.swap(recorded_);
    return out;
}

std::vector<uint8_t> ExelTape::decode_halves(const std::vector<int>& halves) {
    std::vector<uint8_t> out;
    uint8_t value = 0;
    int bits = 0;
    for (size_t i = 0; i + 3 < halves.size(); i += 4) {
        int shorts = 0;
        for (size_t j = i; j < i + 4; j++) {
            if (halves[j] < kHalfThreshold) shorts++;
        }
        value = uint8_t((value << 1) | (shorts >= 2 ? 1 : 0));
        if (++bits == 8) {
            out.push_back(value);
            bits = 0;
            value = 0;
        }
    }
    return out;
}

}  // namespace dsp
