#include "core/rom_loader.h"

#include <zlib.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace dsp {
namespace {

std::string to_lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return char(std::tolower(c)); });
    return value;
}

uint16_t read_u16(const uint8_t* data) { return uint16_t(data[0] | (data[1] << 8)); }

uint32_t read_u32(const uint8_t* data) {
    return uint32_t(data[0]) | (uint32_t(data[1]) << 8) | (uint32_t(data[2]) << 16) |
           (uint32_t(data[3]) << 24);
}

bool read_whole_file(const std::string& path, std::vector<uint8_t>& out) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) return false;
    stream.seekg(0, std::ios::end);
    std::streamoff size = stream.tellg();
    if (size < 0) return false;
    stream.seekg(0, std::ios::beg);
    out.resize(size_t(size));
    stream.read(reinterpret_cast<char*>(out.data()), size);
    return bool(stream);
}

}  // namespace

uint32_t crc32_of(const uint8_t* data, size_t length) {
    return uint32_t(crc32(crc32(0, nullptr, 0), data, uInt(length)));
}

bool RomLoader::open(const std::string& path, std::string* error) {
    path_ = path;
    namespace fs = std::filesystem;
    std::error_code ec;
    if (fs::is_directory(path_, ec)) {
        is_zip_ = false;
        return true;
    }
    if (!fs::exists(path_, ec)) {
        if (error) *error = "ROM path not found: " + path_;
        return false;
    }
    is_zip_ = true;
    if (!read_whole_file(path_, zip_data_)) {
        if (error) *error = "cannot read " + path_;
        return false;
    }
    return load_zip_index(error);
}

bool RomLoader::load_zip_index(std::string* error) {
    // Locate the end of central directory record.
    if (zip_data_.size() < 22) {
        if (error) *error = path_ + ": file too small to be a zip archive";
        return false;
    }
    size_t eocd = 0;
    bool found = false;
    size_t limit = std::min<size_t>(zip_data_.size(), 0xffff + 22);
    for (size_t i = 22; i <= limit; i++) {
        size_t pos = zip_data_.size() - i;
        if (read_u32(&zip_data_[pos]) == 0x06054b50) {
            eocd = pos;
            found = true;
            break;
        }
    }
    if (!found) {
        if (error) *error = path_ + ": not a zip archive";
        return false;
    }

    uint16_t entries = read_u16(&zip_data_[eocd + 10]);
    uint32_t cd_offset = read_u32(&zip_data_[eocd + 16]);
    size_t pos = cd_offset;
    for (uint16_t n = 0; n < entries; n++) {
        if (pos + 46 > zip_data_.size() || read_u32(&zip_data_[pos]) != 0x02014b50) {
            if (error) *error = path_ + ": malformed central directory";
            return false;
        }
        ZipEntry entry;
        entry.method = read_u16(&zip_data_[pos + 10]);
        entry.compressed_size = read_u32(&zip_data_[pos + 20]);
        entry.crc = read_u32(&zip_data_[pos + 16]);
        entry.uncompressed_size = read_u32(&zip_data_[pos + 24]);
        uint16_t name_length = read_u16(&zip_data_[pos + 28]);
        uint16_t extra_length = read_u16(&zip_data_[pos + 30]);
        uint16_t comment_length = read_u16(&zip_data_[pos + 32]);
        entry.local_offset = read_u32(&zip_data_[pos + 42]);
        std::string name(reinterpret_cast<const char*>(&zip_data_[pos + 46]), name_length);
        zip_index_[to_lower(name)] = entry;
        pos += 46u + name_length + extra_length + comment_length;
    }
    return true;
}

std::vector<std::string> RomLoader::filenames() const {
    std::vector<std::string> names;
    auto basename = [](const std::string& path) {
        const size_t slash = path.find_last_of("/\\");
        return slash == std::string::npos ? path : path.substr(slash + 1);
    };
    if (!is_zip_) {
        namespace fs = std::filesystem;
        std::error_code ec;
        for (const auto& item : fs::directory_iterator(path_, ec)) {
            if (!item.is_regular_file(ec)) continue;
            names.push_back(to_lower(item.path().filename().string()));
        }
        return names;
    }
    names.reserve(zip_index_.size());
    for (const auto& kv : zip_index_) names.push_back(basename(kv.first));
    return names;
}

