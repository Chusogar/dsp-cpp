#pragma once

// Read-only file system over the embedded media (tools/embed_media.py): the
// arrays stay in flash and are read in place, nothing is copied to RAM. The
// boards hook it under a folder ("/embedded"): the ESP32 through its VFS, the
// RP2350 through the newlib system calls (ports/rp2350-pizero/syscalls.cpp).

#include <sys/stat.h>
#include <sys/types.h>

#include <cstddef>
#include <cstdint>

#include "core/embedded_media.h"

namespace dsp {
namespace port {
namespace memfs {

// Serves `files` under `prefix` (no trailing '/'), e.g. "/embedded/blockout.zip".
void mount(const DspEmbeddedFile* files, size_t count, const char* prefix);
const char* prefix();

// True when `path` is inside the mounted folder (or is the folder).
bool owns(const char* path);

// Paths below are relative to the folder ("blockout.zip", "" = the folder);
// the functions return -1 and set errno like their POSIX namesakes.
int open(const char* rel, int flags);
int close(int fd);
long read(int fd, void* buffer, size_t size);
long lseek(int fd, long offset, int whence);
int fstat(int fd, struct stat* st);
int stat(const char* rel, struct stat* st);

// Directory listing: opendir(), readdir() until it returns false, closedir().
int opendir(const char* rel);  // handle >= 0
bool readdir(int handle, const char** name, bool* is_dir);
void closedir(int handle);

// `path` without the folder ("/embedded/a/b" -> "a/b"), or nullptr.
const char* relative(const char* path);

}  // namespace memfs
}  // namespace port
}  // namespace dsp
