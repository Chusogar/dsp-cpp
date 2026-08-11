#include <cstdio>
#include <vector>

#include "drivers/c64.h"

using namespace dsp;

int main(int argc, char** argv) {
    if (argc < 2) return 1;
    C64 machine;
    std::string error;
    if (!machine.init(argv[1], &error)) {
        std::fprintf(stderr, "init failed: %s\n", error.c_str());
        return 1;
    }

    // Tiny 6502 program: plays a ~440Hz triangle note on SID voice 1 with a
    // short attack/decay envelope, then spins forever.
    const uint8_t program[] = {
        0xA9, 0x44, 0x8D, 0x00, 0xD4,  // LDA #$44 : STA $D400 (freq lo)
        0xA9, 0x1D, 0x8D, 0x01, 0xD4,  // LDA #$1D : STA $D401 (freq hi)
        0xA9, 0x0F, 0x8D, 0x18, 0xD4,  // LDA #$0F : STA $D418 (volume=15)
        0xA9, 0x09, 0x8D, 0x05, 0xD4,  // LDA #$09 : STA $D405 (attack=0,decay=9)
        0xA9, 0xF0, 0x8D, 0x06, 0xD4,  // LDA #$F0 : STA $D406 (sustain=15,release=0)
        0xA9, 0x11, 0x8D, 0x04, 0xD4,  // LDA #$11 : STA $D404 (gate=1,triangle)
        0x4C, 0x1E, 0xC0,              // LOOP: JMP LOOP  ($C01E)
    };
    for (size_t i = 0; i < sizeof(program); i++) machine.poke(uint16_t(0xC000 + i), program[i]);
    machine.set_pc(0xC000);

    std::vector<int16_t> audio;
    for (int frame = 0; frame < 100; frame++) {
        machine.run_frame();
        machine.drain_audio(audio);
    }
    std::printf("samples=%zu\n", audio.size());
    int16_t min_v = 0, max_v = 0;
    int64_t sum_abs = 0;
    int nonzero = 0;
    for (int16_t s : audio) {
        min_v = std::min(min_v, s);
        max_v = std::max(max_v, s);
        sum_abs += std::abs(int(s));
        if (s != 0) nonzero++;
    }
    std::printf("min=%d max=%d mean_abs=%lld nonzero=%d/%zu\n", min_v, max_v,
                (long long)(sum_abs / (audio.empty() ? 1 : audio.size())), nonzero, audio.size());
    // Print the first 300 samples so we can see the waveform shape/envelope ramp.
    for (size_t i = 0; i < audio.size() && i < 300; i++) {
        std::printf("%d ", audio[i]);
        if (i % 20 == 19) std::printf("\n");
    }
    std::printf("\n");
    return 0;
}
