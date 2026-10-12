// dsp-cpp on the Waveshare RP2350-PiZero (RP2350B, 8 MB PSRAM, HDMI, micro-SD,
// PIO-USB). Board set-up and the Hal of ports/common; the emulator itself is
// the normal dsp code.
#include <cstdio>
#include <cstring>

#include "audio_pwm.h"
#include "hardware/clocks.h"
#include "hardware/vreg.h"
#include "memfs.h"
#include "pico/stdlib.h"
#include "port.h"
#include "posix_io.h"
#include "psram.h"
#include "usb_input.h"
#include "video_dvi.h"

#ifndef DSP_PSRAM_CS_PIN
#define DSP_PSRAM_CS_PIN 47
#endif
#ifndef DSP_MAIN_STACK_BYTES
#define DSP_MAIN_STACK_BYTES (48 * 1024)
#endif

namespace {

inline uint16_t rgb565(uint32_t argb) {
    return uint16_t(((argb >> 8) & 0xf800) | ((argb >> 5) & 0x07e0) | ((argb >> 3) & 0x001f));
}

class PiZeroHal : public dsp::port::Hal {
public:
    const char* board_name() const override { return "RP2350-PiZero"; }
    int screen_width() const override { return VIDEO_WIDTH; }
    int screen_height() const override { return VIDEO_HEIGHT; }

    void present(const uint32_t* argb, int width, int height, int display_width,
                 int display_height) override {
        if (map_.update(width, height, display_width, display_height, VIDEO_WIDTH, VIDEO_HEIGHT))
            std::memset(video_framebuffer, 0, sizeof video_framebuffer);
        for (int y = 0; y < map_.dst_h; ++y) {
            const uint32_t* src = argb + size_t(map_.ys[size_t(y)]) * size_t(width);
            uint16_t* dst = &video_framebuffer[size_t(map_.dst_y + y) * VIDEO_WIDTH + size_t(map_.dst_x)];
            const uint16_t* xs = map_.xs.data();
            for (int x = 0; x < map_.dst_w; ++x) dst[x] = rgb565(src[xs[x]]);
        }
    }

    void draw_rows(int y, int rows, const uint32_t* argb) override {
        uint16_t* dst = &video_framebuffer[size_t(y) * VIDEO_WIDTH];
        for (size_t i = 0; i < size_t(rows) * VIDEO_WIDTH; ++i) dst[i] = rgb565(argb[i]);
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
        // Keep the USB stack running through long waits.
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
        *dir = "/embedded";
        return true;
    }
    void log(const char* text) override { std::printf("%s\n", text); }

private:
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
    std::printf("dsp-cpp RP2350-PiZero, %u MHz\n", unsigned(clock_get_hz(clk_sys) / 1000000));

    const size_t psram = dsp_psram_setup(DSP_PSRAM_CS_PIN);
    if (psram) dsp_heap_add_psram(DSP_PSRAM_BASE, psram);
    std::printf("PSRAM: %u KB\n", unsigned(psram / 1024));

    usb_input_init();  // before the video: PIO0 + DMA channel 0
    video_init();
    audio_pwm_init();
    std::printf("SD card: %s\n", dsp_sd_mount() ? "mounted" : "not found");

    // The SDK's core 0 stack is 2 KB in SCRATCH_Y, right above core 1's: far
    // too small for the emulator, so it runs on its own stack in main RAM.
    run_on_big_stack();
}
