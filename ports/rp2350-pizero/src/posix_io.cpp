// newlib system calls and <dirent.h> for the RP2350 port, so the normal dsp
// code (std::ifstream, fopen, dsp::fsx in fs_compat.cpp) can read files:
//   /sd/...        the micro-SD card, through FatFs
//   /embedded/...  the media compiled into the firmware (ports/common/memfs)
// stdin/stdout/stderr keep going to the SDK's stdio (UART).
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define DSP_NO_POSIX_DIR_TYPEDEF 1
#include <dirent.h>

extern "C" {
#include "ff.h"
#include "pico/stdio.h"
#include "pico/time.h"
}

#include "memfs.h"
#include "posix_io.h"

namespace memfs = dsp::port::memfs;

namespace {

constexpr int kMemBase = 10;   // fds 10..17: embedded files
constexpr int kFatBase = 20;   // fds 20..27: FatFs files
constexpr int kMaxFat = 8;

FATFS g_fatfs;
bool g_mounted = false;
FIL g_files[kMaxFat];
bool g_used[kMaxFat];

int set_errno(FRESULT r) {
    switch (r) {
        case FR_OK: return 0;
        case FR_NO_FILE:
        case FR_NO_PATH:
        case FR_INVALID_NAME: errno = ENOENT; break;
        case FR_DENIED: errno = EACCES; break;
        case FR_EXIST: errno = EEXIST; break;
        case FR_WRITE_PROTECTED: errno = EROFS; break;
        case FR_NOT_READY:
        case FR_NOT_ENABLED:
        case FR_NO_FILESYSTEM: errno = ENODEV; break;
        case FR_TOO_MANY_OPEN_FILES: errno = EMFILE; break;
        case FR_INVALID_PARAMETER: errno = EINVAL; break;
        default: errno = EIO; break;
    }
    return -1;
}

// "/sd/roms/x.zip" -> "/roms/x.zip" (FatFs path); nullptr if not on the card.
const char* sd_path(const char* path) {
    if (!path || strncmp(path, "/sd", 3) != 0 || (path[3] != '\0' && path[3] != '/')) return nullptr;
    return path[3] ? path + 3 : "/";
}

FIL* fat_file(int fd) {
    const int i = fd - kFatBase;
    if (i < 0 || i >= kMaxFat || !g_used[i]) {
        errno = EBADF;
        return nullptr;
    }
    return &g_files[i];
}

void fill_stat(struct stat* st, bool dir, FSIZE_t size) {
    memset(st, 0, sizeof *st);
    st->st_mode = dir ? (S_IFDIR | 0777) : (S_IFREG | 0666);
    st->st_size = off_t(size);
    st->st_nlink = 1;
    st->st_blksize = 512;
}

}  // namespace

bool dsp_sd_mount() {
    if (!g_mounted) g_mounted = f_mount(&g_fatfs, "", 1) == FR_OK;
    return g_mounted;
}

bool dsp_sd_mounted() { return g_mounted; }

struct dsp_dir {
    bool mem = false;
    int mem_handle = -1;
    ::DIR fat;
    struct dirent entry;
};

