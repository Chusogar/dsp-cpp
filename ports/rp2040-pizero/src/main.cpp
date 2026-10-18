// dsp-cpp on the Waveshare RP2040-PiZero (RP2040, 264 KB SRAM, no PSRAM,
// 16 MB flash, HDMI, micro-SD). Board set-up and the Hal of ports/common; the
// emulator itself is the normal dsp code. Only drivers written for little
// RAM fit (the ESP32 CYD's ones, e.g. Elevator Action): they hand over an
// 8-bit picture that core 1 turns into HDMI lines, and read their ROMs in
// place from flash.
#include <cstdio>
#include <cstring>
#include <malloc.h>
#include <new>
#include <string>

#include "audio_pwm.h"
#include "font8x8.h"
#include "core/rom_loader.h"
#include "hardware/clocks.h"
#include "hardware/vreg.h"
#include "memfs.h"
#include "pico/stdlib.h"
#include "port.h"
#include "posix_io.h"
#include "usb_input.h"
#include "video_dvi.h"

#ifndef DSP_MAIN_STACK_BYTES
#define DSP_MAIN_STACK_BYTES (16 * 1024)
#endif

extern "C" char __StackLimit;  // linker: end of the heap area
extern "C" char end;           // linker: start of the heap area

namespace {

size_t g_failed_request = 0;  // size of the last allocation that did not fit

// The largest block malloc can still give (halving search).
size_t largest_block() {
    size_t lo = 0, hi = size_t(&__StackLimit - &end);
    while (hi - lo > 256) {
        const size_t mid = lo + (hi - lo) / 2;
        if (void* p = malloc(mid)) {
            free(p);
            lo = mid;
        } else {
            hi = mid;
        }
    }
    return lo;
}

size_t heap_free() {
    const struct mallinfo m = mallinfo();
    const size_t arena = size_t(&__StackLimit - &end);
    return arena - size_t(m.uordblks);
}

class PiZeroHal : public dsp::port::Hal {
public:
    const char* board_name() const override { return "RP2040-PiZero"; }
    bool turbo_on_f12() const override { return true; }
    int screen_width() const override { return VIDEO_WIDTH; }
    int screen_height() const override { return VIDEO_HEIGHT; }

    bool show_rgb1_rows(const dsp::Machine* machine) override {
        if (machine == nullptr) return video_show_rows(nullptr, nullptr, 0, 0) != 0;
        const dsp::Machine::Rgb1Rows rows = machine->rgb1_rows();
        return video_show_rows(rows.fn, rows.source, machine->screen_width(), machine->screen_height()) != 0;
    }

    bool crops_picture() const override { return true; }
    std::string memory_note() override {
        const struct mallinfo m = mallinfo();
        char text[128];
        std::snprintf(text, sizeof(text), "asked %u KB; %u KB free, largest %u KB; heap %u KB used of %u, top %u",
                      unsigned((g_failed_request + 1023) / 1024), unsigned(heap_free() / 1024),
                      unsigned(largest_block() / 1024), unsigned(m.uordblks / 1024),
                      unsigned(m.arena / 1024), unsigned(m.keepcost / 1024));
        return text;
    }
    // The 38 KB menu screen goes before a driver loads.
    void release_menu() override { video_release_menu(); }

    bool present_indexed(const uint8_t* pens, const uint32_t* palette, int width, int height,
                         int, int) override {
        video_show_indexed(pens, palette, width, height);
        return true;
    }
    bool present_indexed4(const uint8_t* pens, const uint32_t* palette, int width, int height,
                          int, int) override {
        video_show_indexed4(pens, palette, width, height);
        return true;
    }

    // Drivers without an 8-bit picture: scaled into the 16-colour screen
    // (they rarely fit in this RAM anyway).
    void present(const uint32_t* argb, int width, int height, int display_width,
                 int display_height) override {
        map_.update(width, height, display_width, display_height, VIDEO_WIDTH, VIDEO_HEIGHT);
        static uint32_t row[VIDEO_WIDTH];
        for (int y = 0; y < VIDEO_HEIGHT; ++y) {
            const int sy = y - map_.dst_y;
            for (int x = 0; x < VIDEO_WIDTH; ++x) {
                const int sx = x - map_.dst_x;
                row[x] = (sy < 0 || sy >= map_.dst_h || sx < 0 || sx >= map_.dst_w)
                             ? 0xff000000u
                             : argb[size_t(map_.ys[size_t(sy)]) * size_t(width) + map_.xs[size_t(sx)]];
            }
            video_menu_rows(y, 1, row);
        }
    }

    void draw_rows(int y, int rows, const uint32_t* argb) override {
        video_menu_rows(y, rows, argb);
        map_.invalidate();
    }

    int output_rate() const override { return audio_pwm_rate(); }
    void push_audio(const int16_t* samples, size_t count) override { audio_pwm_push(samples, count); }
    size_t audio_queued() const override { return audio_pwm_queued(); }

