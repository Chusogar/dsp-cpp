#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace dsp {

// CPC / Spectrum +3 DSK image (standard and extended), from disk_file_format.pas.
struct SectorInfo {
    uint8_t track = 0;
    uint8_t head = 0;
    uint8_t sector = 0;       // ID
    uint8_t sector_size = 2;  // N (128 << N)
    uint8_t status1 = 0;
    uint8_t status2 = 0;
    uint16_t data_length = 0;
    uint32_t data_offset = 0;  // into track data[]
    bool multi = false;        // weak/multi-sector
};

struct TrackImage {
    uint8_t track_number = 0;
    uint8_t side_number = 0;
    uint8_t sector_size = 2;
    uint8_t number_sector = 0;
    uint8_t gap3 = 0x4e;
    uint8_t filler = 0xe5;
    std::vector<uint8_t> data;
    std::array<SectorInfo, 64> sector{};
};

struct DiskImage {
    bool open = false;
    bool write_protected = false;
    bool extended = false;
    std::string name;

    uint8_t nbof_tracks = 0;
    uint8_t nbof_heads = 1;

    // Runtime head position (FDC state per drive).
    uint8_t track_actual = 0;
    uint8_t cara_actual = 0;  // side
    uint8_t sector_actual = 0;
    uint8_t sector_read_track = 0;

    // Weak-sector rotation.
    uint8_t cont_multi = 3;
    uint8_t max_multi = 3;

    // [side][track]
    std::array<std::array<TrackImage, 83>, 2> tracks{};

    void clear();
    bool load_dsk(const uint8_t* data, size_t size, std::string* error = nullptr);
    bool load_dsk_file(const std::string& path, std::string* error = nullptr);

    TrackImage& cur_track() { return tracks[cara_actual & 1][track_actual]; }
    const TrackImage& cur_track() const { return tracks[cara_actual & 1][track_actual]; }
};

}  // namespace dsp
