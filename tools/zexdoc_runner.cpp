// Runs the Z80 instruction exerciser (zexdoc.com / zexall.com) against the CPU core.
// The binary itself is not part of this repository; pass its path as an argument:
//
//   ./dsp_zexdoc /path/to/zexdoc.com
//
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>

#include "cpu/z80.h"

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <zexdoc.com>\n", argv[0]);
        return 1;
    }
    std::ifstream file(argv[1], std::ios::binary);
    if (!file) {
        std::fprintf(stderr, "cannot open %s\n", argv[1]);
        return 1;
    }
    std::vector<uint8_t> program((std::istreambuf_iterator<char>(file)),
                                 std::istreambuf_iterator<char>());

    std::vector<uint8_t> memory(0x10000, 0);
    std::memcpy(memory.data() + 0x100, program.data(), program.size());

    // CP/M stubs: a warm boot trap at 0x0000 and a BDOS trap at 0x0005.
    memory[0x0000] = 0xd3;  // out (0),a
    memory[0x0001] = 0x00;
    memory[0x0005] = 0xdb;  // in a,(0)
    memory[0x0006] = 0x00;
    memory[0x0007] = 0xc9;  // ret

    dsp::Z80 cpu(4000000);
    bool done = false;
    cpu.set_memory_handlers([&](uint16_t address) { return memory[address]; },
                            [&](uint16_t address, uint8_t value) { memory[address] = value; });
    cpu.set_io_handlers(
        [&](uint16_t) -> uint8_t {
            // BDOS: C=2 prints E, C=9 prints the '$' terminated string at DE.
            if (cpu.c == 2) {
                std::fputc(cpu.e, stdout);
            } else if (cpu.c == 9) {
                uint16_t address = uint16_t((cpu.d << 8) | cpu.e);
                while (memory[address] != '$') std::fputc(memory[address++], stdout);
            }
            std::fflush(stdout);
            return 0xff;
        },
        [&](uint16_t, uint8_t) { done = true; });

    cpu.reset();
    cpu.set_pc(0x0100);
    cpu.sp = 0xf000;
    while (!done && cpu.pc() != 0) cpu.run(100000);
    std::printf("\n");
    return 0;
}
