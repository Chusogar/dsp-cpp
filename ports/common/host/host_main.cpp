// Headless desktop Hal for the port front end (see CMakeLists.txt here):
// a 320x240 "screen" saved as a PPM, scripted key presses, a virtual clock.
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "core/fs_compat.h"
#include "port.h"

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
    bool mount_embedded(const DspEmbeddedFile* files, size_t count, std::string* dir,
                        std::string* error) override {
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
    const int r = dsp::port::run(hal, frames);
    if (!hal.out.empty()) hal.save(hal.out);
    std::fprintf(stderr, "[host] result %d, %d pictures, %zu audio samples at 22050 Hz\n", r,
                 hal.presents, hal.audio_samples);
    return r;
}
