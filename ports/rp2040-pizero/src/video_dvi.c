// HDMI picture of the RP2040-PiZero port: PicoDVI (Luke Wren's libdvi) at
// 640x480 60 Hz. With 264 KB of RAM there is no room for a 320x240 RGB565
// frame buffer (150 KB) next to a driver, so core 1 makes every line itself:
// it turns one row of the driver's 8-bit picture (or of the 4-bit menu
// screen) into 320 RGB565 pixels through a colour table and TMDS encodes it
// like libdvi's dvi_scanbuf_main_16bpp (each pixel doubled by the encoder,
// each line sent twice: DVI_VERTICAL_REPEAT). Full-resolution palette
// encoding (tmds_encode_palette_data) is too slow for one RP2040 core here:
// it left the picture red (no line ready in time).
//
// Waveshare RP2040-PiZero HDMI: D0 GPIO26/27, D1 24/25, D2 22/23, CLK 28/29
// (the pin set pico-infonesPlus uses for this board).
#include "video_dvi.h"

#include <stdlib.h>
#include <string.h>

#include "dvi.h"
#include "dvi_serialiser.h"
#include "dvi_timing.h"
#include "hardware/irq.h"
#include "hardware/structs/bus_ctrl.h"
#include "pico/multicore.h"
#include "pico/sync.h"
#include "pico/time.h"
#include "tmds_encode.h"

static struct dvi_inst g_dvi;

static const struct dvi_serialiser_cfg kPiZeroDvi = {
    .pio = pio1,
    .sm_tmds = {0, 1, 2},
    .pins_tmds = {26, 24, 22},
    .pins_clk = 28,
    .invert_diffpairs = false,
};

// What core 1 shows. Written by core 0; a change takes effect from the next
// line (a frame may show half of each, like a missed vsync).
static volatile const uint8_t* g_pens = 0;
static volatile int g_w = 0, g_h = 0;
// Pictures larger than the screen are cropped around their centre (the
// Spectrum's 352x280 with its border).
static volatile int g_skip_x = 0, g_skip_y = 0;
static volatile int g_bits = 8;  // 4: two pixels a byte, low nibble first
static volatile bool g_menu = true;

static uint16_t g_game_colours[256];  // RGB565 of the driver's palette
// 4 bits a pixel (38 KB), allocated while a menu is up: a running game gets
// the RAM back.
static uint8_t* volatile g_menu_pixels = 0;
static uint32_t g_menu_palette[16];
static uint16_t g_menu_colours565[16];
static int g_menu_colours = 0;
static uint16_t g_line[VIDEO_WIDTH] __attribute__((aligned(4)));
static uint8_t g_osd[8][8];
static volatile int g_osd_count = 0;

static inline uint16_t rgb565(uint32_t argb) {
    return (uint16_t)(((argb >> 8) & 0xf800) | ((argb >> 5) & 0x07e0) | ((argb >> 3) & 0x001f));
}

static void __not_in_flash_func(build_game_line)(int y) {
    const int w = g_w, h = g_h;
    const uint8_t* pens = (const uint8_t*)g_pens;
    const int top = (VIDEO_HEIGHT - h) / 2;
    const int row = y - top;
    int x = 0;
    if (pens != 0 && row >= 0 && row < h) {
        const int shown = w < VIDEO_WIDTH ? w : VIDEO_WIDTH;
        const int left = (VIDEO_WIDTH - shown) / 2;
        for (; x < left; ++x) g_line[x] = 0;
        if (g_bits == 4) {
            const uint8_t* src = pens + (size_t)(row + g_skip_y) * (size_t)((w + 1) / 2);
            const int sx = g_skip_x;
            for (int i = 0; i < shown; ++i) {
                const int p = sx + i;
                const uint8_t b = src[p >> 1];
                g_line[x++] = g_game_colours[(p & 1) ? (b >> 4) : (b & 15)];
            }
        } else {
            const uint8_t* src = pens + (size_t)(row + g_skip_y) * (size_t)w + g_skip_x;
            for (int i = 0; i < shown; ++i) g_line[x++] = g_game_colours[src[i]];
        }
    }
    for (; x < VIDEO_WIDTH; ++x) g_line[x] = 0;
    if (y < 8) {
        const int n = g_osd_count;
        for (int c = 0; c < n; ++c) {
            const uint8_t bits = g_osd[c][y];
            for (int b = 0; b < 8; ++b)
                if ((bits >> b) & 1) g_line[c * 8 + b] = 0xffff;
        }
    }
}

