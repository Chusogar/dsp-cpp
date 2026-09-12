#include "machine/sam_disk.h"

#include <algorithm>
#include <cstring>
#include <fstream>

#include "machine/disk_image.h"

namespace dsp {
namespace {

constexpr int kSdfTrackSize = 512 * 12;  // 6144 bytes/track, both formats agree
constexpr int kSdfSides = 2;

bool load_file(const std::string& path, std::vector<uint8_t>& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    f.seekg(0, std::ios::end);
    const auto sz = f.tellg();
    if (sz <= 0) return false;
    f.seekg(0, std::ios::beg);
    out.resize(size_t(sz));
    f.read(reinterpret_cast<char*>(out.data()), sz);
    return bool(f);
}

}  // namespace

const SamTrack* SamDisk::track(int cyl, int head) const {
    if (!loaded_ || cyl < 0 || cyl >= cylinders_ || head < 0 || head >= sides_) return nullptr;
    const size_t idx = size_t(cyl) * size_t(sides_) + size_t(head);
    if (idx >= tracks_.size()) return nullptr;
    return &tracks_[idx];
}

const SamSector* SamDisk::find(uint8_t track_id, uint8_t sector_id, int side) const {
    // Candidate physical positions, in priority order (see header comment).
    const struct { int cyl, head; } candidates[] = {
        {int(track_id), side & 1},          // standard: track == cylinder
        {int(track_id) / 2, track_id & 1},  // SDF: track encodes cylinder+head
    };
    // First pass: require the sector's own ID field to agree with both the
    // track and sector numbers requested. This is what distinguishes the
    // real sector from the decoy IDs protected disks scatter around.
    for (const auto& c : candidates) {
        const SamTrack* t = track(c.cyl, c.head);
        if (!t) continue;
        for (const auto& s : t->sectors) {
            if (s.cyl == track_id && s.sector == sector_id) return &s;
        }
    }
    // Second pass: some protected tracks deliberately renumber their ID
    // cylinder away from the track they sit on, so fall back to matching
    // the sector number alone at the same candidate positions.
    for (const auto& c : candidates) {
        const SamTrack* t = track(c.cyl, c.head);
        if (!t) continue;
        for (const auto& s : t->sectors) {
            if (s.sector == sector_id) return &s;
        }
    }
    return nullptr;
}

bool SamDisk::load(const std::string& path, std::string* error) {
    std::vector<uint8_t> data;
    if (!load_file(path, data)) {
        if (error) *error = "cannot open disk image: " + path;
        return false;
    }
    // CPC/Spectrum-style DSK (standard or extended). SimCoupe writes SAM
    // disks in this container too, so detect it by signature rather than
    // by file extension or size.
    if (data.size() >= 8 &&
        (std::memcmp(data.data(), "EXTENDED", 8) == 0 ||
         std::memcmp(data.data(), "MV - CPC", 8) == 0)) {
        return load_edsk(path, error);
    }
    // SDF sizes: 80/81/82/83 cylinders x 2 sides x 6144 bytes/track.
    const size_t sdf80 = size_t(kSdfTrackSize) * kSdfSides * 80;
    if (data.size() == sdf80 || data.size() == sdf80 + kSdfTrackSize * 2 ||
        data.size() == sdf80 + kSdfTrackSize * 4 || data.size() == sdf80 + kSdfTrackSize * 6) {
        return load_sdf(data, error);
    }
    // MGT: plain 2 x 80 x 10 x 512 sector dump (819200 bytes), or SAD with
    // its fixed 22-byte header stripped by the caller-visible size check.
    if (data.size() == 819200 || data.size() == 819200 + 22) {
        return load_mgt(data, error);
    }
    if (error) *error = "unrecognised SAM disk image size (" + std::to_string(data.size()) + " bytes)";
    return false;
}

bool SamDisk::load_sdf(const std::vector<uint8_t>& data, std::string* error) {
    sides_ = kSdfSides;
    cylinders_ = int(data.size() / (size_t(kSdfTrackSize) * kSdfSides));
    tracks_.assign(size_t(cylinders_) * size_t(sides_), SamTrack{});

    size_t off = 0;
    for (int cyl = 0; cyl < cylinders_; ++cyl) {
        for (int head = 0; head < sides_; ++head) {
            if (off + size_t(kSdfTrackSize) > data.size()) {
                if (error) *error = "SDF image truncated";
                return false;
            }
            const uint8_t* p = data.data() + off;
            const uint8_t* track_end = p + kSdfTrackSize;
            const uint8_t num_sectors = p[0];
            const uint8_t* q = p + 1;
            SamTrack& t = tracks_[size_t(cyl) * size_t(sides_) + size_t(head)];
            for (int s = 0; s < num_sectors; ++s) {
                if (q + 8 > track_end) break;  // malformed track, stop early
                SamSector sec;
                const uint8_t idstatus = q[0];
                const uint8_t datastatus = q[1];
                sec.cyl = q[2];
                sec.head = q[3];
                sec.sector = q[4];
                sec.size_code = q[5];
                // q[6],q[7] = stored CRC bytes; not needed for emulation reads.
                q += 8;
                sec.id_crc_error = (idstatus & 0x08) != 0;
                sec.data_missing = (datastatus & 0x10) != 0;
                sec.deleted = (datastatus & 0x20) != 0;
                sec.data_crc_error = (datastatus & 0x08) != 0;
                const int n = sec.size_bytes();
                if (q + n > track_end) break;
                if (!sec.data_missing) sec.data.assign(q, q + n);
                q += n;  // the image always reserves the full data length
                t.sectors.push_back(std::move(sec));
            }
            off += size_t(kSdfTrackSize);
        }
    }
    loaded_ = true;
    return true;
}

bool SamDisk::load_mgt(const std::vector<uint8_t>& data, std::string* error) {
    // MGT track order: cyl0/head0, cyl0/head1, cyl1/head0, ... (interleaved
    // by side within each cylinder), 10 x 512-byte sectors per track,
    // sector IDs 1-10, no header. A 22-byte SAD header (if present) is
    // simply skipped; SAD's own track ordering differs from MGT's but the
    // common case here is a plain image without one.
    const uint8_t* base = data.data();
    size_t off = (data.size() == 819200 + 22) ? 22 : 0;
    sides_ = 2;
    cylinders_ = 80;
    tracks_.assign(size_t(cylinders_) * size_t(sides_), SamTrack{});
    for (int cyl = 0; cyl < cylinders_; ++cyl) {
        for (int head = 0; head < sides_; ++head) {
            SamTrack& t = tracks_[size_t(cyl) * size_t(sides_) + size_t(head)];
            for (int s = 1; s <= 10; ++s) {
                if (off + 512 > data.size()) {
                    if (error) *error = "MGT image truncated";
                    return false;
                }
                SamSector sec;
                sec.cyl = uint8_t(cyl);
                sec.head = uint8_t(head);
                sec.sector = uint8_t(s);
                sec.size_code = 2;  // 512 bytes
                sec.data.assign(base + off, base + off + 512);
                t.sectors.push_back(std::move(sec));
                off += 512;
            }
        }
    }
    loaded_ = true;
    return true;
}

bool SamDisk::load_edsk(const std::string& path, std::string* error) {
    // Reuse the project's existing (E)DSK parser, then flatten its
    // per-side/per-track structure into ours. DSK stores each sector's
    // real ID-field bytes and FDC status flags, so copy-protection
    // information survives the conversion exactly as it does for SDF.
    DiskImage img;
    if (!img.load_dsk_file(path, error)) return false;

    cylinders_ = img.nbof_tracks ? img.nbof_tracks : 80;
    sides_ = img.nbof_heads ? img.nbof_heads : 1;
    if (cylinders_ > 83) cylinders_ = 83;
    if (sides_ > 2) sides_ = 2;
    tracks_.assign(size_t(cylinders_) * size_t(sides_), SamTrack{});

    for (int cyl = 0; cyl < cylinders_; ++cyl) {
        for (int head = 0; head < sides_; ++head) {
            const TrackImage& src = img.tracks[size_t(head)][size_t(cyl)];
            SamTrack& dst = tracks_[size_t(cyl) * size_t(sides_) + size_t(head)];
            const int count = std::min<int>(src.number_sector, 64);
            for (int i = 0; i < count; ++i) {
                const SectorInfo& si = src.sector[size_t(i)];
                SamSector sec;
                sec.cyl = si.track;
                sec.head = si.head;
                sec.sector = si.sector;
                sec.size_code = si.sector_size;
                // FDC status register 1 bit 5 = data CRC error, bit 2 =
                // "no data" (sector ID found but data field missing);
                // status register 2 bit 5 = data-field CRC error, bit 6 =
                // control mark (deleted-data address mark).
                sec.id_crc_error = (si.status1 & 0x20) != 0;
                sec.data_missing = (si.status1 & 0x04) != 0;
                sec.data_crc_error = (si.status2 & 0x20) != 0;
                sec.deleted = (si.status2 & 0x40) != 0;
                const size_t want = size_t(sec.size_bytes());
                const size_t avail = si.data_length ? si.data_length : want;
                if (!sec.data_missing && si.data_offset < src.data.size()) {
                    const size_t n = std::min({want, size_t(avail),
                                               src.data.size() - si.data_offset});
                    sec.data.assign(src.data.begin() + long(si.data_offset),
                                    src.data.begin() + long(si.data_offset + n));
                    sec.data.resize(want, 0);
                }
                dst.sectors.push_back(std::move(sec));
            }
        }
    }
    loaded_ = true;
    return true;
}

}  // namespace dsp
