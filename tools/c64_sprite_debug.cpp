#include <cstdio>

#include "drivers/computers/c64.h"

using namespace dsp;

int main(int argc, char** argv) {
    if (argc < 3) return 1;
    C64 machine;
    std::string error;
    if (!machine.init(argv[1], &error)) {
        std::fprintf(stderr, "init failed: %s\n", error.c_str());
        return 1;
    }

    // Sprite 0 shape at $2000 (a block pointer of 0x80 = $2000/64): a solid
    // diamond so it's easy to eyeball in a screenshot. 21 rows x 3 bytes.
    static const uint8_t kDiamond[63] = {
        0x00, 0x00, 0x00, 0x03, 0x80, 0x00, 0x07, 0xC0, 0x00, 0x0F, 0xE0, 0x00, 0x1F, 0xF0, 0x00,
        0x3F, 0xF8, 0x00, 0x7F, 0xFC, 0x00, 0xFF, 0xFE, 0x00, 0xFF, 0xFE, 0x00, 0xFF, 0xFE, 0x00,
        0xFF, 0xFE, 0x00, 0xFF, 0xFE, 0x00, 0xFF, 0xFE, 0x00, 0x7F, 0xFC, 0x00, 0x3F, 0xF8, 0x00,
        0x1F, 0xF0, 0x00, 0x0F, 0xE0, 0x00, 0x07, 0xC0, 0x00, 0x03, 0x80, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00,
    };
    for (int i = 0; i < 63; i++) machine.poke(uint16_t(0x2000 + i), kDiamond[i]);
    // Default screen ($0400) sprite-pointer byte for sprite 0, at $07F8.
    machine.poke(0x07f8, 0x80);

    // Second sprite (pointer byte 0x81 -> $2040, same diamond shape),
    // overlapping sprite 0, to exercise sprite-sprite collision detection.
    for (int i = 0; i < 63; i++) machine.poke(uint16_t(0x2040 + i), kDiamond[i]);
    machine.poke(0x07f9, 0x81);

    const uint8_t program[] = {
        0xA9, 0x15, 0x8D, 0x18, 0xD0,  // LDA #$15 : STA $D018 (screen $0400, chars $1000)
        0xA9, 0x64, 0x8D, 0x00, 0xD0,  // LDA #$64 : STA $D000 (sprite 0 X)
        0xA9, 0x64, 0x8D, 0x01, 0xD0,  // LDA #$64 : STA $D001 (sprite 0 Y)
        0xA9, 0x01, 0x8D, 0x27, 0xD0,  // LDA #$01 : STA $D027 (sprite 0 colour = white)
        0xA9, 0x68, 0x8D, 0x02, 0xD0,  // LDA #$68 : STA $D002 (sprite 1 X, overlaps sprite 0)
        0xA9, 0x64, 0x8D, 0x03, 0xD0,  // LDA #$64 : STA $D003 (sprite 1 Y)
        0xA9, 0x02, 0x8D, 0x28, 0xD0,  // LDA #$02 : STA $D028 (sprite 1 colour = red)
        0xA9, 0x03, 0x8D, 0x15, 0xD0,  // LDA #$03 : STA $D015 (enable sprites 0+1)
        // LOOP: OR-accumulate the collision register into $0400 so a
        // collision seen on any later raster line is still visible once we
        // stop running frames (reading $D01E clears it on real hardware).
        0xAD, 0x1E, 0xD0,              // LOOP ($C028): LDA $D01E
        0x0D, 0x00, 0x04,              // ORA $0400
        0x8D, 0x00, 0x04,              // STA $0400
        0x4C, 0x28, 0xC0,              // JMP LOOP
    };
    for (size_t i = 0; i < sizeof(program); i++) machine.poke(uint16_t(0xC000 + i), program[i]);
    machine.set_pc(0xC000);

    for (int frame = 0; frame < 5; frame++) machine.run_frame();
    std::printf("collision byte at $0400 = %02x (expect bit0|bit1 set = 0x03)\n", machine.peek(0x0400));

    // Dump as a PPM so it can be inspected without extra dependencies.
    std::FILE* f = std::fopen(argv[2], "wb");
    std::fprintf(f, "P6\n%d %d\n255\n", machine.screen_width(), machine.screen_height());
    const uint32_t* fb = machine.framebuffer();
    for (int i = 0; i < machine.screen_width() * machine.screen_height(); i++) {
        const uint32_t p = fb[i];
        std::fputc(int((p >> 16) & 0xff), f);
        std::fputc(int((p >> 8) & 0xff), f);
        std::fputc(int(p & 0xff), f);
    }
    std::fclose(f);
    std::printf("wrote %s\n", argv[2]);
    return 0;
}
