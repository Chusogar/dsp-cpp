#pragma once

// TinyUSB configuration of the RP2350-PiZero port: USB host on the PIO-USB
// port (root port 1, GPIO28 D+ / GPIO29 D-) for keyboards, mice and HID
// gamepads, through a hub too. The native USB port is left for power and
// flashing.

#ifndef CFG_TUSB_MCU
#define CFG_TUSB_MCU OPT_MCU_RP2040
#endif
#ifndef CFG_TUSB_OS
#define CFG_TUSB_OS OPT_OS_PICO
#endif
#define CFG_TUSB_DEBUG 0

#define CFG_TUH_ENABLED 1
#define CFG_TUH_RPI_PIO_USB 1
#define BOARD_TUH_RHPORT 1
#define CFG_TUSB_RHPORT1_MODE OPT_MODE_HOST
#define CFG_TUH_MAX_SPEED OPT_MODE_FULL_SPEED

#define CFG_TUH_ENUMERATION_BUFSIZE 512
#define CFG_TUH_HUB 1
#define CFG_TUH_DEVICE_MAX (3 * CFG_TUH_HUB + 1)
#define CFG_TUH_HID 8
#define CFG_TUH_HID_EPIN_BUFSIZE 64
#define CFG_TUH_HID_EPOUT_BUFSIZE 64

#define CFG_TUH_MEM_SECTION
#define CFG_TUH_MEM_ALIGN __attribute__((aligned(4)))
