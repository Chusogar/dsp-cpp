// Headless Atari ST runner: st_run ROMSET FRAMES OUTPREFIX [diskA [diskB]]
// Saves a screenshot every ST_EVERY frames (default 50) and prints the PC.
// ST_KEYS="frame:key:down,..." (key = dsp::Key index),
// ST_MOUSE="frame:dx:dy:button,..." (relative motion in host pixels),
// ST_JOY="frame:bits,..." (1 up, 2 down, 4 left, 8 right, 16 fire; held
// until the next entry). Debugging: ST_TRAPS=1 logs GEMDOS/BIOS/XBIOS calls,
// ST_ITRACE=from:to[:lo:hi] disassembles, ST_DUMP_PC=pc dumps RAM there,
// ST_IPL=1 logs interrupt mask changes, ST_FMARK=1 marks frames on stderr.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "drivers/computers/atari_st.h"

using namespace dsp;

extern "C" {
#include "cpu/musashi/m68k.h"
}

static AtariSt* g_st = nullptr;
extern "C" unsigned int m68k_read_disassembler_8(unsigned int a) { return g_st->peek(a & 0xffffff); }
extern "C" unsigned int m68k_read_disassembler_16(unsigned int a) {
    return (m68k_read_disassembler_8(a) << 8) | m68k_read_disassembler_8(a + 1);
}
extern "C" unsigned int m68k_read_disassembler_32(unsigned int a) {
    return (m68k_read_disassembler_16(a) << 16) | m68k_read_disassembler_16(a + 2);
}

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

template <typename T, typename F>
static std::vector<T> parse(const char* env, F fn) {
    std::vector<T> out;
    if (!env) return out;
    std::string s = env;
    size_t pos = 0;
    while (pos < s.size()) {
        size_t end = s.find(',', pos);
        if (end == std::string::npos) end = s.size();
        T v{};
        if (fn(s.substr(pos, end - pos).c_str(), v)) out.push_back(v);
        pos = end + 1;
    }
    return out;
}

