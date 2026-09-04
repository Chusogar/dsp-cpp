#include <cstdio>
#include <cstring>

#include "drivers/computers/spectrum.h"

using namespace dsp;

namespace {

bool FramebuffersMatch(const Spectrum48& a, const Spectrum48& b) {
    const int n = a.screen_width() * a.screen_height();
    return std::memcmp(a.framebuffer(), b.framebuffer(), size_t(n) * sizeof(uint32_t)) == 0;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) return 1;
    bool ok = true;

    Spectrum48 original;
    std::string error;
    if (!original.init(argv[1], &error)) {
        std::fprintf(stderr, "init failed: %s\n", error.c_str());
        return 1;
    }
    // Boot far enough to reach the "(C) 1982 Sinclair Research Ltd" screen -
    // gives the snapshot some real, non-trivial CPU/memory state to capture.
    for (int i = 0; i < 300; i++) original.run_frame();

    if (!original.save_snapshot("/tmp/test.sna", &error)) {
        std::fprintf(stderr, "SNA save failed: %s\n", error.c_str());
        return 1;
    }
    if (!original.save_snapshot("/tmp/test.z80", &error)) {
        std::fprintf(stderr, "Z80 save failed: %s\n", error.c_str());
        return 1;
    }

    {
        Spectrum48 restored;
        restored.init(argv[1], &error);
        if (!restored.load_snapshot("/tmp/test.sna", &error)) {
            std::fprintf(stderr, "SNA: LOAD FAILED: %s\n", error.c_str());
            ok = false;
        } else {
            restored.run_frame();  // Render once so the framebuffer reflects the loaded state.
            Spectrum48 reference;
            reference.init(argv[1], &error);
            for (int i = 0; i < 300; i++) reference.run_frame();
            reference.run_frame();
            const bool match = FramebuffersMatch(restored, reference);
            std::printf("SNA: framebuffer matches original after round-trip: %s\n",
                        match ? "yes" : "NO");
            ok &= match;
        }
    }

    {
        Spectrum48 restored;
        restored.init(argv[1], &error);
        if (!restored.load_snapshot("/tmp/test.z80", &error)) {
            std::fprintf(stderr, "Z80: LOAD FAILED: %s\n", error.c_str());
            ok = false;
        } else {
            restored.run_frame();
            Spectrum48 reference;
            reference.init(argv[1], &error);
            for (int i = 0; i < 300; i++) reference.run_frame();
            reference.run_frame();
            const bool match = FramebuffersMatch(restored, reference);
            std::printf("Z80: framebuffer matches original after round-trip: %s\n",
                        match ? "yes" : "NO");
            ok &= match;
        }
    }

    // Directly exercise RLE decompression (descomprimir_z80) with a
    // hand-built compressed version-1 .Z80, since save_z80 above always
    // writes uncompressed data and wouldn't otherwise cover that path.
    {
        std::vector<uint8_t> z80(30, 0);
        z80[6] = 0x34; z80[7] = 0x12;  // PC = $1234 (non-zero -> version 1)
        z80[12] = 0x20;                 // bit 5 set: compressed

        std::vector<uint8_t> plain(0xc000, 0);
        for (int i = 0; i < 200; i++) plain[size_t(i)] = 0x55;   // a run RLE should compress
        for (int i = 200; i < 220; i++) plain[size_t(i)] = uint8_t(i);  // literal bytes
        plain[220] = 0xed; plain[221] = 0xed;                     // a literal ED ED pair

        std::vector<uint8_t> compressed;
        compressed.push_back(0xed); compressed.push_back(0xed);
        compressed.push_back(200);
        compressed.push_back(0x55);
        for (int i = 200; i < 220; i++) compressed.push_back(plain[size_t(i)]);
        compressed.push_back(0xed); compressed.push_back(0xed);
        compressed.push_back(2); compressed.push_back(0xed);
        for (size_t i = 222; i < plain.size(); i++) compressed.push_back(plain[i]);  // rest, literal
        compressed.push_back(0x00); compressed.push_back(0xed);
        compressed.push_back(0xed); compressed.push_back(0x00);  // end-of-block marker

        z80.insert(z80.end(), compressed.begin(), compressed.end());
        std::FILE* f = std::fopen("/tmp/test_compressed.z80", "wb");
        std::fwrite(z80.data(), 1, z80.size(), f);
        std::fclose(f);

        Spectrum48 machine;
        machine.init(argv[1], &error);
        if (!machine.load_snapshot("/tmp/test_compressed.z80", &error)) {
            std::fprintf(stderr, "compressed Z80: LOAD FAILED: %s\n", error.c_str());
            ok = false;
        } else {
            bool match = true;
            for (int i = 0; i < int(plain.size()) && match; i++) {
                if (machine.peek(uint16_t(0x4000 + i)) != plain[size_t(i)]) match = false;
            }
            std::printf("compressed Z80: decompressed memory matches: %s\n", match ? "yes" : "NO");
            ok &= match;
        }
    }

    std::printf(ok ? "ALL OK\n" : "SOME FAILED\n");
    return ok ? 0 : 1;
}