bool RomLoader::load_first_file(std::vector<uint8_t>& dest, std::string* error) const {
    if (!is_zip_) {
        namespace fs = std::filesystem;
        std::error_code ec;
        for (const auto& item : fs::directory_iterator(path_, ec)) {
            if (!item.is_regular_file(ec)) continue;
            if (read_whole_file(item.path().string(), dest)) return true;
            if (error) *error = "cannot read " + item.path().string();
            return false;
        }
        if (error) *error = path_ + ": no files found";
        return false;
    }
    if (zip_index_.empty()) {
        if (error) *error = path_ + ": empty zip archive";
        return false;
    }
    const std::string& name = zip_index_.begin()->first;
    if (read_file(name, dest)) return true;
    if (error) *error = path_ + ": cannot extract " + name;
    return false;
}

bool RomLoader::read_file(const std::string& name, std::vector<uint8_t>& out) const {
    if (!is_zip_) {
        namespace fs = std::filesystem;
        if (read_whole_file((fs::path(path_) / name).string(), out)) return true;
        // Fall back to a case insensitive search in the directory.
        std::error_code ec;
        for (const auto& item : fs::directory_iterator(path_, ec)) {
            if (to_lower(item.path().filename().string()) == to_lower(name)) {
                return read_whole_file(item.path().string(), out);
            }
        }
        return false;
    }

    auto it = zip_index_.find(to_lower(name));
    if (it == zip_index_.end()) {
        const std::string wanted = to_lower(name);
        for (auto kv = zip_index_.begin(); kv != zip_index_.end(); ++kv) {
            const std::string& entry_name = kv->first;
            const size_t slash = entry_name.find_last_of("/\\");
            const std::string base =
                slash == std::string::npos ? entry_name : entry_name.substr(slash + 1);
            if (base == wanted) {
                it = kv;
                break;
            }
        }
    }
    if (it == zip_index_.end()) return false;
    const ZipEntry& entry = it->second;
    size_t local = entry.local_offset;
    if (local + 30 > zip_data_.size() || read_u32(&zip_data_[local]) != 0x04034b50) return false;
    uint16_t name_length = read_u16(&zip_data_[local + 26]);
    uint16_t extra_length = read_u16(&zip_data_[local + 28]);
    size_t data_start = local + 30u + name_length + extra_length;
    if (data_start + entry.compressed_size > zip_data_.size()) return false;

    if (entry.method == 0) {
        out.assign(zip_data_.begin() + std::ptrdiff_t(data_start),
                   zip_data_.begin() + std::ptrdiff_t(data_start + entry.compressed_size));
        return true;
    }
    if (entry.method != 8) return false;

    out.assign(entry.uncompressed_size, 0);
    z_stream stream{};
    stream.next_in = const_cast<Bytef*>(&zip_data_[data_start]);
    stream.avail_in = uInt(entry.compressed_size);
    stream.next_out = out.data();
    stream.avail_out = uInt(out.size());
    if (inflateInit2(&stream, -MAX_WBITS) != Z_OK) return false;
    int result = inflate(&stream, Z_FINISH);
    inflateEnd(&stream);
    return result == Z_STREAM_END;
}

bool is_zip_file(const std::string& path) {
    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec)) return false;
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (f == nullptr) return false;
    unsigned char magic[4] = {};
    const size_t n = std::fread(magic, 1, 4, f);
    std::fclose(f);
    return n == 4 && magic[0] == 'P' && magic[1] == 'K' && magic[2] == 3 && magic[3] == 4;
}

bool read_plain_rom(const std::string& path, std::vector<uint8_t>& out) {
    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec) || is_zip_file(path)) return false;
    return read_whole_file(path, out);
}

