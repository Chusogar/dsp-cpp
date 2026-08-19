#include "machine/d64_image.h"

#include <cstring>
#include <fstream>

namespace dsp {
namespace {
int track_offset(int track) {
    // Cumulative sector offsets for tracks 1..35
    static const int off[41] = {
        0,
        0, 21, 42, 63, 84, 105, 126, 147, 168, 189, 210, 231, 252, 273, 294, 315, 336,
        357, 376, 395, 414, 433, 452, 471,
        490, 508, 526, 544, 562, 580,
        598, 615, 632, 649, 666,
    };
    if (track < 1 || track > 35) return -1;
    return off[track] * 256;
}
}  // namespace

int D64Image::sectors_per_track(int track) {
    if (track < 1) return 0;
    if (track <= 17) return 21;
    if (track <= 24) return 19;
    if (track <= 30) return 18;
    if (track <= 35) return 17;
    return 0;
}

bool D64Image::load_file(const std::string& path, std::string* error) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        if (error) *error = "cannot open D64: " + path;
        return false;
    }
    f.seekg(0, std::ios::end);
    const auto n = f.tellg();
    f.seekg(0, std::ios::beg);
    data_.resize(size_t(n));
    f.read(reinterpret_cast<char*>(data_.data()), n);
    return load_memory(data_.data(), data_.size(), error);
}

bool D64Image::load_memory(const uint8_t* data, size_t size, std::string* error) {
    // Standard 174848 or 175531 (with error map)
    if (size < 174848) {
        if (error) *error = "D64 too small";
        return false;
    }
    data_.assign(data, data + size);
    errors_.clear();
    // 174848 standard; +683 error map optional (175531)
    if (size >= 174848 + 683)
        errors_.assign(data + 174848, data + 174848 + 683);
    open_ = true;
    // BAM track 18 sector 0: disk ID at $A2/$A3
    uint8_t bam[256];
    if (read_sector(18, 0, bam)) {
        id1_ = bam[0xA2];
        id2_ = bam[0xA3];
    }
    return parse_directory();
}

static int sector_index(uint8_t track, uint8_t sector) {
    int idx = 0;
    for (int t = 1; t < track; t++) idx += D64Image::sectors_per_track(t);
    return idx + int(sector);
}

uint8_t D64Image::sector_error(uint8_t track, uint8_t sector) const {
    if (errors_.empty()) return 1;  // OK
    const int idx = sector_index(track, sector);
    if (idx < 0 || idx >= int(errors_.size())) return 1;
    return errors_[size_t(idx)];
}

bool D64Image::read_sector(uint8_t track, uint8_t sector, uint8_t out[256]) const {
    const int spt = sectors_per_track(track);
    if (!open_ || sector >= spt) return false;
    const int base = track_offset(track);
    if (base < 0) return false;
    const size_t off = size_t(base + int(sector) * 256);
    if (off + 256 > data_.size()) return false;
    std::memcpy(out, data_.data() + off, 256);
    return true;
}

std::string D64Image::petscii_name(const uint8_t* p, int n) const {
    std::string s;
    for (int i = 0; i < n; i++) {
        uint8_t c = p[i];
        if (c == 0xA0) break;  // shift-space padding
        if (c >= 0x41 && c <= 0x5A) c = uint8_t(c + 32);  // to lower
        else if (c >= 0xC1 && c <= 0xDA) c = uint8_t(c - 128);
        if (c >= 32 && c < 127) s.push_back(char(c));
    }
    return s;
}

bool D64Image::parse_directory() {
    dir_.clear();
    uint8_t sec[256];
    uint8_t track = 18, sector = 1;
    for (int guard = 0; guard < 50; guard++) {
        if (!read_sector(track, sector, sec)) break;
        for (int i = 0; i < 8; i++) {
            const uint8_t* e = sec + i * 32;
            const uint8_t ft = e[2];
            if ((ft & 0x0F) == 0) continue;  // empty
            D64File f;
            f.type = ft;
            f.track = e[3];
            f.sector = e[4];
            f.name = petscii_name(e + 5, 16);
            f.blocks = uint16_t(e[0x1E] | (e[0x1F] << 8));
            dir_.push_back(f);
        }
        track = sec[0];
        sector = sec[1];
        if (track == 0) break;
    }
    return true;
}

bool D64Image::load_prg(int index, std::vector<uint8_t>* out, std::string* error) const {
    if (index < 0 || index >= int(dir_.size())) {
        if (error) *error = "directory index out of range";
        return false;
    }
    const D64File& f = dir_[size_t(index)];
    if ((f.type & 0x0F) != 0x02) {
        if (error) *error = "not a PRG file";
        return false;
    }
    out->clear();
    uint8_t track = f.track, sector = f.sector;
    uint8_t sec[256];
    for (int guard = 0; guard < 1024; guard++) {
        if (!read_sector(track, sector, sec)) {
            if (error) *error = "sector read failed";
            return false;
        }
        const uint8_t next_t = sec[0];
        const uint8_t next_s = sec[1];
        const int nbytes = (next_t == 0) ? int(next_s) : 254;
        out->insert(out->end(), sec + 2, sec + 2 + nbytes);
        if (next_t == 0) break;
        track = next_t;
        sector = next_s;
    }
    return !out->empty();
}

bool D64Image::load_first_prg(std::vector<uint8_t>* out, std::string* error) const {
    for (size_t i = 0; i < dir_.size(); i++) {
        if ((dir_[i].type & 0x0F) == 0x02) return load_prg(int(i), out, error);
    }
    if (error) *error = "no PRG files on disk";
    return false;
}

}  // namespace dsp
