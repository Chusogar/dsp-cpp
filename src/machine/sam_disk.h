#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace dsp {

// A single physical sector as stored in a disk image: the ID field values
// (which may not match the sector's true physical cyl/head, e.g. for
// copy-protected disks using "SDF" images) plus its data and status flags.
struct SamSector {
    uint8_t cyl = 0, head = 0, sector = 1, size_code = 2;  // size_code: 128<<n bytes
    std::vector<uint8_t> data;
    bool id_crc_error = false;
    bool data_missing = false;
    bool data_crc_error = false;
    bool deleted = false;

    int size_bytes() const { return 128 << size_code; }
};

struct SamTrack {
    std::vector<SamSector> sectors;
};

// Holds a whole disk as tracks indexed by [cyl][head], loaded from either
// the simple MGT sector-dump format (2 sides x 80 tracks x 10 x 512 bytes)
// or Simon Owen's SDF format (which additionally stores each sector's own
// ID-field bytes and status flags, needed for copy-protected disks that
// rely on mismatched/duplicate/missing sector IDs).
class SamDisk {
public:
    bool load(const std::string& path, std::string* error);

    int cylinders() const { return cylinders_; }
    int sides() const { return sides_; }
    bool loaded() const { return loaded_; }

    // Sectors physically present on a given (cyl, head), in on-disk order.
    const SamTrack* track(int cyl, int head) const;

    // Resolves a sector the way the FDC asks for it: by the track number in
    // the controller's track register, the sector ID, and the side-select
    // line. Different SAM image formats number their tracks differently --
    // standard SAMDOS images (including the ones SimCoupe writes as
    // .dsk/EDSK) use track = physical cylinder with the head coming from
    // side-select, while the SDF images of copy-protected originals encode
    // both in one number (cylinder = track/2, head = track&1). Rather than
    // hardcode either convention, try both candidate positions and accept
    // the one whose stored ID field actually matches what was asked for.
    const SamSector* find(uint8_t track_id, uint8_t sector_id, int side) const;

private:
    bool load_sdf(const std::vector<uint8_t>& data, std::string* error);
    bool load_mgt(const std::vector<uint8_t>& data, std::string* error);
    bool load_edsk(const std::string& path, std::string* error);

    bool loaded_ = false;
    int cylinders_ = 80;
    int sides_ = 2;
    std::vector<SamTrack> tracks_;  // index = cyl * sides_ + head
};

}  // namespace dsp