int main(int argc, char** argv) {
    if (argc < 4) {
        std::fprintf(stderr, "usage: %s ROMSET FRAMES OUTPREFIX [diskA [diskB]]\n", argv[0]);
        return 1;
    }
    AtariSt st;
    std::string error;
    if (!st.init(argv[1], &error)) {
        std::fprintf(stderr, "init: %s\n", error.c_str());
        return 1;
    }
    for (int i = 4; i < argc; i++) {  // drive A, then drive B
        if (!st.load_media(argv[i], &error)) {
            std::fprintf(stderr, "disk: %s\n", error.c_str());
            return 1;
        }
    }
    st.reset();
    int cur_frame = 0;
    g_st = &st;
    // ST_ITRACE=from_frame:to_frame[:lo:hi] disassembles every instruction
    // (optionally only PCs in lo..hi, hex). ST_DUMP_PC=pc dumps RAM the first
    // time that PC is reached.
    int it_from = -1, it_to = -1;
    unsigned it_lo = 0, it_hi = 0xffffff;
    if (const char* t = std::getenv("ST_ITRACE")) std::sscanf(t, "%d:%d:%x:%x", &it_from, &it_to, &it_lo, &it_hi);
    long dump_pc = std::getenv("ST_DUMP_PC") ? std::strtol(std::getenv("ST_DUMP_PC"), nullptr, 16) : -1;
    const bool traps = std::getenv("ST_TRAPS") != nullptr;
    if (traps || it_from >= 0 || dump_pc >= 0 || std::getenv("ST_IPL")) {
        st.debug_cpu().set_instruction_hook([&](uint32_t pc) {
            if (long(pc) == dump_pc) {
                FILE* fp = std::fopen(std::getenv("ST_DUMP_FILE") ? std::getenv("ST_DUMP_FILE") : "st_pc.ram", "wb");
                for (uint32_t a = 0; a < AtariSt::kRamSize; a++) std::fputc(st.peek(a), fp);
                std::fclose(fp);
                std::fprintf(stderr, "[%d] dumped at %06x\n", cur_frame, pc);
                dump_pc = -1;
            }
            if (cur_frame >= it_from && cur_frame <= it_to && pc >= it_lo && pc <= it_hi) {
                char buf[128];
                m68k_disassemble(buf, pc, M68K_CPU_TYPE_68000);
                auto& c = st.debug_cpu();
                std::fprintf(stderr, "[%d] %06x %-32s d0=%08x d1=%08x d2=%08x a0=%08x a1=%08x a6=%08x a7=%08x sr=%d%d\n",
                             cur_frame, pc, buf, c.d[0].l, c.d[1].l, c.d[2].l, c.a[0].l, c.a[1].l, c.a[6].l, c.a[7].l,
                             c.cc.s, c.cc.t);
            }
            static int last_im = -1;
            if (std::getenv("ST_IPL") && st.debug_cpu().cc.im != last_im) {
                std::fprintf(stderr, "[%d] IPL %d -> %d at %06x s=%d\n", cur_frame, last_im, st.debug_cpu().cc.im, pc,
                             st.debug_cpu().cc.s);
                last_im = st.debug_cpu().cc.im;
            }
            if (!traps) return;
            const uint16_t op = uint16_t((st.peek(pc) << 8) | st.peek(pc + 1));
            if (op != 0x4e41 && op != 0x4e4d && op != 0x4e4e) return;
            const uint32_t sp = st.debug_a(7);
            auto w = [&](uint32_t a) { return uint16_t((st.peek(a) << 8) | st.peek(a + 1)); };
            std::fprintf(stderr, "[%d] %06x trap#%d fn=%02x args=%04x %04x %04x %04x %04x %04x\n", cur_frame, pc,
                         op & 15, w(sp), w(sp + 2), w(sp + 4), w(sp + 6), w(sp + 8), w(sp + 10), w(sp + 12));
        });
    }
    st.debug_cpu().set_exception_handler([&](uint32_t vector, uint32_t pc) {
        if (vector >= 2 && vector <= 10 && (vector != 9 || std::getenv("ST_TRACE9")))
            std::fprintf(stderr, "[%d] EXC vector=%u pc=%06x\n", cur_frame, vector, pc);
    });
    struct Key3 { int f, k, d; };
    auto keys = parse<Key3>(std::getenv("ST_KEYS"), [](const char* s, Key3& k) {
        return std::sscanf(s, "%d:%d:%d", &k.f, &k.k, &k.d) == 3;
    });
    struct Mv { int f, dx, dy, b; };
    auto moves = parse<Mv>(std::getenv("ST_MOUSE"), [](const char* s, Mv& m) {
        return std::sscanf(s, "%d:%d:%d:%d", &m.f, &m.dx, &m.dy, &m.b) == 4;
    });
    struct Joy { int f, bits; };
    auto joys = parse<Joy>(std::getenv("ST_JOY"), [](const char* s, Joy& j) {
        return std::sscanf(s, "%d:%d", &j.f, &j.bits) == 2;
    });
    const int every = std::getenv("ST_EVERY") ? std::atoi(std::getenv("ST_EVERY")) : 50;
    const int frames = std::atoi(argv[2]);
    MachineInputs in;
    bool button = false;
    for (int f = 1; f <= frames; f++) {
        cur_frame = f;
        in.has_pointer = true;
        in.pointer_relative = true;
        in.pointer_dx = in.pointer_dy = 0;
        for (auto& k : keys) {
            if (k.f == f && k.k >= 0 && k.k < int(Key::Count)) in.keys[size_t(k.k)] = k.d != 0;
        }
        for (auto& m : moves) {
            if (m.f != f) continue;
            in.pointer_dx += m.dx;
            in.pointer_dy += m.dy;
            button = m.b != 0;
        }
        in.pointer_button1 = button;
        for (auto& j : joys) {
            if (j.f != f) continue;
            in.player1.up = j.bits & 1;
            in.player1.down = j.bits & 2;
            in.player1.left = j.bits & 4;
            in.player1.right = j.bits & 8;
            in.player1.button1 = j.bits & 16;
        }
        st.set_inputs(in);
        if (std::getenv("ST_FMARK")) std::fprintf(stderr, "F %d pc=%06x\n", f, st.debug_pc());
        st.run_frame();
        if (f % every == 0 || f == frames) {
            char name[512];
            std::snprintf(name, sizeof name, "%s_%05d.bmp", argv[3], f);
            write_bmp(name, st.framebuffer(), st.screen_width(), st.screen_height());
            std::printf("frame %d pc=%06x\n", f, st.debug_pc());
            std::fflush(stdout);
        }
    }
    if (const char* d = std::getenv("ST_DUMP")) {
        FILE* fp = std::fopen(d, "wb");
        for (uint32_t a = 0; a < AtariSt::kRamSize; a++) std::fputc(st.peek(a), fp);
        std::fclose(fp);
    }
    return 0;
}
