// Headless Shuuz capture: run attract mode and write BMP screenshots.
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "drivers/arcade/shuuz.h"

namespace {

bool write_bmp(const char* path, const uint32_t* pixels, int width, int height) {
    const int row_bytes = width * 3;
    const int pad = (4 - (row_bytes % 4)) & 3;
    const int img = (row_bytes + pad) * height;
    const int file_size = 54 + img;
    std::vector<uint8_t> bmp(size_t(file_size), 0);
    bmp[0] = 'B';
    bmp[1] = 'M';
    auto u32 = [&](int o, uint32_t v) {
        bmp[size_t(o)] = uint8_t(v);
        bmp[size_t(o + 1)] = uint8_t(v >> 8);
        bmp[size_t(o + 2)] = uint8_t(v >> 16);
        bmp[size_t(o + 3)] = uint8_t(v >> 24);
    };
    auto u16 = [&](int o, uint16_t v) {
        bmp[size_t(o)] = uint8_t(v);
        bmp[size_t(o + 1)] = uint8_t(v >> 8);
    };
    u32(2, uint32_t(file_size));
    u32(10, 54);
    u32(14, 40);
    u32(18, uint32_t(width));
    u32(22, uint32_t(height));
    u16(26, 1);
    u16(28, 24);
    u32(34, uint32_t(img));
    uint8_t* dest = bmp.data() + 54;
    for (int y = height - 1; y >= 0; y--) {
        for (int x = 0; x < width; x++) {
            const uint32_t p = pixels[y * width + x];
            *dest++ = uint8_t(p);
            *dest++ = uint8_t(p >> 8);
            *dest++ = uint8_t(p >> 16);
        }
        dest += pad;
    }
    FILE* f = std::fopen(path, "wb");
    if (!f) return false;
    const bool ok = std::fwrite(bmp.data(), 1, bmp.size(), f) == bmp.size();
    std::fclose(f);
    return ok;
}

int unique_colours(const uint32_t* pixels, int n) {
    std::vector<uint32_t> seen;
    seen.reserve(256);
    for (int i = 0; i < n; i++) {
        const uint32_t p = pixels[i] & 0x00ffffffu;
        bool found = false;
        for (uint32_t s : seen) {
            if (s == p) {
                found = true;
                break;
            }
        }
        if (!found) {
            seen.push_back(p);
            if (seen.size() > 512) break;
        }
    }
    return int(seen.size());
}

}  // namespace

int main(int argc, char** argv) {
    const char* rom = argc > 1 ? argv[1] : "/tmp/roms/shuuz.zip";
    const std::string out_dir = argc > 2 ? argv[2] : "/tmp";

    dsp::Shuuz machine;
    std::string error;
    if (!machine.init(rom, &error)) {
        std::fprintf(stderr, "init failed: %s\n", error.c_str());
        return 1;
    }
    std::printf("loaded %s pc=%06x\n", machine.title(), machine.debug_pc());

    const int frames[] = {400, 800, 1000, 1500, 1800, 2200};
    int at = 0;
    dsp::MachineInputs idle;
    machine.set_inputs(idle);
    for (int frame : frames) {
        while (at < frame) {
            machine.run_frame();
            at++;
        }
        char name[256];
        std::snprintf(name, sizeof(name), "%s/shuuz_%d.bmp", out_dir.c_str(), frame);
        if (!write_bmp(name, machine.framebuffer(), machine.screen_width(), machine.screen_height())) {
            std::fprintf(stderr, "cannot write %s\n", name);
            return 1;
        }
        const int colours =
            unique_colours(machine.framebuffer(), machine.screen_width() * machine.screen_height());
        std::printf("wrote %s pc=%06x pal=%d mo=%d colours=%d\n", name, machine.debug_pc(),
                    machine.debug_palette_used(), machine.debug_motion_object_pixels(), colours);
    }
    return 0;
}
