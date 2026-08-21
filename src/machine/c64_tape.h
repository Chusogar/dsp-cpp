#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace dsp {

// Commodore .TAP player ("C64-TAPE-RAW").
// v0/v1: each value = cycles between successive falling edges (full period).
// v2: each value = half-wave.
class C64Tape {
public:
    using EdgeCallback = std::function<void()>;  // called on each falling edge

    bool load_file(const std::string& path, std::string* error = nullptr);
    bool load_memory(const uint8_t* data, size_t size, std::string* error = nullptr);

    void clear();
    void play(bool restart = true);
    void stop();
    void pause() { paused_ = true; }

    bool is_playing() const { return playing_ && !paused_; }
    bool is_paused() const { return paused_; }
    bool is_loaded() const { return loaded_; }

    // Advance tape by CPU cycles. Invokes on_falling_edge() for every
    // falling edge that occurs inside this window (so multi-cycle batches
    // cannot skip a 1-cycle FLAG pulse).
    int advance(int cycles, const EdgeCallback& on_falling_edge = nullptr);

    uint8_t level() const { return level_; }
    size_t pulse_count() const { return pulses_.size(); }
    size_t current_pulse() const { return index_; }

private:
    void next_edge(const EdgeCallback& on_falling_edge);

    std::vector<uint32_t> pulses_;
    size_t index_ = 0;
    int remaining_ = 0;
    int phase_ = 0;
    uint8_t level_ = 1;
    bool loaded_ = false;
    bool playing_ = false;
    bool paused_ = false;
    bool halfwave_ = false;
};

}  // namespace dsp
