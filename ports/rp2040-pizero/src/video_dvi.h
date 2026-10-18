#pragma once

#include <stdint.h>

// HDMI output of the RP2040-PiZero port: 640x480 60 Hz from a 320x240 logical
// screen, each pixel doubled. There is no frame buffer: core 1 builds each
// line straight from what is on show (the RP2040 has 264 KB of RAM) and TMDS
// encodes it.
#define VIDEO_WIDTH 320
#define VIDEO_HEIGHT 240

#ifdef __cplusplus
extern "C" {
#endif

// Starts the DVI output on core 1; the system clock must already be 252 MHz.
void video_init(void);

// Shows a driver's 8-bit picture (`w` x `h`, at most 320 x 240, centred)
// through its 256-entry ARGB palette. The pixels are read while they are
// displayed, so `pens` must stay valid (it is the driver's own picture).
void video_show_indexed(const uint8_t* pens, const uint32_t* palette, int w, int h);
// The same with 4 bits a pixel (two a byte, the low nibble on the left).
void video_show_indexed4(const uint8_t* pens, const uint32_t* palette, int w, int h);

// Machines that hand over rows of 1-bit red/green/blue planes (the QL): from
// now on core 1 calls `row(source, y, r, g, b)` for each row it needs, with
// planes width / 8 bytes long; null `row` goes back to the other modes. Only
// the 8-colour full-resolution build (DSP_VIDEO_RGB1) supports it: false
// otherwise. `width` up to 640, `height` up to 480.
typedef void (*VideoRowFn)(const void* source, int y, uint8_t* red, uint8_t* green, uint8_t* blue);
int video_show_rows(VideoRowFn row, const void* source, int width, int height);

// Up to 8 characters (8x8 glyphs, bit 0 = leftmost pixel) shown in white at
// the top left over a game, e.g. its speed; count 0 hides them.
void video_set_osd(const uint8_t (*glyphs)[8], int count);

// Menu screens (and drivers without an 8-bit picture): `rows` rows of 320
// ARGB pixels from row `y`, kept with 16 colours (4 bits a pixel, 38 KB).
void video_menu_rows(int y, int rows, const uint32_t* argb);
// Frees the menu screen (black until the next one or a game picture).
void video_release_menu(void);

#ifdef __cplusplus
}
#endif
