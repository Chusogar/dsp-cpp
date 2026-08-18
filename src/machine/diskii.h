#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace dsp {

// Apple Disk II analog card (slot 6): 16-sector DOS 3.3 / ProDOS images and
// the Wozniak sequencer that the P5 boot PROM and RWTS talk to.
class DiskIi {
public:
    static constexpr int kTracks = 35;
    static constexpr int kSectors = 16;
    static constexpr int kSectorSize = 256;
    static constexpr int kDosSize = kTracks * kSectors * kSectorSize;  // 143360
    static constexpr int kMaxNibbles = 6656;

    bool load_file(const std::string& path, std::string* error);
    bool load_bytes(const uint8_t* data, size_t size, std::string* error);
    bool load_bytes(const uint8_t* data, size_t size, const std::string& hint, std::string* error);
    bool loaded() const { return loaded_; }

    void reset();
    void tick(int cycles);

    uint8_t read_io(uint8_t offset);
    void write_io(uint8_t offset, uint8_t value);

    int half_track() const { return half_track_; }
    bool motor_on() const { return motor_on_; }

    // Rebuilds the nibble stream of the current track (used by tests).
    void rebuild_track();
    const std::vector<uint8_t>& nibbles() const { return nibbles_; }

    static void encode_62(const uint8_t src[256], uint8_t dest[343]);
    static bool decode_62(const uint8_t src[343], uint8_t dest[256]);

private:
    enum ImageKind { kNone, kDosOrder, kProdosOrder, kNibble };

    void step_phase(int phase, bool on);
    uint8_t next_nibble();
    void write_nibble(uint8_t value);
    void flush_write();
    int dos_sector_for_physical(int physical) const;
    void encode_track(int track);
    bool decode_track(int track);

    bool loaded_ = false;
    ImageKind kind_ = kNone;
    std::vector<uint8_t> image_;
    std::vector<uint8_t> nibbles_;
    std::vector<std::vector<uint8_t>> nibble_tracks_;
    int nibble_pos_ = 0;
    int half_track_ = 0;
    int encoded_track_ = -1;
    uint8_t phases_ = 0;
    bool motor_on_ = false;
    int drive_ = 0;
    bool q6_ = false;
    bool q7_ = false;
    uint8_t latch_ = 0;
    int cycles_until_nibble_ = 0;
    bool write_mode_ = false;
    bool dirty_ = false;
    std::vector<uint8_t> write_buf_;
};

}  // namespace dsp
