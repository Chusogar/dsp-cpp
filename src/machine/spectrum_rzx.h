#pragma once

#include "machine/spectrum_snap.h"

#include <cstdint>
#include <string>
#include <vector>

namespace dsp {

// RZX (Ramsoft) input-recording player for ZX Spectrum family machines.
// Playback feeds recorded IN results and ends each RZX frame after the logged
// number of M1 fetches (same cadence as the R register, INTACK excluded).
class SpectrumRzx {
public:
    bool load(const std::string& path, std::string* error);
    bool load_bytes(const uint8_t* data, size_t size, std::string* error);

    bool ok() const { return !frames_.empty() && snap_ok_; }
    bool playing() const { return playing_; }
    bool finished() const { return finished_; }
    const SpectrumSnap& snap() const { return snap_; }
    uint32_t start_tstates() const { return start_tstates_; }

    // Begin playback after the host has applied snap().
    void start();
    void stop();

    // Start the next RZX frame (sets fetch budget + IN queue). Returns false when done.
    bool begin_frame();
    // Remaining M1 fetches in the current RZX frame.
    uint16_t fetches_left() const { return fetches_left_; }
    // Call on every opcode-fetch M1 (including CB/ED/DD/FD second bytes; not INTACK).
    void on_m1();
    // Next recorded IN result (must be called once per CPU IN during playback).
    uint8_t next_in();

    size_t frame_index() const { return frame_index_; }
    size_t frame_count() const { return frames_.size(); }

private:
    struct Frame {
        uint16_t fetch_count = 0;
        std::vector<uint8_t> ins;
        bool repeat = false;
    };

    bool parse(const uint8_t* data, size_t size, std::string* error);
    static bool inflate(const uint8_t* src, size_t src_len, size_t expected, std::vector<uint8_t>& out,
                        std::string* error);

    SpectrumSnap snap_{};
    bool snap_ok_ = false;
    uint32_t start_tstates_ = 0;
    std::vector<Frame> frames_;
    std::vector<uint8_t> last_ins_;

    bool playing_ = false;
    bool finished_ = false;
    size_t frame_index_ = 0;
    uint16_t fetches_left_ = 0;
    size_t in_pos_ = 0;
    std::vector<uint8_t> cur_ins_;
};

}  // namespace dsp
