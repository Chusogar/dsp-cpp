#include "machine/g64_image.h"

#include <cstring>
#include <fstream>

namespace dsp {
namespace {

bool read_file(const std::string& path, std::vector<uint8_t>* out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    f.seekg(0, std::ios::end);
    const auto n = f.tellg();
    if (n <= 0) return false;
    f.seekg(0, std::ios::beg);
    out->resize(size_t(n));
    f.read(reinterpret_cast<char*>(out->data()), n);
    return bool(f);
}

uint32_t le32(const uint8_t* p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) |
           (uint32_t(p[3]) << 24);
}
uint16_t le16(const uint8_t* p) {
    return uint16_t(p[0] | (uint16_t(p[1]) << 8));
}

}  // namespace

bool G64Image::load_file(const std::string& path, std::string* error) {
    std::vector<uint8_t> data;
    if (!read_file(path, &data)) {
        if (error) *error = "cannot open G64: " + path;
        return false;
    }
    return load_memory(data.data(), data.size(), error);
}

bool G64Image::load_memory(const uint8_t* data, size_t size, std::string* error) {
    open_ = false;
    tracks_.clear();
    speed_.clear();
    if (!data || size < 12) {
        if (error) *error = "G64 too small";
        return false;
    }
    if (std::memcmp(data, "GCR-1541", 8) != 0) {
        if (error) *error = "not a G64 image (bad signature)";
        return false;
    }
    // data[8] = version
    num_half_tracks_ = data[9];
    if (num_half_tracks_ <= 0 || num_half_tracks_ > kMaxHalfTracks)
        num_half_tracks_ = 84;
    max_track_size_ = int(le16(data + 10));
    if (max_track_size_ <= 0) max_track_size_ = 7928;

    const size_t table_off = 12;
    const size_t need = table_off + size_t(num_half_tracks_) * 4;
    if (size < need) {
        if (error) *error = "G64 truncated (offset table)";
        return false;
    }

    tracks_.assign(size_t(kMaxHalfTracks + 1), {});
    speed_.assign(size_t(kMaxHalfTracks + 1), 0);

    // Optional speed table sits right after offsets (VICE variant)
    const size_t speed_off = table_off + size_t(num_half_tracks_) * 4;
    const bool has_speed = size >= speed_off + size_t(num_half_tracks_) * 4;

    for (int ht = 1; ht <= num_half_tracks_; ht++) {
        const uint32_t off = le32(data + table_off + size_t(ht - 1) * 4);
        if (off == 0) continue;
        if (size_t(off) + 2 > size) continue;
        const int len = int(le16(data + off));
        if (len <= 0 || size_t(off) + 2 + size_t(len) > size) continue;
        tracks_[size_t(ht)].assign(data + off + 2, data + off + 2 + len);

        if (has_speed) {
            // Speed entries: either a zone 0..3 or an offset to a map; simple values.
            const uint32_t sp = le32(data + speed_off + size_t(ht - 1) * 4);
            if (sp <= 3) speed_[size_t(ht)] = int(sp);
        } else {
            // Default density from half-track number → full track
            const int full = (ht + 1) / 2;
            if (full <= 17) speed_[size_t(ht)] = 3;
            else if (full <= 24) speed_[size_t(ht)] = 2;
            else if (full <= 30) speed_[size_t(ht)] = 1;
            else speed_[size_t(ht)] = 0;
        }
    }

    open_ = true;
    return true;
}

const std::vector<uint8_t>& G64Image::track_data(int half_track) const {
    static const std::vector<uint8_t> empty;
    if (half_track < 1 || half_track > kMaxHalfTracks) return empty;
    return tracks_[size_t(half_track)];
}

int G64Image::speed_zone(int half_track) const {
    if (half_track < 1 || half_track > kMaxHalfTracks) return 0;
    return speed_[size_t(half_track)];
}

std::vector<bool> G64Image::bytes_to_bits(const std::vector<uint8_t>& gcr) {
    std::vector<bool> bits;
    bits.reserve(gcr.size() * 8);
    for (uint8_t b : gcr)
        for (int i = 7; i >= 0; i--) bits.push_back(((b >> i) & 1) != 0);
    return bits;
}

}  // namespace dsp
