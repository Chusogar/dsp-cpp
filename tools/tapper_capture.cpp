// Headless Tapper capture: run attract / coin-up frames and write BMP screenshots.
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "core/machine.h"
#include "drivers/mcr.h"

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
            *dest++ = uint8_t(p);          // B
            *dest++ = uint8_t(p >> 8);     // G
            *dest++ = uint8_t(p >> 16);    // R
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
        for (uint32_t s : seen)
            if (s == p) {
                found = true;
                break;
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
    const char* rom = argc > 1 ? argv[1] : "/tmp/roms/tapper.zip";
    const std::string out_dir = argc > 2 ? argv[2] : "/opt/cursor/artifacts/screenshots";
    const bool trace = argc > 3 && std::string(argv[3]) == "trace";

    dsp::Mcr tapper(dsp::Mcr::Game::Tapper);
    std::string error;
    if (!tapper.init(rom, &error)) {
        std::fprintf(stderr, "init failed: %s\n", error.c_str());
        return 1;
    }
    std::printf("loaded %s pc=%04x\n", tapper.title(), tapper.debug_pc());

    if (trace) {
        std::vector<uint16_t> last(64, 0);
        size_t last_i = 0;
        int insns = 0;
        uint16_t prev = 0xffff;
        int same = 0;
        int printed = 0;
        tapper.debug_set_instruction_hook([&](uint16_t pc) {
            last[last_i++ % last.size()] = pc;
            const uint8_t op = tapper.debug_read(pc);
            if (op == 0xd3 || op == 0xdb) {
                const uint8_t port = tapper.debug_read(uint16_t(pc + 1));
                std::printf("io %s pc=%04x port=%02x a=%02x sp=%04x ix=%04x\n",
                            op == 0xd3 ? "OUT" : "IN ", pc, port, tapper.debug_a(),
                            tapper.debug_sp(), tapper.debug_ix());
            }
            if (pc == prev) {
                same++;
            } else {
                if (same > 1) std::printf("  (repeated %d times)\n", same);
                same = 1;
                prev = pc;
                if (printed < 500) {
                    std::printf("pc=%04x op=%02x sp=%04x a=%02x f=%02x ix=%04x im=%d iff=%d\n",
                                pc, op, tapper.debug_sp(), tapper.debug_a(), tapper.debug_f(),
                                tapper.debug_ix(), tapper.debug_im(), int(tapper.debug_iff1()));
                    printed++;
                }
            }
            insns++;
            if (op == 0x76) {
                std::printf("HALT at %04x after %d insns sp=%04x im=%d iff=%d\n", pc, insns,
                            tapper.debug_sp(), tapper.debug_im(), int(tapper.debug_iff1()));
                std::printf("recent pc:");
                for (size_t i = 0; i < last.size(); i++) {
                    std::printf(" %04x", last[(last_i + i) % last.size()]);
                }
                std::printf("\n");
            }
        });
        for (int f = 0; f < 30 && !tapper.debug_halted(); f++) tapper.run_frame();
        std::printf("after 30 frames pc=%04x halt=%d insns~%d ctc=%d vram=%02x%02x\n",
                    tapper.debug_pc(), int(tapper.debug_halted()), insns, tapper.debug_ctc_irqs(),
                    tapper.debug_read(0xf000), tapper.debug_read(0xf001));
        return 0;
    }

    auto shot = [&](const char* name, int frames, const dsp::MachineInputs& inputs) {
        tapper.set_inputs(inputs);
        for (int i = 0; i < frames; i++) tapper.run_frame();
        const std::string path = out_dir + "/" + name;
        if (!write_bmp(path.c_str(), tapper.framebuffer(), tapper.screen_width(),
                       tapper.screen_height())) {
            std::fprintf(stderr, "cannot write %s\n", path.c_str());
            return false;
        }
        const int colours = unique_colours(tapper.framebuffer(),
                                           tapper.screen_width() * tapper.screen_height());
        std::printf("wrote %s pc=%04x sp=%04x im=%d i=%02x iff1=%d halt=%d ctc_irqs=%d snd_pc=%04x colours=%d vram=%02x%02x pal0=%08x\n",
                    path.c_str(), tapper.debug_pc(), tapper.debug_sp(), tapper.debug_im(),
                    tapper.debug_i(), int(tapper.debug_iff1()), int(tapper.debug_halted()),
                    tapper.debug_ctc_irqs(), tapper.debug_sound_pc(), colours,
                    tapper.debug_read(0xf000), tapper.debug_read(0xf001),
                    tapper.framebuffer()[0]);
        return true;
    };

    dsp::MachineInputs idle;
    if (!shot("tapper_attract.bmp", 180, idle)) return 1;

    dsp::MachineInputs coin;
    coin.coin1 = true;
    tapper.set_inputs(coin);
    for (int i = 0; i < 20; i++) tapper.run_frame();
    coin.coin1 = false;
    tapper.set_inputs(coin);
    for (int i = 0; i < 10; i++) tapper.run_frame();

    dsp::MachineInputs start;
    start.player1.start = true;
    tapper.set_inputs(start);
    for (int i = 0; i < 15; i++) tapper.run_frame();
    start.player1.start = false;
    if (!shot("tapper_ingame.bmp", 90, start)) return 1;

    dsp::MachineInputs play;
    play.player1.right = true;
    play.player1.button1 = true;
    if (!shot("tapper_play.bmp", 60, play)) return 1;
    return 0;
}
