#include "core/embedded_media.h"

#include <filesystem>
#include <fstream>

namespace dsp {

bool extract_embedded_media(const DspEmbeddedFile* files, size_t count, const char* const* embedded_args,
                            const std::string& dir, std::vector<std::string>* args, std::string* error) {
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::create_directories(dir, ec);
    for (size_t i = 0; i < count; ++i) {
        const std::string rel = files[i].path ? files[i].path : "";
        // Only plain relative paths: nothing may land outside `dir`.
        if (rel.empty() || rel[0] == '/' || rel.find("..") != std::string::npos) {
            if (error) *error = "bad embedded file name: " + rel;
            return false;
        }
        const fs::path target = fs::path(dir) / rel;
        fs::create_directories(target.parent_path(), ec);
        std::ofstream out(target, std::ios::binary | std::ios::trunc);
        if (out) out.write(reinterpret_cast<const char*>(files[i].data), std::streamsize(files[i].size));
        if (!out) {
            if (error) *error = "cannot write " + target.string();
            return false;
        }
    }
    if (args) {
        args->clear();
        for (const char* const* a = embedded_args; a && *a; ++a) {
            std::string value = *a;
            if (value.compare(0, 2, "@/") == 0) value = (fs::path(dir) / value.substr(2)).string();
            if (value == "@") value = dir;
            args->push_back(value);
        }
    }
    return true;
}

}  // namespace dsp
