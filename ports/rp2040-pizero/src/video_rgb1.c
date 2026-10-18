// 8-colour full-resolution HDMI for the RP2040-PiZero port (DSP_VIDEO_RGB1,
// e.g. the Sinclair QL build): 640x480 60 Hz, every one of the 480 lines
// TMDS encoded on its own (DVI_VERTICAL_REPEAT 1) from three 1-bit planes
// with tmds_encode_1bpp (about 2 cycles a pixel), so a 512-pixel-wide machine
// keeps all its pixels. No frame buffer: core 1 asks the machine for each row
// (video_show_rows), or builds it from the 4-bit menu screen or a driver's
// 8-bit picture, whose colours become 8 by thresholding each channel.
//
// Waveshare RP2040-PiZero HDMI: D0 GPIO26/27, D1 24/25, D2 22/23, CLK 28/29.
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
#include "video_dvi.h"

#define OUT_W 640
#define OUT_H 480
#define PLANE_BYTES (OUT_W / 8)

static struct dvi_inst g_dvi;

static const struct dvi_serialiser_cfg kPiZeroDvi = {
    .pio = pio1,
    .sm_tmds = {0, 1, 2},
    .pins_tmds = {26, 24, 22},
    .pins_clk = 28,
    .invert_diffpairs = false,
};

enum { MODE_MENU, MODE_INDEXED, MODE_ROWS };
static volatile int g_mode = MODE_MENU;

// MODE_ROWS
static volatile VideoRowFn g_row_fn = 0;
static const void* volatile g_row_source = 0;
static int g_row_w = 0, g_row_left = 0;  // left border in bytes
static int16_t g_row_of_line[OUT_H];     // -1: border

// MODE_INDEXED
static volatile const uint8_t* g_pens = 0;
static volatile int g_w = 0, g_h = 0;
static volatile int g_bits = 8;  // 4: two pixels a byte, low nibble first
static uint8_t g_game_rgb[256];  // palette as 3 bits: 4 red, 2 green, 1 blue

// MODE_MENU: 4 bits a pixel (38 KB), allocated while a menu is up.
static uint8_t* volatile g_menu_pixels = 0;
static uint32_t g_menu_palette[16];
static uint8_t g_menu_rgb[16];
static int g_menu_colours = 0;

static uint32_t g_planes[3][PLANE_BYTES / 4];  // red, green, blue
static uint8_t g_osd[8][8];
static volatile int g_osd_count = 0;

static uint8_t rgb3(uint32_t argb) {
    return (uint8_t)((((argb >> 16) & 0xff) >= 0x80 ? 4 : 0) | (((argb >> 8) & 0xff) >= 0x80 ? 2 : 0) |
                     ((argb & 0xff) >= 0x80 ? 1 : 0));
}

static void __not_in_flash_func(clear_planes)(void) {
    for (int c = 0; c < 3; ++c)
        for (int i = 0; i < PLANE_BYTES / 4; ++i) g_planes[c][i] = 0;
}

// `n` source pixels of 3-bit colour, each two output pixels wide, from byte
// `at` of the planes (n a multiple of 4).
static void __not_in_flash_func(put_doubled)(int at, const uint8_t* colours, const uint8_t* src, int n,
                                             int four_bits) {
    uint8_t* r = (uint8_t*)g_planes[0] + at;
    uint8_t* g = (uint8_t*)g_planes[1] + at;
    uint8_t* b = (uint8_t*)g_planes[2] + at;
    for (int i = 0; i < n; i += 4, ++r, ++g, ++b) {
        uint8_t rr = 0, gg = 0, bb = 0;
        for (int k = 0; k < 4; ++k) {
            const int idx = four_bits ? (((src[(i + k) >> 1] >> (((i + k) & 1) * 4)) & 15)) : src[i + k];
            const uint8_t c = colours[idx];
            const uint8_t two = (uint8_t)(3 << (2 * k));
            if (c & 4) rr |= two;
            if (c & 2) gg |= two;
            if (c & 1) bb |= two;
        }
        *r = rr;
        *g = gg;
        *b = bb;
    }
}

