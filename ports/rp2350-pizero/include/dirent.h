#pragma once

// <dirent.h> for the RP2350 port: newlib for arm-none-eabi has none. The
// functions are in src/posix_io.cpp, over FatFs (/sd) and the embedded
// files (/embedded).

#ifdef __cplusplus
extern "C" {
#endif

struct dirent {
    unsigned char d_type;
    char d_name[256];
};

#define DT_UNKNOWN 0
#define DT_DIR 4
#define DT_REG 8

struct dsp_dir;
#ifndef DSP_NO_POSIX_DIR_TYPEDEF  // posix_io.cpp: FatFs has its own DIR
typedef struct dsp_dir DIR;
#endif

struct dsp_dir* opendir(const char* path);
struct dirent* readdir(struct dsp_dir* dir);
int closedir(struct dsp_dir* dir);

#ifdef __cplusplus
}
#endif
