// Headless Macintosh II runner: macii_run ROMSET FRAMES OUTPREFIX [disk]
// Saves a screenshot every MACII_EVERY frames (default 60) and prints the PC.
// MACII_TRACE=frame:count traces instructions; MACII_KEYS="frame:code:down,...";
// MACII_MOUSE="frame:dx:dy:button,...".
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "drivers/computers/macii.h"

using namespace dsp;

static void write_bmp(const std::string& path, const uint32_t* pixels, int width, int height) {
    std::FILE* file = std::fopen(path.c_str(), "wb");
    if (!file) return;
    const uint32_t row_size = ((width * 3 + 3) / 4) * 4;
    const uint32_t data_size = row_size * uint32_t(height);
    const uint32_t file_size = 54 + data_size;
    uint8_t header[54] = {0};
    header[0] = 'B';
    header[1] = 'M';
    std::memcpy(header + 2, &file_size, 4);
    uint32_t offset_bits = 54, dib_size = 40;
    std::memcpy(header + 10, &offset_bits, 4);
    std::memcpy(header + 14, &dib_size, 4);
    int32_t w = width, h = height;
    std::memcpy(header + 18, &w, 4);
    std::memcpy(header + 22, &h, 4);
    uint16_t planes = 1, bpp = 24;
    std::memcpy(header + 26, &planes, 2);
    std::memcpy(header + 28, &bpp, 2);
    std::memcpy(header + 34, &data_size, 4);
    std::fwrite(header, 1, 54, file);
    std::vector<uint8_t> padded(row_size, 0);
    for (int y = height - 1; y >= 0; y--) {
        for (int x = 0; x < width; x++) {
            const uint32_t p = pixels[size_t(y) * width + x];
            padded[size_t(x) * 3 + 0] = uint8_t(p);
            padded[size_t(x) * 3 + 1] = uint8_t(p >> 8);
            padded[size_t(x) * 3 + 2] = uint8_t(p >> 16);
        }
        std::fwrite(padded.data(), 1, row_size, file);
    }
    std::fclose(file);
}

template <typename T, typename F>
static std::vector<T> parse(const char* env, F fn) {
    std::vector<T> out;
    if (!env) return out;
    std::string s = env;
    size_t pos = 0;
    while (pos < s.size()) {
        size_t end = s.find(',', pos);
        if (end == std::string::npos) end = s.size();
        T v{};
        if (fn(s.substr(pos, end - pos).c_str(), v)) out.push_back(v);
        pos = end + 1;
    }
    return out;
}

int main(int argc, char** argv) {
    if (argc < 4) {
        std::fprintf(stderr, "usage: %s ROMSET FRAMES OUTPREFIX [disk]\n", argv[0]);
        return 1;
    }
    MacII mac;
    std::string error;
    if (!mac.init(argv[1], &error)) {
        std::fprintf(stderr, "init: %s\n", error.c_str());
        return 1;
    }
    if (argc > 4 && !mac.load_media(argv[4], &error)) {
        std::fprintf(stderr, "disk: %s\n", error.c_str());
        return 1;
    }
    mac.reset();
    struct Key3 { int f, c, d; };
    auto keys = parse<Key3>(std::getenv("MACII_KEYS"), [](const char* s, Key3& k) { return std::sscanf(s, "%d:%x:%d", &k.f, &k.c, &k.d) == 3; });
    struct Mv { int f, dx, dy, b; };
    auto moves = parse<Mv>(std::getenv("MACII_MOUSE"), [](const char* s, Mv& m) { return std::sscanf(s, "%d:%d:%d:%d", &m.f, &m.dx, &m.dy, &m.b) == 4; });
    int trace_frame = -1, trace_count = 0;
    if (const char* t = std::getenv("MACII_TRACE")) std::sscanf(t, "%d:%d", &trace_frame, &trace_count);
    const int every = std::getenv("MACII_EVERY") ? std::atoi(std::getenv("MACII_EVERY")) : 60;
    const int frames = std::atoi(argv[2]);
    for (int f = 1; f <= frames; f++) {
        if (f == trace_frame) mac.set_trace(trace_count);
        for (auto& k : keys) if (k.f == f) mac.post_key(k.c, k.d != 0);
        for (auto& m : moves) if (m.f == f) mac.debug_mouse(m.dx, m.dy, m.b != 0);
        mac.run_frame();
        if (f % every == 0 || f == frames) {
            char name[512];
            std::snprintf(name, sizeof name, "%s_%05d.bmp", argv[3], f);
            write_bmp(name, mac.framebuffer(), mac.screen_width(), mac.screen_height());
            std::printf("frame %d pc=%08x\n", f, mac.debug_pc());
            std::fflush(stdout);
        }
    }
    if (std::getenv("MACII_PRAM")) {
        for (int i = 0; i < 256; i++) std::printf("%02x%s", mac.debug_pram()[i], (i & 15) == 15 ? "\n" : " ");
    }
    if (const char* d = std::getenv("MACII_DUMP")) {
        FILE* fp = std::fopen(d, "wb");
        for (uint32_t a = 0; a < (8u << 20); a++) std::fputc(mac.peek(a), fp);
        std::fclose(fp);
    }
    return 0;
}
