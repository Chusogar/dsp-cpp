# dsp-cpp on the ESP32 "Cheap Yellow Display"

Firmware for the ESP32 **Cheap Yellow Display** (ESP32-2432S028R: ESP32-WROOM-32,
2.8" 320x240 ILI9341 touch screen, micro-SD, speaker connector) and its
"CYD2USB" variant with an ST7789 panel. Built on the Arduino-ESP32 core 2.0.17
with no extra libraries: the display, touch and sound drivers are in this
folder. See [`../README.md`](../README.md) for what the ports share (menu, SD
card layout, embedded games).

> Compiles with the scripts below; **not yet tried on the board**.

> Indiana Jones and the Temple of Doom, Road Runner and Gauntlet run on a
> stock CYD as single-game firmware built from [`../lean/`](../lean/README.md).

## Memory: read this first

The standard CYD has **no PSRAM**: about 300 KB of RAM in total, and the
largest single block the heap can give is around **110 KB**. dsp's drivers
keep their picture as a full-size 32-bit frame buffer inside the machine
object, so even the smallest ones need more than that in one piece
(measured with `ports/common/host`'s `dsp-port-mem`, see below):

| Driver | Machine object (one allocation) | Peak heap |
|---|---|---|
| a2600 (Atari 2600) | 124 KB | 124 KB |
| atari_lynx (`lynx`) | 130 KB | 130 KB |
| gameboy (`gb`) | 140 KB | 140 KB |
| wonderswan | 192 KB | 192 KB |
| arabian | 234 KB | 544 KB |
| sg1000 | 273 KB | 273 KB |
| galaxian | 645 KB | 869 KB |

So on a stock CYD the firmware boots and shows the menu, but almost
certainly cannot start any driver yet: it reports "not enough memory for this driver on this board" (the serial log
prints the free heap and the largest block). It runs on boards in the CYD
format **with PSRAM** — an ESP32-WROVER module fitted in place of the WROOM is
a known modification — built with `--psram`. Making the small drivers fit a
stock CYD needs them to render with less memory (a 16-bit or line-based frame
buffer); that is a change to the drivers, not to this port.

## Hardware

| Function | Pins | Notes |
|---|---|---|
| Display | HSPI: SCK 14, MOSI 13, CS 15, DC 2, backlight 21 | ILI9341 (default) or ST7789 (`--panel st7789`), landscape, 40 MHz SPI with DMA |
| Touch | XPT2046: CLK 25, MOSI 32, MISO 39, CS 33, IRQ 36 | read by bit-banging; calibration with `--touch-cal` |
| Nunchuk | CN1: SDA 22, SCL 27 (default; `--no-galagino` leaves it out) | I2C 400 kHz, address 0x52 |
| Buttons | BOOT (GPIO0) = coin, then START; held = menu (with `--no-galagino`: A) | more with `--buttons A,B,START,SELECT` (e.g. GPIO22 and 27 on the CN1/P3 connectors; wire to ground) |
| Sound | speaker connector (amplifier on GPIO26, DAC2) | I2S0 built-in DAC, 22050 Hz mono |
| micro-SD | VSPI: SCK 18, MISO 19, MOSI 23, CS 5 | FAT32, mounted at `/sd` |
| RGB LED | 4, 16, 17 | switched off |

## Controls

The nunchuk (below) is the controller. The touch screen also works as a
game pad (the XPT2046 only senses one finger at a time):

```
+------------+------------+------------+
|            |  SELECT    |            |
|   D-PAD    |  (coin)    |     B      |
|            +------------+            |
|  push away |  START     +------------+
|  from the  |            |            |
|  centre of +------------+     A      |
|  this third|  MENU      |            |
|            | (hold)     |            |
+------------+------------+------------+
```

In the menu, the d-pad moves and A (or START) starts. Holding MENU half a
second leaves the game.

### Galagino's controller (the default)

Every CYD build uses it (`--no-galagino` leaves it out). The same as [galagino](https://github.com/harbaum/galagino) on the CYD: a Wii
Nunchuk (or clone) on the **CN1** connector, I2C on GPIO22 (SDA) and GPIO27
(SCL), plus 3.3 V and ground, and the BOOT button:

| Input | Does |
|---|---|
| Nunchuk stick | d-pad (dead zone 30 around the centre, `--nunchuk-threshold`) |
| Z | A (fire) |
| C | B (second button, e.g. jump) |
| BOOT tap | a coin, then START (in the menu: starts the game) |
| BOOT held 1.5 s | back to the menu |

The touch zones keep working. The parts can be had separately:
`--nunchuk [SDA,SCL]` (default 22,27) and `--boot-coin-start`. The nunchuk can
be plugged in at any time; the serial log says when it is found or lost.

## Building with arduino-cli (`build.sh`)

Needs [arduino-cli](https://arduino.github.io/arduino-cli/), git and python3.

```sh
cd ports/esp32-cyd
./setup.sh                      # ESP32 core 2.0.17 and zlib (once)
./build.sh                      # default drivers: a2600 atari_lynx gameboy
./build.sh -p /dev/ttyUSB0      # build and flash
```

Options (all go to `prepare.py`, see `python3 prepare.py --help`):

```sh
./build.sh --psram                              # board with PSRAM
./build.sh --panel st7789                       # CYD with 2 USB ports (ESP32-2432S028 USB-C + micro-USB)
./build.sh --invert / --swap-colours            # if the colours look wrong (negative / red-blue)
./build.sh --flip                               # picture turned 180 degrees
./build.sh --touch-debug                        # raw touch readings on the serial port
./build.sh --touch-cal 300,3800,200,3700        # raw values at left,right,top,bottom
./build.sh --touch-swap --touch-flip-x          # if the touch axes are swapped/mirrored
./build.sh --buttons 0,22,27,-1                 # A=BOOT, B=GPIO22, START=GPIO27
./build.sh -d a2600 --embed-game a2600 --embed-rom ~/roms/game.a26
./build.sh -d "tetris_atari_hw;c64;spectrum" --psram \
    --embed-entry "Atari Tetris=--game atetris $HOME/roms/atetris.zip" \
    --embed-entry "Commodore 64=--game c64 $HOME/roms/c64" \
    --embed-entry "ZX Spectrum 48K=--game spectrum48 $HOME/roms/zx48"
```

To calibrate the touch screen: build with `--touch-debug`, open the serial
monitor at 115200 baud, touch near each edge and use the raw numbers with
`--touch-cal`.

## Building with PlatformIO

```sh
cd ports/esp32-cyd
./setup.sh --zlib-only
pio run -t upload               # or: pio run -e cyd-psram -t upload
```

Drivers and the other options are the `custom_dsp_*` lines of
`platformio.ini` (`custom_dsp_prepare_args` takes any `prepare.py` option).
The PlatformIO configuration could not be built in the environment the port
was written in; `build.sh` was.

## Files

| File | |
|---|---|
| `dsp_cyd/board.cpp` | set-up, touch-to-gamepad mapping and the `Hal` of `ports/common` |
| `dsp_cyd/lcd.cpp` | ILI9341/ST7789 driver (ESP-IDF `spi_master`, DMA, two 16-line buffers) |
| `dsp_cyd/touch.cpp` | XPT2046 driver |
| `prepare.py` | chooses drivers, writes `dsp_cyd/src/gen/` (uses `../common/arduino_prepare.py`) |
| `setup.sh`, `build.sh` | arduino-cli build |
| `platformio.ini`, `pio_prepare.py` | PlatformIO build |

Sound and the embedded-media file system are shared with the TTGO VGA32 port
(`../common/esp32_support.cpp`).

## Measuring a driver's memory

```sh
cmake -S ports/common/host -B build-port-mem -DDSP_PORT_DRIVERS=all
cmake --build build-port-mem --target dsp-port-mem
build-port-mem/dsp-port-mem --roms /path/to/roms        # or: ... a2600 gb lynx
```

It prints, for every driver name, the size of the machine object, the largest
single allocation and the peak heap (after starting it with the ROM in
`--roms`, when there is one).
