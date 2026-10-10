#pragma once

#include <cstdint>
#include <initializer_list>
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

    bool try_read(const std::string& name, std::vector<uint8_t>& out) const {
        return read_file(name, out);
    }

    // Looks for a BIOS/ROM file: first by any of the '|' separated `names`
    // (case insensitive, also inside zip subdirectories), then by CRC32 among
    // every file of the zip or directory, whatever it is called, so MAME sets
    // and other dumps with different file names are found too. Files shorter
    // than `min_size` are skipped. `used` receives the file name that matched.
    bool find(const std::string& names, std::initializer_list<uint32_t> crcs, size_t min_size,
              std::vector<uint8_t>& out, std::string* used = nullptr) const;

    // Finds a file by its CRC32 (and exact size when `size` is not 0).
    bool find_by_crc(uint32_t crc, size_t size, std::vector<uint8_t>& out,
                     std::string* used = nullptr) const;

    bool is_zip() const { return is_zip_; }

    // MAME keeps some ROMs in device zips next to the machine's own (e.g.
    // nb_mdc824.zip, betadisk.zip). Opens `zip_name` from the directory of
    // `path` when `path` is a zip, or from `path` when it is a directory.
    bool open_sibling(const std::string& path, const std::string& zip_name, std::string* error);

    // Basenames of every file in the zip or directory (lower case).
    std::vector<std::string> filenames() const;

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
        uint32_t crc = 0;
    };
    std::map<std::string, ZipEntry> zip_index_;
    std::vector<std::string> warnings_;
};

uint32_t crc32_of(const uint8_t* data, size_t length);

// True when `path` is a zip archive (by its signature, not its extension).
bool is_zip_file(const std::string& path);

// Reads `path` when it is a plain ROM file (not a directory and not a zip), for
// drivers that also accept a single ROM image instead of a set.
bool read_plain_rom(const std::string& path, std::vector<uint8_t>& out);

}  // namespace dsp
