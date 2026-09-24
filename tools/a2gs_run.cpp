// Headless Apple IIGS runner: a2gs_run ROMDIR FRAMES OUTPREFIX [disk...]
// Runs the machine, saves a screenshot every 60 frames (OUTPREFIX_NNNN.bmp)
// and prints the CPU position, so boot problems can be diagnosed without SDL.
// Optional env: A2GS_KEYS="frame:keycode:down,..." scripted ADB key events.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "drivers/computers/apple2gs.h"

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
    uint32_t offset_bits = 54;
    std::memcpy(header + 10, &offset_bits, 4);
    uint32_t dib_size = 40;
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
            const uint32_t pixel = pixels[size_t(y) * width + x];
            padded[size_t(x) * 3 + 0] = uint8_t(pixel & 0xff);
            padded[size_t(x) * 3 + 1] = uint8_t((pixel >> 8) & 0xff);
            padded[size_t(x) * 3 + 2] = uint8_t((pixel >> 16) & 0xff);
        }
        std::fwrite(padded.data(), 1, row_size, file);
    }
    std::fclose(file);
}

int main(int argc, char** argv) {
    if (argc < 4) {
        std::fprintf(stderr, "usage: %s ROMDIR FRAMES OUTPREFIX [disk...]\n", argv[0]);
        return 1;
    }
    Apple2GS machine;
    std::string error;
    if (!machine.init(argv[1], &error)) {
        std::fprintf(stderr, "init: %s\n", error.c_str());
        return 1;
    }
    for (int i = 4; i < argc; i++) {
        if (!machine.load_media(argv[i], &error)) {
            std::fprintf(stderr, "media %s: %s\n", argv[i], error.c_str());
            return 1;
        }
    }
    machine.reset();
    if (std::getenv("A2GS_TRACE")) machine.set_trace(true);
    struct Ev { int frame, code; bool down; };
    std::vector<Ev> events;
    if (const char* keys = std::getenv("A2GS_KEYS")) {
        std::string s = keys;
        size_t pos = 0;
        while (pos < s.size()) {
            size_t end = s.find(',', pos);
            if (end == std::string::npos) end = s.size();
            int f = 0, c = 0, d = 0;
            if (std::sscanf(s.substr(pos, end - pos).c_str(), "%d:%x:%d", &f, &c, &d) == 3) events.push_back({f, c, d != 0});
            pos = end + 1;
        }
    }
    struct Mv { int frame, x, y, b; };
    std::vector<Mv> moves;
    if (const char* mv = std::getenv("A2GS_MOUSE")) {
        std::string s = mv;
        size_t pos = 0;
        while (pos < s.size()) {
            size_t end = s.find(',', pos);
            if (end == std::string::npos) end = s.size();
            Mv m{};
            if (std::sscanf(s.substr(pos, end - pos).c_str(), "%d:%d:%d:%d", &m.frame, &m.x, &m.y, &m.b) == 4) moves.push_back(m);
            pos = end + 1;
        }
    }
    MachineInputs inputs;
    inputs.has_pointer = !moves.empty();
    const int every = std::getenv("A2GS_EVERY") ? std::atoi(std::getenv("A2GS_EVERY")) : 60;
    int frames = std::atoi(argv[2]);
    std::vector<int16_t> audio, chunk;
    for (int f = 1; f <= frames; f++) {
        for (const Ev& e : events) if (e.frame == f) machine.post_key(e.code, e.down);
        for (const Mv& m : moves) {
            if (m.frame == f) {
                inputs.pointer_x = m.x;
                inputs.pointer_y = m.y;
                inputs.pointer_button1 = m.b != 0;
            }
        }
        if (inputs.has_pointer) machine.set_inputs(inputs);
        machine.run_frame();
        chunk.clear();
        machine.drain_audio(chunk);
        audio.insert(audio.end(), chunk.begin(), chunk.end());
        if (f % every == 0 || f == frames) {
            char name[512];
            std::snprintf(name, sizeof name, "%s_%05d.bmp", argv[3], f);
            write_bmp(name, machine.framebuffer(), machine.screen_width(), machine.screen_height());
            auto& cpu = machine.cpu();
            std::printf("frame %d pc=%06x a=%04x x=%04x y=%04x sp=%04x d=%04x dbr=%02x p=%02x e=%d\n", f, cpu.pc(),
                        cpu.a, cpu.x, cpu.y, cpu.sp, cpu.d, cpu.dbr, cpu.get_p(), cpu.emulation());
            std::fflush(stdout);
        }
    }
    if (const char* wav = std::getenv("A2GS_WAV")) {
        std::FILE* f = std::fopen(wav, "wb");
        if (f) {
            uint32_t data = uint32_t(audio.size() * 2), riff = 36 + data, rate = 44100, byte_rate = rate * 2, fmt = 16;
            uint16_t pcm = 1, ch = 1, align = 2, bits = 16;
            std::fwrite("RIFF", 1, 4, f); std::fwrite(&riff, 4, 1, f); std::fwrite("WAVEfmt ", 1, 8, f);
            std::fwrite(&fmt, 4, 1, f); std::fwrite(&pcm, 2, 1, f); std::fwrite(&ch, 2, 1, f);
            std::fwrite(&rate, 4, 1, f); std::fwrite(&byte_rate, 4, 1, f); std::fwrite(&align, 2, 1, f);
            std::fwrite(&bits, 2, 1, f); std::fwrite("data", 1, 4, f); std::fwrite(&data, 4, 1, f);
            std::fwrite(audio.data(), 2, audio.size(), f);
            std::fclose(f);
        }
    }
    return 0;
}
