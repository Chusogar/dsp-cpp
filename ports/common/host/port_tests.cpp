// Desktop tests of the board-independent port code: memfs, HID parsing,
// scaling, resampling, argument splitting and the POSIX dsp::fsx layer
// (compared with std::filesystem).
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fcntl.h>
#include <string>
#include <vector>

#include "core/fs_compat.h"
#include "hid.h"
#include "memfs.h"
#include "port.h"

namespace {

int g_failures = 0;
#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                                        \
        }                                                                        \
    } while (0)

namespace memfs = dsp::port::memfs;
namespace hid = dsp::port::hid;
using dsp::port::HostInput;

void test_memfs() {
    static const unsigned char a[] = {'P', 'K', 3, 4, 'x'};
    static const unsigned char b[] = {1, 2, 3};
    static const DspEmbeddedFile files[] = {{"game.zip", a, sizeof a}, {"media/disk.adf", b, sizeof b}};
    memfs::mount(files, 2, "/embedded");
    CHECK(memfs::owns("/embedded/game.zip"));
    CHECK(memfs::owns("/embedded"));
    CHECK(!memfs::owns("/embeddedx/game.zip"));
    CHECK(std::string(memfs::relative("/embedded/media/disk.adf")) == "/media/disk.adf");
    struct stat st;
    CHECK(memfs::stat("/game.zip", &st) == 0 && S_ISREG(st.st_mode) && st.st_size == 5);
    CHECK(memfs::stat("/media", &st) == 0 && S_ISDIR(st.st_mode));
    CHECK(memfs::stat("", &st) == 0 && S_ISDIR(st.st_mode));
    CHECK(memfs::stat("/nope", &st) == -1);
    const int fd = memfs::open("/game.zip", O_RDONLY);
    CHECK(fd >= 0);
    char buf[8] = {};
    CHECK(memfs::read(fd, buf, 2) == 2 && buf[0] == 'P' && buf[1] == 'K');
    CHECK(memfs::lseek(fd, -1, SEEK_END) == 4);
    CHECK(memfs::read(fd, buf, 8) == 1 && buf[0] == 'x');
    CHECK(memfs::read(fd, buf, 8) == 0);
    CHECK(memfs::close(fd) == 0);
    CHECK(memfs::open("/game.zip", O_WRONLY) == -1);
    const int d = memfs::opendir("");
    CHECK(d >= 0);
    std::vector<std::string> names;
    const char* name;
    bool dir;
    while (memfs::readdir(d, &name, &dir)) names.push_back(std::string(name) + (dir ? "/" : ""));
    memfs::closedir(d);
    CHECK(names.size() == 2 && names[0] == "game.zip" && names[1] == "media/");
}

void test_hid_keyboard() {
    HostInput in;
    const uint8_t report[8] = {0x05, 0, 0x04, 0x52, 0x28, 0, 0, 0};  // LCtrl+LAlt, A, Up, Enter
    hid::apply_keyboard_report(report, 8, in);
    CHECK(in.key(dsp::Key::LeftCtrl) && in.left_alt && in.key(dsp::Key::A) && in.key(dsp::Key::Up) &&
          in.key(dsp::Key::Enter) && !in.key(dsp::Key::B));
    const uint8_t none[8] = {};
    hid::apply_keyboard_report(none, 8, in);
    CHECK(!in.key(dsp::Key::A) && !in.left_alt);
}

void test_hid_gamepad() {
    // A typical generic USB gamepad: X/Y bytes 0..255, 4-bit hat, 12 buttons.
    const uint8_t desc[] = {
        0x05, 0x01, 0x09, 0x05, 0xa1, 0x01,              // usage page desktop, gamepad, collection
        0x15, 0x00, 0x26, 0xff, 0x00, 0x75, 0x08, 0x95, 0x02, 0x09, 0x30, 0x09, 0x31,
        0x81, 0x02,                                      // input X, Y
        0x15, 0x00, 0x25, 0x07, 0x75, 0x04, 0x95, 0x01, 0x09, 0x39, 0x81, 0x42,  // hat
        0x75, 0x04, 0x95, 0x01, 0x81, 0x03,              // 4 bits padding
        0x05, 0x09, 0x19, 0x01, 0x29, 0x0c, 0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x0c,
        0x81, 0x02,                                      // 12 buttons
        0x75, 0x04, 0x95, 0x01, 0x81, 0x03,              // padding
        0xc0};
    hid::Gamepad pad;
    CHECK(pad.parse(desc, sizeof desc));
    CHECK(!pad.uses_report_ids());
    HostInput in;
    // X left, Y centre, hat 0x0f (released), buttons 1 and 10.
    uint8_t report[5] = {0x00, 0x80, 0x0f, 0x01, 0x02};
    pad.apply_report(report, sizeof report, in);
    CHECK(in.pad_connected && in.pad_left && !in.pad_right && !in.pad_up && !in.pad_down);
    CHECK(in.pad_a && !in.pad_b && in.pad_start && !in.pad_select);
    // Centre stick, hat = 2 (right), button 9 (select).
    uint8_t report2[5] = {0x80, 0x80, 0x02, 0x00, 0x01};
    pad.apply_report(report2, sizeof report2, in);
    CHECK(in.pad_right && !in.pad_left && !in.pad_a && in.pad_select && !in.pad_start);
}

