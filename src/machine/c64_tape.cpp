#include "machine/c64_tape.h"
#include <cstdio>
#include <cstring>

namespace dsp {
namespace {
constexpr char kMagic[] = "C64-TAPE-RAW";
constexpr size_t kHeaderSize = 20;
}  // namespace

bool C64Tape::load_file(const std::string& path, std::string* error) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) {
        if (error) *error = "cannot open tape: " + path;
        return false;
    }
    std::fseek(f, 0, SEEK_END);
    const long sz = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (sz <= 0) {
        std::fclose(f);
        if (error) *error = "empty tape";
        return false;
    }
    std::vector<uint8_t> buf(static_cast<size_t>(sz));
    if (std::fread(buf.data(), 1, buf.size(), f) != buf.size()) {
        std::fclose(f);
        if (error) *error = "read error: " + path;
        return false;
    }
    std::fclose(f);
    return load_memory(buf.data(), buf.size(), error);
}

bool C64Tape::load_memory(const uint8_t* data, size_t size, std::string* error) {
    clear();
    if (size < kHeaderSize) {
        if (error) *error = "TAP image too small";
        return false;
    }
    if (std::memcmp(data, kMagic, 12) != 0) {
        if (error) *error = "not a C64-TAPE-RAW image";
        return false;
    }
    const uint8_t version = data[0x0C];
    if (version > 2) {
        if (error) *error = "unsupported TAP version";
        return false;
    }
    halfwave_ = (version == 2);

    const size_t declared =
        size_t(data[0x10]) | (size_t(data[0x11]) << 8) |
        (size_t(data[0x12]) << 16) | (size_t(data[0x13]) << 24);
    size_t pos = kHeaderSize;
    size_t end = size;
    if (declared > 0 && kHeaderSize + declared <= size)
        end = kHeaderSize + declared;

    pulses_.reserve((end - pos) / 2 + 8);
    while (pos < end) {
        const uint8_t b = data[pos++];
        uint32_t cycles = 0;
        if (b == 0) {
            if (version == 0) {
                cycles = 256u * 8u;
            } else {
                if (pos + 3 > end) break;
                cycles = uint32_t(data[pos]) | (uint32_t(data[pos + 1]) << 8) |
                         (uint32_t(data[pos + 2]) << 16);
                pos += 3;
            }
        } else {
            cycles = uint32_t(b) * 8u;
        }
        if (cycles == 0) cycles = 1;
        pulses_.push_back(cycles);
    }
    if (pulses_.empty()) {
        if (error) *error = "TAP contains no pulses";
        return false;
    }
    loaded_ = true;
    level_ = 1;
    index_ = 0;
    remaining_ = 0;
    phase_ = 0;
    return true;
}

void C64Tape::clear() {
    pulses_.clear();
    index_ = remaining_ = phase_ = 0;
    level_ = 1;
    loaded_ = playing_ = paused_ = halfwave_ = false;
}

void C64Tape::play(bool restart) {
    if (!loaded_) return;
    if (restart || index_ >= pulses_.size()) {
        index_ = 0;
        remaining_ = 0;
        level_ = 1;
        phase_ = 0;
    }
    if (remaining_ <= 0 && index_ < pulses_.size()) {
        // Schedule first falling edge at the end of pulses_[index_].
        remaining_ = int(pulses_[index_]);
        level_ = 1;
        phase_ = 0;
    }
    playing_ = true;
    paused_ = false;
}

void C64Tape::stop() {
    playing_ = false;
    paused_ = false;
}

void C64Tape::next_edge(const EdgeCallback& on_falling_edge) {
    // End of current high interval → falling edge.
    level_ = 0;
    if (on_falling_edge) on_falling_edge();

    // Immediately return high for the next interval (1-cycle low is enough
    // for FLAG; the CIA only latches the falling edge).
    level_ = 1;
    ++index_;
    if (index_ >= pulses_.size()) {
        remaining_ = 0;
        playing_ = false;
        return;
    }
    remaining_ = int(pulses_[index_]);
}

int C64Tape::advance(int cycles, const EdgeCallback& on_falling_edge) {
    if (!playing_ || paused_ || cycles <= 0 || !loaded_) return 0;
    int consumed = 0;
    while (cycles > 0 && playing_) {
        if (remaining_ <= 0) {
            next_edge(on_falling_edge);
            if (!playing_) break;
        }
        const int step = remaining_ < cycles ? remaining_ : cycles;
        remaining_ -= step;
        cycles -= step;
        consumed += step;
        if (remaining_ <= 0) {
            next_edge(on_falling_edge);
        }
    }
    return consumed;
}

}  // namespace dsp
