# dsp-cpp on the Waveshare RP2040-PiZero

Firmware for the [Waveshare RP2040-PiZero](https://www.waveshare.com/wiki/RP2040-PiZero)
(RP2040, 264 KB SRAM, no PSRAM, 16 MB flash, HDMI, micro-SD), built with the
Pico SDK. It is **not** the RP2350-PiZero firmware (`../rp2350-pizero`): an
RP2040 ignores that UF2 (other chip family), the drive simply keeps it.

> Compiles; **not yet tried on the board**.

With 264 KB of RAM only drivers written for little memory fit: so far
`taitosj` (Elevator Action), the same one the ESP32 CYD runs. They give an
8-bit picture that core 1 turns into HDMI lines, and read their ROMs in place
from the firmware (flash). The emulation runs on core 0 (Cortex-M0+ at
252 MHz, from flash): expect it below full speed.

## Hardware

| Function | Pins | Notes |
|---|---|---|
| HDMI | D0 26/27, D1 24/25, D2 22/23, CLK 28/29 | PicoDVI on PIO1, 640x480 60 Hz from a 320x240 screen; no frame buffer: core 1 turns each row of the 8-bit picture into RGB565 through the palette and TMDS encodes it |
| USB host | native USB-C port (marked USB) | keyboards and HID pads; power the board and the pad through an OTG / Y cable. The PIO-USB port is not usable with HDMI on the RP2040 |
| Sound | GPIO4 | 10-bit PWM, 22050 Hz mono, add an RC low-pass; another pin: `-- -DDSP_AUDIO_PIN=n` |
| micro-SD | SPI0: SCK 18, MOSI 19, MISO 20, CS 21 | FAT32/exFAT at `/sd` |
| Serial log | UART0: TX 0, RX 1 | 115200 baud (fps every 5 seconds, errors) |

## Building and flashing

```sh
../rp2350-pizero/setup.sh   # once: SDK, PicoDVI, zlib, FatFs
./build.sh -d taitosj --embed-entry "Elevator Action=--game elevator $HOME/roms/elevator/"
```

The QL (8-colour full-resolution video, low-memory QL driver):

```sh
./build.sh -d ql -- -DDSP_VIDEO_RGB1=ON "-DDSP_RAM_CODE=zx8301.cpp.o;mcs48.cpp.o;zx8302.cpp.o" \
  "-DDSP_EMBED_ENTRIES=QL Chess=--game ql $HOME/roms/ql/ --cart $HOME/ql/chess.mdv"
```

Give the ROM set as a **folder** of the needed files (a zip would have to be
loaded whole into RAM). Hold BOOT while plugging the USB-C in: the `RPI-RP2`
drive appears; copy `build/dsp_pizero2040.uf2` to it.
