// Standalone harness: boots the Spectrum48 driver, types LOAD "" and runs
// frames while the tape plays, dumping periodic screenshots and the decoded
// screen text so tape-loading bugs can be diagnosed without a real display.
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "drivers/spectrum.h"

using namespace dsp;

namespace {

void write_bmp(const std::string& path, const uint32_t* pixels, int width, int height) {
    std::vector<uint8_t> row_buffer(size_t(width) * 3);
    std::FILE* file = std::fopen(path.c_str(), "wb");
    const uint32_t row_size = ((width * 3 + 3) / 4) * 4;
    const uint32_t data_size = row_size * uint32_t(height);
    const uint32_t file_size = 54 + data_size;
    uint8_t header[54] = {0};
    header[0] = 'B'; header[1] = 'M';
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

// Presses `keys` for `frames` frames, then releases everything for a few
// frames so the ROM's keyboard debounce sees a clean up edge before the next
// key goes down.
void press(Spectrum48& machine, std::vector<Key> keys, int frames) {
    MachineInputs inputs;
    for (Key key : keys) inputs.keys[size_t(key)] = true;
    for (int i = 0; i < frames; i++) {
        machine.set_inputs(inputs);
        machine.run_frame();
    }
    MachineInputs released;
    for (int i = 0; i < 6; i++) {
        machine.set_inputs(released);
        machine.run_frame();
    }
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: %s <48.rom> <tape.tzx>\n", argv[0]);
        return 1;
    }
    Spectrum48 machine;
    std::string error;
    if (!machine.init(argv[1], &error)) {
        std::fprintf(stderr, "init failed: %s\n", error.c_str());
        return 1;
    }
    if (!machine.load_media(argv[2], &error)) {
        std::fprintf(stderr, "load_media failed: %s\n", error.c_str());
        return 1;
    }
    std::printf("tape loaded ok\n");

    // Let the ROM boot to the "(C) 1982 Sinclair Research Ltd" prompt.
    MachineInputs none;
    for (int i = 0; i < 300; i++) {
        machine.set_inputs(none);
        machine.run_frame();
        if (i % 30 == 0) {
            char path[256];
            std::snprintf(path, sizeof(path), "/home/claude/work/boot_%03d.bmp", i);
            write_bmp(path, machine.framebuffer(), machine.screen_width(), machine.screen_height());
        }
    }
    write_bmp("/home/claude/work/boot.bmp", machine.framebuffer(), machine.screen_width(),
              machine.screen_height());

    // Type LOAD "" <ENTER>: J = LOAD keyword, SymbolShift+P = ", ENTER.
    press(machine, {Key::J}, 10);
    write_bmp("/home/claude/work/step_j.bmp", machine.framebuffer(), machine.screen_width(),
              machine.screen_height());
    press(machine, {Key::RightCtrl, Key::P}, 10);  // RightCtrl acts as symbol shift per matrix
    write_bmp("/home/claude/work/step_q1.bmp", machine.framebuffer(), machine.screen_width(),
              machine.screen_height());
    press(machine, {Key::RightCtrl, Key::P}, 10);
    write_bmp("/home/claude/work/step_q2.bmp", machine.framebuffer(), machine.screen_width(),
              machine.screen_height());
    press(machine, {Key::Enter}, 10);

    write_bmp("/home/claude/work/typed.bmp", machine.framebuffer(), machine.screen_width(),
              machine.screen_height());

    // Run until the tape finishes or we give up after a generous number of frames.
int frame = 0;
    const int max_frames = 50 * 60 * 20;  // ~20 minutes of Spectrum time
    for (; frame < max_frames; frame++) {
        machine.set_inputs(none);
        machine.run_frame();
        if (frame % 200 == 0) {
            std::printf("frame %5d block=%zu/%zu playing=%d finished=%d\n", frame,
                        machine.tape().current_block(), machine.tape().block_count(),
                        machine.tape().playing(), machine.tape().finished());
        }
        if (frame % (50 * 60) == 0) {
            char path[256];
            std::snprintf(path, sizeof(path), "/home/claude/work/frame_%05d.bmp", frame);
            write_bmp(path, machine.framebuffer(), machine.screen_width(), machine.screen_height());
        }
        if (machine.tape().finished()) break;
    }
    write_bmp("/home/claude/work/final.bmp", machine.framebuffer(), machine.screen_width(),
              machine.screen_height());
    std::printf("done after %d frames\n", frame);
    return 0;
}
