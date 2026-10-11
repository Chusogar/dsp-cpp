#pragma once

// File-system access for the whole emulator: `dsp::fsx` is std::filesystem on
// desktop and the web, and a small POSIX implementation of the subset dsp uses
// when DSP_FS_POSIX is defined. Microcontroller toolchains (Xtensa GCC 8 of
// the ESP32, arm-none-eabi newlib) ship <filesystem> without the library
// behind it, but they do have stat/opendir/mkdir/unlink on top of their FAT or
// SD card layers, which is all this needs.
//
// Code uses it as:   namespace fs = dsp::fsx;

#if !defined(DSP_FS_POSIX)

#include <filesystem>

namespace dsp {
namespace fsx = std::filesystem;
}  // namespace dsp

#else

#include <cstdint>
#include <memory>
#include <string>
#include <system_error>
#include <vector>

namespace dsp {
namespace fsx {

class path {
public:
    using value_type = char;
    using string_type = std::string;
    static constexpr char preferred_separator = '/';

    path() = default;
    path(const std::string& s) : s_(s) {}
    path(std::string&& s) : s_(std::move(s)) {}
    path(const char* s) : s_(s ? s : "") {}
    template <class It>
    path(It first, It last) : s_(first, last) {}

    const std::string& native() const noexcept { return s_; }
    const char* c_str() const noexcept { return s_.c_str(); }
    std::string string() const { return s_; }
    std::string u8string() const { return s_; }
    std::string generic_string() const { return s_; }
    operator std::string() const { return s_; }

    bool empty() const noexcept { return s_.empty(); }
    bool is_absolute() const { return !s_.empty() && s_[0] == '/'; }
    bool is_relative() const { return !is_absolute(); }

    path filename() const;
    path parent_path() const;
    path extension() const;
    path stem() const;
    bool has_filename() const { return !filename().empty(); }
    bool has_parent_path() const { return !parent_path().empty(); }
    bool has_extension() const { return !extension().empty(); }

    path& replace_extension(const path& ext = path());
    path& remove_filename();
    path& replace_filename(const path& name);

    path& operator/=(const path& rhs);
    path& operator+=(const path& rhs) { s_ += rhs.s_; return *this; }
    path& operator+=(const std::string& rhs) { s_ += rhs; return *this; }
    path& operator+=(const char* rhs) { s_ += rhs; return *this; }
    path& operator+=(char rhs) { s_ += rhs; return *this; }

    int compare(const path& other) const { return s_.compare(other.s_); }
    void clear() noexcept { s_.clear(); }

private:
    std::string s_;
};

inline path operator/(const path& a, const path& b) { path r(a); r /= b; return r; }
inline bool operator==(const path& a, const path& b) { return a.compare(b) == 0; }
inline bool operator!=(const path& a, const path& b) { return a.compare(b) != 0; }
inline bool operator<(const path& a, const path& b) { return a.compare(b) < 0; }

class filesystem_error : public std::system_error {
public:
    filesystem_error(const std::string& what, const path& p, std::error_code ec)
        : std::system_error(ec, what + ": " + p.string()), path1_(p) {}
    const path& path1() const noexcept { return path1_; }

private:
    path path1_;
};

class directory_entry {
public:
    directory_entry() = default;
    explicit directory_entry(const fsx::path& p) : path_(p) {}
    const fsx::path& path() const noexcept { return path_; }
    operator const fsx::path&() const noexcept { return path_; }
    bool exists() const;
    bool exists(std::error_code& ec) const;
    bool is_regular_file() const;
    bool is_regular_file(std::error_code& ec) const;
    bool is_directory() const;
    bool is_directory(std::error_code& ec) const;
    std::uintmax_t file_size() const;
    std::uintmax_t file_size(std::error_code& ec) const;

private:
    fsx::path path_;
};

// Reads the whole directory when constructed (dsp only walks small ROM
// folders), so copies share the listing like the standard iterator.
class directory_iterator {
public:
    using value_type = directory_entry;
    using difference_type = std::ptrdiff_t;
    using pointer = const directory_entry*;
    using reference = const directory_entry&;
    using iterator_category = std::input_iterator_tag;

    directory_iterator() noexcept = default;
    explicit directory_iterator(const path& p);
    directory_iterator(const path& p, std::error_code& ec);

    const directory_entry& operator*() const { return (*entries_)[index_]; }
    const directory_entry* operator->() const { return &(*entries_)[index_]; }
    directory_iterator& operator++();
    directory_iterator& increment(std::error_code& ec) { ec.clear(); return ++*this; }
    bool operator==(const directory_iterator& o) const { return at_end() == o.at_end() && (at_end() || index_ == o.index_); }
    bool operator!=(const directory_iterator& o) const { return !(*this == o); }

private:
    bool open(const path& p, std::error_code& ec);
    bool at_end() const { return !entries_ || index_ >= entries_->size(); }
    std::shared_ptr<std::vector<directory_entry>> entries_;
    size_t index_ = 0;
};

inline directory_iterator begin(directory_iterator it) noexcept { return it; }
inline directory_iterator end(const directory_iterator&) noexcept { return directory_iterator(); }

bool exists(const path& p);
bool exists(const path& p, std::error_code& ec) noexcept;
bool is_regular_file(const path& p);
bool is_regular_file(const path& p, std::error_code& ec) noexcept;
bool is_directory(const path& p);
bool is_directory(const path& p, std::error_code& ec) noexcept;
std::uintmax_t file_size(const path& p);
std::uintmax_t file_size(const path& p, std::error_code& ec) noexcept;
bool create_directory(const path& p, std::error_code& ec) noexcept;
bool create_directories(const path& p);
bool create_directories(const path& p, std::error_code& ec);
bool remove(const path& p);
bool remove(const path& p, std::error_code& ec) noexcept;
std::uintmax_t remove_all(const path& p);
std::uintmax_t remove_all(const path& p, std::error_code& ec);
void rename(const path& from, const path& to);
void rename(const path& from, const path& to, std::error_code& ec) noexcept;
path temp_directory_path();
path temp_directory_path(std::error_code& ec);
path current_path();
path absolute(const path& p);
path absolute(const path& p, std::error_code& ec);

}  // namespace fsx
}  // namespace dsp

#endif
