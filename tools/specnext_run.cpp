// Headless ZX Spectrum Next runner: boots the machine for N frames and writes
// a BMP every SPECNEXT_EVERY frames (and the sound with SPECNEXT_WAV).
//
//   specnext_run <tbblue.zip|dir> <sd.img|-> <frames> <outdir> [program.nex]
//
// SPECNEXT_KEYS="frame:key[:frames],..." presses keys (a-z, 0-9, enter,
// space, up, down, left, right, esc, bs, fire, f9, f10). The SD image is
// opened read-only (writes stay in memory) unless SPECNEXT_WRITE is set.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <tuple>
#include <vector>

#include "drivers/computers/specnext.h"

namespace {

void write_bmp(const std::string& path, const uint32_t* pixels, int width, int height) {
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return;
    const int out_h = height * 2;  // double the lines for a 4:3 picture
    const uint32_t row = uint32_t(width) * 3;
    const uint32_t size = 54 + row * uint32_t(out_h);
    uint8_t h[54] = {'B', 'M'};
    auto put32 = [&](int at, uint32_t v) {
        for (int i = 0; i < 4; ++i) h[at + i] = uint8_t(v >> (8 * i));
    };
    put32(2, size);
    put32(10, 54);
    put32(14, 40);
    put32(18, uint32_t(width));
    put32(22, uint32_t(out_h));
    h[26] = 1;
    h[28] = 24;
    put32(34, row * uint32_t(out_h));
    std::fwrite(h, 1, 54, f);
    std::vector<uint8_t> line(row);
    for (int y = out_h - 1; y >= 0; --y) {
        const uint32_t* src = pixels + size_t(y / 2) * size_t(width);
        for (int x = 0; x < width; ++x) {
            line[size_t(x) * 3 + 0] = uint8_t(src[x]);
            line[size_t(x) * 3 + 1] = uint8_t(src[x] >> 8);
            line[size_t(x) * 3 + 2] = uint8_t(src[x] >> 16);
        }
        std::fwrite(line.data(), 1, line.size(), f);
    }
    std::fclose(f);
}

void write_wav(const std::string& path, const std::vector<int16_t>& s, int rate) {
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return;
    const uint32_t data = uint32_t(s.size() * 2), riff = 36 + data, fmt_size = 16, sr = uint32_t(rate),
                   br = uint32_t(rate) * 2;
    const uint16_t fmt = 1, ch = 1, align = 2, bits = 16;
    std::fwrite("RIFF", 1, 4, f);
    std::fwrite(&riff, 4, 1, f);
    std::fwrite("WAVEfmt ", 1, 8, f);
    std::fwrite(&fmt_size, 4, 1, f);
    std::fwrite(&fmt, 2, 1, f);
    std::fwrite(&ch, 2, 1, f);
    std::fwrite(&sr, 4, 1, f);
    std::fwrite(&br, 4, 1, f);
    std::fwrite(&align, 2, 1, f);
    std::fwrite(&bits, 2, 1, f);
    std::fwrite("data", 1, 4, f);
    std::fwrite(&data, 4, 1, f);
    std::fwrite(s.data(), 2, s.size(), f);
    std::fclose(f);
}

int key_of(const std::string& n) {
    using dsp::Key;
    if (n.size() == 1 && n[0] >= 'a' && n[0] <= 'z') return int(Key::A) + (n[0] - 'a');
    if (n.size() == 1 && n[0] >= '0' && n[0] <= '9') return int(Key::Num0) + (n[0] - '0');
    const std::pair<const char*, Key> names[] = {
        {"enter", Key::Enter}, {"space", Key::Space}, {"up", Key::Up},   {"down", Key::Down},
        {"left", Key::Left},   {"right", Key::Right}, {"esc", Key::Escape}, {"bs", Key::Backspace},
        {"f9", Key::F9},       {"f10", Key::F10},     {"shift", Key::LeftShift}, {"ctrl", Key::LeftCtrl},
    };
    for (const auto& [name, key] : names)
        if (n == name) return int(key);
    return n == "fire" ? -2 : -1;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 5) {
        std::fprintf(stderr, "usage: %s <tbblue.zip|dir> <sd.img|-> <frames> <outdir> [program.nex]\n", argv[0]);
        return 1;
    }
    dsp::SpecNext m;
    std::string err;
    if (!m.init(argv[1], &err)) {
        std::fprintf(stderr, "%s\n", err.c_str());
        return 1;
    }
    if (std::strcmp(argv[2], "-") != 0 && !m.insert_sd(argv[2], std::getenv("SPECNEXT_WRITE") == nullptr, &err)) {
        std::fprintf(stderr, "%s\n", err.c_str());
        return 1;
    }
    if (argc > 5 && !m.load_media(argv[5], &err)) std::fprintf(stderr, "%s\n", err.c_str());
    const int frames = std::atoi(argv[3]);
    const std::string out = argv[4];
    const int every = std::getenv("SPECNEXT_EVERY") ? std::atoi(std::getenv("SPECNEXT_EVERY")) : 50;

    std::vector<std::tuple<int, int, int>> keys;  // frame, key, length
    if (const char* k = std::getenv("SPECNEXT_KEYS")) {
        std::string s = k;
        size_t p = 0;
        while (p < s.size()) {
            size_t q = s.find(',', p);
            if (q == std::string::npos) q = s.size();
            const std::string item = s.substr(p, q - p);
            const size_t a = item.find(':'), b = item.find(':', a + 1);
            if (a != std::string::npos) {
                const std::string name = item.substr(a + 1, b == std::string::npos ? std::string::npos : b - a - 1);
                keys.emplace_back(std::atoi(item.c_str()), key_of(name),
                                  b == std::string::npos ? 5 : std::atoi(item.c_str() + b + 1));
            }
            p = q + 1;
        }
    }
    std::vector<int16_t> sound, chunk;
    for (int f = 1; f <= frames; ++f) {
        dsp::MachineInputs in{};
        for (const auto& [kf, key, len] : keys) {
            if (f < kf || f >= kf + len) continue;
            if (key >= 0) in.keys[size_t(key)] = true;
            if (key == -2) in.player1.button1 = true;
        }
        m.set_inputs(in);
        m.run_frame();
        m.drain_audio(chunk);
        if (std::getenv("SPECNEXT_WAV")) sound.insert(sound.end(), chunk.begin(), chunk.end());
        if (f % every == 0 || f == frames) {
            char name[64];
            std::snprintf(name, sizeof(name), "/next_%05d.bmp", f);
            write_bmp(out + name, m.framebuffer(), m.screen_width(), m.screen_height());
            std::printf("frame %d pc=%04x config=%d sd_reads=%u\n", f, m.cpu().pc(), int(m.config_mode()),
                        m.sd_sectors_read());
        }
    }
    if (const char* wav = std::getenv("SPECNEXT_WAV")) write_wav(wav, sound, m.sample_rate());
    for (const auto& w : m.warnings()) std::printf("warning: %s\n", w.c_str());
    return 0;
}