int video_show_rows(VideoRowFn row, const void* source, int width, int height) {
    (void)row, (void)source, (void)width, (void)height;
    return 0;  // this build makes 320-wide 16-bit lines; see video_rgb1.c
}

void video_set_osd(const uint8_t (*glyphs)[8], int count) {
    if (count > 8) count = 8;
    g_osd_count = 0;
    memcpy(g_osd, glyphs, (size_t)count * 8);
    g_osd_count = count;
}

static void __not_in_flash_func(build_menu_line)(int y) {
    const uint8_t* pixels = g_menu_pixels;
    if (pixels == 0) {
        for (int x = 0; x < VIDEO_WIDTH; ++x) g_line[x] = 0;
        return;
    }
    const uint8_t* src = pixels + (size_t)y * (VIDEO_WIDTH / 2);
    for (int x = 0; x < VIDEO_WIDTH / 2; ++x) {
        g_line[2 * x] = g_menu_colours565[src[x] & 15];
        g_line[2 * x + 1] = g_menu_colours565[src[x] >> 4];
    }
}

static void __not_in_flash_func(core1_main)(void) {
    dvi_register_irqs_this_core(&g_dvi, DMA_IRQ_0);
    dvi_start(&g_dvi);
    const uint words = g_dvi.timing->h_active_pixels / DVI_SYMBOLS_PER_WORD;
    for (int y = 0;; y = y + 1 == VIDEO_HEIGHT ? 0 : y + 1) {
        if (g_menu) build_menu_line(y);
        else build_game_line(y);
        uint32_t* tmds;
        queue_remove_blocking_u32(&g_dvi.q_tmds_free, &tmds);
        const uint32_t* pix = (const uint32_t*)g_line;
        tmds_encode_data_channel_16bpp(pix, tmds + 0 * words, VIDEO_WIDTH, DVI_16BPP_BLUE_MSB, DVI_16BPP_BLUE_LSB);
        tmds_encode_data_channel_16bpp(pix, tmds + 1 * words, VIDEO_WIDTH, DVI_16BPP_GREEN_MSB, DVI_16BPP_GREEN_LSB);
        tmds_encode_data_channel_16bpp(pix, tmds + 2 * words, VIDEO_WIDTH, DVI_16BPP_RED_MSB, DVI_16BPP_RED_LSB);
        queue_add_blocking_u32(&g_dvi.q_tmds_valid, &tmds);
    }
}

static void show_indexed(const uint8_t* pens, const uint32_t* palette, int w, int h, int bits);

void video_show_indexed(const uint8_t* pens, const uint32_t* palette, int w, int h) {
    show_indexed(pens, palette, w, h, 8);
}

void video_show_indexed4(const uint8_t* pens, const uint32_t* palette, int w, int h) {
    show_indexed(pens, palette, w, h, 4);
}

