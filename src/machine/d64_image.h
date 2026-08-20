#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace dsp {

// Commodore 1541 disk image (.D64), 35 tracks.
struct D64File {
    std::string name;   // PETSCII converted to ASCII-ish
    uint8_t type = 0;   // low 4 bits: 0x02 PRG etc.
    uint8_t track = 0, sector = 0;
    uint16_t blocks = 0;
};

// A PRG (load address followed by data) to place on a freshly built image.
struct D64BuildFile {
    std::string name;
    std::vector<uint8_t> prg;
};

// Format a 35 track image holding the given files, so archives without a disk
// structure of their own (.T64) can still be served by the emulated drive.
// Returns an empty vector if the files do not fit.
std::vector<uint8_t> build_d64(const std::vector<D64BuildFile>& files,
                               const std::string& disk_name,
                               uint8_t id1 = 0x41, uint8_t id2 = 0x42);

class D64Image {
public:
    bool load_file(const std::string& path, std::string* error = nullptr);
    bool load_memory(const uint8_t* data, size_t size, std::string* error = nullptr);

    bool open() const { return open_; }
    uint8_t disk_id1() const { return id1_; }
    uint8_t disk_id2() const { return id2_; }
    const std::vector<D64File>& directory() const { return dir_; }

    // Read a sector (256 bytes). Track 1..35.
    bool read_sector(uint8_t track, uint8_t sector, uint8_t out[256]) const;
    // Error map byte (1=OK). 0 if no error map.
    uint8_t sector_error(uint8_t track, uint8_t sector) const;
    bool has_error_map() const { return !errors_.empty(); }

    // Load a PRG file by directory index into memory image (load addr + data).
    bool load_prg(int index, std::vector<uint8_t>* out, std::string* error = nullptr) const;
    // First PRG in directory.
    bool load_first_prg(std::vector<uint8_t>* out, std::string* error = nullptr) const;

    static int sectors_per_track(int track);

private:
    bool parse_directory();
    std::string petscii_name(const uint8_t* p, int n) const;

    bool open_ = false;
    std::vector<uint8_t> data_;
    std::vector<D64File> dir_;
    uint8_t id1_ = 0x41, id2_ = 0x42;
    std::vector<uint8_t> errors_;  // 683 entries if present
};

}  // namespace dsp
