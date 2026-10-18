// Headless desktop Hal for the port front end (see CMakeLists.txt here):
// a 320x240 "screen" saved as a PPM, scripted key presses, a virtual clock.
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

#include <new>

#include "core/fs_compat.h"
#include "core/rom_loader.h"
#include "memfs.h"
#include "port.h"

// --board-memory BYTES: a heap that small, as on a board without PSRAM:
// first fit over an address-ordered free list (like newlib's nano malloc),
// so a block left in the middle splits it as it would on the board.
// operator new throws when no free block is big enough; the peak and the
// largest free block are reported.
static size_t g_heap_limit = 0, g_heap_used = 0, g_heap_peak = 0;
static bool g_heap_flash = false;  // allocations standing for the board's flash
namespace arena {
struct Free {
    size_t size;  // bytes, header included
    Free* next;
};
static unsigned char* base = nullptr;
static Free* free_list = nullptr;
constexpr size_t kHeader = 16;
inline bool owns(void* p) {
    return base && static_cast<unsigned char*>(p) >= base && static_cast<unsigned char*>(p) < base + g_heap_limit;
}
void* alloc(size_t n) {
    if (!base) {
        base = static_cast<unsigned char*>(std::malloc(g_heap_limit));
        free_list = reinterpret_cast<Free*>(base);
        free_list->size = g_heap_limit;
        free_list->next = nullptr;
    }
    const size_t need = (n + kHeader + 15) & ~size_t(15);
    for (Free** link = &free_list; *link; link = &(*link)->next) {
        Free* f = *link;
        if (f->size < need) continue;
        if (f->size - need >= 32) {
            Free* rest = reinterpret_cast<Free*>(reinterpret_cast<unsigned char*>(f) + need);
            rest->size = f->size - need;
            rest->next = f->next;
            *link = rest;
            f->size = need;
        } else {
            *link = f->next;
        }
        *reinterpret_cast<size_t*>(f) = f->size;
        return reinterpret_cast<unsigned char*>(f) + kHeader;
    }
    return nullptr;
}
size_t release(void* p) {
    Free* f = reinterpret_cast<Free*>(static_cast<unsigned char*>(p) - kHeader);
    const size_t size = *reinterpret_cast<size_t*>(f);
    f->size = size;
    Free** link = &free_list;
    while (*link && *link < f) link = &(*link)->next;
    f->next = *link;
    *link = f;
    if (f->next && reinterpret_cast<unsigned char*>(f) + f->size == reinterpret_cast<unsigned char*>(f->next)) {
        f->size += f->next->size;
        f->next = f->next->next;
    }
    // Merge with the block before.
    for (Free* q = free_list; q && q != f; q = q->next)
        if (q->next == f && reinterpret_cast<unsigned char*>(q) + q->size == reinterpret_cast<unsigned char*>(f)) {
            q->size += f->size;
            q->next = f->next;
            break;
        }
    return size;
}
size_t largest() {
    size_t best = 0;
    for (Free* f = free_list; f; f = f->next) best = std::max(best, f->size);
    return best > kHeader ? best - kHeader : 0;
}
}  // namespace arena

void* operator new(size_t n) {
    if (g_heap_limit && !g_heap_flash) {
        void* p = arena::alloc(n);
        if (!p) {
            std::fprintf(stderr, "[host] no block of %zu bytes (largest free %zu)\n", n, arena::largest());
            throw std::bad_alloc();
        }
        g_heap_used += *reinterpret_cast<size_t*>(static_cast<unsigned char*>(p) - arena::kHeader);
        g_heap_peak = std::max(g_heap_peak, g_heap_used);
        if (std::getenv("DSP_HOST_BIG") && n > size_t(std::atoi(std::getenv("DSP_HOST_BIG")))) std::fprintf(stderr, "[host] new %zu (used %zu)\n", n, g_heap_used);
        return p;
    }
    void* p = std::malloc(n ? n : 1);
    if (!p) throw std::bad_alloc();
    return p;
}
void operator delete(void* p) noexcept {
    if (!p) return;
    if (arena::owns(p)) {
        g_heap_used -= arena::release(p);
        return;
    }
    std::free(p);
}
void operator delete(void* p, size_t) noexcept { operator delete(p); }

