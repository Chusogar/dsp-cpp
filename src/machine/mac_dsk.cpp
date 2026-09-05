#include "machine/mac_dsk.h"

#include <cstring>
#include <fstream>

namespace dsp {
namespace {

const uint8_t kGcr6[64] = {
    0x96, 0x97, 0x9A, 0x9B, 0x9D, 0x9E, 0x9F, 0xA6, 0xA7, 0xAB, 0xAC, 0xAD, 0xAE, 0xAF, 0xB2, 0xB3,
    0xB4, 0xB5, 0xB6, 0xB7, 0xB9, 0xBA, 0xBB, 0xBC, 0xBD, 0xBE, 0xBF, 0xCB, 0xCD, 0xCE, 0xCF, 0xD3,
    0xD6, 0xD7, 0xD9, 0xDA, 0xDB, 0xDC, 0xDD, 0xDE, 0xDF, 0xE5, 0xE6, 0xE7, 0xE9, 0xEA, 0xEB, 0xEC,
    0xED, 0xEE, 0xEF, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6, 0xF7, 0xF9, 0xFA, 0xFB, 0xFC, 0xFD, 0xFE, 0xFF,
};

uint32_t be32(const uint8_t* p) {
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
}

int rotl8(int value) { return ((value << 1) | ((value >> 7) & 1)) & 0xff; }

}  // namespace

uint8_t MacDsk::gcr6(uint8_t sixbit) { return kGcr6[sixbit & 0x3f]; }

int MacDsk::sectors_per_track(int track) {
    if (track < 0) track = 0;
    if (track > 79) track = 79;
    return 12 - (track / 16);
}

int MacDsk::logical_offset(int track, int side, int sector, int sides) {
    int offset = 0;
    for (int t = 0; t < track; t++) offset += sectors_per_track(t) * sides;
    offset += side * sectors_per_track(track);
    offset += sector;
    return offset * kSectorSize;
}

void MacDsk::reset() {
    loaded_ = false;
    hd_ = false;
    image_.clear();
    tags_.clear();
    for (int t = 0; t < kTracks; t++) {
        nibbles_[t][0].clear();
        nibbles_[t][1].clear();
    }
}

const uint8_t* MacDsk::sector(int track, int side, int sector) const {
    if (!loaded_ || track < 0 || track >= tracks_ || side < 0 || side >= sides_) return nullptr;
    if (sector < 0 || sector >= sectors_per_track(track)) return nullptr;
    const int off = logical_offset(track, side, sector, sides_);
    if (off < 0 || off + kSectorSize > int(image_.size())) return nullptr;
    return image_.data() + off;
}

uint8_t* MacDsk::sector(int track, int side, int sector) {
    return const_cast<uint8_t*>(static_cast<const MacDsk*>(this)->sector(track, side, sector));
}

const std::vector<uint8_t>& MacDsk::nibbles(int track, int side) const {
    static const std::vector<uint8_t> kEmpty;
    if (track < 0 || track >= kTracks || side < 0 || side > 1) return kEmpty;
    return nibbles_[track][side];
}

void MacDsk::encode_data(uint8_t sector, const uint8_t src[524], std::vector<uint8_t>& dest) {
    dest.assign(709, 0);
    dest[0] = 0xD5;
    dest[1] = 0xAA;
    dest[2] = 0xAD;
    dest[3] = gcr6(sector & 0x3f);

    int checksum[3] = {0, 0, 0};
    const uint8_t* source = src;
    for (int c = 0; c < 175; c++) {
        uint8_t values[3];
        checksum[0] = rotl8(checksum[0]);
        values[0] = uint8_t(*source ^ checksum[0]);
        checksum[2] += *source + (checksum[0] & 1);
        ++source;

        values[1] = uint8_t(*source ^ checksum[2]);
        checksum[1] += *source + (checksum[2] >> 8);
        ++source;

        if (c == 174) {
            values[2] = 0;
        } else {
            values[2] = uint8_t(*source ^ checksum[1]);
            checksum[0] += *source + (checksum[1] >> 8);
            ++source;
        }
        checksum[1] &= 0xff;
        checksum[2] &= 0xff;

        dest[4 + c * 4 + 1] = gcr6(values[0] & 0x3f);
        dest[4 + c * 4 + 2] = gcr6(values[1] & 0x3f);
        dest[4 + c * 4 + 3] = gcr6(values[2] & 0x3f);
        dest[4 + c * 4 + 0] =
            gcr6(uint8_t(((values[0] >> 2) & 0x30) | ((values[1] >> 4) & 0x0c) |
                         ((values[2] >> 6) & 0x03)));
    }

    dest[704] = gcr6(checksum[2] & 0x3f);
    dest[705] = gcr6(checksum[1] & 0x3f);
    dest[706] = gcr6(checksum[0] & 0x3f);
    dest[703] = gcr6(uint8_t(((checksum[2] >> 2) & 0x30) | ((checksum[1] >> 4) & 0x0c) |
                            ((checksum[0] >> 6) & 0x03)));
    dest[707] = 0xDE;
    dest[708] = 0xAA;
}

bool MacDsk::decode_data(const uint8_t* gcr, size_t length, uint8_t dest[524]) {
    if (gcr == nullptr || length < 709) return false;
    if (gcr[0] != 0xD5 || gcr[1] != 0xAA || gcr[2] != 0xAD) return false;

    uint8_t ungcr[256];
    std::memset(ungcr, 0xff, sizeof(ungcr));
    for (int i = 0; i < 64; i++) ungcr[kGcr6[i]] = uint8_t(i);

    int checksum[3] = {0, 0, 0};
    uint8_t* out = dest;
    for (int c = 0; c < 175; c++) {
        const uint8_t n0 = ungcr[gcr[4 + c * 4 + 0]];
        const uint8_t n1 = ungcr[gcr[4 + c * 4 + 1]];
        const uint8_t n2 = ungcr[gcr[4 + c * 4 + 2]];
        const uint8_t n3 = ungcr[gcr[4 + c * 4 + 3]];
        if (n0 > 0x3f || n1 > 0x3f || n2 > 0x3f || n3 > 0x3f) return false;
        const uint8_t v0 = uint8_t(n1 | ((n0 & 0x30) << 2));
        const uint8_t v1 = uint8_t(n2 | ((n0 & 0x0c) << 4));
        const uint8_t v2 = uint8_t(n3 | ((n0 & 0x03) << 6));

        checksum[0] = rotl8(checksum[0]);
        const uint8_t b0 = uint8_t(v0 ^ checksum[0]);
        checksum[2] += b0 + (checksum[0] & 1);
        *out++ = b0;

        const uint8_t b1 = uint8_t(v1 ^ checksum[2]);
        checksum[1] += b1 + (checksum[2] >> 8);
        checksum[2] &= 0xff;
        *out++ = b1;

        if (c != 174) {
            const uint8_t b2 = uint8_t(v2 ^ checksum[1]);
            checksum[0] += b2 + (checksum[1] >> 8);
            checksum[1] &= 0xff;
            *out++ = b2;
        }
        checksum[0] &= 0xff;
    }
    return true;
}

void MacDsk::append_header(std::vector<uint8_t>& dest, int track, int sector, int side,
                           uint8_t format) {
    dest.push_back(0xD5);
    dest.push_back(0xAA);
    dest.push_back(0x96);
    const uint8_t b0 = uint8_t(track & 0x3f);
    const uint8_t b1 = uint8_t(sector & 0x3f);
    const uint8_t b2 = uint8_t((side ? 0x20 : 0x00) | ((track >> 6) & 0x1f));
    const uint8_t b3 = format;
    const uint8_t sum = uint8_t(b0 ^ b1 ^ b2 ^ b3);
    dest.push_back(gcr6(b0));
    dest.push_back(gcr6(b1));
    dest.push_back(gcr6(b2));
    dest.push_back(gcr6(b3));
    dest.push_back(gcr6(sum));
    dest.push_back(0xDE);
    dest.push_back(0xAA);
}

void MacDsk::encode_track(int track, int side) {
    std::vector<uint8_t>& dest = nibbles_[track][side];
    dest.clear();
    if (!loaded_ || side >= sides_) return;

    const int ns = sectors_per_track(track);
    // 2:1 interleave used by Macintosh 800K disks.
    int si = 0;
    for (int i = 0; i < ns; i++) {
        for (int g = 0; g < 36; g++) dest.push_back(0xFF);
        append_header(dest, track, si, side, format_);
        for (int g = 0; g < 6; g++) dest.push_back(0xFF);

        uint8_t raw[524];
        std::memset(raw, 0, sizeof(raw));
        const int tag_index = (logical_offset(track, side, si, sides_) / kSectorSize) * kTagSize;
        if (tag_index >= 0 && tag_index + kTagSize <= int(tags_.size())) {
            std::memcpy(raw, tags_.data() + tag_index, kTagSize);
        }
        const uint8_t* data = sector(track, side, si);
        if (data) std::memcpy(raw + kTagSize, data, kSectorSize);

        std::vector<uint8_t> encoded;
        encode_data(uint8_t(si), raw, encoded);
        dest.insert(dest.end(), encoded.begin(), encoded.end());
        dest.push_back(0xFF);

        si = (si + 2) % ns;
        if (si == 0) si++;
    }
    for (int g = 0; g < 64; g++) dest.push_back(0xFF);
}

void MacDsk::rebuild_all() {
    for (int t = 0; t < tracks_; t++) {
        for (int s = 0; s < 2; s++) encode_track(t, s);
    }
}

bool MacDsk::load_file(const std::string& path, std::string* error) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        if (error) *error = "cannot open " + path;
        return false;
    }
    in.seekg(0, std::ios::end);
    const std::streamoff size = in.tellg();
    in.seekg(0, std::ios::beg);
    std::vector<uint8_t> bytes(static_cast<size_t>(size < 0 ? 0 : size));
    if (size > 0) in.read(reinterpret_cast<char*>(bytes.data()), size);
    if (!in && size > 0) {
        if (error) *error = "cannot read " + path;
        return false;
    }
    if (!load_bytes(bytes.data(), bytes.size(), error)) {
        if (error && error->empty()) *error = path + ": not a Macintosh 400K/800K/1.44MB disk";
        return false;
    }
    return true;
}

