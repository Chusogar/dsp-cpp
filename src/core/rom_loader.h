#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace dsp {

struct RomEntry {
    const char* name;
    uint32_t length;
    uint32_t offset;  // destination offset inside the target buffer
    uint32_t crc;
};

// Loads ROM files either from a plain directory or from a MAME style zip file.
class RomLoader {
public:
    // `path` can point to a directory containing the ROM files or to a .zip archive.
    bool open(const std::string& path, std::string* error);

    // Copies every entry into `dest`. Missing files or CRC mismatches are reported
    // through `error`; a CRC mismatch is a warning and does not fail the load.
    bool load(const std::vector<RomEntry>& entries, std::vector<uint8_t>& dest,
              std::string* error);
	
	bool load_first_file(std::vector<uint8_t>& dest, std::string* error) const;

    const std::vector<std::string>& warnings() const { return warnings_; }

private:
    bool read_file(const std::string& name, std::vector<uint8_t>& out) const;
    bool load_zip_index(std::string* error);

    std::string path_;
    bool is_zip_ = false;
    std::vector<uint8_t> zip_data_;
    // File name -> (local header offset, compressed size, uncompressed size, method)
    struct ZipEntry {
        uint32_t local_offset = 0;
        uint32_t compressed_size = 0;
        uint32_t uncompressed_size = 0;
        uint16_t method = 0;
    };
    std::map<std::string, ZipEntry> zip_index_;
    std::vector<std::string> warnings_;
};

uint32_t crc32_of(const uint8_t* data, size_t length);

}  // namespace dsp
