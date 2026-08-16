# dsp-cpp

C++17 + SDL2 port of [dsp-emulator](https://github.com/leniad/dsp-emulator) (Free Pascal).
Supported games: **Bagman** (Valadon Automation, 1982), **Mikie** (Konami, 1984),
**Gauntlet** (Atari, 1985), **Double Dragon** and **Double Dragon II** (Technos, 1987/1988),
**Elevator Action** and **Jungle King** (Taito, 1983, Taito SJ hardware).
It also emulates the **ZX Spectrum 48K** home computer (Sinclair, 1982).

To add another machine follow [docs/adding-a-driver.md](docs/adding-a-driver.md), which
explains the port workflow and comes with a driver skeleton (`tools/new_driver.py`).

## What is ported

| Component | Origin | Notes |
| --- | --- | --- |
| Z80 CPU | `src/cpu/z80/nz80.pas` | Passes the `zexdoc` instruction exerciser (67/67) |
| M6809 CPU | `src/cpu/m6809.pas` | Mikie main CPU |
| AY-3-8910 PSG | `src/snd/ay_8910.pas` | 44100 Hz mono output |
| SN76496 PSG | `src/snd/sn_76496.pas` | Two chips in Mikie |
| PAL16R6 protection | `src/arcade/misc/bagman_pal.pas` | Original fuse map |
| Graphics decoding, palette | `src/misc/gfx_engine.pas`, `pal_engine.pas` | Bit-level layouts and resistor weights |
| Bagman driver | `src/arcade/bagman_hw.pas` | Memory map, video, inputs, DIP switches |
| Mikie driver | `src/arcade/mikie_hw.pas` | M6809 + sound Z80, PROM colour lookup tables, sprites |
| M68000/68010 CPU | `src/cpu/m68000.pas` | Gauntlet main CPU |
| M6502 CPU | `src/cpu/m6502.pas` | Gauntlet sound CPU |
| YM2151 FM, POKEY | `src/snd/fm_2151.pas`, `src/snd/pokey.pas` | Gauntlet sound board |
| SLAPSTIC | `src/arcade/misc/slapstic.pas` | Types 101-107, bank switched protected ROM |
| Atari motion objects | `src/arcade/misc/atari_mo.pas` | SLIP based sprite lists |
| Gauntlet driver | `src/arcade/gauntlet_hw.pas` | Memory map, playfield/char/sprite video, EEPROM, sound communication |
| HD63701Y MCU | `src/cpu/m680x.pas` | Double Dragon sub CPU: internal RAM/ROM, I/O ports, output compare timer |
| MSM5205 ADPCM | `src/snd/msm5205.pas` | Two chips in Double Dragon |
| OKI MSM6295 | `src/snd/oki6295.pas` | Double Dragon II sample player |
| Double Dragon driver | `src/arcade/doubledragon_hw.pas` | Both variants: banked ROM, shared RAM, scroll, sprites, sound CPUs |
| M6805/M68705 MCU | `src/cpu/m6805.pas` | MC68705P3 protection MCU of Elevator Action |
| Taito SJ driver | `src/arcade/taitosj_hw.pas` | Main and sound Z80, four AY-3-8910, DAC, MCU handshake, three tile layers with per column scroll, sprites and PROM priorities |
| Spectrum ULA | `src/computer/spectrum_hw.pas` | Keyboard matrix, border, one bit beeper, EAR input, contended timing |
| Spectrum driver | `src/computer/spectrum_hw.pas`, `spectrum_misc.pas` | 48K memory map, display file with attributes and flash, Kempston joystick |
| Tape player | `src/misc/tap_tzx.pas` | `.tap` blocks and `.tzx` images (turbo, pure tone/data, direct recording, pauses, loops), hooked to the ROM loader |
| Front end | `src/misc/main_engine.pas` | SDL2 window, texture, audio queue, keyboard |

## Building

Requirements: CMake >= 3.16, a C++17 compiler, SDL2 and zlib.

```bash
sudo apt-get install cmake libsdl2-dev zlib1g-dev   # Debian/Ubuntu
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

This produces `build/dsp` and the unit test binary `build/dsp_tests`.

## Running

ROMs are **not** included. Point the emulator to a `bagman.zip` set or to a directory
holding the individual files:

```bash
./build/dsp /path/to/bagman.zip
./build/dsp --scale 3 --dip 0xfe /path/to/roms/bagman/
./build/dsp --game mikie /path/to/mikie.zip
./build/dsp --game gauntlet /path/to/gauntlet.zip
./build/dsp --game ddragon /path/to/ddragon.zip
./build/dsp --game ddragon2 /path/to/ddragon2.zip
./build/dsp --game elevator /path/to/elevator.zip
./build/dsp --game junglek /path/to/junglek.zip
./build/dsp --game spectrum48 --tape /path/to/game.tzx /path/to/48.rom
```

The game is taken from `--game` (`bagman`, `mikie`, `gauntlet`, `ddragon`,
`ddragon2`, `elevator`, `junglek` or `spectrum48`); when omitted it is guessed from the ROM set name. Gauntlet accepts both the four player parent set
(SLAPSTIC 104) and the two player `136041-xxx` set (SLAPSTIC 107).

Required files: `e9_b05.bin`, `f9_b06.bin`, `f9_b07.bin`, `k9_b08.bin`, `m9_b09s.bin`,
`n9_b10.bin`, `c1_b01.bin`, `e1_b02.bin`, `f1_b03s.bin`, `j1_b04.bin`, `p3.bin`, `r3.bin`.

Options:

```
--game NAME        machine to run: bagman, mikie, gauntlet, ddragon, ddragon2,
                   elevator, junglek or spectrum48
--scale N          window scale factor (default 3)
--dip [BANK:]VALUE DIP switch byte, decimal or 0x hex (bagman: one bank,
                   mikie: 0=A coinage, 1=B gameplay, 2=C flip screen;
                   gauntlet: service switch;
                   double dragon: 0=A coinage/cabinet, 1=B gameplay)
--mute             disable audio
--fullscreen       start in full screen
--screenshot FILE  headless mode: render frames and write FILE (BMP)
--frames N         frames to run in headless mode (default 300)
--tape FILE        tape image to insert, .tap or .tzx (spectrum48)
```

### Taito SJ (Elevator Action, Jungle King)

The board runs a 4 MHz Z80 for the game, a 3 MHz Z80 for the sound with four AY-3-8910
and a DAC, and Elevator Action adds a MC68705P3 MCU that talks to the main CPU through
a two byte handshake and can read and write its RAM. Both games use three 8x8 tile
layers whose characters live in RAM, 16x16 sprites and a PROM that decides the drawing
order of the four layers for every priority code.

```bash
./build/dsp --game elevator /path/to/elevator.zip
./build/dsp --game junglek /path/to/junglek.zip
```

DIP banks: 0 = A (bonus/finish bonus on bits 0-1, lives on bits 3-4, flip screen on
bit 6, cabinet on bit 7), 1 = B (coin A and coin B), 2 = C (difficulty or bonus life on
bits 0-1, year and coinage displays, hit detection or infinite lives, coin slots).
The defaults are `--dip 0:0x7f --dip 1:0x00 --dip 2:0xff` for Elevator Action and
`--dip 0:0x3f` for Jungle King. Jungle King runs on a monitor rotated 180 degrees, and
the port rotates its picture back.

Elevator Action ROM set: `ba3__01.2764.ic1`, `ba3__02.2764.ic2`, `ba3__03-1.2764.ic3`,
`ba3__04-1.2764.ic6`, `ba3__05.2764.ic4`, `ba3__06.2764.ic5`, `ba3__07.2764.ic9`,
`ba3__08.2764.ic10`, `ba3__09.2732.ic70`, `ba3__10.2732.ic71`,
`ba3__11.mc68705p3.ic24`, `eb16.22`.

Jungle King ROM set: `kn21-1.bin`, `kn22-1.bin`, `kn43.bin`, `kn24.bin`, `kn25.bin`,
`kn46.bin`, `kn47.bin`, `kn28.bin`, `kn60.bin`, `kn29.bin`, `kn30.bin`, `kn51.bin`,
`kn52.bin`, `kn53.bin`, `kn34.bin`, `kn55.bin`, `kn56.bin`, `kn37.bin`, `kn38.bin`,
`kn59-1.bin`, `eb16.22`.

Neither set has been run here with real ROMs yet: the driver is only checked against a
synthetic set, so this hardware is still unverified.

### ZX Spectrum 48K

The machine needs the 16 KiB Sinclair ROM, given as a plain `48.rom` image, a zip or a
directory holding it (Debian/Ubuntu ship it in the `spectrum-roms` package):

```bash
./build/dsp --game spectrum48 /usr/share/spectrum-roms/48.rom
./build/dsp --game spectrum48 --tape jetpac.tzx /usr/share/spectrum-roms/48.rom
```

The host keyboard is mapped one to one onto the Spectrum matrix (Left/Right Ctrl are
symbol shift, Shift is caps shift), so `P` is not the pause key here: use `F2`. The
arrows and Left Ctrl are also read as a Kempston joystick. A tape given with `--tape`
plays by itself whenever the ROM loader is running, so typing `LOAD ""` and pressing
Enter loads it. Both `.tap` and `.tzx` images work: the `.tzx` player handles standard
and turbo blocks, pure tones, pulse sequences, pure data, direct recordings, pauses,
signal level changes, jumps and loops. Games that only play under a custom loader still
need the loader to be running, and the CSW (`$18`) and generalized data (`$19`) blocks
are skipped. A "stop the tape" block only pauses for two seconds, because the machine
restarts the tape whenever the loader runs.

### Game Boy / Game Boy Color

`--game gb` loads a `.gb` / `.gbc` cartridge (plain or zipped). The machine is
chosen from header byte `$0143` the same way `gb.pas` does: bit 7 set (`$80`
CGB-enhanced or `$C0` CGB-exclusive) runs as Game Boy Color; otherwise it is
a DMG Game Boy. Optional boot ROMs (`dmg_boot.bin`, `cgb_boot.bin`) may sit
next to the cartridge; without them the CPU is left in `reset_gb`'s post-boot
state so games still start at `$0100`.

```bash
./build/dsp --game gb /path/to/game.gbc
```

### Controls

| Key | Action |
| --- | --- |
| Arrows | Player 1 movement |
| Left Ctrl / Space | Player 1 button 1 |
| Left Alt / Z | Player 1 button 2 |
| X | Player 1 button 3 (Double Dragon jump) |
| D / G / R / F | Player 2 movement |
| A / S | Player 2 buttons |
| Q | Player 2 button 3 |
| 1, 2 | Start 1P / 2P |
| 5, 6 | Insert coin 1 / 2 |
| P | Pause (F2 on the Spectrum, whose keyboard uses every letter) |
| F3 | Reset |
| Esc | Quit |

### DIP switches (`--dip`)

| Bits | Meaning |
| --- | --- |
| 0-1 | Lives: 3=2, 2=3, 1=4, 0=5 |
| 2 | Coinage |
| 3-4 | Difficulty: 0x18 easy … 0x00 hardest |
| 5 | Language: set = English |
| 6 | Bonus life: set = 30K, clear = 40K |
| 7 | Cabinet: set = upright |

Mikie has three banks (`--dip 0:0xff --dip 1:0x7b --dip 2:0xfe` are the defaults):
bank A holds coin A (low nibble) and coin B (high nibble), bank B holds lives (bits 0-1),
cabinet (bit 2), bonus life (bits 3-4), difficulty (bits 5-6) and demo sounds (bit 7),
and bank C holds flip screen (bit 0) and upright controls (bit 1).

Mikie ROM set: `n14.11c`, `o13.12a`, `o17.12d`, `n10.6e`, `o11.8i`, `001.f1`, `003.f3`,
`005.h1`, `007.h3`, `d19.1i`, `d21.3i`, `d20.2i`, `d22.12h`, `d18.f9`.

## Tests

```bash
ctest --test-dir build          # unit tests (CPU, PAL, graphics, palette, PSG)
```

The Z80 core can additionally be validated with the standard instruction exerciser,
which is not shipped here:

```bash
cmake --build build --target dsp_zexdoc
./build/dsp_zexdoc /path/to/zexdoc.com
```

## Layout

```
src/cpu/        Z80, M6809, M6502, M68000, HD63701 and M6805 cores
src/sound/      AY-3-8910 and SN76496
src/video/      graphics decoding and resistor based palette helpers
src/machine/    Bagman PAL16R6, SLAPSTIC, Spectrum tape player
src/drivers/    the machines themselves (memory map, video, inputs)
src/frontend/   SDL2 front end, driven through the core/machine.h interface
src/core/       ROM loader (directory or zip) and the Machine interface
```
