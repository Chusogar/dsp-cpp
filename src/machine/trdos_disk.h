#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace dsp {

// TR-DOS floppy image (TRD raw geometry or SCL catalogue). 256-byte sectors,
// 16 per track per side. Used by the Beta 128 / WD1793 on Pentagon and Scorpion.
class TrdosDisk {
public:
    static constexpr int kSectorSize = 256;
    static constexpr int kSectorsPerTrack = 16;

    bool load_file(const std::string& path, std::string* error);
    bool load_bytes(const uint8_t* data, size_t size, std::string* error);

    bool present() const { return !image_.empty(); }
    void eject() { image_.clear(); tracks_ = 0; heads_ = 0; }

    int tracks() const { return tracks_; }
    int heads() const { return heads_; }

    const uint8_t* sector(int track, int head, int sector /* 1-16 */) const;
    uint8_t* sector(int track, int head, int sector);

private:
    bool load_trd(const uint8_t* data, size_t size, std::string* error);
    bool load_scl(const uint8_t* data, size_t size, std::string* error);
    void format(int tracks, int heads);
    size_t sector_offset(int track, int head, int sector) const;

    std::vector<uint8_t> image_;
    int tracks_ = 0;
    int heads_ = 0;
};

}  // namespace dsp
