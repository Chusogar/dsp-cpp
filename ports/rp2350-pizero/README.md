# dsp-cpp on the Waveshare RP2350-PiZero

Firmware for the [Waveshare RP2350-PiZero](https://www.waveshare.com/wiki/RP2350-PiZero)
(RP2350B, 520 KB SRAM, 16 MB flash, 8 MB PSRAM, HDMI, micro-SD, PIO-USB),
built with the Pico SDK. See [`../README.md`](../README.md) for what the ports
share (menu, SD card layout, embedded games, keys).

> Compiles with the scripts below; **not yet tried on the board**.

## Hardware

| Function | Pins | Notes |
|---|---|---|
| HDMI | D0 36/37, D1 34/35, D2 32/33, CLK 38/39 | PicoDVI on PIO1 (GPIO base 16), 640x480 60 Hz from a 320x240 RGB565 frame buffer; core 1 does the TMDS encoding; system clock 252 MHz |
| USB host | PIO-USB port: D+ 28, D- 29 | TinyUSB + Pico-PIO-USB on PIO0: keyboard, mouse and HID gamepads/joysticks (through a hub too). Xbox (XInput) pads are not HID and are not supported |
| Sound | GPIO18 (pin 12 of the 40-pin header) | 10-bit PWM, 22050 Hz mono; add an RC low-pass (1 kΩ + 10 nF) before an amplifier or headphones. Another pin: `-- -DDSP_AUDIO_PIN=19` |
| micro-SD | SPI1: SCK 30, MOSI 31, MISO 40, CS 43 | FAT32 or exFAT (FatFs), mounted at `/sd` |
| PSRAM | QMI CS1 = GPIO47 | APS6404L, detected at start; the heap continues there when SRAM is full |
| Serial log | UART0: TX 0, RX 1 | 115200 baud (fps every 5 seconds, errors) |

The native USB-C port is only used for power and for flashing.

## Building

Needs git, CMake 3.13+, python3 and the Arm GNU toolchain
(Debian/Ubuntu: `sudo apt install cmake gcc-arm-none-eabi libnewlib-arm-none-eabi
libstdc++-arm-none-eabi-newlib`).

```sh
cd ports/rp2350-pizero
./setup.sh        # Pico SDK 2.1.1 (+TinyUSB), PicoDVI, Pico-PIO-USB 0.7.2, zlib 1.3.1, FatFs R0.15 -> deps/
./build.sh        # -> build/dsp_pizero.uf2
./flash.sh        # board in BOOTSEL mode (hold BOOT while plugging the USB-C in)
```

Default drivers: galaxian, arabian, bagman, mrdo, sms, gameboy, nes,
spectrum, c64. Other choices and an embedded game:

```sh
./build.sh -d "galaxian;bagman;mrdo"
./build.sh -d blockout --embed-game blockout --embed-rom ~/roms/blockout.zip
./build.sh -d c64 --embed-game c64 --embed-rom ~/roms/c64 --embed-tape ~/games/game.prg
./build.sh -- -DDSP_AUDIO_PIN=19        # extra CMake options after --
```

An SDK you already have can be used with `PICO_SDK_PATH=/path/to/pico-sdk`
(2.1.1 or later, with the TinyUSB submodule); the other dependencies go to
`deps/` (or `$DSP_PICO_DEPS`). By hand:

```sh
cmake -S . -B build -DDSP_PORT_DRIVERS="galaxian;spectrum"
cmake --build build -j
```

## What is in here

| File | |
|---|---|
| `CMakeLists.txt` | the firmware; the dsp sources come from `../common/dsp_port.cmake` |
| `boards/dsp_rp2350_pizero.h` | board header (the SDK has none for this board) |
| `src/main.cpp` | clocks, PSRAM, USB, video, sound, SD; the `Hal` for `ports/common` |
| `src/video_dvi.c` | PicoDVI set-up for the board's pins |
| `src/usb_input.cpp`, `src/tusb_config.h` | TinyUSB host on PIO-USB |
| `src/audio_pwm.c` | PWM + DMA sound |
| `src/sd_spi.c`, `src/ffconf.h` | FatFs disk driver for the SD slot (SPI mode) |
| `src/posix_io.cpp`, `include/dirent.h` | newlib system calls: `/sd` (FatFs) and `/embedded` (memfs) |
| `src/heap.c` | `_sbrk`: SRAM first, then PSRAM |
| `src/psram.c` | PSRAM detection and QMI set-up (from SparkFun's sparkfun-pico, MIT) |

## Notes and limits

* The emulation runs on core 0 at 252 MHz from XIP flash, with RAM-hungry
  machines in PSRAM (slower than SRAM). The 8-bit machines are the ones with a
  chance of full speed; frames are skipped (up to 3 in a row) when it falls
  behind.
* RAM: the frame buffer (150 KB), a 48 KB stack and the drivers' static tables
  live in SRAM. Most combinations of a dozen drivers fit; all of them at once
  do not (by about 50 KB). `model3` and `macii` cannot be built for the board.
* C++ exceptions and RTTI are off (Pico SDK defaults).
