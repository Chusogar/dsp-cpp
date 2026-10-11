#pragma once

#include <stdint.h>

#define VIDEO_WIDTH 320
#define VIDEO_HEIGHT 240

#ifdef __cplusplus
extern "C" {
#endif

// RGB565 picture shown on HDMI (written by the Hal, read by core 1).
extern uint16_t video_framebuffer[VIDEO_WIDTH * VIDEO_HEIGHT];

// Starts the DVI output; the system clock must already be 252 MHz.
void video_init(void);

#ifdef __cplusplus
}
#endif