// Returns 0 when the planes are those of the previous line (nothing to do).
static int __not_in_flash_func(build_line)(int y, int* last_key) {
    const int mode = g_mode;
    if (mode == MODE_ROWS) {
        const int row = g_row_of_line[y];
        const int key = (mode << 16) | (row & 0xffff);
        if (key == *last_key && y >= 8) return 0;
        *last_key = y < 8 ? -1 : key;  // the speed overlay changes per line
        clear_planes();
        const VideoRowFn fn = g_row_fn;
        if (row >= 0 && fn) {
            const int at = g_row_left;
            fn((const void*)g_row_source, row, (uint8_t*)g_planes[0] + at, (uint8_t*)g_planes[1] + at,
               (uint8_t*)g_planes[2] + at);
        }
        if (y < 8) {  // speed in the top left corner, white (in the border of a 512-wide machine)
            const int n = g_osd_count;
            for (int c = 0; c < n; ++c)
                for (int p = 0; p < 3; ++p) ((uint8_t*)g_planes[p])[c] |= g_osd[c][y];
        }
        return 1;
    }
    const int src_row = y >> 1;  // 320x240 screens, every row twice
    if (mode == MODE_MENU) {
        const int key = (mode << 16) | src_row;
        if (key == *last_key) return 0;
        *last_key = key;
        const uint8_t* pixels = g_menu_pixels;
        if (pixels == 0) {
            clear_planes();
            return 1;
        }
        put_doubled(0, g_menu_rgb, pixels + (size_t)src_row * (VIDEO_WIDTH / 2), VIDEO_WIDTH, 1);
        return 1;
    }
    const int w = g_w, h = g_h;
    const uint8_t* pens = (const uint8_t*)g_pens;
    const int top = (VIDEO_HEIGHT - h) / 2, row = src_row - top;
    const int key = (mode << 16) | src_row;
    if (key == *last_key) return 0;
    *last_key = key;
    clear_planes();
    if (pens != 0 && row >= 0 && row < h) {
        const int shown = (w < VIDEO_WIDTH ? w : VIDEO_WIDTH) & ~3;
        const int left = ((VIDEO_WIDTH - shown) / 2) & ~3;
        if (g_bits == 4)
            put_doubled(left / 4, g_game_rgb, pens + (size_t)row * (size_t)((w + 1) / 2), shown, 1);
        else
            put_doubled(left / 4, g_game_rgb, pens + (size_t)row * (size_t)w, shown, 0);
    }
    if (src_row < 8) {  // speed in the top border: white, 8 pixels a character, doubled
        const int n = g_osd_count;
        for (int c = 0; c < n; ++c) {
            const uint8_t bits = g_osd[c][src_row];
            uint16_t wide = 0;
            for (int bit = 0; bit < 8; ++bit)
                if ((bits >> bit) & 1) wide |= (uint16_t)(3u << (2 * bit));
            for (int p = 0; p < 3; ++p) {
                ((uint8_t*)g_planes[p])[2 * c] |= (uint8_t)wide;
                ((uint8_t*)g_planes[p])[2 * c + 1] |= (uint8_t)(wide >> 8);
            }
        }
    }
    return 1;
}

static void __not_in_flash_func(core1_main)(void) {
    dvi_register_irqs_this_core(&g_dvi, DMA_IRQ_0);
    dvi_start(&g_dvi);
    const uint words = g_dvi.timing->h_active_pixels / DVI_SYMBOLS_PER_WORD;
    int last_key = -1;
    const uint32_t* last_tmds = 0;
    for (int y = 0;; y = y + 1 == OUT_H ? 0 : y + 1) {
        const int changed = build_line(y, &last_key);
        uint32_t* tmds;
        queue_remove_blocking_u32(&g_dvi.q_tmds_free, &tmds);
        if (!changed && last_tmds != 0) {
            // Same row as the line before (still queued or on show, so intact):
            // copy its symbols, a third of the work of encoding them again.
            const uint32_t* src = last_tmds;
            if (src != tmds)
                for (uint i = 0; i < 3 * words; i += 4) {
                    tmds[i] = src[i];
                    tmds[i + 1] = src[i + 1];
                    tmds[i + 2] = src[i + 2];
                    tmds[i + 3] = src[i + 3];
                }
        } else {
            tmds_encode_1bpp(g_planes[2], tmds + 0 * words, OUT_W);  // lane 0: blue
            tmds_encode_1bpp(g_planes[1], tmds + 1 * words, OUT_W);  // lane 1: green
            tmds_encode_1bpp(g_planes[0], tmds + 2 * words, OUT_W);  // lane 2: red
        }
        last_tmds = tmds;
        queue_add_blocking_u32(&g_dvi.q_tmds_valid, &tmds);
    }
}

static void leave_menu(void) {
    if (g_mode != MODE_MENU) return;
    uint8_t* pixels = g_menu_pixels;
    g_mode = MODE_INDEXED;  // anything but the menu: core 1 stops reading it
    sleep_us(200);          // a line takes ~32 us
    g_menu_pixels = 0;
    free(pixels);
}

