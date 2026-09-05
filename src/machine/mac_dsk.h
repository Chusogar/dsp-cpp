#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace dsp {

// Macintosh floppy images: raw .dsk/.img and Disk Copy 4.2. 400K/800K are
// Sony GCR. 1.44MB MFM (SuperDrive / System 6.0.8 Startup) is stored as
// linear 2880 sectors; the first 800K is also GCR-encoded so the Plus
// IWM can see a disk while .Sony Prime reads the full volume by LBA.
class MacDsk {
public:
    static constexpr int kSectorSize = 512;
    static constexpr int kTagSize = 12;
    static constexpr int kTracks = 80;

    void reset();
    bool load_file(const std::string& path, std::string* error);
    bool load_bytes(const uint8_t* data, size_t size, std::string* error);
    bool loaded() const { return loaded_; }
    bool hd() const { return hd_; }
    uint32_t blocks() const { return loaded_ ? uint32_t(image_.size() / kSectorSize) : 0; }
    bool read_lba(uint32_t lba, uint8_t dest[kSectorSize]) const;
    bool write_lba(uint32_t lba, const uint8_t src[kSectorSize]);
    int tracks() const { return tracks_; }
    int sides() const { return sides_; }
    uint8_t format_byte() const { return format_; }

    static int sectors_per_track(int track);
    static int logical_offset(int track, int side, int sector, int sides);

    const uint8_t* sector(int track, int side, int sector) const;
    uint8_t* sector(int track, int side, int sector);

    const std::vector<uint8_t>& nibbles(int track, int side) const;

    // 524-byte (12 tag + 512 data) Macintosh 6-and-2 GCR, for tests.
    static void encode_data(uint8_t sector, const uint8_t src[524], std::vector<uint8_t>& dest);
    static bool decode_data(const uint8_t* gcr, size_t length, uint8_t dest[524]);

private:
    void rebuild_all();
    void encode_track(int track, int side);
    static void append_header(std::vector<uint8_t>& dest, int track, int sector, int side,
                              uint8_t format);
    static uint8_t gcr6(uint8_t sixbit);

    std::vector<uint8_t> image_;
    std::vector<uint8_t> tags_;
    std::vector<uint8_t> nibbles_[kTracks][2];
    int tracks_ = kTracks;
    int sides_ = 2;
    uint8_t format_ = 0x22;
    bool loaded_ = false;
    bool hd_ = false;
};

}  // namespace dsp
