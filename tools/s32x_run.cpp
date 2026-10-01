// Headless Sega 32X runner: s32x_run BIOSSET CART FRAMES OUTPREFIX
// Saves a screenshot every S32X_EVERY frames (default 60) and prints the
// 68000 and SH-2 PCs, the communication ports and the 32X VDP mode.
// S32X_PAD="frame:bits,..." holds pad 1 buttons from that frame
// (1 up, 2 down, 4 left, 8 right, 16 A, 32 B, 64 C, 128 Start).
// S32X_WAV=file.wav saves the sound.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "drivers/consoles/sega32x.h"

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

int main(int argc, char** argv) {
    if (argc < 5) {
        std::fprintf(stderr, "usage: %s BIOSSET CART FRAMES OUTPREFIX\n", argv[0]);
        return 1;
    }
    Sega32X md;
    std::string error;
    if (!md.init(argv[1], &error) || !md.load_media(argv[2], &error)) {
        std::fprintf(stderr, "error: %s\n", error.c_str());
        return 1;
    }
    struct Pad { int frame, bits; };
    std::vector<Pad> pads;
    if (const char* env = std::getenv("S32X_PAD")) {
        std::string s = env;
        size_t pos = 0;
        while (pos < s.size()) {
            size_t end = s.find(',', pos);
            if (end == std::string::npos) end = s.size();
            Pad p{};
            if (std::sscanf(s.substr(pos, end - pos).c_str(), "%d:%d", &p.frame, &p.bits) == 2) pads.push_back(p);
            pos = end + 1;
        }
    }
    const int every = std::getenv("S32X_EVERY") ? std::atoi(std::getenv("S32X_EVERY")) : 60;
    const int frames = std::atoi(argv[3]);
    MachineInputs in;
    std::vector<int16_t> audio;
    const char* wav = std::getenv("S32X_WAV");  // write the audio to this .wav
    for (int f = 1; f <= frames; f++) {
        for (const Pad& p : pads) {
            if (p.frame != f) continue;
            in.player1.up = p.bits & 1;
            in.player1.down = p.bits & 2;
            in.player1.left = p.bits & 4;
            in.player1.right = p.bits & 8;
            in.player1.button1 = p.bits & 16;
            in.player1.button2 = p.bits & 32;
            in.player1.button3 = p.bits & 64;
            in.player1.start = p.bits & 128;
        }
        md.set_inputs(in);
        md.run_frame();
        if (wav) md.drain_audio(audio);
        if (f % every == 0 || f == frames) {
            char name[512];
            std::snprintf(name, sizeof name, "%s_%05d.bmp", argv[4], f);
            write_bmp(name, md.framebuffer(), md.screen_width(), md.screen_height());
            std::printf("frame %d 68k=%06x msh2=%08x ssh2=%08x aden=%d run=%d mode=%04x comm=", f, md.debug_pc(),
                        md.master().pc(), md.slave().pc(), md.adapter_enabled(), md.sh2_running(),
                        md.bitmap_mode());
            for (int i = 0; i < 8; i++) std::printf("%04x%s", md.comm(i), i < 7 ? " " : "\n");
            std::fflush(stdout);
        }
    }
    if (wav) {
        std::FILE* fp = std::fopen(wav, "wb");
        const uint32_t rate = uint32_t(md.sample_rate()), bytes = uint32_t(audio.size() * 2);
        auto u32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, fp); };
        auto u16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, fp); };
        std::fwrite("RIFF", 1, 4, fp);
        u32(36 + bytes);
        std::fwrite("WAVEfmt ", 1, 8, fp);
        u32(16);
        u16(1);
        u16(1);
        u32(rate);
        u32(rate * 2);
        u16(2);
        u16(16);
        std::fwrite("data", 1, 4, fp);
        u32(bytes);
        std::fwrite(audio.data(), 2, audio.size(), fp);
        std::fclose(fp);
    }
    return 0;
}
