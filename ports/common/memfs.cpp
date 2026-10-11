#include "memfs.h"

#include <fcntl.h>

#include <cerrno>
#include <cstring>
#include <string>
#include <vector>

namespace dsp {
namespace port {
namespace memfs {

namespace {

const DspEmbeddedFile* g_files = nullptr;
size_t g_count = 0;
std::string g_prefix;

constexpr int kMaxOpen = 8;
struct OpenFile {
    const DspEmbeddedFile* file = nullptr;
    size_t pos = 0;
};
OpenFile g_open[kMaxOpen];

constexpr int kMaxDirs = 4;
struct OpenDir {
    bool used = false;
    std::vector<std::string> names;
    std::vector<bool> dirs;
    size_t next = 0;
};
OpenDir g_dirs[kMaxDirs];

std::string clean(const char* rel) {
    std::string s = rel ? rel : "";
    while (!s.empty() && s[0] == '/') s.erase(0, 1);
    while (!s.empty() && s.back() == '/') s.pop_back();
    if (s == ".") s.clear();
    return s;
}

const DspEmbeddedFile* find_file(const std::string& rel) {
    for (size_t i = 0; i < g_count; ++i)
        if (g_files[i].path && rel == g_files[i].path) return &g_files[i];
    return nullptr;
}

bool is_dir(const std::string& rel) {
    if (rel.empty()) return true;
    const std::string head = rel + "/";
    for (size_t i = 0; i < g_count; ++i)
        if (g_files[i].path && std::strncmp(g_files[i].path, head.c_str(), head.size()) == 0) return true;
    return false;
}

void fill_stat(struct stat* st, bool dir, size_t size) {
    std::memset(st, 0, sizeof *st);
    st->st_mode = dir ? (S_IFDIR | 0555) : (S_IFREG | 0444);
    st->st_size = off_t(size);
    st->st_nlink = 1;
}

}  // namespace

void mount(const DspEmbeddedFile* files, size_t count, const char* prefix) {
    g_files = files;
    g_count = count;
    g_prefix = prefix ? prefix : "";
    while (!g_prefix.empty() && g_prefix.back() == '/') g_prefix.pop_back();
}

const char* prefix() { return g_prefix.c_str(); }

const char* relative(const char* path) {
    if (!path || g_prefix.empty()) return nullptr;
    const size_t n = g_prefix.size();
    if (std::strncmp(path, g_prefix.c_str(), n) != 0) return nullptr;
    if (path[n] != '\0' && path[n] != '/') return nullptr;
    return path + n;
}

bool owns(const char* path) { return relative(path) != nullptr; }

int open(const char* rel, int flags) {
    if ((flags & O_ACCMODE) != O_RDONLY || (flags & (O_CREAT | O_TRUNC))) {
        errno = EROFS;
        return -1;
    }
    const std::string r = clean(rel);
    const DspEmbeddedFile* f = find_file(r);
    if (!f) {
        errno = is_dir(r) ? EISDIR : ENOENT;
        return -1;
    }
    for (int fd = 0; fd < kMaxOpen; ++fd) {
        if (!g_open[fd].file) {
            g_open[fd].file = f;
            g_open[fd].pos = 0;
            return fd;
        }
    }
    errno = EMFILE;
    return -1;
}

static OpenFile* slot(int fd) {
    if (fd < 0 || fd >= kMaxOpen || !g_open[fd].file) {
        errno = EBADF;
        return nullptr;
    }
    return &g_open[fd];
}

int close(int fd) {
    OpenFile* f = slot(fd);
    if (!f) return -1;
    f->file = nullptr;
    return 0;
}

long read(int fd, void* buffer, size_t size) {
    OpenFile* f = slot(fd);
    if (!f) return -1;
    const size_t left = f->pos < f->file->size ? f->file->size - f->pos : 0;
    const size_t n = size < left ? size : left;
    std::memcpy(buffer, f->file->data + f->pos, n);
    f->pos += n;
    return long(n);
}

long lseek(int fd, long offset, int whence) {
    OpenFile* f = slot(fd);
    if (!f) return -1;
    long base = 0;
    if (whence == SEEK_CUR) base = long(f->pos);
    else if (whence == SEEK_END) base = long(f->file->size);
    else if (whence != SEEK_SET) {
        errno = EINVAL;
        return -1;
    }
    if (base + offset < 0) {
        errno = EINVAL;
        return -1;
    }
    f->pos = size_t(base + offset);
    return long(f->pos);
}

int fstat(int fd, struct stat* st) {
    OpenFile* f = slot(fd);
    if (!f) return -1;
    fill_stat(st, false, f->file->size);
    return 0;
}

int stat(const char* rel, struct stat* st) {
    const std::string r = clean(rel);
    if (const DspEmbeddedFile* f = find_file(r)) {
        fill_stat(st, false, f->size);
        return 0;
    }
    if (is_dir(r)) {
        fill_stat(st, true, 0);
        return 0;
    }
    errno = ENOENT;
    return -1;
}

int opendir(const char* rel) {
    const std::string r = clean(rel);
    if (!is_dir(r)) {
        errno = find_file(r) ? ENOTDIR : ENOENT;
        return -1;
    }
    for (int h = 0; h < kMaxDirs; ++h) {
        OpenDir& d = g_dirs[h];
        if (d.used) continue;
        d = OpenDir();
        d.used = true;
        const std::string head = r.empty() ? "" : r + "/";
        for (size_t i = 0; i < g_count; ++i) {
            const char* p = g_files[i].path;
            if (!p || std::strncmp(p, head.c_str(), head.size()) != 0) continue;
            std::string rest = p + head.size();
            const size_t slash = rest.find('/');
            const bool dir = slash != std::string::npos;
            if (dir) rest.resize(slash);
            bool seen = false;
            for (const std::string& n : d.names) seen = seen || n == rest;
            if (!seen && !rest.empty()) {
                d.names.push_back(rest);
                d.dirs.push_back(dir);
            }
        }
        return h;
    }
    errno = EMFILE;
    return -1;
}

bool readdir(int handle, const char** name, bool* dir) {
    if (handle < 0 || handle >= kMaxDirs || !g_dirs[handle].used) return false;
    OpenDir& d = g_dirs[handle];
    if (d.next >= d.names.size()) return false;
    *name = d.names[d.next].c_str();
    *dir = d.dirs[d.next];
    ++d.next;
    return true;
}

void closedir(int handle) {
    if (handle >= 0 && handle < kMaxDirs) g_dirs[handle] = OpenDir();
}

}  // namespace memfs
}  // namespace port
}  // namespace dsp
