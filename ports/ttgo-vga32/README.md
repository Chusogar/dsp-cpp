# dsp-cpp on the LilyGO TTGO VGA32

Firmware for the [LilyGO TTGO VGA32](https://github.com/LilyGO/FabGL) v1.4
(ESP32-WROVER-B with 4 MB of PSRAM), built on the Arduino-ESP32 core and
[FabGL](https://github.com/fdivitto/FabGL). See [`../README.md`](../README.md)
for what the ports share (menu, SD card layout, embedded games, keys).

> Compiles with the scripts below; **not yet tried on the board**.

## Hardware

| Function | Pins | Notes |
|---|---|---|
| VGA | R 22/21, G 19/18, B 5/4, HSync 23, VSync 15 | FabGL `VGAController`, 320x240 at 60 Hz, 64 colours |
| PS/2 keyboard | CLK 33, DATA 32 | port 0 |
| PS/2 mouse | CLK 26, DATA 27 | port 1 (Atari ST / Amiga mouse) |
| Sound | GPIO25 (DAC1, I2S0) | 22050 Hz mono, to the board's 3.5 mm jack |
| micro-SD | MISO 2, MOSI 12, CLK 14, CS 13 | FAT32, mounted at `/sd`; other pins: `--sd-pins MISO,MOSI,CLK,CS` |

Older boards (v1.1-v1.3, no PSRAM) do not have the memory for dsp.

## Building with arduino-cli (`build.sh`)

Needs [arduino-cli](https://arduino.github.io/arduino-cli/), git and python3.

```sh
cd ports/ttgo-vga32
./setup.sh                    # ESP32 core 2.0.17, FabGL 1.0.9, zlib (once)
./build.sh                    # default drivers: galaxian arabian bagman mrdo sg1000 spectrum
./build.sh -p /dev/ttyUSB0    # build and flash (-p COM3 on Windows)
```

Choosing drivers and embedding a game:

```sh
./build.sh -d "galaxian;bagman;mrdo"
./build.sh -d blockout --embed-game blockout --embed-rom ~/roms/blockout.zip
./build.sh -d spectrum --embed-game spectrum48 --embed-rom ~/roms/zx48 --embed-tape ~/games/manic.tzx
```

`build.sh` runs `prepare.py`, which fills `dsp_vga32/src/gen/` with one tiny
file per dsp source the chosen drivers need (each `#include`s the file from
the repository), the port core, zlib and the generated `dsp_drivers.h`; then it
compiles the `dsp_vga32` sketch with C++17, `-DDSP_FS_POSIX=1` and the
"Huge APP" partition scheme (3 MB of program). The result is in `build/`.

Environment variables: `ARDUINO_CLI` (the arduino-cli binary),
`ARDUINO_CLI_FLAGS` (e.g. `--config-file my.yaml`), `CTAGS_DIR` (when the
core's ctags is missing).

### Arduino IDE

Install "esp32 by Espressif" **2.0.17** and the FabGL 1.0.9 library, run
`./setup.sh --zlib-only` and `python3 prepare.py --drivers "..."`, then open
`dsp_vga32/dsp_vga32.ino`. The IDE cannot add include folders, so copy the
flags printed by `python3 prepare.py --print-flags` plus
`-std=gnu++17 -DDSP_FS_POSIX=1` into `compiler.cpp.extra_flags` (and the `-I`
ones into `compiler.c.extra_flags`) of a `platform.local.txt` next to the
core's `platform.txt`. Board: "ESP32 Dev Module", PSRAM enabled, partition
scheme "Huge APP". `build.sh` does all of this for you.

## Building with PlatformIO

```sh
cd ports/ttgo-vga32
./setup.sh --zlib-only
pio run -t upload
```

`platformio.ini` uses platform `espressif32@6.9.0` (Arduino core 2.0.17) and
FabGL 1.0.9; set the drivers and the embedded game with its `custom_dsp_*`
options. (The PlatformIO configuration could not be built in the environment
the port was written in; `build.sh` was.)

## SD card

```
/sd/roms/galaxian.zip        ROM sets named after the driver (or an alias)
/sd/roms/zx48/48.rom         ... or folders
/sd/dsp/manic.txt            command lines shown in the menu
/sd/dsp/autostart.txt        started at power-up
```

Format the card as FAT32. The serial monitor (115200 baud) shows the log,
including the frames per second every 5 seconds.

## Notes and limits

* The ESP32 runs the emulation on one core at 240 MHz; FabGL's VGA output,
  the PS/2 decoding (ULP) and the sound task use the rest. Simple 8-bit
  machines are the ones with a chance of full speed. Frames are skipped
  (up to 3 in a row) when the emulation falls behind.
* Large ROM sets and machines are allocated in the 4 MB PSRAM; drivers that
  need more than that, and `model3`/`macii`, cannot run here. Every other
  driver compiles for the ESP32, but their static tables do not all fit in
  the internal RAM at once: build a few at a time.
* The C++ code is compiled with the core's Xtensa GCC 8.4, which has no
  `std::filesystem` library: that is why dsp has its own POSIX file layer.
