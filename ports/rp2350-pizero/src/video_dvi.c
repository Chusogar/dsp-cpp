// HDMI picture of the RP2350-PiZero port: PicoDVI (Luke Wren's libdvi) at
// 640x480 60 Hz from a 320x240 RGB565 frame buffer, each pixel doubled.
// The TMDS pins of the board (GPIO32-39) are only reachable by a PIO whose
// GPIO base is 16, so PIO1 is moved there; core 1 does the TMDS encoding.
#include "video_dvi.h"

#include "dvi.h"
#include "dvi_serialiser.h"
#include "dvi_timing.h"
#include "hardware/irq.h"
#include "hardware/pio.h"
#include "pico/multicore.h"
#include "pico/sync.h"

uint16_t video_framebuffer[VIDEO_WIDTH * VIDEO_HEIGHT] __attribute__((aligned(4)));

static struct dvi_inst g_dvi;

// Waveshare RP2350-PiZero HDMI: D0 GPIO36/37, D1 34/35, D2 32/33, CLK 38/39.
static const struct dvi_serialiser_cfg kPiZeroDvi = {
    .pio = pio1,
    .sm_tmds = {0, 1, 2},
    .pins_tmds = {36, 34, 32},
    .pins_clk = 38,
    .invert_diffpairs = false,
};

static void core1_scanline_callback(void) {
    uint16_t* bufptr;
    while (queue_try_remove_u32(&g_dvi.q_colour_free, &bufptr))
        ;
    static uint scanline = 2;  // the first two lines are queued before the start
    bufptr = &video_framebuffer[VIDEO_WIDTH * scanline];
    queue_add_blocking_u32(&g_dvi.q_colour_valid, &bufptr);
    scanline = (scanline + 1) % VIDEO_HEIGHT;
}

static void core1_main(void) {
    dvi_register_irqs_this_core(&g_dvi, DMA_IRQ_0);
    dvi_start(&g_dvi);
    dvi_scanbuf_main_16bpp(&g_dvi);
    __builtin_unreachable();
}

void video_init(void) {
    pio_set_gpio_base(pio1, 16);
    g_dvi.timing = &dvi_timing_640x480p_60hz;
    g_dvi.ser_cfg = kPiZeroDvi;
    g_dvi.scanline_callback = core1_scanline_callback;
    dvi_init(&g_dvi, next_striped_spin_lock_num(), next_striped_spin_lock_num());
    for (unsigned i = 0; i < VIDEO_WIDTH * VIDEO_HEIGHT; ++i) video_framebuffer[i] = 0;
    uint16_t* bufptr = video_framebuffer;
    queue_add_blocking_u32(&g_dvi.q_colour_valid, &bufptr);
    bufptr += VIDEO_WIDTH;
    queue_add_blocking_u32(&g_dvi.q_colour_valid, &bufptr);
    multicore_launch_core1(core1_main);
}
