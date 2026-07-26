# dsp-cpp

C++17 + SDL2 port of [dsp-emulator](https://github.com/leniad/dsp-emulator) (Free Pascal),
currently limited to the arcade game **Bagman** (Valadon Automation, 1982).

## What is ported

| Component | Origin | Notes |
| --- | --- | --- |
| Z80 CPU | `src/cpu/z80/nz80.pas` | Passes the `zexdoc` instruction exerciser (67/67) |
| AY-3-8910 PSG | `src/snd/ay_8910.pas` | 44100 Hz mono output |
| PAL16R6 protection | `src/arcade/misc/bagman_pal.pas` | Original fuse map |
| Graphics decoding, palette | `src/misc/gfx_engine.pas`, `pal_engine.pas` | Bit-level layouts and resistor weights |
| Bagman driver | `src/arcade/bagman_hw.pas` | Memory map, video, inputs, DIP switches |
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
```

Required files: `e9_b05.bin`, `f9_b06.bin`, `f9_b07.bin`, `k9_b08.bin`, `m9_b09s.bin`,
`n9_b10.bin`, `c1_b01.bin`, `e1_b02.bin`, `f1_b03s.bin`, `j1_b04.bin`, `p3.bin`, `r3.bin`.

Options:

```
--scale N          window scale factor (default 3)
--dip VALUE        DIP switch byte, decimal or 0x hex (default 0xfe)
--mute             disable audio
--fullscreen       start in full screen
--screenshot FILE  headless mode: render frames and write FILE (BMP)
--frames N         frames to run in headless mode (default 300)
```

### Controls

| Key | Action |
| --- | --- |
| Arrows | Player 1 movement |
| Left Ctrl / Space | Player 1 button |
| D / G / R / F, A | Player 2 movement and button |
| 1, 2 | Start 1P / 2P |
| 5, 6 | Insert coin 1 / 2 |
| P | Pause |
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
src/cpu/        Z80 core
src/sound/      AY-3-8910
src/video/      graphics decoding and resistor based palette helpers
src/machine/    Bagman PAL16R6
src/drivers/    Bagman machine (memory map, video, inputs)
src/frontend/   SDL2 front end
src/core/       ROM loader (directory or zip)
```
