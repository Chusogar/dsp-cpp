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

bool RomLoader::load(const std::vector<RomEntry>& entries, std::vector<uint8_t>& dest,
                     std::string* error) {
    for (const RomEntry& entry : entries) {
        if (entry.name == nullptr) continue;
        std::vector<uint8_t> data;
        if (!read_file(entry.name, data)) {
            if (error) *error = std::string("missing ROM file: ") + entry.name;
            return false;
        }
        if (data.size() != entry.length) {
            if (error) {
                *error = std::string("wrong size for ") + entry.name + " (expected " +
                         std::to_string(entry.length) + ", got " + std::to_string(data.size()) +
                         ")";
            }
            return false;
        }
        uint32_t crc = crc32_of(data.data(), data.size());
        if (crc != entry.crc) {
            char buffer[128];
            std::snprintf(buffer, sizeof(buffer), "%s: CRC mismatch (expected %08x, got %08x)",
                          entry.name, entry.crc, crc);
            warnings_.emplace_back(buffer);
        }
        if (entry.offset + entry.length > dest.size()) dest.resize(entry.offset + entry.length);
        std::memcpy(dest.data() + entry.offset, data.data(), data.size());
    }
    return true;
}

}  // namespace dsp
