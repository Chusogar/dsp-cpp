#include "machine/t64_image.h"

#include <algorithm>
#include <fstream>

namespace dsp {
namespace {

uint16_t le16(const uint8_t* p) { return uint16_t(p[0] | (p[1] << 8)); }

uint32_t le32(const uint8_t* p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) |
           (uint32_t(p[3]) << 24);
}

std::string trim_name(const uint8_t* p, size_t n) {
    std::string s;
    for (size_t i = 0; i < n; i++) {
        const uint8_t c = p[i];
        s.push_back((c >= 0x20 && c < 0x7F) ? char(c) : ' ');
    }
    while (!s.empty() && s.back() == ' ') s.pop_back();
    return s;
}

}  // namespace

bool T64Image::load_file(const std::string& path, std::string* error) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        if (error) *error = "cannot open T64: " + path;
        return false;
    }
    f.seekg(0, std::ios::end);
    const auto n = f.tellg();
    f.seekg(0, std::ios::beg);
    std::vector<uint8_t> buf(static_cast<size_t>(n));
    f.read(reinterpret_cast<char*>(buf.data()), n);
    return load_memory(buf.data(), buf.size(), error);
}

bool T64Image::load_memory(const uint8_t* data, size_t size, std::string* error) {
    open_ = false;
    dir_.clear();
    tape_name_.clear();

    if (size < 0x40) {
        if (error) *error = "T64 too small";
        return false;
    }

    // Every known writer starts the 32 byte signature with "C64" and mentions
    // "tape"; the rest of the wording varies between tools.
    const std::string sig = trim_name(data, 32);
    if (sig.compare(0, 3, "C64") != 0 ||
        sig.find("tape") == std::string::npos) {
        if (error) *error = "not a T64 image";
        return false;
    }

    data_.assign(data, data + size);
    tape_name_ = trim_name(data_.data() + 0x28, 24);
    return parse_directory(error);
}

bool T64Image::parse_directory(std::string* error) {
    const uint16_t max_entries = le16(data_.data() + 0x22);
    uint16_t used = le16(data_.data() + 0x24);

    // Some writers leave the used count at zero or above the table size.
    if (used == 0 || used > max_entries) used = max_entries;

    for (uint16_t i = 0; i < used; i++) {
        const size_t base = 0x40 + size_t(i) * 32;
        if (base + 32 > data_.size()) break;

        const uint8_t* e = data_.data() + base;
        if (e[0] == 0) continue;  // free slot

        T64File f;
        f.name = trim_name(e + 0x10, 16);
        f.start = le16(e + 2);
        f.end = le16(e + 4);
        f.offset = le32(e + 8);
        if (f.offset >= data_.size()) continue;

        // The end address is unreliable: fall back to what the file holds.
        const size_t avail = data_.size() - f.offset;
        size_t len = (f.end > f.start) ? size_t(f.end - f.start) : 0;
        if (len == 0 || len > avail) len = avail;
        f.end = uint16_t(f.start + len);

        dir_.push_back(f);
    }

    if (dir_.empty()) {
        if (error) *error = "T64 has no files";
        return false;
    }

    open_ = true;
    return true;
}

bool T64Image::load_prg(size_t index, std::vector<uint8_t>* out,
                        std::string* error) const {
    if (!open_ || index >= dir_.size()) {
        if (error) *error = "T64 entry out of range";
        return false;
    }
    const T64File& f = dir_[index];
    const size_t len = size_t(f.end - f.start);

    out->clear();
    out->reserve(len + 2);
    out->push_back(uint8_t(f.start & 0xFF));
    out->push_back(uint8_t(f.start >> 8));
    out->insert(out->end(), data_.begin() + f.offset,
                data_.begin() + f.offset + len);
    return true;
}

bool T64Image::load_first_prg(std::vector<uint8_t>* out,
                              std::string* error) const {
    return load_prg(0, out, error);
}

}  // namespace dsp
