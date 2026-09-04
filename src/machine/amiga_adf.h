#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace dsp {

// AmigaDOS ADF (decoded 512-byte sectors) plus Paula MFM track encode.
// Standard double-density: 80 cylinders × 2 sides × 11 sectors × 512 = 880 KiB.
class AmigaAdf {
public:
    static constexpr int kBytesPerSector = 512;
    static constexpr int kSectorsPerTrack = 11;
    static constexpr int kMfmWords = 6334;  // 12668 MFM bytes / 2

    bool load_file(const std::string& path, std::string* error);
    bool load_bytes(const std::vector<uint8_t>& data, std::string* error);
    void unload();

    bool loaded() const { return !image_.empty(); }
    int tracks() const { return tracks_; }
    int sides() const { return sides_; }
    int spt() const { return kSectorsPerTrack; }

    const uint8_t* sector(int cyl, int side, int sec) const;
    uint8_t* sector(int cyl, int side, int sec);

    // AmigaDOS MFM for one revolution of (cyl, side), ready for DSK DMA.
    std::vector<uint16_t> encode_track(int cyl, int side) const;

    // Build an 880K ADF whose bootblock JSR target paints COLOR00 and loops.
    static bool write_color_boot(const std::string& path, uint16_t color00, std::string* error);

    static uint32_t bootblock_checksum(const uint8_t* block /*1024 bytes*/);
    static void set_bootblock_checksum(uint8_t* block);

private:
    int offset(int cyl, int side, int sec) const;

    std::vector<uint8_t> image_;
    int tracks_ = 80;
    int sides_ = 2;
};

}  // namespace dsp