namespace {

using dsp::Key;
using dsp::port::HostInput;

struct NamedKey {
    const char* name;
    Key key;
};
const NamedKey kKeys[] = {
    {"up", Key::Up},       {"down", Key::Down},   {"left", Key::Left},       {"right", Key::Right},
    {"enter", Key::Enter}, {"space", Key::Space}, {"ctrl", Key::LeftCtrl},   {"esc", Key::Escape},
    {"f12", Key::F12},     {"1", Key::Num1},      {"5", Key::Num5},          {"z", Key::Z},
};

class HostHal : public dsp::port::Hal {
public:
    std::string root;
    std::vector<Key> script;  // keys pressed one after another
    int hold_polls = 4;       // polls each key stays down (then as many up)
    std::string out;
    std::string menu_out;
    size_t audio_samples = 0;
    int presents = 0;

    const char* board_name() const override { return "host"; }
    int screen_width() const override { return 320; }
    int screen_height() const override { return 240; }

    void present(const uint32_t* argb, int width, int height, int display_width,
                 int display_height) override {
        if (map_.update(width, height, display_width, display_height, 320, 240))
            std::fill(screen_.begin(), screen_.end(), 0);
        for (int y = 0; y < map_.dst_h; ++y) {
            const uint32_t* src = argb + size_t(map_.ys[size_t(y)]) * size_t(width);
            uint32_t* dst = &screen_[size_t(map_.dst_y + y) * 320 + size_t(map_.dst_x)];
            for (int x = 0; x < map_.dst_w; ++x) dst[x] = src[map_.xs[size_t(x)]];
        }
        ++presents;
        if (!menu_out.empty() && presents == 1) save(menu_out);
    }

    // --board-memory: the RP2040 keeps a menu screen as 4-bit pixels (38 KB)
    // until a game picture or release_menu().
    std::vector<uint8_t> board_menu;
    void release_menu() override { std::vector<uint8_t>().swap(board_menu); }

    void draw_rows(int y, int rows, const uint32_t* argb) override {
        if (board_like && board_menu.empty()) board_menu.assign(320 * 240 / 2, 0);
        std::copy(argb, argb + size_t(rows) * 320, screen_.begin() + size_t(y) * 320);
        map_.invalidate();
        if (y + rows >= 240) {  // a whole menu screen
            ++presents;
            if (!menu_out.empty() && presents == 1) save(menu_out);
        }
    }

    int output_rate() const override { return 22050; }
    void push_audio(const int16_t*, size_t count) override { audio_samples += count; }
    size_t audio_queued() const override { return 0; }

    void poll(HostInput& in) override {
        in = HostInput();
        const size_t step = size_t(polls_++ / hold_polls);
        if (step / 2 < script.size() && step % 2 == 0) in.keys[size_t(script[step / 2])] = true;
    }
    uint64_t micros() override { return clock_; }
    void sleep_us(uint32_t us) override { clock_ += us; }

    std::string storage_root() override { return root; }
    bool board_like = false;  // --board-memory: embedded media in place, cropped picture
    bool crops_picture() const override { return board_like; }
    bool present_indexed(const uint8_t* pens, const uint32_t* palette, int width, int height, int dw,
                         int dh) override {
        if (!board_like) return false;
        release_menu();
        // The board's own screen memory: not counted against --board-memory.
        g_heap_flash = true;
        static std::vector<uint32_t> argb;
        argb.resize(size_t(width) * size_t(height));
        g_heap_flash = false;
        for (size_t i = 0; i < argb.size(); ++i) argb[i] = palette[pens[i]];
        present(argb.data(), width, height, dw, dh);
        return true;
    }
    bool present_indexed4(const uint8_t* pens, const uint32_t* palette, int width, int height, int dw,
                          int dh) override {
        if (!board_like) return false;
        release_menu();
        g_heap_flash = true;
        static std::vector<uint32_t> argb;
        argb.resize(size_t(width) * size_t(height));
        g_heap_flash = false;
        const size_t stride = size_t((width + 1) / 2);
        for (int y = 0; y < height; ++y)
            for (int x = 0; x < width; ++x) {
                const uint8_t byte = pens[size_t(y) * stride + size_t(x / 2)];
                argb[size_t(y) * size_t(width) + size_t(x)] = palette[(x & 1) ? (byte >> 4) : (byte & 15)];
            }
        present(argb.data(), width, height, dw, dh);
        return true;
    }
    bool mount_embedded(const DspEmbeddedFile* files, size_t count, std::string* dir,
                        std::string* error) override {
        if (board_like) {
            // The files are read as if from flash: in place, not on the heap.
            dsp::RomLoader::set_file_mapper([](const std::string& path, size_t* size) -> const uint8_t* {
                static std::vector<std::pair<std::string, std::vector<uint8_t>>>* files = nullptr;
                g_heap_flash = true;
                if (!files) files = new std::vector<std::pair<std::string, std::vector<uint8_t>>>;
                std::string p = path;
                for (size_t k; (k = p.find("//")) != std::string::npos;) p.erase(k, 1);
                const uint8_t* data = nullptr;
                for (auto& f : *files)
                    if (f.first == p) data = f.second.data(), *size = f.second.size();
                if (!data) {
                    if (FILE* fp = std::fopen(p.c_str(), "rb")) {
                        std::vector<uint8_t> bytes;
                        int c;
                        while ((c = std::fgetc(fp)) != EOF) bytes.push_back(uint8_t(c));
                        std::fclose(fp);
                        files->emplace_back(p, std::move(bytes));
                        data = files->back().second.data();
                        *size = files->back().second.size();
                    }
                }
                g_heap_flash = false;
                return data;
            });
        }
        char tmpl[] = "/tmp/dsp-port-host-XXXXXX";
        if (!mkdtemp(tmpl)) {
            *error = "mkdtemp failed";
            return false;
        }
        *dir = tmpl;
        return dsp::extract_embedded_media(files, count, nullptr, *dir, nullptr, error);
    }
    void log(const char* text) override { std::fprintf(stderr, "[port] %s\n", text); }

