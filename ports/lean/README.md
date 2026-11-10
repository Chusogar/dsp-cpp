# Lean single-game firmware: Indy, Road Runner, Gauntlet (ESP32 CYD, RP2350-PiZero)

dsp's own drivers (`src/drivers/arcade/atari_system1.cpp`, `gauntlet.cpp`)
keep their ROMs, graphics and a full 32-bit picture in RAM: on the boards
they need PSRAM and run slowly. This directory builds **one game per
firmware** with the memory-lean C cores of cMESYS (the same machines, ported
from these drivers and tuned for the boards), so that each of these games
runs at full speed with its full sound on a **stock CYD (no PSRAM)** and on
the **RP2350-PiZero**:

| Game | GAME | ROM sets (MAME) |
|---|---|---|
| Indiana Jones and the Temple of Doom (Atari System 1) | `indytemp` | `indytemp.zip` + `atarisy1.zip` |
| Road Runner (Atari System 1) | `roadrunn` | `roadrunn.zip` + `atarisy1.zip` |
| Gauntlet | `gauntlet` | `gauntlet.zip` |

## What makes them fit and keep up

* **ROMs in the firmware's flash**, never copied to RAM: `tools/regen.sh`
  turns the sets into C arrays (`rom_data.c`, generated, never committed), the
  tiles already packed to 4 bpp rows.
* **The picture is drawn a few lines at a time** from the video RAM on the
  second core (CYD: core 0 with the LCD's SPI/DMA; RP2350: core 1 with the
  HDMI), never kept as a frame buffer of 32-bit pixels.
* **68010: Musashi** with its opcode tables in flash (`m68k_tables.c`,
  generated per game) and the handlers measured hottest in IRAM / RAM
  (`<game>.lf`, also read by the RP2350 build); the driver's hook runs inline
  only when an interrupt line changed.
* **Busy loops skipped exactly**: the main program's wait loops (BIOS delay,
  waiting for the vertical blank while stirring the random seed) and the sound
  program's polling loop are fast-forwarded with their results computed, not
  run.
* **The sound 6502 compiled inside the machine** (`cpu/m6502/dsp6502.c`) with
  its memory handlers inline; the YM2151's synthesis on the other core (CYD).
* RP2350: the hot code (68010 core, the machine, POKEY, YM2151, TMS5220, the
  HDMI driver) runs from RAM, the sound goes out over HDMI.

## Building

```sh
# once per game: ROMs -> main/drivers/arcades/<game>/rom_data.c
tools/regen.sh indytemp ~/roms/indytemp.zip ~/roms/atarisy1.zip
tools/regen.sh roadrunn ~/roms/roadrunn.zip ~/roms/atarisy1.zip
tools/regen.sh gauntlet ~/roms/gauntlet.zip

# ESP32 CYD (ESP-IDF 5.x: . $IDF_PATH/export.sh)
./build_cyd.sh                  # -> dist/<game>_cyd_st7789_spi{40,80}mhz_*.bin
./build_cyd.sh gauntlet         # one game

# RP2350-PiZero (tools/setup_pico.sh once, or PICO_DEPS=<dir with pico-sdk,
# PicoDVI, Pico-PIO-USB>; Arm toolchain)
./build_rp2350.sh               # -> dist/<game>_rp2350_pizero.uf2
USB=native ./build_rp2350.sh    # keyboard / pad on the USB-C instead
```

Flashing the CYD: `esptool.py write_flash 0x0 dist/<game>_cyd_st7789_spi80mhz_full.bin`
(the 40 MHz build for panels that do not take 80 MHz). The RP2350: hold BOOT
while plugging the USB-C in and copy the `.uf2` to the drive.

## Controls

CYD: Wii Nunchuk on CN1 (stick and its two buttons; C+Z together = a coin,
then START), the BOOT button (the same), the touch screen split in 3x3 zones
(shown at start) and the serial console's keys; see the comments at the top
of `main/drivers/arcades/<game>/<game>_cyd.c`.
RP2350: USB keyboard (5 coin, 1 start, cursors, Z / Ctrl and X / Alt the
buttons, F3 reset) or a USB gamepad (A, B, SELECT coin, START start).

## Layout

```
main/cpu/m68k/                   Musashi (68000/68010)
main/cpu/m6502/dsp6502.c         the sound 6502, compiled inside each machine
main/sound/ machine/ video/      YM2151, POKEY, TMS5220, VIA 6522, SLAPSTIC, Atari MOs
main/drivers/arcades/atarisy1/   Atari System 1 (Indy, Road Runner)
main/drivers/arcades/<game>/     each game's board front ends, .lf hot list, generated sources
main/ports/esp32-cyd/            CYD: LCD, touch, DAC sound, Nunchuk (ESP-IDF)
main/ports/rp2040-pizero/        RP2350-PiZero: HDMI with sound, PIO-USB, PSRAM (Pico SDK)
tools/                           ROM and Musashi table generators, setup_pico.sh
```

The rest of dsp's drivers stay in the general firmware of `ports/esp32-cyd`
and `ports/rp2350-pizero`.
