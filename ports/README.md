# Microcontroller ports

dsp-cpp also builds as firmware for three boards. They run the same emulator
code as the desktop version (drivers, CPUs, sound chips, `RomLoader`); only
the front end is different.

| Folder | Board | Picture | Input | Sound | Files |
|---|---|---|---|---|---|
| [`ttgo-vga32/`](ttgo-vga32/README.md) | LilyGO TTGO VGA32 v1.4 (ESP32-WROVER, 4 MB PSRAM) | VGA 320x240, 64 colours | PS/2 keyboard and mouse | DAC on GPIO25 | micro-SD |
| [`esp32-cyd/`](esp32-cyd/README.md) | ESP32 "Cheap Yellow Display" (ESP32-2432S028R / CYD2USB) | 2.8" ILI9341 or ST7789, 320x240 | touch screen as a game pad, BOOT button | speaker (DAC on GPIO26) | micro-SD |
| [`rp2350-pizero/`](rp2350-pizero/README.md) | Waveshare RP2350-PiZero (RP2350B, 8 MB PSRAM) | HDMI 640x480 (320x240 doubled), 65536 colours | USB keyboard, mouse or HID gamepad (PIO-USB port) | PWM on GPIO18 | micro-SD |

> **Status:** all three firmwares compile with the scripts here (ESP32 core 2.0.17
> + FabGL 1.0.9; Pico SDK 2.1.1 + PicoDVI), and the shared front end is run
> and tested on the desktop (`common/host`). They have **not been tried on the
> real boards yet**: expect to adjust things, and keep in mind that the
> heavier drivers (16/32-bit machines) will not reach full speed on these
> chips. The 8-bit arcade games and computers are the realistic targets.
> The stock CYD has no PSRAM, which today's drivers need (see its README).

## How it fits together

```
ports/
  common/              shared by both boards
    port.h, port_core.cpp   Hal interface, ROM menu, driver start, frame loop
    memfs.*                 read-only file system over the embedded media
    esp32_support.*         ESP32: DAC sound, embedded media through the VFS
    arduino_prepare.py      ESP32 ports: driver selection for arduino-cli/PlatformIO
    hid.*                   USB HID keyboard/mouse/gamepad decoding
    font8x8.h               menu font (public domain)
    driver_sources.json     dsp sources each driver needs (+ .cmake)
    dsp_port.cmake          CMake helpers (RP2350 build, host build)
    host/                   desktop build of the port front end + its tests
  ttgo-vga32/          arduino-cli (build.sh) or PlatformIO
  esp32-cyd/           arduino-cli (build.sh) or PlatformIO
  rp2350-pizero/       Pico SDK (CMake)
```

* **One firmware, a few drivers.** A board has room for a handful of drivers
  at a time: choose them when building (`-d "galaxian;bagman;spectrum"`). The
  list of source files each driver needs is in `common/driver_sources.json`,
  generated from a desktop build by `tools/driver_sources.py` (run it again
  after adding a driver). `model3` and `macii` cannot be built for the boards.
* **Files.** The code reads files through `dsp::fsx` (`src/core/fs_compat.h`),
  which is `std::filesystem` on the desktop and a small POSIX layer on the
  boards (`-DDSP_FS_POSIX=1`, also testable on the desktop with
  `cmake -DDSP_FS_POSIX=ON`). The SD card is mounted at `/sd`; embedded media
  appear under `/embedded`.
* **Menu.** At power-up the firmware lists what it can start from the card:
  `/sd/roms/NAME.zip` (or a folder `/sd/roms/NAME/`) for every built-in driver
  name or alias, and every command line saved as `/sd/dsp/*.txt`. A command
  line is what you would type on the desktop, with paths relative to the card:

  ```
  # /sd/dsp/manic.txt
  --game spectrum48 roms/zx48 --tape tapes/manic.tzx
  ```

  `/sd/dsp/autostart.txt` is started straight away. F12 (or Start+Select on a
  gamepad, held for half a second) leaves the game and returns to the menu.
* **Embedded media.** As on the desktop, a game can be compiled into the
  firmware (`--embed-game` / `--embed-rom` in both build scripts, see
  `tools/embed_media.py`); it starts without an SD card. The arrays stay in
  flash and are read in place.
* **Keys.** As on the desktop: arrows, Ctrl/Space and Z/X/C for the buttons,
  1/2 start, 5/6 coin, F1 service; home computers get the whole keyboard.

## Testing the front end on the desktop

```sh
cmake -S ports/common/host -B build-port-host -DDSP_PORT_DRIVERS="galaxian;blockout;c64"
cmake --build build-port-host -j
build-port-host/dsp-port-tests                       # memfs, HID, scaling, dsp::fsx...
cmake --build build-port-host --target dsp-port-mem  # RAM each driver needs (object, largest block, peak)
build-port-host/dsp-port-host --root /path/to/sdcard --keys down,enter --frames 600 --out shot.ppm
```

`dsp-port-host` is the board front end with a headless "board": a 320x240
screen saved as a PPM, key presses from `--keys`, and a folder in place of the
SD card. It builds from the same per-driver source lists as the firmware, so
it also checks that those lists link.
