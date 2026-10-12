#!/usr/bin/env python3
"""Fills dsp_vga32/src/gen/ with what the TTGO VGA32 firmware compiles besides
board.cpp: one small .cpp/.c per dsp source of the chosen drivers (each just
#includes the file from the repository, so nothing is copied), the port core,
zlib, the generated dsp_drivers.h and, optionally, the embedded media.

    python3 prepare.py --drivers "galaxian;bagman"
    python3 prepare.py --drivers blockout --embed-game blockout --embed-rom ~/roms/blockout.zip
    python3 prepare.py --print-flags         # include flags for the compiler

build.sh and the PlatformIO script (pio_prepare.py) call it; it can also be
run by hand before opening dsp_vga32/ in the Arduino IDE (see the README).
"""

import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "common"))
import arduino_prepare  # noqa: E402


def add_options(p):
    p.add_argument("--sd-pins", default="2,12,14,13", help="micro-SD MISO,MOSI,CLK,CS (default 2,12,14,13)")


def config(args):
    miso, mosi, clk, cs = (int(x) for x in args.sd_pins.split(","))
    return [f"#define DSP_SD_MISO {miso}", f"#define DSP_SD_MOSI {mosi}", f"#define DSP_SD_CLK {clk}",
            f"#define DSP_SD_CS {cs}", "#define DSP_LOOP_STACK_BYTES (32 * 1024)"]


if __name__ == "__main__":
    arduino_prepare.main(HERE, "dsp_vga32", "TTGO VGA32", "galaxian;arabian;bagman;mrdo;sg1000;spectrum",
                         add_options, config, extra=["esp32_support.cpp"])
