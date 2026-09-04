#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace dsp {

// One file (or the synthetic root directory) extracted from a QLWA volume.
struct QlWinFile {
    std::string name;
    uint8_t type = 0;
    uint32_t dataspace = 0;
    std::array<uint8_t, 64> header{};
    std::vector<uint8_t> data;

    uint32_t logical_size() const { return 64 + uint32_t(data.size()); }
    uint8_t byte_at(uint32_t pos) const;
};

// QXL.WIN / QLWA hard-disk image (QXL, QL-SD, SMSQ/E WIN volumes).
class QlWin {
public:
    void reset();
    bool load_file(const std::string& path, std::string* error);
    bool load_image(const std::vector<uint8_t>& image, std::string* error);
    bool loaded() const { return loaded_; }

    const std::string& medium_name() const { return medium_; }
    const std::vector<QlWinFile>& files() const { return files_; }
    const QlWinFile& directory() const { return directory_; }
    uint32_t total_sectors() const { return total_sectors_; }
    uint32_t empty_sectors() const { return empty_sectors_; }

    const QlWinFile* find(const std::string& name) const;

private:
    bool parse(const std::vector<uint8_t>& image, std::string* error);

    bool loaded_ = false;
    std::string medium_;
    std::vector<QlWinFile> files_;
    QlWinFile directory_;
    uint32_t total_sectors_ = 0;
    uint32_t empty_sectors_ = 0;
};

bool is_ql_win_file(const std::string& path);
void install_ql_win_rom(uint8_t* dest);

}  // namespace dsp