bool MacDsk::load_bytes(const uint8_t* data, size_t size, std::string* error) {
    reset();
    const uint8_t* payload = data;
    size_t payload_size = size;
    size_t tag_size = 0;
    uint8_t encoding = 0x01;
    uint8_t format = 0x22;

    if (size >= 0x54 && data[0] < 64 && data[0x52] == 1 && data[0x53] == 0) {
        const uint32_t dsize = be32(data + 0x40);
        const uint32_t tsize = be32(data + 0x44);
        if (size == 0x54 + dsize + tsize &&
            (dsize == 409600u || dsize == 819200u || dsize == 1474560u)) {
            payload = data + 0x54;
            payload_size = dsize;
            tag_size = tsize;
            encoding = data[0x50];
            format = data[0x51];
        }
    }

    if (payload_size == 409600) {
        sides_ = 1;
        hd_ = false;
        format_ = format == 0 ? 0x02 : format;
        if (encoding == 0) format_ = 0x02;
    } else if (payload_size == 819200) {
        sides_ = 2;
        hd_ = false;
        format_ = format == 0 ? 0x22 : format;
        if (encoding == 1 || encoding == 3) format_ = (format ? format : 0x22);
    } else if (payload_size == 1474560) {
        sides_ = 2;
        hd_ = true;
        format_ = format ? format : 0x22;
    } else {
        if (error) *error = "Macintosh disk must be 400K, 800K, or 1.44MB";
        return false;
    }

    tracks_ = kTracks;
    image_.assign(payload, payload + payload_size);
    if (tag_size) {
        tags_.assign(payload + payload_size, payload + payload_size + tag_size);
    } else {
        tags_.assign((payload_size / kSectorSize) * kTagSize, 0);
    }
    loaded_ = true;
    rebuild_all();
    return true;
}

bool MacDsk::read_lba(uint32_t lba, uint8_t dest[kSectorSize]) const {
    if (!loaded_ || dest == nullptr) return false;
    const size_t off = size_t(lba) * kSectorSize;
    if (off + kSectorSize > image_.size()) return false;
    std::memcpy(dest, image_.data() + off, kSectorSize);
    return true;
}

bool MacDsk::write_lba(uint32_t lba, const uint8_t src[kSectorSize]) {
    if (!loaded_ || src == nullptr) return false;
    const size_t off = size_t(lba) * kSectorSize;
    if (off + kSectorSize > image_.size()) return false;
    std::memcpy(image_.data() + off, src, kSectorSize);
    return true;
}

}  // namespace dsp
