#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace dsp {

// G64 raw GCR disk image — preferred format for copy-protected titles.
// Layout follows VICE/mnib: signature "GCR-1541", up to 84 half-tracks,
// per-track length + raw GCR bytes, optional speed-zone map.
class G64Image {
public:
    static constexpr int kMaxHalfTracks = 84;

    bool load_file(const std::string& path, std::string* error = nullptr);
    bool load_memory(const uint8_t* data, size_t size, std::string* error = nullptr);

    bool open() const { return open_; }
    int num_half_tracks() const { return num_half_tracks_; }

    // half_track: 1..84 (1 = track 0.5 in some docs; we use 2..70 for 1..35)
    // Returns empty if no data for that half-track.
    const std::vector<uint8_t>& track_data(int half_track) const;
    int speed_zone(int half_track) const;  // 0..3

    // Expand raw GCR bytes to a bit vector (MSB first).
    static std::vector<bool> bytes_to_bits(const std::vector<uint8_t>& gcr);

private:
    bool open_ = false;
    int num_half_tracks_ = 84;
    int max_track_size_ = 7928;
    std::vector<std::vector<uint8_t>> tracks_;  // index 0 unused; 1..84
    std::vector<int> speed_;                    // per half-track, default zone
};

}  // namespace dsp
