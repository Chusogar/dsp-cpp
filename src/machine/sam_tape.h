#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace dsp {

// Tape images in the ZX Spectrum's TAP and TZX containers, as used for SAM
// Coupe software too. Both are expanded into a flat list of pulse lengths;
// the EAR line flips at every pulse boundary, which is what the loading
// routines in the ROM measure.
class SamTape {
public:
    bool load(const std::string& path, std::string* error);
    void eject();

    bool inserted() const { return !pulses_.empty(); }
    bool playing() const { return playing_; }
    void play() { if (inserted()) playing_ = true; }
    void stop() { playing_ = false; }
    void rewind();

    // Advances the tape by a number of SAM T-states.
    void tick(int sam_tstates);
    bool ear() const { return ear_; }

    size_t pulse_count() const { return pulses_.size(); }
    uint32_t pulse(size_t i) const { return pulses_[i]; }

private:
    bool parse_tap(const std::vector<uint8_t>& data, std::string* error);
    bool parse_tzx(const std::vector<uint8_t>& data, std::string* error);
    // Appends the pulses for one data block: pilot tone, the two sync
    // pulses, then two pulses per bit.
    void emit_data_block(const uint8_t* data, size_t len, uint32_t pilot,
                         uint32_t sync1, uint32_t sync2, uint32_t zero,
                         uint32_t one, int pilot_pulses, int used_bits,
                         uint32_t pause_ms);
    void emit_pause(uint32_t pause_ms);
    uint32_t to_sam(uint32_t zx_tstates);

    std::vector<uint32_t> pulses_;   // durations in 3.5 MHz T-states
    size_t index_ = 0;
    int64_t counter_ = 0;
    uint64_t frac_ = 0;
    bool ear_ = false;
    bool playing_ = false;
};

}  // namespace dsp
