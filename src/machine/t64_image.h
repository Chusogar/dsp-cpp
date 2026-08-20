#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace dsp {

// Commodore tape archive (.T64): a directory of memory snapshots, in practice
// always PRG files.
struct T64File {
    std::string name;      // trailing spaces/$20 padding removed
    uint16_t start = 0;    // load address
    uint16_t end = 0;      // address after the last byte
    uint32_t offset = 0;   // file offset of the data
};

class T64Image {
public:
    bool load_file(const std::string& path, std::string* error = nullptr);
    bool load_memory(const uint8_t* data, size_t size, std::string* error = nullptr);

    bool open() const { return open_; }
    const std::string& tape_name() const { return tape_name_; }
    const std::vector<T64File>& directory() const { return dir_; }

    // PRG contents (load address followed by the data) of a directory entry.
    bool load_prg(size_t index, std::vector<uint8_t>* out,
                  std::string* error = nullptr) const;
    bool load_first_prg(std::vector<uint8_t>* out,
                        std::string* error = nullptr) const;

private:
    bool parse_directory(std::string* error);

    bool open_ = false;
    std::string tape_name_;
    std::vector<uint8_t> data_;
    std::vector<T64File> dir_;
};

}  // namespace dsp
