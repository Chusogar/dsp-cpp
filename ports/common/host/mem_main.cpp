// Memory needs of each driver, to judge which ones fit a board: the size of
// the machine object (one allocation), the largest single allocation and the
// peak heap use while constructing, initialising and running it.
//
//   dsp-port-mem [--roms DIR] [--frames N] [driver...]
// DIR/NAME.zip or DIR/NAME/ is used as the ROM when present; without it only
// the constructor is measured.
#include <cstdio>
#include <cstdlib>
#include <new>
#include <string>
#include <vector>

#include "core/fs_compat.h"
#include "frontend/machine_registry.h"

namespace {
size_t g_current = 0, g_peak = 0, g_largest = 0;
bool g_track = false;
}  // namespace

void* operator new(size_t size) {
    void* p = std::malloc(size + 16);
    if (!p) throw std::bad_alloc();
    *static_cast<size_t*>(p) = size;
    if (g_track) {
        g_current += size;
        if (g_current > g_peak) g_peak = g_current;
        if (size > g_largest) g_largest = size;
    }
    return static_cast<char*>(p) + 16;
}
void operator delete(void* p) noexcept {
    if (!p) return;
    char* base = static_cast<char*>(p) - 16;
    if (g_track) g_current -= *reinterpret_cast<size_t*>(base);
    std::free(base);
}
void operator delete(void* p, size_t) noexcept { operator delete(p); }
void* operator new[](size_t size) { return operator new(size); }
void operator delete[](void* p) noexcept { operator delete(p); }
void operator delete[](void* p, size_t) noexcept { operator delete(p); }

int main(int argc, char** argv) {
    std::string roms;
    int frames = 120;
    std::vector<std::string> only;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--roms" && i + 1 < argc) roms = argv[++i];
        else if (a == "--frames" && i + 1 < argc) frames = std::atoi(argv[++i]);
        else only.push_back(a);
    }
    std::printf("%-16s %10s %10s %10s  %s\n", "driver", "object", "largest", "peak", "measured");
    for (const dsp::DriverInfo& d : dsp::list_drivers()) {
        if (!only.empty()) {
            bool want = false;
            for (const std::string& o : only) want = want || o == d.name;
            if (!want) continue;
        }
        std::string rom;
        if (!roms.empty()) {
            std::vector<std::string> names = {d.name};
            names.insert(names.end(), d.aliases.begin(), d.aliases.end());
            for (const std::string& n : names) {
                for (const std::string& c : {roms + "/" + n + ".zip", roms + "/" + n}) {
                    std::error_code ec;
                    if (rom.empty() && dsp::fsx::exists(c, ec)) rom = c;
                }
            }
        }
        g_current = g_peak = g_largest = 0;
        g_track = true;
        size_t object = 0;
        std::string what = "constructor";
        {
            size_t before = g_largest;
            std::unique_ptr<dsp::Machine> m = dsp::create_machine(d.name);
            object = g_largest > before ? g_largest : 0;
            std::string error;
            if (m && !rom.empty()) {
                if (m->init(rom, &error)) {
                    std::vector<int16_t> audio;
                    for (int f = 0; f < frames; ++f) {
                        m->run_frame();
                        audio.clear();
                        m->drain_audio(audio);
                    }
                    what = "init + " + std::to_string(frames) + " frames";
                } else {
                    what = "constructor (init failed: " + error.substr(0, 40) + ")";
                }
            }
        }
        g_track = false;
        std::printf("%-16s %10zu %10zu %10zu  %s\n", d.name.c_str(), object, g_largest, g_peak, what.c_str());
        std::fflush(stdout);
    }
    return 0;
}
