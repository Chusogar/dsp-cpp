#include "machine/trdos_disk.h"

#include <algorithm>
#include <cstring>
#include <fstream>

namespace dsp {
namespace {

bool read_file(const std::string& path, std::vector<uint8_t>& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    f.seekg(0, std::ios::end);
    const auto sz = f.tellg();
    if (sz <= 0) return false;
    f.seekg(0, std::ios::beg);
    out.resize(size_t(sz));
    f.read(reinterpret_cast<char*>(out.data()), std::streamsize(sz));
    return bool(f);
}

}  // namespace

void TrdosDisk::format(int tracks, int heads) {
    tracks_ = tracks;
    heads_ = heads;
    image_.assign(size_t(tracks) * size_t(heads) * kSectorsPerTrack * kSectorSize, 0);
}

size_t TrdosDisk::sector_offset(int track, int head, int sector) const {
    if (tracks_ <= 0 || heads_ <= 0) return size_t(-1);
    if (track < 0 || track >= tracks_) return size_t(-1);
    if (head < 0 || head >= heads_) return size_t(-1);
    if (sector < 1 || sector > kSectorsPerTrack) return size_t(-1);
    const size_t cyl = size_t(track) * size_t(heads_) + size_t(head);
    return (cyl * kSectorsPerTrack + size_t(sector - 1)) * kSectorSize;
}

const uint8_t* TrdosDisk::sector(int track, int head, int sector) const {
    const size_t off = sector_offset(track, head, sector);
    if (off == size_t(-1) || off + kSectorSize > image_.size()) return nullptr;
    return image_.data() + off;
}

uint8_t* TrdosDisk::sector(int track, int head, int sector) {
    const size_t off = sector_offset(track, head, sector);
    if (off == size_t(-1) || off + kSectorSize > image_.size()) return nullptr;
    return image_.data() + off;
}

bool TrdosDisk::load_file(const std::string& path, std::string* error) {
    std::vector<uint8_t> data;
    if (!read_file(path, data)) {
        if (error) *error = "cannot open disk image: " + path;
        return false;
    }
    return load_bytes(data.data(), data.size(), error);
}

bool TrdosDisk::load_bytes(const uint8_t* data, size_t size, std::string* error) {
    if (data == nullptr || size < 8) {
        if (error) *error = "disk image is empty";
        return false;
    }
    if (size >= 8 && std::memcmp(data, "SINCLAIR", 8) == 0) return load_scl(data, size, error);
    return load_trd(data, size, error);
}

bool TrdosDisk::load_trd(const uint8_t* data, size_t size, std::string* error) {
    int tracks = 80;
    int heads = 2;
    const size_t full = 80ull * 2 * 16 * 256;
    const size_t half = 80ull * 1 * 16 * 256;
    const size_t forty = 40ull * 2 * 16 * 256;
    if (size >= full) {
        tracks = 80;
        heads = 2;
    } else if (size >= forty && size < half) {
        tracks = 40;
        heads = 2;
    } else if (size >= half) {
        tracks = 80;
        heads = 1;
    } else if (size >= 40ull * 16 * 256) {
        tracks = 40;
        heads = 1;
    } else {
        if (error) *error = "TRD image is too small";
        return false;
    }
    format(tracks, heads);
    const size_t copy = std::min(size, image_.size());
    std::memcpy(image_.data(), data, copy);
    return true;
}

bool TrdosDisk::load_scl(const uint8_t* data, size_t size, std::string* error) {
    if (size < 9 || std::memcmp(data, "SINCLAIR", 8) != 0) {
        if (error) *error = "not an SCL image";
        return false;
    }
    const int nfiles = data[8];
    if (nfiles < 0 || nfiles > 128) {
        if (error) *error = "SCL catalogue is invalid";
        return false;
    }
    const size_t header = 9 + size_t(nfiles) * 14;
    if (size < header) {
        if (error) *error = "SCL image is truncated";
        return false;
    }

    format(80, 2);
    // TR-DOS logical tracks interleave sides: 0 = C0H0, 1 = C0H1, 2 = C1H0, ...
    // Files start at logical track 1 (cylinder 0, head 1). Track 0 side 0 is the
    // catalogue; the rest of that physical track is unused.
    int log_track = 1;
    int sec0 = 0;
    size_t data_off = header;
    int used = 0;

    for (int i = 0; i < nfiles; i++) {
        const uint8_t* h = data + 9 + size_t(i) * 14;
        const int nsec = h[13];
        uint8_t* cat = image_.data() + size_t(i) * 16;
        std::memcpy(cat, h, 14);
        cat[14] = uint8_t(sec0);
        cat[15] = uint8_t(log_track);
        for (int s = 0; s < nsec; s++) {
            const int cyl = log_track / heads_;
            const int head = log_track % heads_;
            uint8_t* dest = sector(cyl, head, sec0 + 1);
            if (dest == nullptr) {
                if (error) *error = "SCL files do not fit on a DS/80 disk";
                return false;
            }
            if (data_off < size) {
                const size_t chunk = std::min(size_t(kSectorSize), size - data_off);
                std::memcpy(dest, data + data_off, chunk);
                data_off += chunk;
            }
            ++used;
            if (++sec0 >= kSectorsPerTrack) {
                sec0 = 0;
                ++log_track;
            }
        }
    }

    uint8_t* info = sector(0, 0, 9);
    if (info == nullptr) return false;
    info[0xe1] = uint8_t(sec0);
    info[0xe2] = uint8_t(log_track);
    info[0xe3] = 0x16;  // 80 track, double sided
    info[0xe4] = uint8_t(nfiles);
    const int free = 80 * 2 * 16 - 16 - used;
    info[0xe5] = uint8_t(free & 0xff);
    info[0xe6] = uint8_t((free >> 8) & 0xff);
    info[0xe7] = 0x10;
    info[0xe8] = 0;
    std::memset(info + 0xe9, 0x20, 10);
    info[0xf3] = 0;
    info[0xf4] = 0;
    std::memcpy(info + 0xf5, "SCLDISK ", 8);
    return true;
}

}  // namespace dsp