void test_scale() {
    dsp::port::ScaleMap m;
    CHECK(m.update(256, 224, 256, 224, 320, 240));
    CHECK(m.dst_w == 256 && m.dst_h == 224 && m.dst_x == 32 && m.dst_y == 8);
    CHECK(!m.update(256, 224, 256, 224, 320, 240));
    CHECK(m.update(384, 224, 384, 224, 320, 240));
    CHECK(m.dst_w == 320 && m.dst_h == 186 && m.xs.back() == 382);
    CHECK(m.update(160, 120, 160, 120, 320, 240));
    CHECK(m.dst_w == 320 && m.dst_h == 240 && m.xs[1] == 0 && m.xs[2] == 1);
    CHECK(m.update(512, 212, 512, 424, 320, 240));  // MSX2-like: tall display aspect
    CHECK(m.dst_h == 240 && m.dst_w == 289);
}

void test_resampler() {
    dsp::port::Resampler r;
    r.reset(44100, 22050);
    std::vector<int16_t> in(4410, 1000), out;
    r.process(in, out);
    CHECK(out.size() >= 2204 && out.size() <= 2206);
    CHECK(out.back() == 1000);
    out.clear();
    r.reset(11025, 22050);
    std::vector<int16_t> ramp = {0, 100, 200, 300};
    r.process(ramp, out);
    CHECK(out.size() == 8 && out[3] == 50 && out[5] == 150);
}

void test_split_args() {
    const auto a = dsp::port::split_args("# comment\n--game c64 roms/c64\n  --tape \"my game.prg\" --dip 1:0x6f\n");
    CHECK(a.size() == 7 && a[0] == "--game" && a[2] == "roms/c64" && a[4] == "my game.prg" && a[6] == "1:0x6f");
}

void test_fs_paths() {
#if defined(DSP_FS_POSIX)
    const char* paths[] = {"", "/", "a", "a/", "/a", "a/b", "a/b/", "/a/b.zip", "a.b.c", ".hidden",
                           "dir/.hidden", "..", ".", "a/..", "/x/y/z.tar.gz", "noext.", "//", "a//b"};
    for (const char* p : paths) {
        const std::filesystem::path s(p);
        const dsp::fsx::path d(p);
        if (d.filename().string() != s.filename().string() ||
            d.extension().string() != s.extension().string() || d.stem().string() != s.stem().string()) {
            std::fprintf(stderr, "path '%s': filename/extension/stem differ\n", p);
            ++g_failures;
        }
        // "//" and "a//b" parents differ in the standard's corner cases; dsp never builds them.
        if (std::string(p) != "//" && std::string(p) != "a//b" &&
            d.parent_path().string() != s.parent_path().string()) {
            std::fprintf(stderr, "path '%s': parent '%s' vs '%s'\n", p, d.parent_path().c_str(),
                         s.parent_path().c_str());
            ++g_failures;
        }
    }
    CHECK((dsp::fsx::path("a") / "b").string() == "a/b");
    CHECK((dsp::fsx::path("a/") / "b").string() == "a/b");
    CHECK((dsp::fsx::path("a") / "/b").string() == "/b");
    CHECK(dsp::fsx::path("x/y.zip").replace_extension(".bin").string() == "x/y.bin");

    // Operations, on a scratch directory.
    namespace fs = dsp::fsx;
    std::error_code ec;
    const fs::path root = fs::temp_directory_path() / "dsp-fsx-test";
    fs::remove_all(root, ec);
    CHECK(fs::create_directories(root / "one" / "two", ec) && !ec);
    CHECK(fs::is_directory(root / "one" / "two"));
    FILE* f = std::fopen((root / "one" / "file.bin").c_str(), "wb");
    CHECK(f != nullptr);
    if (f) {
        std::fputs("hello", f);
        std::fclose(f);
    }
    CHECK(fs::is_regular_file(root / "one" / "file.bin") && fs::file_size(root / "one" / "file.bin") == 5);
    CHECK(!fs::exists(root / "missing", ec) && !ec);
    int entries = 0;
    for (const auto& e : fs::directory_iterator(root / "one", ec)) {
        ++entries;
        CHECK(e.is_directory() || e.is_regular_file());
    }
    CHECK(entries == 2);
    fs::rename(root / "one" / "file.bin", root / "one" / "renamed.bin", ec);
    CHECK(!ec && fs::exists(root / "one" / "renamed.bin"));
    CHECK(fs::remove_all(root, ec) == 4 && !ec);
    CHECK(!fs::exists(root));
    fs::directory_iterator none(root / "missing", ec);
    CHECK(ec && none == fs::directory_iterator());
#endif
}

}  // namespace

int main() {
    test_memfs();
    test_hid_keyboard();
    test_hid_gamepad();
    test_scale();
    test_resampler();
    test_split_args();
    test_fs_paths();
    if (g_failures) {
        std::fprintf(stderr, "%d check(s) failed\n", g_failures);
        return 1;
    }
    std::printf("port tests passed\n");
    return 0;
}