extern "C" {

int _open(const char* path, int flags, ...) {
    if (const char* rel = memfs::relative(path)) {
        const int fd = memfs::open(rel, flags);
        return fd < 0 ? -1 : kMemBase + fd;
    }
    const char* p = sd_path(path);
    if (!p || !g_mounted) {
        errno = ENOENT;
        return -1;
    }
    int slot = -1;
    for (int i = 0; i < kMaxFat; ++i)
        if (!g_used[i]) {
            slot = i;
            break;
        }
    if (slot < 0) {
        errno = EMFILE;
        return -1;
    }
    BYTE mode = 0;
    switch (flags & O_ACCMODE) {
        case O_RDONLY: mode = FA_READ; break;
        case O_WRONLY: mode = FA_WRITE; break;
        default: mode = FA_READ | FA_WRITE; break;
    }
    if (flags & O_CREAT) {
        if (flags & O_EXCL) mode |= FA_CREATE_NEW;
        else if (flags & O_TRUNC) mode |= FA_CREATE_ALWAYS;
        else mode |= FA_OPEN_ALWAYS;
    } else if (flags & O_TRUNC) {
        mode |= FA_CREATE_ALWAYS;
    }
    if (flags & O_APPEND) mode |= FA_OPEN_APPEND;
    const FRESULT r = f_open(&g_files[slot], p, mode);
    if (r != FR_OK) return set_errno(r);
    g_used[slot] = true;
    return kFatBase + slot;
}

int _close(int fd) {
    if (fd >= kMemBase && fd < kFatBase) return memfs::close(fd - kMemBase);
    FIL* f = fat_file(fd);
    if (!f) return -1;
    g_used[fd - kFatBase] = false;
    return f_close(f) == FR_OK ? 0 : -1;
}

int _read(int fd, char* buffer, int length) {
    if (fd == 0) return stdio_get_until(buffer, length, at_the_end_of_time);
    if (fd >= kMemBase && fd < kFatBase) return int(memfs::read(fd - kMemBase, buffer, size_t(length)));
    FIL* f = fat_file(fd);
    if (!f) return -1;
    UINT got = 0;
    const FRESULT r = f_read(f, buffer, UINT(length), &got);
    return r == FR_OK ? int(got) : set_errno(r);
}

int _write(int fd, char* buffer, int length) {
    if (fd == 1 || fd == 2) {
        stdio_put_string(buffer, length, false, true);
        return length;
    }
    if (fd >= kMemBase && fd < kFatBase) {
        errno = EROFS;
        return -1;
    }
    FIL* f = fat_file(fd);
    if (!f) return -1;
    UINT put = 0;
    const FRESULT r = f_write(f, buffer, UINT(length), &put);
    return r == FR_OK ? int(put) : set_errno(r);
}

off_t _lseek(int fd, off_t pos, int whence) {
    if (fd >= kMemBase && fd < kFatBase) return off_t(memfs::lseek(fd - kMemBase, long(pos), whence));
    FIL* f = fat_file(fd);
    if (!f) return -1;
    FSIZE_t base = 0;
    if (whence == SEEK_CUR) base = f_tell(f);
    else if (whence == SEEK_END) base = f_size(f);
    else if (whence != SEEK_SET) {
        errno = EINVAL;
        return -1;
    }
    if (pos < 0 && FSIZE_t(-pos) > base) {
        errno = EINVAL;
        return -1;
    }
    const FRESULT r = f_lseek(f, base + FSIZE_t(pos));
    return r == FR_OK ? off_t(f_tell(f)) : set_errno(r);
}

int _fstat(int fd, struct stat* st) {
    if (fd >= 0 && fd <= 2) {
        memset(st, 0, sizeof *st);
        st->st_mode = S_IFCHR;
        return 0;
    }
    if (fd >= kMemBase && fd < kFatBase) return memfs::fstat(fd - kMemBase, st);
    FIL* f = fat_file(fd);
    if (!f) return -1;
    fill_stat(st, false, f_size(f));
    return 0;
}

int _isatty(int fd) { return fd >= 0 && fd <= 2; }

int _stat(const char* path, struct stat* st) {
    if (const char* rel = memfs::relative(path)) return memfs::stat(rel, st);
    if (path && (strcmp(path, "/") == 0)) {
        fill_stat(st, true, 0);
        return 0;
    }
    const char* p = sd_path(path);
    if (!p || !g_mounted) {
        errno = ENOENT;
        return -1;
    }
    if (strcmp(p, "/") == 0) {
        fill_stat(st, true, 0);
        return 0;
    }
    FILINFO info;
    const FRESULT r = f_stat(p, &info);
    if (r != FR_OK) return set_errno(r);
    fill_stat(st, (info.fattrib & AM_DIR) != 0, info.fsize);
    return 0;
}

int stat(const char* path, struct stat* st) { return _stat(path, st); }

int _unlink(const char* path) {
    const char* p = sd_path(path);
    if (!p || !g_mounted) {
        errno = memfs::owns(path) ? EROFS : ENOENT;
        return -1;
    }
    const FRESULT r = f_unlink(p);
    return r == FR_OK ? 0 : set_errno(r);
}

int unlink(const char* path) { return _unlink(path); }
int rmdir(const char* path) { return _unlink(path); }

int mkdir(const char* path, mode_t) {
    const char* p = sd_path(path);
    if (!p || !g_mounted) {
        errno = memfs::owns(path) ? EROFS : ENOENT;
        return -1;
    }
    const FRESULT r = f_mkdir(p);
    return r == FR_OK ? 0 : set_errno(r);
}

int _rename(const char* from, const char* to) {
    const char* a = sd_path(from);
    const char* b = sd_path(to);
    if (!a || !b || !g_mounted) {
        errno = EXDEV;
        return -1;
    }
    f_unlink(b);  // POSIX rename replaces the target
    const FRESULT r = f_rename(a, b);
    return r == FR_OK ? 0 : set_errno(r);
}

int rename(const char* from, const char* to) { return _rename(from, to); }

int _link(const char*, const char*) {
    errno = EMLINK;
    return -1;
}

char* getcwd(char* buffer, size_t size) {
    if (!buffer || size < 2) {
        errno = ERANGE;
        return nullptr;
    }
    buffer[0] = '/';
    buffer[1] = '\0';
    return buffer;
}

struct dsp_dir* opendir(const char* path) {
    dsp_dir* d = new dsp_dir();
    if (const char* rel = memfs::relative(path)) {
        d->mem = true;
        d->mem_handle = memfs::opendir(rel);
        if (d->mem_handle < 0) {
            delete d;
            return nullptr;
        }
        return d;
    }
    const char* p = sd_path(path);
    if (!p || !g_mounted) {
        delete d;
        errno = ENOENT;
        return nullptr;
    }
    const FRESULT r = f_opendir(&d->fat, p);
    if (r != FR_OK) {
        delete d;
        set_errno(r);
        return nullptr;
    }
    return d;
}

struct dirent* readdir(struct dsp_dir* d) {
    if (!d) return nullptr;
    if (d->mem) {
        const char* name;
        bool is_dir;
        if (!memfs::readdir(d->mem_handle, &name, &is_dir)) return nullptr;
        strncpy(d->entry.d_name, name, sizeof d->entry.d_name - 1);
        d->entry.d_name[sizeof d->entry.d_name - 1] = '\0';
        d->entry.d_type = is_dir ? DT_DIR : DT_REG;
        return &d->entry;
    }
    FILINFO info;
    if (f_readdir(&d->fat, &info) != FR_OK || info.fname[0] == '\0') return nullptr;
    strncpy(d->entry.d_name, info.fname, sizeof d->entry.d_name - 1);
    d->entry.d_name[sizeof d->entry.d_name - 1] = '\0';
    d->entry.d_type = (info.fattrib & AM_DIR) ? DT_DIR : DT_REG;
    return &d->entry;
}

int closedir(struct dsp_dir* d) {
    if (!d) return -1;
    if (d->mem) memfs::closedir(d->mem_handle);
    else f_closedir(&d->fat);
    delete d;
    return 0;
}

}  // extern "C"
