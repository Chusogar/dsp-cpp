#pragma once

// ROMs, disks, tapes and cartridges compiled into the executable as C arrays
// (like galagino's romconv): tools/embed_media.py writes a .c file with one
// array per file, the table below and the command line to start them with.
// Builds without embedded media link src/core/embedded_media_none.c.

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct DspEmbeddedFile {
    const char* path;           // relative path, e.g. "blockout.zip" or "rom/kernal.rom"
    const unsigned char* data;
    size_t size;
} DspEmbeddedFile;

extern const DspEmbeddedFile dsp_embedded_files[];
extern const size_t dsp_embedded_file_count;
// Command line (without the program name), NULL terminated. "@/" at the start
// of an argument stands for the directory the files are written to.
extern const char* const dsp_embedded_args[];

#ifdef __cplusplus
}

#include <string>
#include <vector>

namespace dsp {

// Writes the embedded files under `dir` (created if needed) and returns the
// embedded command line in `args`, with "@/" replaced by `dir` + "/".
bool extract_embedded_media(const DspEmbeddedFile* files, size_t count, const char* const* embedded_args,
                            const std::string& dir, std::vector<std::string>* args, std::string* error);

}  // namespace dsp
#endif
