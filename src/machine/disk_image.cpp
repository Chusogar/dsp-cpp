#include "machine/disk_image.h"

#include <algorithm>
#include <cstring>
#include <fstream>

namespace dsp {
namespace {

#pragma pack(push, 1)
struct DskHeader {
    char magic[34];  // "MV - CPC" or "EXTENDED CPC DSK File\r\nDisk-Info\r\n"
    char creator[14];
    uint8_t tracks;
    uint8_t sides;
    uint16_t track_size;           // standard only
    uint8_t track_size_map[204];   // extended: size/256 per track
};

struct DskTrackHeader {
    char magic[12];  // "Track-Info\r\n"
    uint8_t unused[4];
    uint8_t track;
    uint8_t side;
    uint8_t data_rate;
    uint8_t record_mode;
    uint8_t sector_size;
    uint8_t number_of_sectors;
    uint8_t gap;
    uint8_t filler;
};

struct DskSectorHeader {
    uint8_t track;
    uint8_t side;
    uint8_t id;
    uint8_t size;
    uint8_t status1;
    uint8_t status2;
    uint16_t length;  // actual data length (extended) / unused in standard
};
#pragma pack(pop)

bool starts_with(const char* s, const char* prefix) {
    return std::strncmp(s, prefix, std::strlen(prefix)) == 0;
}

}  // namespace

void DiskImage::clear() {
    open = false;
    write_protected = false;
    extended = false;
    name.clear();
    nbof_tracks = 0;
    nbof_heads = 1;
    track_actual = 0;
    cara_actual = 0;
    sector_actual = 0;
    sector_read_track = 0;
    cont_multi = 3;
    max_multi = 3;
    for (auto& side : tracks) {
        for (auto& t : side) {
            t = TrackImage{};
        }
    }
}

bool DiskImage::load_dsk_file(const std::string& path, std::string* error) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        if (error) *error = "cannot open: " + path;
        return false;
    }
    f.seekg(0, std::ios::end);
    const auto sz = f.tellg();
    if (sz <= 0) {
        if (error) *error = "empty file: " + path;
        return false;
    }
    f.seekg(0, std::ios::beg);
    std::vector<uint8_t> buf(static_cast<size_t>(sz));
    f.read(reinterpret_cast<char*>(buf.data()), sz);
    if (!load_dsk(buf.data(), buf.size(), error)) return false;
    name = path;
    return true;
}

