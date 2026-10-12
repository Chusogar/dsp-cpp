#!/usr/bin/env python3
"""Fills dsp_cyd/src/gen/ with what the ESP32 CYD firmware compiles besides
board.cpp, lcd.cpp and touch.cpp: one small .cpp/.c per dsp source of the
chosen drivers (each #includes the file from the repository), the port core,
zlib, dsp_drivers.h, dsp_port_config.h (panel, touch and pin settings) and,
optionally, the embedded media.

    python3 prepare.py --drivers "a2600;gameboy"
    python3 prepare.py --panel st7789 --touch-cal 300,3800,200,3700
    python3 prepare.py --drivers blockout --embed-game blockout --embed-rom ~/roms/blockout.zip
    python3 prepare.py --print-flags

build.sh and the PlatformIO script (pio_prepare.py) call it.
"""

import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "common"))
import arduino_prepare  # noqa: E402

# Drivers small enough to have a chance without PSRAM (see the README).
DEFAULT_DRIVERS = "a2600;atari_lynx;gameboy"


def add_options(p):
    p.add_argument("--panel", choices=["ili9341", "st7789"], default="ili9341",
                   help="display controller: ili9341 (ESP32-2432S028R) or st7789 (CYD2USB)")
    p.add_argument("--invert", dest="invert", action="store_true", default=None,
                   help="turn display inversion on (default: on for st7789, off for ili9341)")
    p.add_argument("--no-invert", dest="invert", action="store_false")
    p.add_argument("--swap-colours", action="store_true",
                   help="swap red and blue (RGB/BGR order) when the colours look wrong")
    p.add_argument("--flip", action="store_true", help="turn the picture (and the touch zones) 180 degrees")
    p.add_argument("--lcd-mhz", type=int, default=40, help="display SPI clock (default 40)")
    p.add_argument("--touch-cal", default="200,3700,240,3800",
                   help="raw touch readings at the left,right,top,bottom edges (default 200,3700,240,3800)")
    p.add_argument("--touch-swap", action="store_true", help="swap the touch X and Y axes")
    p.add_argument("--touch-flip-x", action="store_true")
    p.add_argument("--touch-flip-y", action="store_true")
    p.add_argument("--touch-debug", action="store_true", help="print raw touch readings on the serial port")
    p.add_argument("--buttons", default="0,-1,-1,-1",
                   help="GPIOs of buttons A,B,START,SELECT to ground, -1 = none (default 0,-1,-1,-1: BOOT is A)")
    p.add_argument("--sd-pins", default="19,23,18,5", help="micro-SD MISO,MOSI,CLK,CS (default 19,23,18,5)")


def config(args):
    st7789 = args.panel == "st7789"
    invert = args.invert if args.invert is not None else st7789
    # ILI9341 panels of the CYD are BGR, the ST7789 ones RGB; --swap-colours swaps them.
    bgr = (not st7789) != args.swap_colours
    x0, x1, y0, y1 = (int(v) for v in args.touch_cal.split(","))
    a, b, start, select = (int(v) for v in args.buttons.split(","))
    miso, mosi, clk, cs = (int(v) for v in args.sd_pins.split(","))
    flip_x = args.touch_flip_x != args.flip
    flip_y = args.touch_flip_y != args.flip
    b01 = lambda v: 1 if v else 0  # noqa: E731
    return [
        f"#define DSP_LCD_ST7789 {b01(st7789)}", f"#define DSP_LCD_INVERT {b01(invert)}",
        f"#define DSP_LCD_BGR {b01(bgr)}", f"#define DSP_LCD_FLIP {b01(args.flip)}",
        f"#define DSP_LCD_MHZ {args.lcd_mhz}",
        f"#define DSP_TOUCH_X_MIN {x0}", f"#define DSP_TOUCH_X_MAX {x1}",
        f"#define DSP_TOUCH_Y_MIN {y0}", f"#define DSP_TOUCH_Y_MAX {y1}",
        f"#define DSP_TOUCH_SWAP_XY {b01(args.touch_swap)}",
        f"#define DSP_TOUCH_FLIP_X {b01(flip_x)}", f"#define DSP_TOUCH_FLIP_Y {b01(flip_y)}",
        f"#define DSP_TOUCH_DEBUG {b01(args.touch_debug)}",
        f"#define DSP_BTN_A {a}", f"#define DSP_BTN_B {b}",
        f"#define DSP_BTN_START {start}", f"#define DSP_BTN_SELECT {select}",
        f"#define DSP_SD_MISO {miso}", f"#define DSP_SD_MOSI {mosi}", f"#define DSP_SD_CLK {clk}",
        f"#define DSP_SD_CS {cs}",
        "#define DSP_LOOP_STACK_BYTES (16 * 1024)",
    ]


if __name__ == "__main__":
    arduino_prepare.main(HERE, "dsp_cyd", "ESP32 CYD", DEFAULT_DRIVERS, add_options, config,
                         extra=["esp32_support.cpp"])
