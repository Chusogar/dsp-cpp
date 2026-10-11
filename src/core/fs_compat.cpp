// POSIX implementation of dsp::fsx (see fs_compat.h). Compiled to nothing
// unless DSP_FS_POSIX is defined.
#include "core/fs_compat.h"

#if defined(DSP_FS_POSIX)

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>

#if defined(DSP_FS_NO_EXCEPTIONS) || !(defined(__cpp_exceptions) || defined(__EXCEPTIONS))
#define DSP_FS_THROW(what, p, ec) std::abort()
#else
#define DSP_FS_THROW(what, p, ec) throw filesystem_error(what, p, ec)
#endif

namespace dsp {
namespace fsx {

namespace {

std::error_code last_error() { return std::error_code(errno, std::generic_category()); }

bool stat_path(const path& p, struct stat& st, std::error_code& ec) {
    if (::stat(p.c_str(), &st) == 0) {
        ec.clear();
        return true;
    }
    ec = last_error();
    return false;
}

bool missing(const std::error_code& ec) {
    return ec.value() == ENOENT || ec.value() == ENOTDIR;
}

}  // namespace

// --- path -------------------------------------------------------------------

path path::filename() const {
    if (s_.empty() || s_.back() == '/') return path();
    const size_t slash = s_.rfind('/');
    return slash == std::string::npos ? *this : path(s_.substr(slash + 1));
}

path path::parent_path() const {
    if (s_.empty()) return path();
    if (s_.find_first_not_of('/') == std::string::npos) return path("/");
    size_t end;
    if (s_.back() == '/') {
        end = s_.size() - 1;  // "a/b/" -> "a/b": the empty filename goes
    } else {
        const size_t slash = s_.rfind('/');
        if (slash == std::string::npos) return path();
        end = slash;
    }
    while (end > 0 && s_[end - 1] == '/') --end;
    if (end == 0) return s_[0] == '/' ? path("/") : path();
    return path(s_.substr(0, end));
}

path path::extension() const {
    const std::string name = filename().s_;
    if (name == "." || name == "..") return path();
    const size_t dot = name.rfind('.');
    if (dot == std::string::npos || dot == 0) return path();
    return path(name.substr(dot));
}

path path::stem() const {
    const std::string name = filename().s_;
    const std::string ext = extension().s_;
    return path(name.substr(0, name.size() - ext.size()));
}

path& path::replace_extension(const path& ext) {
    const std::string old = extension().s_;
    s_.resize(s_.size() - old.size());
    if (!ext.empty()) {
        if (ext.s_[0] != '.') s_ += '.';
        s_ += ext.s_;
    }
    return *this;
}

path& path::remove_filename() {
    s_.resize(s_.size() - filename().s_.size());
    return *this;
}

path& path::replace_filename(const path& name) {
    remove_filename();
    return *this /= name;
}

path& path::operator/=(const path& rhs) {
    if (rhs.is_absolute()) {
        s_ = rhs.s_;
        return *this;
    }
    if (!s_.empty() && s_.back() != '/') s_ += '/';
    s_ += rhs.s_;
    return *this;
}

// --- directory_entry ----------------------------------------------------------

bool directory_entry::exists() const { return fsx::exists(path_); }
bool directory_entry::exists(std::error_code& ec) const { return fsx::exists(path_, ec); }
bool directory_entry::is_regular_file() const { return fsx::is_regular_file(path_); }
bool directory_entry::is_regular_file(std::error_code& ec) const { return fsx::is_regular_file(path_, ec); }
bool directory_entry::is_directory() const { return fsx::is_directory(path_); }
bool directory_entry::is_directory(std::error_code& ec) const { return fsx::is_directory(path_, ec); }
std::uintmax_t directory_entry::file_size() const { return fsx::file_size(path_); }
std::uintmax_t directory_entry::file_size(std::error_code& ec) const { return fsx::file_size(path_, ec); }

// --- directory_iterator -------------------------------------------------------

bool directory_iterator::open(const path& p, std::error_code& ec) {
    DIR* dir = ::opendir(p.c_str());
    if (!dir) {
        ec = last_error();
        return false;
    }
    auto entries = std::make_shared<std::vector<directory_entry>>();
    while (const dirent* e = ::readdir(dir)) {
        const std::string name = e->d_name;
        if (name == "." || name == "..") continue;
        entries->emplace_back(p / name);
    }
    ::closedir(dir);
    ec.clear();
    if (!entries->empty()) entries_ = std::move(entries);
    return true;
}

directory_iterator::directory_iterator(const path& p) {
    std::error_code ec;
    if (!open(p, ec)) DSP_FS_THROW("directory_iterator", p, ec);
}

directory_iterator::directory_iterator(const path& p, std::error_code& ec) { open(p, ec); }

directory_iterator& directory_iterator::operator++() {
    if (entries_ && ++index_ >= entries_->size()) {
        entries_.reset();
        index_ = 0;
    }
    return *this;
}

// --- operations ---------------------------------------------------------------

bool exists(const path& p, std::error_code& ec) noexcept {
    struct stat st;
    if (stat_path(p, st, ec)) return true;
    if (missing(ec)) ec.clear();
    return false;
}

bool exists(const path& p) {
    std::error_code ec;
    const bool r = exists(p, ec);
    if (ec) DSP_FS_THROW("exists", p, ec);
    return r;
}

bool is_regular_file(const path& p, std::error_code& ec) noexcept {
    struct stat st;
    if (stat_path(p, st, ec)) return S_ISREG(st.st_mode);
    if (missing(ec)) ec.clear();
    return false;
}

bool is_regular_file(const path& p) {
    std::error_code ec;
    const bool r = is_regular_file(p, ec);
    if (ec) DSP_FS_THROW("is_regular_file", p, ec);
    return r;
}

bool is_directory(const path& p, std::error_code& ec) noexcept {
    struct stat st;
    if (stat_path(p, st, ec)) return S_ISDIR(st.st_mode);
    if (missing(ec)) ec.clear();
    return false;
}

bool is_directory(const path& p) {
    std::error_code ec;
    const bool r = is_directory(p, ec);
    if (ec) DSP_FS_THROW("is_directory", p, ec);
    return r;
}

std::uintmax_t file_size(const path& p, std::error_code& ec) noexcept {
    struct stat st;
    if (!stat_path(p, st, ec)) return static_cast<std::uintmax_t>(-1);
    if (!S_ISREG(st.st_mode)) {
        ec = std::make_error_code(std::errc::is_a_directory);
        return static_cast<std::uintmax_t>(-1);
    }
    return static_cast<std::uintmax_t>(st.st_size);
}

std::uintmax_t file_size(const path& p) {
    std::error_code ec;
    const std::uintmax_t r = file_size(p, ec);
    if (ec) DSP_FS_THROW("file_size", p, ec);
    return r;
}

bool create_directory(const path& p, std::error_code& ec) noexcept {
    if (::mkdir(p.c_str(), 0777) == 0) {
        ec.clear();
        return true;
    }
    const std::error_code err = last_error();
    if (is_directory(p, ec)) {
        ec.clear();
        return false;
    }
    ec = err;
    return false;
}

bool create_directories(const path& p, std::error_code& ec) {
    ec.clear();
    if (p.empty()) return false;
    if (is_directory(p, ec)) return false;
    std::string s = p.string();
    while (s.size() > 1 && s.back() == '/') s.pop_back();
    bool created = false;
    for (size_t i = 1; i <= s.size(); ++i) {
        if (i != s.size() && s[i] != '/') continue;
        const path part(s.substr(0, i));
        if (is_directory(part, ec)) continue;
        if (ec) return false;
        created = create_directory(part, ec);
        if (ec) return false;
    }
    return created;
}

bool create_directories(const path& p) {
    std::error_code ec;
    const bool r = create_directories(p, ec);
    if (ec) DSP_FS_THROW("create_directories", p, ec);
    return r;
}

bool remove(const path& p, std::error_code& ec) noexcept {
    ec.clear();
    if (is_directory(p, ec)) {
        if (::rmdir(p.c_str()) == 0) return true;
    } else if (::unlink(p.c_str()) == 0) {
        return true;
    }
    const std::error_code err = last_error();
    if (missing(err)) return false;
    ec = err;
    return false;
}

bool remove(const path& p) {
    std::error_code ec;
    const bool r = remove(p, ec);
    if (ec) DSP_FS_THROW("remove", p, ec);
    return r;
}

std::uintmax_t remove_all(const path& p, std::error_code& ec) {
    ec.clear();
    std::uintmax_t count = 0;
    if (is_directory(p, ec)) {
        for (directory_iterator it(p, ec), end; !ec && it != end; ++it) {
            count += remove_all(it->path(), ec);
            if (ec) return static_cast<std::uintmax_t>(-1);
        }
        if (ec) return static_cast<std::uintmax_t>(-1);
    }
    if (remove(p, ec)) ++count;
    return ec ? static_cast<std::uintmax_t>(-1) : count;
}

std::uintmax_t remove_all(const path& p) {
    std::error_code ec;
    const std::uintmax_t r = remove_all(p, ec);
    if (ec) DSP_FS_THROW("remove_all", p, ec);
    return r;
}

void rename(const path& from, const path& to, std::error_code& ec) noexcept {
    if (std::rename(from.c_str(), to.c_str()) == 0) ec.clear();
    else ec = last_error();
}

void rename(const path& from, const path& to) {
    std::error_code ec;
    rename(from, to, ec);
    if (ec) DSP_FS_THROW("rename", from, ec);
}

path temp_directory_path(std::error_code& ec) {
    ec.clear();
    for (const char* var : {"TMPDIR", "TMP", "TEMP", "TEMPDIR"}) {
        if (const char* v = std::getenv(var)) {
            if (*v) return path(v);
        }
    }
#if defined(DSP_FS_TEMP_DIR)
    return path(DSP_FS_TEMP_DIR);
#else
    return path("/tmp");
#endif
}

path temp_directory_path() {
    std::error_code ec;
    return temp_directory_path(ec);
}

path current_path() {
    char buffer[512];
    if (::getcwd(buffer, sizeof buffer)) return path(buffer);
    return path("/");
}

path absolute(const path& p, std::error_code& ec) {
    ec.clear();
    return p.is_absolute() ? p : current_path() / p;
}

path absolute(const path& p) { return p.is_absolute() ? p : current_path() / p; }

}  // namespace fsx
}  // namespace dsp

#endif
