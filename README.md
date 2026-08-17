# dsp-cpp

C++17 + SDL2 port of [dsp-emulator](https://github.com/leniad/dsp-emulator) (Free Pascal).
Supported games: **Bagman** (Valadon Automation, 1982), **Mikie** (Konami, 1984),
**Gauntlet** (Atari, 1985), **Double Dragon** and **Double Dragon II** (Technos, 1987/1988),
**Elevator Action** and **Jungle King** (Taito, 1983, Taito SJ hardware),
**Kung-Fu Master**, **Spelunker**, **Spelunker II**, **Lode Runner** and **Lode Runner II**
(Irem M62 hardware).
**Ikari Warriors**, **Athena**, **TNK III** and **ASO** (SNK, 1985–1986).
and Capcom **CPS1** (Ghouls'n Ghosts, Final Fight, Street Fighter II, Strider,
Cadillacs and Dinosaurs, The Punisher, and the rest of the Pascal `cps1_hw`
set).
and Irem **M72** (**R-Type**, **Hammerin' Harry**, **R-Type II**).
It also emulates the **ZX Spectrum 48K** home computer (Sinclair, 1982).
**Elevator Action** and **Jungle King** (Taito, 1983, Taito SJ hardware).
It also emulates the **ZX Spectrum 48K** home computer (Sinclair, 1982) and consoles
including the **Atari Lynx** (1989).

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
| M6502 CPU | `src/cpu/m6502.pas` | Gauntlet sound CPU; optional 65C02 CMOS opcodes for the Lynx |
| Lynx Suzy / Mikey | new | Sprite blitter, math coprocessor, timers, LCD DMA, 4-channel sound |
| Atari Lynx driver | new | 64 KiB DRAM, MAPCTL, LNX/LYX carts, 160×102 LCD |
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
| M6803 MCU | `src/cpu/m680x.pas` (`TCPU_M6803`) | Irem M62 sound CPU: 128 bytes of internal RAM, ports 1-4, no internal ROM |
| Irem M62 driver | `src/arcade/m62_hw.pas` | Kung-Fu Master, Spelunker, Spelunker II, Lode Runner and Lode Runner II: Z80, M6803, two AY-3-8910, two MSM5205, tiles and multi-height sprites |
| SNK driver | `src/arcade/snk_hw.pas` | Three Z80s, YM3526, Ikari/Athena/TNK III/ASO video (chars, tiles, 16x16 and 32x32 sprites, hardflags) |
| CPS1 driver | `src/arcade/cps1_hw.pas` | 68000 + Z80, CPS-A/B, three scroll layers, sprites, YM2151+OKI or QSound, Kabuki, 93C46 |
| NEC V20/V30 CPU | `src/cpu/nec_v20_v30.pas` | R-Type main CPU, 20-bit segmented addressing |
| M72 driver | `src/arcade/m72_hw.pas` | R-Type, Hammerin' Harry and R-Type II: V30 + Z80, YM2151, tiled FG/BG with priority, sprites |
| Spectrum ULA | `src/computer/spectrum_hw.pas` | Keyboard matrix, border, one bit beeper, EAR input, contended timing |
| Spectrum driver | `src/computer/spectrum_hw.pas`, `spectrum_misc.pas` | 48K memory map, display file with attributes and flash, Kempston joystick |
| Tape player | `src/misc/tap_tzx.pas` | `.tap` blocks and `.tzx` images (turbo, pure tone/data, direct recording, pauses, loops), hooked to the ROM loader |
| NES PPU | `src/consolas/nes_ppu.pas` | 2C02: nametables, sprites, loopy scroll, YUV palette |
| NES APU | `src/cpu/n2a03.pas` | 2A03 squares/triangle/noise/DPCM, resampled to 44100 Hz |
| NES mappers | `src/consolas/nes_mappers.pas` | 0, 1 (MMC1), 2, 3, 4 (MMC3/MMC6), 7, 9–11, 13, 15, 34, 66, 68, 70, 71, 76, 79/146, 87, 88, 93–95, 113, 180, 184, 185, 206 |
| NES driver | `src/consolas/nes.pas` | NTSC 256×240, iNES carts (plain or zipped), two controllers |
| C64 driver | `ordenadores/commodore64.pas` | PLA, 6510 port, keyboard matrix, TAP/PRG/T64/D64 loaders |
| VIC-II | `mos6566.pas` | PAL 6569, 384×270, sprites, bad lines |
| MOS 6526 CIA | `mos6526_old.pas` | Two chips: CIA1 IRQ + keyboard, CIA2 NMI + VIC bank |
| SID 6581 | `sid_sound.pas` | Three voices, 44100 Hz mono |
| Front end | `src/misc/main_engine.pas` | SDL2 window, texture, audio queue, keyboard |
| µPD7801 CPU | `src/cpu/upd7810.pas` (`CPU_7801`) | Epoch Super Cassette Vision CPU, 4 MHz crystal /2 |
| µPD1771C | `src/snd/upd1771.pas` | SCV tone / noise / ADPCM sound |
| Super Cassette Vision | `src/consolas/super_cassette_vision.pas` | BIOS + cartridge map, 192×222 video, keyboard and two joysticks |

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
./build/dsp --game ikari /path/to/ikari.zip
./build/dsp --game spectrum48 --tape /path/to/game.tzx /path/to/48.rom
./build/dsp --game c64 --tape /path/to/game.prg /path/to/c64-roms/
```

The game is taken from `--game` (`bagman`, `mikie`, `gauntlet`, `ddragon`,
`ddragon2`, `elevator`, `junglek`, `kungfum`, `spelunkr`, `spelunk2`, `ldrun`,
`ldrun2` or `spectrum48`); when omitted it is guessed from the ROM set name. Gauntlet accepts both the four player parent set
`ddragon2`, `elevator`, `junglek`, `ikari`, `athena`, `tnk3`, `aso` or `spectrum48`); when omitted it is guessed from the ROM set name. Gauntlet accepts both the four player parent set
`ddragon2`, `elevator`, `junglek`, `rtype`, `hharry`, `rtype2` or `spectrum48`); when omitted it is guessed from the ROM set name. Gauntlet accepts both the four player parent set
./build/dsp --game lynx /path/to/game.lnx
```

The game is taken from `--game` (`bagman`, `mikie`, `gauntlet`, `ddragon`,
`ddragon2`, `elevator`, `junglek`, `spectrum48` or `lynx`); when omitted it is guessed from the ROM set name. Gauntlet accepts both the four player parent set
./build/dsp --game scv /path/to/scv.zip
```

The game is taken from `--game` (`bagman`, `mikie`, `gauntlet`, `ddragon`,
`ddragon2`, `elevator`, `junglek`, `spectrum48` or `scv`); when omitted it is guessed from the ROM set name. Gauntlet accepts both the four player parent set
(SLAPSTIC 104) and the two player `136041-xxx` set (SLAPSTIC 107).

Required files: `e9_b05.bin`, `f9_b06.bin`, `f9_b07.bin`, `k9_b08.bin`, `m9_b09s.bin`,
`n9_b10.bin`, `c1_b01.bin`, `e1_b02.bin`, `f1_b03s.bin`, `j1_b04.bin`, `p3.bin`, `r3.bin`.

Options:

```
--game NAME        machine to run: bagman, mikie, gauntlet, ddragon, ddragon2,
                   elevator, junglek, kungfum, spelunkr, spelunk2, ldrun,
                   ldrun2 or spectrum48
                   elevator, junglek, ikari, athena, tnk3, aso or spectrum48
                   elevator, junglek, rtype, hharry, rtype2 or spectrum48
                   elevator, junglek, spectrum48 or scv
--scale N          window scale factor (default 3)
--dip [BANK:]VALUE DIP switch byte, decimal or 0x hex (bagman: one bank,
                   mikie: 0=A coinage, 1=B gameplay, 2=C flip screen;
                   gauntlet: service switch;
                   double dragon: 0=A coinage/cabinet, 1=B gameplay)
--mute             disable audio
--fullscreen       start in full screen
--screenshot FILE  headless mode: render frames and write FILE (BMP)
--frames N         frames to run in headless mode (default 300)
--tape FILE        tape image to insert: .tap/.tzx (spectrum48), .prg/.t64/.tap/.d64 (c64)
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

### Irem M72 (R-Type, Hammerin' Harry, R-Type II)

The board runs an 8 MHz NEC V30, a 3.579545 MHz Z80 with a YM2151, and (on Harry
and R-Type II) a DAC sample ROM. Video is two 8x8 tilemaps with a per-tile
priority bit plus 16x16 sprites.

```bash
./build/dsp --game rtype /path/to/rtype.zip
./build/dsp --game hharry /path/to/hharry.zip
./build/dsp --game rtype2 /path/to/rtype2.zip
```

R-Type has no dedicated sound ROM: the V30 copies the Z80 program into shared
RAM at `$e0000`. DIP bank 0 is the low byte of the board ID / DSW word
(`$fdfb` on R-Type).

DIP banks: 0 = A (bonus/finish bonus on bits 0-1, lives on bits 3-4, flip screen on
bit 6, cabinet on bit 7), 1 = B (coin A and coin B), 2 = C (difficulty or bonus life on
bits 0-1, year and coinage displays, hit detection or infinite lives, coin slots).
The defaults are `--dip 0:0x7f --dip 1:0x00 --dip 2:0xff` for Elevator Action and
`--dip 0:0x3f` for Jungle King. Jungle King runs on a monitor rotated 180 degrees, and
the port rotates its picture back.

### CPS1 (Final Fight, Street Fighter II, …)

Capcom Play System 1: a 10 or 12 MHz 68000, a sound Z80 (YM2151 + OKI6295, or
Kabuki-encrypted QSound on Cadillacs and Dinosaurs / The Punisher), and the CPS-A/B
pair that maps three tile layers plus sprites. The visible area is 384×224 (1941 is
rotated 270°). `--game` names match the MAME set: `ghouls`, `ffight`, `kod`, `sf2`,
`strider`, `3wonders`, `captcomm`, `knights`, `sf2ce`, `dino`, `punisher`, `willow`,
`1941`, `nemo`.

```bash
./build/dsp --game ffight /path/to/ffight.zip
```

Final Fight DIP defaults are A=`0xff`, B=`0xf4`, C=`0x9f`.

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

### Irem M62 (Kung-Fu Master, Spelunker, Lode Runner)

Irem's M62 board runs a Z80 (3.072 MHz on Kung-Fu Master, 4 MHz on the others) and an
M6803 sound CPU at 3.579545 MHz / 4 with two AY-3-8910 and two MSM5205 ADPCM chips.
The sound CPU streams ADPCM nibbles; the first MSM5205 clocks the second in slave
mode and pulses NMI so the 6803 can feed the next sample. Video is an 8x8 (or 12x8
on Spelunker) tilemap plus 16x16 sprites that the height PROM can stack into 32 or
64 pixel tall objects. Kung-Fu Master is 256x256 with a status bar that does not
scroll; the other games are 384x256.

```bash
./build/dsp --game kungfum /path/to/kungfum.zip
./build/dsp --game spelunkr /path/to/spelunkr.zip
./build/dsp --game spelunk2 /path/to/spelunk2.zip
./build/dsp --game ldrun /path/to/ldrun.zip
./build/dsp --game ldrun2 /path/to/ldrun2.zip
```

DIP banks: 0 = A (gameplay / coinage), 1 = B (cabinet, flip screen, service). The
Pascal driver finishes initialisation with `--dip 0:0xff --dip 1:0xfd`.

Kung-Fu Master ROM set: `a-4e-c.bin`, `a-4d-c.bin`, `g-4c-a.bin`, `g-4d-a.bin`,
`g-4e-a.bin`, `a-3e-.bin`, `a-3f-.bin`, `a-3h-.bin`, `b-4k-.bin` through
`b-4a-.bin`, plus the colour and sprite-height PROMs `g-1j-.bin`, `g-1f-.bin`,
`g-1h-.bin`, `b-1m-.bin`, `b-1n-.bin`, `b-1l-.bin`, `b-5f-.bin`.

None of these sets has been run here with real ROMs yet.
### SNK (Ikari Warriors, Athena, TNK III, ASO)

Three Z80s (main and sub at 3.35 MHz, sound at 4 MHz) driving one or two YM3526 chips.
Ikari Warriors is a portrait 216x288 game with 16x16 and 32x32 sprites and a hardflags
collision port; Athena is 288x216; TNK III and ASO draw 288x216 and rotate the picture
270 degrees. DIP banks are 0=A, 1=B and 2=C (bonus life). Ikari defaults are
`--dip 0:0x3b --dip 1:0x4b --dip 2:0x34`. Button 2 / 3 step the rotary stick on Ikari
and TNK III.

The Ikari set accepts both the old `1.rom` / `7.rom` / `7122er.prm` names and the
MAME 0.221 `1.4p` / `p7.3b` / `a6002-1.1k` names.

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

### Nintendo Entertainment System

NTSC NES, ported from `nes.pas`. Give it an iNES (`.nes`) ROM, plain or inside a zip:

```bash
./build/dsp --game nes /path/to/game.nes
./build/dsp /path/to/game.nes
```

The 2A03 CPU ignores decimal mode and implements the unofficial opcodes from
`m6502.pas`. Mappers 0 (NROM), 1 (MMC1), 2 (UxROM), 3 (CNROM), 4 (MMC3, MMC6 as
NES 2.0 submapper 1), 7 (AxROM), 9/10 (MMC2/4), 11, 13, 15, 34, 66, 68, 70, 71,
76, 79/146, 87, 88, 93, 94, 95, 113, 180, 184, 185 and 206 are implemented.
CHR-RAM carts (header CHR = 0) work. Player 1 uses the arrows plus Ctrl/Alt for
A/B, `1` for Start and `5` for Select (the arcade coin button). There is no BIOS;
the CPU starts at the cartridge reset vector. IRQ-heavy boards (VRC, FME-7, …)
are still rejected at load time.

### Commodore 64

The machine needs the three copyrighted Commodore ROMs (KERNAL 901227-03, BASIC
901226-01, character generator 901225-01). Point `--game c64` at a directory or
zip that holds them; common aliases such as `kernal.bin` / `basic.bin` /
`chargen.bin` are accepted.

```bash
./build/dsp --game c64 /path/to/c64-roms/
./build/dsp --game c64 --tape game.prg /path/to/c64-roms/
./build/dsp --game c64 --tape game.tap /path/to/c64-roms/
```

The host keyboard is mapped onto the C64 matrix (Left Shift is C64 left shift,
Left Ctrl is CTRL, Tab is RUN/STOP). F1/F3/F5/F7 are the C64 function keys.
F6 starts and stops a `.tap` cassette (the 6510 motor bit still has to enable
the datasette). Arrows are also a joystick in Control Port 2. There is no 1541:
a `.d64` image injects its first PRG into RAM.

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
### Midway MCR (Tapper, Tron, …)

`--game tapper` (also `tron`, `shollow`, `domino`, `wacko`, `dotron`, `timber`) is the
Midway MCR-II/III board from `mcr_hw.pas`: dual Z80, Z80 CTC daisy IRQs, SSIO sound
(two AY-3-8910 plus the 14024 `/SINT` clock) and 16×16 tiles with 32×32 sprites.
Point it at a MAME merged `tapper.zip`.

```bash
./build/dsp --game tapper /path/to/tapper.zip
./build/dsp --game tapper --screenshot tapper.bmp --frames 180 /path/to/tapper.zip
```

Start is player 1 `1` / player 2 `2`, coin is `5`/`6`, Tapper pours with Ctrl/Space.
### Atari Lynx

The handheld is a 65C02 (G65SC02 inside **Mikey**) at 16 MHz with wait states, 64 KiB
of shared DRAM, and **Suzy** for sprites, 16-bit math and the cartridge port. Mikey
also owns the eight timers (HBL/VBL), 16-colour 12-bit palette, LCD DMA (160×102),
four-channel polynomial sound and the cart address shifter.

The 512-byte Mikey boot ROM is the same `lynxboot.img` dump Handy and Mednafen use
(CRC `0d973c9d` or `e1ffecb6`). It is not shipped in this tree. Put it next to the
cartridge, in the ROM directory, or pass it as the positional path:

```bash
# https://github.com/Abdess/retrobios/blob/main/bios/Atari/Lynx/lynxboot.img
./build/dsp --game lynx /path/to/roms/          # directory with lynxboot.img + game.lnx
./build/dsp --game lynx /path/to/game.lnx       # looks for lynxboot.img beside the cart
```

Without that file a tiny open bootstrap is mapped instead, which is enough for
raw homebrew that expects the first 256 bytes at `$0200`. Commercial carts need
the Atari ROM.

Lynx controls: arrows, Left Ctrl/Space = A, Left Alt/Z = B, X = Option 1, 1 = Option 2,
5 = Pause.
### Super Cassette Vision

Epoch's Super Cassette Vision (1984) needs the 4 KiB BIOS `upd7801g.s01`
(CRC `7ac06182`) and the 1 KiB character ROM `epochtv.chr` (CRC `db521533`).
Those two files are the MAME `scv.zip` set (for example
[Abdess/retrobios](https://github.com/Abdess/retrobios) ships them under
`bios/Epoch/Super Cassette Vision/`). Give a directory or zip that holds both,
or put them next to the cartridge:

```bash
./build/dsp --game scv /path/to/scv.zip
./build/dsp --game scv /path/to/AstroWars.bin
```

The cartridge is the positional ROM path (plain `.bin`, a zip, or a split
`.0`/`.1` pair). Extra RAM and the Pole Position II mapper follow the same CRCs
as `super_cassette_vision.pas`. The host keyboard supplies 0–9, Q, W and P
(pause); the arrows and buttons are the two joysticks.

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
src/sound/      AY-3-8910, SN76496, NES 2A03 APU, SID 6581
src/video/      graphics decoding, resistor palettes, NES PPU, VIC-II
src/machine/    Bagman PAL16R6, SLAPSTIC, Spectrum tape player, NES mappers, MOS 6526 CIA
src/cpu/        Z80, M6809, M6502, M68000, HD63701, M6805 and µPD7801 cores
src/sound/      AY-3-8910, SN76496, µPD1771C and the other arcade chips
src/video/      graphics decoding and resistor based palette helpers
src/machine/    Bagman PAL16R6, SLAPSTIC, Spectrum tape player
src/drivers/    the machines themselves (memory map, video, inputs)
src/frontend/   SDL2 front end, driven through the core/machine.h interface
src/core/       ROM loader (directory or zip) and the Machine interface
```