bool RomLoader::open_sibling(const std::string& path, const std::string& zip_name,
                             std::string* error) {
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::path dir = fs::is_directory(path, ec) ? fs::path(path) : fs::path(path).parent_path();
    if (dir.empty()) dir = ".";
    const fs::path candidate = dir / zip_name;
    if (!fs::is_regular_file(candidate, ec)) {
        if (error) *error = candidate.string() + " not found";
        return false;
    }
    return open(candidate.string(), error);
}

bool RomLoader::find_by_crc(uint32_t crc, size_t size, std::vector<uint8_t>& out,
                            std::string* used) const {
    if (is_zip_) {
        for (const auto& kv : zip_index_) {
            const ZipEntry& e = kv.second;
            if (e.crc != crc || (size != 0 && e.uncompressed_size != size)) continue;
            if (read_file(kv.first, out) && crc32_of(out.data(), out.size()) == crc) {
                if (used) *used = kv.first;
                return true;
            }
        }
        return false;
    }
    namespace fs = std::filesystem;
    std::error_code ec;
    for (const auto& item : fs::directory_iterator(path_, ec)) {
        if (!item.is_regular_file(ec)) continue;
        const auto file_size = item.file_size(ec);
        if (ec || file_size == 0 || file_size > (8u << 20)) continue;
        if (size != 0 && file_size != size) continue;
        std::vector<uint8_t> data;
        if (!read_whole_file(item.path().string(), data)) continue;
        if (crc32_of(data.data(), data.size()) != crc) continue;
        out = std::move(data);
        if (used) *used = item.path().filename().string();
        return true;
    }
    return false;
}

bool RomLoader::find(const std::string& names, std::initializer_list<uint32_t> crcs,
                     size_t min_size, std::vector<uint8_t>& out, std::string* used) const {
    for (size_t start = 0; start <= names.size();) {
        size_t separator = names.find('|', start);
        if (separator == std::string::npos) separator = names.size();
        const std::string candidate = names.substr(start, separator - start);
        if (!candidate.empty() && read_file(candidate, out) && out.size() >= min_size) {
            if (used) *used = candidate;
            return true;
        }
        start = separator + 1;
    }
    for (uint32_t crc : crcs) {
        if (crc != 0 && find_by_crc(crc, 0, out, used) && out.size() >= min_size) return true;
    }
    return false;
}

bool RomLoader::load(const std::vector<RomEntry>& entries, std::vector<uint8_t>& dest,
                     std::string* error) {
    for (const RomEntry& entry : entries) {
        if (entry.name == nullptr) continue;
        std::vector<uint8_t> data;
        // A name can list several alternatives separated by '|', so a driver can
        // accept the file names of more than one revision of the same set.
        std::string names(entry.name);
        std::string used;
        for (size_t start = 0; start <= names.size();) {
            size_t separator = names.find('|', start);
            if (separator == std::string::npos) separator = names.size();
            std::string candidate = names.substr(start, separator - start);
            if (!candidate.empty() && read_file(candidate, data)) {
                used = candidate;
                break;
            }
            start = separator + 1;
        }
        // Not under any of its names: look for the same dump by CRC (MAME
        // sets and other collections name the files differently).
        if (used.empty() && entry.crc != 0) find_by_crc(entry.crc, entry.length, data, &used);
        if (used.empty()) {
            if (error) *error = std::string("missing ROM file: ") + entry.name;
            return false;
        }
        if (data.size() != entry.length) {
            if (error) {
                *error = std::string("wrong size for ") + used + " (expected " +
                         std::to_string(entry.length) + ", got " + std::to_string(data.size()) +
                         ")";
            }
            return false;
        }
        uint32_t crc = crc32_of(data.data(), data.size());
        if (entry.crc != 0 && crc != entry.crc) {
            char buffer[128];
            std::snprintf(buffer, sizeof(buffer), "%s: CRC mismatch (expected %08x, got %08x)",
                          used.c_str(), entry.crc, crc);
            warnings_.emplace_back(buffer);
        }
        if (entry.offset + entry.length > dest.size()) dest.resize(entry.offset + entry.length);
        std::memcpy(dest.data() + entry.offset, data.data(), data.size());
    }
    return true;
}

}  // namespace dsp