static void show_indexed(const uint8_t* pens, const uint32_t* palette, int w, int h, int bits) {
    for (int i = 0; i < (bits == 4 ? 16 : 256); ++i) g_game_colours[i] = rgb565(palette[i]);
    if (g_bits != bits) g_pens = 0;  // no line read with the wrong layout
    g_bits = bits;
    g_w = w;
    g_h = h > VIDEO_HEIGHT ? VIDEO_HEIGHT : h;
    g_skip_x = w > VIDEO_WIDTH ? (w - VIDEO_WIDTH) / 2 : 0;
    g_skip_y = h > VIDEO_HEIGHT ? (h - VIDEO_HEIGHT) / 2 : 0;
    g_pens = pens;
    if (g_menu) {
        g_menu = false;
        // Core 1 may still be building a menu line: let it finish (a line
        // takes ~64 us) before the menu screen goes.
        uint8_t* pixels = g_menu_pixels;
        sleep_us(200);
        g_menu_pixels = 0;
        free(pixels);
    }
}

static unsigned menu_index(uint32_t argb) {
    argb |= 0xff000000u;
    for (int i = 0; i < g_menu_colours; ++i)
        if (g_menu_palette[i] == argb) return (unsigned)i;
    if (g_menu_colours < 16) {
        g_menu_palette[g_menu_colours] = argb;
        g_menu_colours565[g_menu_colours] = rgb565(argb);
        return (unsigned)g_menu_colours++;
    }
    // Full: the nearest colour already there.
    unsigned best = 0;
    int best_d = 1 << 30;
    for (int i = 0; i < 16; ++i) {
        const int dr = (int)((argb >> 16) & 255) - (int)((g_menu_palette[i] >> 16) & 255);
        const int dg = (int)((argb >> 8) & 255) - (int)((g_menu_palette[i] >> 8) & 255);
        const int db = (int)(argb & 255) - (int)(g_menu_palette[i] & 255);
        const int d = dr * dr + dg * dg + db * db;
        if (d < best_d) best_d = d, best = (unsigned)i;
    }
    return best;
}

// Frees the menu screen's pixels (a black screen until the next menu or
// game picture): the driver about to start may need the RAM.
void video_release_menu(void) {
    uint8_t* pixels = g_menu_pixels;
    if (pixels == 0) return;
    g_menu_pixels = 0;
    // Core 1 may still be building a line from it (a line takes ~64 us).
    sleep_us(200);
    free(pixels);
}

void video_menu_rows(int y, int rows, const uint32_t* argb) {
    if (g_menu_pixels == 0) {
        uint8_t* pixels = (uint8_t*)calloc(VIDEO_WIDTH * VIDEO_HEIGHT / 2, 1);
        if (pixels == 0) return;  // no room: the game's picture stays
        g_menu_colours = 1;       // new colours (black is entry 0)
        g_menu_pixels = pixels;
    }
    g_menu = true;
    uint8_t* const menu = g_menu_pixels;
    for (int r = 0; r < rows && y + r < VIDEO_HEIGHT; ++r) {
        uint8_t* dst = menu + (size_t)(y + r) * (VIDEO_WIDTH / 2);
        const uint32_t* src = argb + (size_t)r * VIDEO_WIDTH;
        uint32_t last = ~src[0];
        unsigned index = 0;
        for (int x = 0; x < VIDEO_WIDTH; x += 2) {
            if (src[x] != last) last = src[x], index = menu_index(last);
            const unsigned lo = index;
            if (src[x + 1] != last) last = src[x + 1], index = menu_index(last);
            dst[x / 2] = (uint8_t)(lo | (index << 4));
        }
    }
}

void video_init(void) {
    g_dvi.timing = &dvi_timing_640x480p_60hz;
    g_dvi.ser_cfg = kPiZeroDvi;
    g_dvi.scanline_callback = 0;
    dvi_init(&g_dvi, next_striped_spin_lock_num(), next_striped_spin_lock_num());
    g_menu_palette[0] = 0xff000000u;
    g_menu_colours565[0] = 0;
    g_menu_colours = 1;
    // Core 1 must never wait for the bus behind core 0 (emulation from flash).
    hw_set_bits(&bus_ctrl_hw->priority, BUSCTRL_BUS_PRIORITY_PROC1_BITS);
    multicore_launch_core1(core1_main);
}
