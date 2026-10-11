#pragma once

#include "port.h"

#ifndef DSP_USB_DP_PIN
#define DSP_USB_DP_PIN 28  // PIO-USB port of the RP2350-PiZero: D+ GPIO28, D- GPIO29
#endif

// Starts the USB host on the PIO-USB port. Call before the video starts (both
// claim PIO state machines and DMA channels).
void usb_input_init();
// Runs the USB stack; call often (every frame and while in the menus).
void usb_input_task();
// Current keyboard/mouse/gamepad state; mouse motion is consumed.
void usb_input_read(dsp::port::HostInput& out);
