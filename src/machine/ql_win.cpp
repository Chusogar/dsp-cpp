#include "machine/ql_win.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>

namespace dsp {
namespace {

uint16_t be16(const uint8_t* p) { return uint16_t((uint16_t(p[0]) << 8) | p[1]); }
uint32_t be32(const uint8_t* p) {
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
}
void put_be16(uint8_t* p, uint16_t v) {
    p[0] = uint8_t(v >> 8);
    p[1] = uint8_t(v);
}
void put_be32(uint8_t* p, uint32_t v) {
    p[0] = uint8_t(v >> 24);
    p[1] = uint8_t(v >> 16);
    p[2] = uint8_t(v >> 8);
    p[3] = uint8_t(v);
}

std::string lower_copy(std::string s) {
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool read_all(const std::string& path, std::vector<uint8_t>& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    in.seekg(0, std::ios::end);
    const auto n = in.tellg();
    if (n <= 0) return false;
    in.seekg(0);
    out.resize(size_t(n));
    in.read(reinterpret_cast<char*>(out.data()), n);
    return bool(in);
}

std::vector<uint8_t> read_chain(const std::vector<uint8_t>& image, uint32_t cluster_size,
                                uint16_t first, uint16_t clusters) {
    std::vector<uint8_t> out;
    uint16_t c = first;
    std::vector<uint8_t> seen(clusters, 0);
    while (c != 0 && c < clusters && !seen[c]) {
        seen[c] = 1;
        const size_t off = size_t(c) * cluster_size;
        if (off + cluster_size > image.size()) break;
        out.insert(out.end(), image.begin() + off, image.begin() + off + cluster_size);
        c = be16(&image[0x40 + size_t(c) * 2]);
    }
    return out;
}

void fill_header(QlWinFile& f) {
    f.header.fill(0);
    put_be32(f.header.data(), f.logical_size());
    f.header[5] = f.type;
    put_be32(f.header.data() + 6, f.dataspace);
    const uint16_t nlen = uint16_t(std::min<size_t>(f.name.size(), 36));
    put_be16(f.header.data() + 0x0e, nlen);
    std::memcpy(f.header.data() + 0x10, f.name.data(), nlen);
}

// Plug-in ROM at $C000: $4AFB0001 header, init links a WIN directory driver via
// MT.ALCHP / MT.LDD. Open/I/O write a trap word to $1BF00 for the host.
const uint8_t kWinRom[] = {
    0x4a, 0xfb, 0x00, 0x01, 0x00, 0x00, 0x00, 0x20, 0x00, 0x07, 0x51, 0x58, 0x4c, 0x20, 0x57, 0x49,
    0x4e, 0x0a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x2f, 0x08, 0x2f, 0x0b, 0x2f, 0x09, 0x70, 0x18, 0x72, 0x50, 0x74, 0x00, 0x4e, 0x41, 0x4a, 0x80,
    0x66, 0x66, 0x72, 0x00, 0x20, 0x81, 0x21, 0x41, 0x00, 0x04, 0x21, 0x41, 0x00, 0x08, 0x21, 0x41,
    0x00, 0x0c, 0x21, 0x41, 0x00, 0x10, 0x21, 0x41, 0x00, 0x14, 0x43, 0xfa, 0x00, 0x5c, 0x21, 0x49,
    0x00, 0x1c, 0x43, 0xfa, 0x00, 0x5e, 0x21, 0x49, 0x00, 0x20, 0x43, 0xfa, 0x00, 0x60, 0x21, 0x49,
    0x00, 0x24, 0x43, 0xfa, 0x00, 0x84, 0x21, 0x49, 0x00, 0x28, 0x43, 0xfa, 0x00, 0x7e, 0x21, 0x49,
    0x00, 0x34, 0x72, 0x40, 0x21, 0x41, 0x00, 0x38, 0x72, 0x03, 0x31, 0x41, 0x00, 0x3c, 0x72, 0x57,
    0x11, 0x41, 0x00, 0x3e, 0x72, 0x49, 0x11, 0x41, 0x00, 0x3f, 0x72, 0x4e, 0x11, 0x41, 0x00, 0x40,
    0x41, 0xe8, 0x00, 0x18, 0x70, 0x22, 0x4e, 0x41, 0x22, 0x1f, 0x26, 0x1f, 0x20, 0x1f, 0x4e, 0x75,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x33, 0xfc, 0x00, 0x02, 0x00, 0x01, 0xbf, 0x00,
    0x4e, 0x75, 0x33, 0xfc, 0x00, 0x01, 0x00, 0x01, 0xbf, 0x00, 0x4e, 0x75, 0x70, 0x00, 0x10, 0x28,
    0x00, 0x1d, 0xe5, 0x08, 0x45, 0xee, 0x01, 0x00, 0x24, 0xf2, 0x00, 0x00, 0x53, 0x2a, 0x00, 0x22,
    0x41, 0xe8, 0x00, 0x18, 0x43, 0xee, 0x01, 0x40, 0x38, 0x78, 0x00, 0xd4, 0x4e, 0x94, 0x41, 0xe8,
    0xff, 0xe8, 0x38, 0x78, 0x00, 0xc2, 0x4e, 0xd4, 0x4e, 0x75, 0x70, 0xed, 0x4e, 0x75,
};

}  // namespace

uint8_t QlWinFile::byte_at(uint32_t pos) const {
    if (pos < 64) return header[pos];
    pos -= 64;
    if (pos >= data.size()) return 0;
    return data[pos];
}

void QlWin::reset() {
    loaded_ = false;
    medium_.clear();
    files_.clear();
    directory_ = {};
    total_sectors_ = 0;
    empty_sectors_ = 0;
}

bool QlWin::load_file(const std::string& path, std::string* error) {
    std::vector<uint8_t> image;
    if (!read_all(path, image)) {
        if (error) *error = "cannot read " + path;
        return false;
    }
    return load_image(image, error);
}

bool QlWin::load_image(const std::vector<uint8_t>& image, std::string* error) {
    reset();
    return parse(image, error);
}

bool QlWin::parse(const std::vector<uint8_t>& image, std::string* error) {
    if (image.size() < 0x44 || std::memcmp(image.data(), "QLWA", 4) != 0) {
        if (error) *error = "file is not a QLWA QXL.WIN volume";
        return false;
    }
    const uint16_t name_len = std::min<uint16_t>(be16(&image[4]), 20);
    medium_.assign(reinterpret_cast<const char*>(&image[6]), name_len);
    while (!medium_.empty() && medium_.back() == ' ') medium_.pop_back();
    if (medium_.empty()) medium_ = "QXL";

    const uint16_t spc = be16(&image[0x22]);
    if (spc == 0 || (uint32_t(spc) * 512u) > image.size()) {
        if (error) *error = "QLWA cluster size is invalid";
        return false;
    }
    const uint32_t cluster_size = uint32_t(spc) * 512u;
    const uint16_t raw_total = be16(&image[0x2a]);
    uint32_t clusters = uint32_t(image.size() / cluster_size);
    if (raw_total != 0 && uint32_t(raw_total) * cluster_size == uint32_t(image.size())) {
        clusters = raw_total;
    } else if (raw_total != 0 && spc != 0 && uint32_t(raw_total) * 512u == uint32_t(image.size())) {
        clusters = uint32_t(raw_total) / spc;
    }
    if (clusters < 2 || 0x40 + clusters * 2 > image.size()) {
        if (error) *error = "QLWA cluster map is truncated";
        return false;
    }

    const uint16_t root = be16(&image[0x34]);
    const uint32_t dir_len = be32(&image[0x36]);
    if (root == 0 || root >= clusters) {
        if (error) *error = "QLWA root directory cluster is invalid";
        return false;
    }
    const std::vector<uint8_t> dir = read_chain(image, cluster_size, root, uint16_t(clusters));
    if (dir.size() < 64) {
        if (error) *error = "QLWA root directory is empty";
        return false;
    }
    const uint32_t usable = std::min<uint32_t>(dir_len, uint32_t(dir.size()));
    const uint32_t slots = usable / 64;

    files_.clear();
    for (uint32_t i = 1; i < slots; i++) {
        const uint8_t* e = &dir[i * 64];
        const uint32_t length = be32(e);
        const uint16_t nlen = be16(e + 0x0e);
        const uint16_t first = be16(e + 0x3a);
        if (nlen == 0 || nlen > 36 || first == 0 || first >= clusters) continue;
        QlWinFile f;
        f.name.assign(reinterpret_cast<const char*>(e + 0x10), nlen);
        while (!f.name.empty() && f.name.back() == ' ') f.name.pop_back();
        if (f.name.empty()) continue;
        const uint16_t type_word = be16(e + 4);
        f.type = (type_word == 0x00ff) ? uint8_t(0xff) : uint8_t(type_word);
        f.dataspace = be32(e + 6);
        std::vector<uint8_t> raw = read_chain(image, cluster_size, first, uint16_t(clusters));
        if (raw.size() < 64) continue;
        const uint32_t data_len = length > 64 ? length - 64 : 0;
        f.data.assign(raw.begin() + 64, raw.end());
        if (f.data.size() > data_len) f.data.resize(data_len);
        fill_header(f);
        files_.push_back(std::move(f));
    }

    directory_.name.clear();
    directory_.type = 0xff;
    directory_.data.assign(64 * files_.size(), 0);
    for (size_t i = 0; i < files_.size(); i++) {
        std::memcpy(directory_.data.data() + i * 64, files_[i].header.data(), 64);
    }
    fill_header(directory_);
    put_be32(directory_.header.data(), directory_.logical_size());
    directory_.header[5] = 0xff;

    total_sectors_ = uint32_t(image.size() / 512);
    std::vector<uint8_t> in_use(clusters, 0);
    in_use[0] = 1;
    if (root < clusters) in_use[root] = 1;
    for (uint16_t c = 0; c < clusters; c++) {
        const uint16_t n = be16(&image[0x40 + size_t(c) * 2]);
        if (n != 0 && n < clusters) in_use[n] = 1;
    }
    for (uint32_t i = 1; i < slots; i++) {
        const uint16_t first = be16(&dir[i * 64 + 0x3a]);
        const uint16_t nlen = be16(&dir[i * 64 + 0x0e]);
        if (nlen != 0 && first != 0 && first < clusters) in_use[first] = 1;
    }
    uint32_t free_clusters = 0;
    for (uint32_t c = 0; c < clusters; c++) {
        if (!in_use[c] && be16(&image[0x40 + size_t(c) * 2]) == 0) free_clusters++;
    }
    empty_sectors_ = free_clusters * (cluster_size / 512);
    loaded_ = true;
    return true;
}

const QlWinFile* QlWin::find(const std::string& name) const {
    if (!loaded_) return nullptr;
    if (name.empty()) return &directory_;
    const std::string key = lower_copy(name);
    for (const QlWinFile& f : files_) {
        if (lower_copy(f.name) == key) return &f;
    }
    return nullptr;
}

bool is_ql_win_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    char mag[4]{};
    in.read(mag, 4);
    return in.gcount() == 4 && std::memcmp(mag, "QLWA", 4) == 0;
}

void install_ql_win_rom(uint8_t* dest) {
    std::memcpy(dest, kWinRom, sizeof(kWinRom));
}

}  // namespace dsp
