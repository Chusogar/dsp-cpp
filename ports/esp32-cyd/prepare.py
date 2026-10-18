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
                   help="turn display inversion on (default: off)")
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
    p.add_argument("--nunchuk", nargs="?", const="22,27", default=None, metavar="SDA,SCL",
                   help="Wii Nunchuk on I2C (default pins 22,27: the CN1 connector, as galagino)")
    p.add_argument("--nunchuk-threshold", type=int, default=30,
                   help="stick dead zone around the centre, 0-127 (default 30, as galagino)")
    p.add_argument("--boot-coin-start", action="store_true",
                   help="BOOT inserts a coin then presses START, held = menu (galagino's button)")
    p.add_argument("--galagino", dest="galagino", action="store_true", default=True,
                   help="galagino's CYD controls (the default): --nunchuk 22,27 --boot-coin-start, BOOT no longer A")
    p.add_argument("--no-galagino", dest="galagino", action="store_false",
                   help="no nunchuk; BOOT is button A (the touch zones as the only pad)")
    p.add_argument("--no-sound", action="store_true",
                   help="no sound: drivers skip making it (faster), no I2S/DAC buffers")
    p.add_argument("--sd-pins", default="19,23,18,5", help="micro-SD MISO,MOSI,CLK,CS (default 19,23,18,5)")


def config(args):
    st7789 = args.panel == "st7789"
    # Inversion off and BGR order: right on the ESP32-2432S028R (ILI9341) and on
    # a 2-USB ESP32-2432S028 tried with the st7789 set-up. Other ST7789 panels
    # may want --invert and/or --swap-colours.
    invert = args.invert if args.invert is not None else False
    bgr = not args.swap_colours
    x0, x1, y0, y1 = (int(v) for v in args.touch_cal.split(","))
    a, b, start, select = (int(v) for v in args.buttons.split(","))
    nunchuk = args.nunchuk
    boot_coin = args.boot_coin_start
    if args.galagino:
        nunchuk = nunchuk or "22,27"
        boot_coin = True
    if boot_coin:
        # BOOT is GPIO0: not also a plain button.
        a, b, start, select = (-1 if v == 0 else v for v in (a, b, start, select))
    sda, scl = (int(v) for v in (nunchuk or "-1,-1").split(","))
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
        f"#define DSP_NUNCHUK {b01(nunchuk)}", f"#define DSP_NUNCHUK_SDA {sda}",
        f"#define DSP_NUNCHUK_SCL {scl}", f"#define DSP_NUNCHUK_THRESHOLD {args.nunchuk_threshold}",
        f"#define DSP_BOOT_COIN_START {b01(boot_coin)}",
        f"#define DSP_NO_SOUND {b01(args.no_sound)}",
        "#define DSP_LOOP_STACK_BYTES (16 * 1024)",
    ]


if __name__ == "__main__":
    arduino_prepare.main(HERE, "dsp_cyd", "ESP32 CYD", DEFAULT_DRIVERS, add_options, config,
                         extra=["esp32_support.cpp"])