    void poll(dsp::port::HostInput& input) override {
        usb_input_task();
        usb_input_read(input);
    }
    uint64_t micros() override { return time_us_64(); }
    void sleep_us(uint32_t us) override {
        const uint64_t end = time_us_64() + us;
        while (time_us_64() + 1000 < end) {
            usb_input_task();
            ::sleep_us(1000);
        }
        const uint64_t now = time_us_64();
        if (now < end) ::sleep_us(end - now);
    }
    void idle() override { usb_input_task(); }

    std::string storage_root() override {
        if (!dsp_sd_mounted()) dsp_sd_mount();
        return dsp_sd_mounted() ? "/sd" : "";
    }
    bool mount_embedded(const DspEmbeddedFile* files, size_t count, std::string* dir,
                        std::string*) override {
        dsp::port::memfs::mount(files, count, "/embedded");
        // Drivers that can read their ROMs in place get them straight from flash.
        dsp::RomLoader::set_file_mapper(
            [](const std::string& path, size_t* size) { return dsp::port::memfs::map(path.c_str(), size); });
        *dir = "/embedded";
        return true;
    }
    void log(const char* text) override {
        std::printf("%s  [heap %u KB free]\n", text, unsigned(heap_free() / 1024));
        show_speed(text);
    }

private:
    // The port logs "<game>: 23.4 fps (target 60.0)" every 5 seconds: show the
    // number on screen too, since the serial log needs an adapter.
    static void show_speed(const char* text) {
        static bool turbo = false;  // "T" in front of the speed while it is on
        if (std::strcmp(text, "turbo on") == 0 || std::strcmp(text, "turbo off") == 0) {
            turbo = text[7] == 'n';
            uint8_t glyphs[8][8];
            const char* label = turbo ? "TURBO" : "";
            int n = 0;
            for (const char* p = label; *p; ++p, ++n) std::memcpy(glyphs[n], dsp::port::kFont8x8[*p - 0x20], 8);
            video_set_osd(glyphs, n);
            return;
        }
        const char* fps = std::strstr(text, " fps (target");
        if (fps == nullptr) return;
        const char* start = fps;
        while (start > text && (start[-1] == '.' || (start[-1] >= '0' && start[-1] <= '9'))) --start;
        uint8_t glyphs[8][8];
        int n = 0;
        if (turbo) std::memcpy(glyphs[n++], dsp::port::kFont8x8['T' - 0x20], 8);
        for (const char* p = start; p < fps && n < 5; ++p, ++n)
            std::memcpy(glyphs[n], dsp::port::kFont8x8[*p - 0x20], 8);
        for (const char* p = "fps"; *p && n < 8; ++p, ++n) std::memcpy(glyphs[n], dsp::port::kFont8x8[*p - 0x20], 8);
        video_set_osd(glyphs, n);
    }

    dsp::port::ScaleMap map_;
};

uint64_t g_main_stack[DSP_MAIN_STACK_BYTES / 8];

[[noreturn]] void run_port() {
    PiZeroHal hal;
    dsp::port::run(hal);
    for (;;) tight_loop_contents();
}

[[noreturn]] void run_on_big_stack() {
    void* top = g_main_stack + DSP_MAIN_STACK_BYTES / 8;
    __asm volatile(
        "mov sp, %0\n"
        "bx %1\n"
        :
        : "r"(top), "r"(run_port)
        : "memory");
    __builtin_unreachable();
}

}  // namespace

int main() {
    // 640x480 DVI needs a 252 MHz system clock (the TMDS bit rate).
    vreg_set_voltage(VREG_VOLTAGE_1_20);
    sleep_ms(10);
    set_sys_clock_khz(252000, true);
    stdio_init_all();  // UART0, GPIO0 TX / GPIO1 RX, 115200
    std::printf("dsp-cpp RP2040-PiZero, %u MHz, heap %u KB free\n",
                unsigned(clock_get_hz(clk_sys) / 1000000), unsigned(heap_free() / 1024));

    usb_input_init();
    video_init();
    audio_pwm_init();
    std::printf("SD card: %s\n", dsp_sd_mount() ? "mounted" : "not found");

    // The SDK's core 0 stack is 2 KB in SCRATCH_Y: too small for the emulator,
    // so it runs on its own stack in main RAM.
    run_on_big_stack();
}

// Records what did not fit, for the "not enough memory" message.
void* operator new(size_t size) {
    if (void* p = malloc(size ? size : 1)) return p;
    g_failed_request = size;
    throw std::bad_alloc();
}
void* operator new[](size_t size) { return operator new(size); }
void operator delete(void* p) noexcept { free(p); }
void operator delete[](void* p) noexcept { free(p); }
void operator delete(void* p, size_t) noexcept { free(p); }
void operator delete[](void* p, size_t) noexcept { free(p); }
