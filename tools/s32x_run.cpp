// Headless Sega 32X runner: s32x_run BIOSSET CART FRAMES OUTPREFIX
// Saves a screenshot every S32X_EVERY frames (default 60) and prints the
// 68000 and SH-2 PCs, the communication ports and the 32X VDP mode.
// S32X_PAD="frame:bits,..." holds pad 1 buttons from that frame
// (1 up, 2 down, 4 left, 8 right, 16 A, 32 B, 64 C, 128 Start).
// S32X_WAV=file.wav saves the sound. S32X_EXC=1 logs 68000 exceptions.
// S32X_SDRAM=file dumps the SH-2 SDRAM at the end.
#include <algorithm>
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
    int cur_frame = 0;
    // Last 32 68000 PCs, printed with each exception.
    static uint32_t ring[32];
    static uint32_t big_pc[4096], big_sp[4096];
    static int big_pos = 0;
    static int ring_pos = 0;
    if (std::getenv("S32X_EXC")) {  // log 68000 exceptions (not interrupts)
        // S32X_WATCH=addr: report every change of the long at addr (68000 view).
        static long watch = std::getenv("S32X_WATCH") ? std::strtol(std::getenv("S32X_WATCH"), nullptr, 16) : -1;
        static uint32_t watched = 0;
        md.debug_m68k().set_instruction_hook([&md, &cur_frame](uint32_t pc) {
            ring[ring_pos++ & 31] = pc;
            big_pc[big_pos & 4095] = pc;
            big_sp[big_pos++ & 4095] = md.debug_m68k().a[7].l;
            if (watch < 0) return;
            const uint32_t v = (uint32_t(md.debug_read_word(uint32_t(watch))) << 16) | md.debug_read_word(uint32_t(watch) + 2);
            if (v != watched) {
                std::fprintf(stderr, "[%d] watch %06lx: %08x -> %08x before pc=%06x (prev pc %06x)\n", cur_frame, watch,
                             watched, v, pc, ring[(ring_pos - 2) & 31]);
                watched = v;
            }
        });
        md.debug_m68k().set_exception_handler([&](uint32_t vector, uint32_t pc) {
            std::fprintf(stderr, "[%d] 68k exception %u at %06x a7=%08x; last PCs:", cur_frame, vector, pc,
                         md.debug_m68k().a[7].l);
            for (int i = 0; i < 32; i++) std::fprintf(stderr, " %06x", ring[(ring_pos + i) & 31]);
            std::fprintf(stderr, "\n");
            if (std::getenv("S32X_EXC_DUMP")) {
                for (int i = 0; i < 4096; i++) {
                    const int k = (big_pos + i) & 4095;
                    std::fprintf(stderr, "  %06x sp=%08x\n", big_pc[k], big_sp[k]);
                }
            }
        });
    }
    // S32X_HIST=from_frame: PC histogram of both SH-2s from that frame on.
    static std::vector<std::pair<uint32_t, int>> hist[2];
    const int hist_from = std::getenv("S32X_HIST") ? std::atoi(std::getenv("S32X_HIST")) : -1;
    if (hist_from >= 0) {
        for (int c = 0; c < 2; c++) {
            Sh2& cpu = c == 0 ? md.master() : md.slave();
            cpu.set_instruction_hook([c, &cur_frame, hist_from](uint32_t pc) {
                if (cur_frame < hist_from) return;
                for (auto& e : hist[c]) {
                    if (e.first == pc) {
                        e.second++;
                        return;
                    }
                }
                hist[c].push_back({pc, 1});
            });
        }
    }
    // S32X_SHBREAK=pc: print master/slave registers each time that PC runs.
    if (const char* b = std::getenv("S32X_SHBREAK")) {
        const uint32_t bp = uint32_t(std::strtoul(b, nullptr, 16));
        for (int c = 0; c < 2; c++) {
            Sh2& cpu = c == 0 ? md.master() : md.slave();
            cpu.set_instruction_hook([c, bp, &cpu, &cur_frame](uint32_t pc) {
                if (pc != bp) return;
                std::fprintf(stderr, "[%d] %s at %08x pr=%08x", cur_frame, c ? "slave" : "master", pc, cpu.pr());
                for (int r = 0; r < 16; r++) std::fprintf(stderr, " r%d=%08x", r, cpu.r(r));
                std::fprintf(stderr, "\n");
            });
        }
    }
    // S32X_SHEXC=1: log SH-2 exceptions other than the 32X IRL interrupts.
    if (std::getenv("S32X_SHEXC")) {
        for (int c = 0; c < 2; c++) {
            Sh2& cpu = c == 0 ? md.master() : md.slave();
            cpu.set_exception_hook([c, &cur_frame](uint32_t vector, uint32_t pc) {
                if (vector >= 64 && vector <= 71) return;
                std::fprintf(stderr, "[%d] %s exception %u, return pc %08x\n", cur_frame, c ? "slave" : "master", vector, pc);
            });
        }
    }
    // S32X_PEEK=addr: report changes of that long, checked before each master instruction.
    if (const char* pk = std::getenv("S32X_PEEK")) {
        const uint32_t addr = uint32_t(std::strtoul(pk, nullptr, 16));
        static uint32_t last = 0;
        md.master().set_instruction_hook([addr, &md, &cur_frame](uint32_t pc) {
            const uint32_t v = md.master().debug_read32(addr);
            if (v != last) {
                std::fprintf(stderr, "[%d] peek %08x: %08x -> %08x (master pc %08x, slave pc %08x)\n", cur_frame, addr,
                             last, v, pc, md.slave().pc());
                last = v;
            }
        });
    }
    // S32X_SHWATCH=addr: log SH-2 writes to that long.
    if (const char* w = std::getenv("S32X_SHWATCH")) {
        const uint32_t addr = uint32_t(std::strtoul(w, nullptr, 16));
        for (int c = 0; c < 2; c++) {
            Sh2& cpu = c == 0 ? md.master() : md.slave();
            cpu.set_write_watch(addr, [c, &cur_frame](uint32_t pc, uint32_t a, uint32_t v) {
                std::fprintf(stderr, "[%d] %s write %08x = %08x at pc %08x\n", cur_frame, c ? "slave" : "master", a, v, pc);
            });
        }
    }
    const int every = std::getenv("S32X_EVERY") ? std::atoi(std::getenv("S32X_EVERY")) : 60;
    const int frames = std::atoi(argv[3]);
    MachineInputs in;
    std::vector<int16_t> audio;
    const char* wav = std::getenv("S32X_WAV");  // write the audio to this .wav
    for (int f = 1; f <= frames; f++) {
        cur_frame = f;
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
            for (int i = 0; i < 8; i++) std::printf("%04x ", md.comm(i));
            std::printf("sr=%03x/%03x mask=%02x/%02x pend=%02x/%02x\n", md.master().sr(), md.slave().sr(),
                        md.int_mask(0), md.int_mask(1), md.pending(0), md.pending(1));
            std::fflush(stdout);
        }
    }
    if (const char* row = std::getenv("S32X_ROW")) {  // y: palette indices of that 32X line (packed mode)
        const int y = std::atoi(row);
        const uint8_t* d = md.frame_buffer(1) ;
        (void)d;
        std::fprintf(stderr, "palette:");
        for (int i = 0; i < 16; i++) std::fprintf(stderr, " %04x", md.palette(i));
        std::fprintf(stderr, "\n");
        for (int fb = 0; fb < 2; fb++) {
            const uint8_t* f = md.frame_buffer(fb);
            const uint32_t lt = (uint32_t(f[y * 2]) << 8) | f[y * 2 + 1];
            std::fprintf(stderr, "fb%d line %d:", fb, y);
            for (int x = 90; x < 170; x++) std::fprintf(stderr, " %02x", f[(lt * 2 + x) & 0x1ffff]);
            std::fprintf(stderr, "\n");
        }
    }
    if (std::getenv("S32X_REGS")) {
        for (int c = 0; c < 2; c++) {
            Sh2& cpu = c == 0 ? md.master() : md.slave();
            std::fprintf(stderr, "%s pc=%08x sr=%03x gbr=%08x vbr=%08x pr=%08x", c ? "slave " : "master", cpu.pc(), cpu.sr(), cpu.gbr(), cpu.vbr(), cpu.pr());
            for (int r = 0; r < 16; r++) std::fprintf(stderr, " r%d=%08x", r, cpu.r(r));
            std::fprintf(stderr, "\n");
        }
    }
    for (int c = 0; c < 2 && hist_from >= 0; c++) {
        std::sort(hist[c].begin(), hist[c].end(), [](auto& x, auto& y) { return x.second > y.second; });
        std::fprintf(stderr, "%s SH-2 top PCs:", c ? "slave" : "master");
        for (size_t i = 0; i < hist[c].size() && i < 24; i++)
            std::fprintf(stderr, " %08x:%d", hist[c][i].first, hist[c][i].second);
        std::fprintf(stderr, "\n");
    }
    if (const char* path = std::getenv("S32X_SDRAM")) {  // dump SDRAM ($06000000) at the end
        std::FILE* fp = std::fopen(path, "wb");
        for (uint32_t a = 0; a < 0x40000; a += 4) {
            const uint32_t v = md.master().debug_read32(0x26000000 + a);
            const uint8_t b[4] = {uint8_t(v >> 24), uint8_t(v >> 16), uint8_t(v >> 8), uint8_t(v)};
            std::fwrite(b, 1, 4, fp);
        }
        std::fclose(fp);
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