int video_show_rows(VideoRowFn row, const void* source, int width, int height) {
    if (row == 0) {
        g_mode = MODE_INDEXED;
        g_pens = 0;
        sleep_us(200);  // core 1 is done with the machine
        g_row_fn = 0;
        g_row_source = 0;
        return 1;
    }
    if (width > OUT_W || height > OUT_H || height <= 0) return 0;
    leave_menu();
    g_row_w = width;
    g_row_left = ((OUT_W - width) / 2) / 8;
    // Rows to lines: doubled when they fit twice, else spread over 480.
    const int doubled = height * 2 <= OUT_H;
    const int top = doubled ? (OUT_H - 2 * height) / 2 : 0;
    for (int y = 0; y < OUT_H; ++y) {
        int row = doubled ? (y - top) / 2 : (int)((long)y * height / OUT_H);
        if (y < top || row >= height) row = -1;
        g_row_of_line[y] = (int16_t)row;
    }
    g_row_source = source;
    g_row_fn = row;
    g_mode = MODE_ROWS;
    return 1;
}

static void show_indexed(const uint8_t* pens, const uint32_t* palette, int w, int h, int bits);

void video_show_indexed(const uint8_t* pens, const uint32_t* palette, int w, int h) {
    show_indexed(pens, palette, w, h, 8);
}

void video_show_indexed4(const uint8_t* pens, const uint32_t* palette, int w, int h) {
    show_indexed(pens, palette, w, h, 4);
}

static void show_indexed(const uint8_t* pens, const uint32_t* palette, int w, int h, int bits) {
    for (int i = 0; i < (bits == 4 ? 16 : 256); ++i) g_game_rgb[i] = rgb3(palette[i]);
    if (g_bits != bits) g_pens = 0;  // no line read with the wrong layout
    g_bits = bits;
    g_w = w;
    g_h = h > VIDEO_HEIGHT ? VIDEO_HEIGHT : h;
    g_pens = pens;
    leave_menu();
    g_mode = MODE_INDEXED;
}

void video_set_osd(const uint8_t (*glyphs)[8], int count) {
    if (count > 8) count = 8;
    g_osd_count = 0;
    memcpy(g_osd, glyphs, (size_t)count * 8);
    g_osd_count = count;
}

static unsigned menu_index(uint32_t argb) {
    argb |= 0xff000000u;
    for (int i = 0; i < g_menu_colours; ++i)
        if (g_menu_palette[i] == argb) return (unsigned)i;
    if (g_menu_colours < 16) {
        g_menu_palette[g_menu_colours] = argb;
        // Menu colours are mostly dark blues and greys: anything brighter
        // than mid-grey becomes white so text stays readable.
        const uint32_t sum = ((argb >> 16) & 255) + ((argb >> 8) & 255) + (argb & 255);
        g_menu_rgb[g_menu_colours] = rgb3(argb) ? rgb3(argb) : (sum >= 3 * 0x60 ? 7 : 0);
        return (unsigned)g_menu_colours++;
    }
    return 0;
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
    if (g_mode == MODE_ROWS) return;  // a machine is on show
    if (g_menu_pixels == 0) {
        uint8_t* pixels = (uint8_t*)calloc(VIDEO_WIDTH * VIDEO_HEIGHT / 2, 1);
        if (pixels == 0) return;
        g_menu_colours = 1;
        g_menu_pixels = pixels;
    }
    g_mode = MODE_MENU;
    uint8_t* const menu = g_menu_pixels;
    for (int r = 0; r < rows && y + r < VIDEO_HEIGHT; ++r) {
        uint8_t* dst = menu + (size_t)(y + r) * (VIDEO_WIDTH / 2);
        const uint32_t* src = argb + (size_t)r * VIDEO_WIDTH;
        for (int x = 0; x < VIDEO_WIDTH; x += 2)
            dst[x / 2] = (uint8_t)(menu_index(src[x]) | (menu_index(src[x + 1]) << 4));
    }
}

void video_init(void) {
    g_dvi.timing = &dvi_timing_640x480p_60hz;
    g_dvi.ser_cfg = kPiZeroDvi;
    g_dvi.scanline_callback = 0;
    dvi_init(&g_dvi, next_striped_spin_lock_num(), next_striped_spin_lock_num());
    g_menu_palette[0] = 0xff000000u;
    g_menu_rgb[0] = 0;
    g_menu_colours = 1;
    hw_set_bits(&bus_ctrl_hw->priority, BUSCTRL_BUS_PRIORITY_PROC1_BITS);
    multicore_launch_core1(core1_main);
}