bool DiskImage::load_dsk(const uint8_t* data, size_t size, std::string* error) {
    clear();
    if (size < sizeof(DskHeader)) {
        if (error) *error = "DSK too small";
        return false;
    }

    DskHeader hdr{};
    std::memcpy(&hdr, data, sizeof(hdr));

    const bool is_ext = starts_with(hdr.magic, "EXTENDED");
    const bool is_std = starts_with(hdr.magic, "MV - CPC") || starts_with(hdr.magic, "MV - CPCEMU");
    if (!is_ext && !is_std) {
        if (error) *error = "not a DSK image (bad magic)";
        return false;
    }

    extended = is_ext;
    nbof_tracks = hdr.tracks;
    nbof_heads = hdr.sides ? hdr.sides : 1;
    if (nbof_tracks > 82) nbof_tracks = 82;
    if (nbof_heads > 2) nbof_heads = 2;

    size_t pos = 0x100;  // fixed header size

    for (int trk = 0; trk < int(nbof_tracks); ++trk) {
        for (int side = 0; side < int(nbof_heads); ++side) {
            uint32_t declared_size = 0;
            if (extended) {
                const int idx = trk * int(nbof_heads) + side;
                declared_size = uint32_t(hdr.track_size_map[idx]) * 256u;
                if (declared_size == 0) continue;  // unformatted
            } else {
                declared_size = hdr.track_size;
                if (declared_size == 0) declared_size = 0x1300;
            }

            if (pos + sizeof(DskTrackHeader) > size) {
                if (error) *error = "truncated track header";
                return false;
            }

            DskTrackHeader th{};
            std::memcpy(&th, data + pos, sizeof(th));
            // Some images omit exact "Track-Info" padding; accept if enough room.

            TrackImage& ti = tracks[side][trk];
            ti.track_number = th.track;
            ti.side_number = th.side;
            ti.sector_size = th.sector_size;
            ti.number_sector = th.number_of_sectors;
            ti.gap3 = th.gap;
            ti.filler = th.filler;
            if (ti.number_sector > 64) ti.number_sector = 64;

            // Sector info table starts right after track header (0x18 bytes into track block).
            const size_t sec_info = pos + 0x18;
            uint32_t data_off = 0x100;  // sector data starts at offset 0x100 within track block

            // In extended DSK, actual sector lengths may differ; accumulate.
            uint32_t total_data = 0;
            for (int s = 0; s < int(ti.number_sector); ++s) {
                if (sec_info + size_t(s + 1) * sizeof(DskSectorHeader) > size) {
                    if (error) *error = "truncated sector info";
                    return false;
                }
                DskSectorHeader sh{};
                std::memcpy(&sh, data + sec_info + size_t(s) * sizeof(DskSectorHeader), sizeof(sh));
                SectorInfo& si = ti.sector[s];
                si.track = sh.track;
                si.head = sh.side;
                si.sector = sh.id;
                si.sector_size = sh.size;
                si.status1 = sh.status1;
                si.status2 = sh.status2;
                if (extended) {
                    si.data_length = sh.length ? sh.length : uint16_t(128 << (sh.size & 7));
                } else {
                    si.data_length = uint16_t(128 << (sh.size & 7));
                }
                // Weak sectors: length multiple of nominal size.
                const uint16_t nominal = uint16_t(128 << (sh.size & 7));
                if (si.data_length > nominal && nominal != 0) {
                    si.multi = true;
                    max_multi = uint8_t(si.data_length / nominal);
                    if (max_multi < 1) max_multi = 1;
                }
                si.data_offset = data_off + total_data;
                total_data += si.data_length;
            }

            const size_t track_block = extended ? declared_size : (0x100 + total_data);
            // Standard images pad track to track_size.
            const size_t copy_size = extended ? (declared_size > 0x100 ? declared_size - 0x100 : 0)
                                              : total_data;
            if (pos + 0x100 + copy_size > size && pos + track_block > size) {
                // Best-effort: copy what's available.
            }
            const size_t avail = (pos + 0x100 < size) ? (size - (pos + 0x100)) : 0;
            const size_t n = std::min(copy_size, avail);
            ti.data.assign(data + pos + 0x100, data + pos + 0x100 + n);
            // If data shorter than offsets expect, pad with filler.
            if (ti.data.size() < total_data) {
                ti.data.resize(total_data, ti.filler);
            }

            // Rebase sector offsets to 0 within ti.data.
            for (int s = 0; s < int(ti.number_sector); ++s) {
                if (ti.sector[s].data_offset >= 0x100) {
                    ti.sector[s].data_offset -= 0x100;
                }
            }

            pos += extended ? declared_size : (hdr.track_size ? hdr.track_size : (0x100 + total_data));
            // Align standard track size.
            if (!extended && hdr.track_size) {
                // pos already advanced by track_size above if we used hdr.track_size
            }
        }
    }

    // Fix standard advance: re-parse with correct stride if needed.
    // Simpler second pass for standard DSK with fixed track_size.
    if (!extended && hdr.track_size) {
        pos = 0x100;
        for (int trk = 0; trk < int(nbof_tracks); ++trk) {
            for (int side = 0; side < int(nbof_heads); ++side) {
                if (pos + 0x100 > size) break;
                DskTrackHeader th{};
                std::memcpy(&th, data + pos, sizeof(th));
                TrackImage& ti = tracks[side][trk];
                ti.track_number = th.track;
                ti.side_number = th.side;
                ti.sector_size = th.sector_size;
                ti.number_sector = th.number_of_sectors;
                ti.gap3 = th.gap;
                ti.filler = th.filler;
                if (ti.number_sector > 64) ti.number_sector = 64;

                const size_t sec_info = pos + 0x18;
                uint32_t total_data = 0;
                for (int s = 0; s < int(ti.number_sector); ++s) {
                    DskSectorHeader sh{};
                    std::memcpy(&sh, data + sec_info + size_t(s) * sizeof(DskSectorHeader), sizeof(sh));
                    SectorInfo& si = ti.sector[s];
                    si.track = sh.track;
                    si.head = sh.side;
                    si.sector = sh.id;
                    si.sector_size = sh.size;
                    si.status1 = sh.status1;
                    si.status2 = sh.status2;
                    si.data_length = uint16_t(128 << (sh.size & 7));
                    si.data_offset = total_data;
                    total_data += si.data_length;
                }
                const size_t avail = (pos + 0x100 < size) ? (size - (pos + 0x100)) : 0;
                const size_t n = std::min(size_t(total_data), avail);
                ti.data.assign(data + pos + 0x100, data + pos + 0x100 + n);
                if (ti.data.size() < total_data) ti.data.resize(total_data, ti.filler);
                pos += hdr.track_size;
            }
        }
    }

    open = true;
    track_actual = 0;
    cara_actual = 0;
    sector_actual = 0;
    return true;
}

}  // namespace dsp