    bool save(const std::string& path) const {
        FILE* f = std::fopen(path.c_str(), "wb");
        if (!f) return false;
        std::fprintf(f, "P6\n320 240\n255\n");
        for (uint32_t p : screen_) {
            const unsigned char rgb[3] = {uint8_t(p >> 16), uint8_t(p >> 8), uint8_t(p)};
            std::fwrite(rgb, 1, 3, f);
        }
        std::fclose(f);
        return true;
    }

private:
    dsp::port::ScaleMap map_;
    std::vector<uint32_t> screen_ = std::vector<uint32_t>(320 * 240, 0);
    uint64_t clock_ = 0;
    int polls_ = 0;
};

}  // namespace

int main(int argc, char** argv) {
    HostHal hal;
    int frames = 300;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        const bool v = i + 1 < argc;
        if (a == "--root" && v) {
            hal.root = argv[++i];
        } else if (a == "--frames" && v) {
            frames = std::atoi(argv[++i]);
        } else if (a == "--out" && v) {
            hal.out = argv[++i];
        } else if (a == "--board-memory" && v) {
            g_heap_limit = size_t(std::strtoul(argv[++i], nullptr, 0));
            hal.board_like = true;
        } else if (a == "--menu-out" && v) {
            hal.menu_out = argv[++i];
        } else if (a == "--keys" && v) {
            std::string list = argv[++i];
            size_t p = 0;
            while (p <= list.size()) {
                const size_t c = list.find(',', p);
                const std::string name = list.substr(p, c == std::string::npos ? std::string::npos : c - p);
                bool found = false;
                for (const NamedKey& k : kKeys)
                    if (name == k.name) {
                        hal.script.push_back(k.key);
                        found = true;
                    }
                if (!found && !name.empty()) {
                    std::fprintf(stderr, "unknown key %s\n", name.c_str());
                    return 1;
                }
                if (c == std::string::npos) break;
                p = c + 1;
            }
        } else {
            std::fprintf(stderr,
                         "usage: %s [--root DIR] [--keys down,enter,...] [--frames N] [--out shot.ppm] "
                         "[--menu-out menu.ppm]\n",
                         argv[0]);
            return 1;
        }
    }
    // DSP_HOST_FRAGMENT: a 38 KB block freed under a small one that stays,
    // as a board's boot can leave its heap.
    static std::vector<uint8_t>* keep = nullptr;
    if (g_heap_limit && std::getenv("DSP_HOST_FRAGMENT")) {
        auto* big = new std::vector<uint8_t>(38400);
        keep = new std::vector<uint8_t>(2048);
        delete big;
    }
    const int r = dsp::port::run(hal, frames);
    if (!hal.out.empty()) hal.save(hal.out);
    std::fprintf(stderr, "[host] result %d, %d pictures, %zu audio samples at 22050 Hz\n", r,
                 hal.presents, hal.audio_samples);
    if (g_heap_limit)
        std::fprintf(stderr, "[host] heap peak %zu bytes (limit %zu), largest free block now %zu\n", g_heap_peak,
                     g_heap_limit, arena::largest());
    return r;
}
