# dsp-cpp

C++17 + SDL2 port of [dsp-emulator](https://github.com/leniad/dsp-emulator) (Free Pascal).
Arcade: **Bagman**, **Mikie**, **Gauntlet**, **Mr. Do**, **Bubble Bobble**, **Double Dragon** /
**Double Dragon II**, Taito SJ (**Elevator Action**, **Jungle King**), Irem M62
(**Kung-Fu Master**, **Spelunker**, **Lode Runner**), SNK (**Ikari Warriors**,
**Athena**, **TNK III**, **ASO**), Capcom **CPS1**, Irem **M72** (**R-Type**),
Midway **MCR** (**Tapper** and family), Atari **Star Wars**, and Namco
**Pole Position** / **Pole Position II**.
Computers: **ZX Spectrum 48K**, **Pentagon 1024**, **Scorpion 256**, Amstrad CPC, **Commodore 64**, **EXL-100** /
Midway **MCR** (**Tapper** and family), and Atari **Star Wars**.
Computers: **ZX Spectrum 48K**, Amstrad CPC, **Commodore 64**, **EXL-100** /
**EXELTEL**. Consoles: NES, Game Boy / Game Boy Color, **Atari Lynx**,
**Super Cassette Vision**.
Midway **MCR** (**Tapper** and family), Atari **Star Wars**, and Sega
**OutRun**, **Hang-On**, and System 16 (**Fantasy Zone**, **Shinobi**, **Tetris**,
**Altered Beast**).
Computers: **ZX Spectrum 48K**, **Pentagon 1024**, **Scorpion 256**, Amstrad CPC,
**MSX1** / **MSX2**, **Commodore 64**, **Apple II / II+ / IIe / IIe Enhanced**,
**EXL-100** / **EXELTEL**. Consoles: NES, Game Boy / Game Boy
Color, **Atari 2600**, **Atari Lynx**, **Super Cassette Vision**, Sega Master System / Game Gear,
**Sega Genesis / Mega Drive**, Casio **PV-1000** / **PV-2000**, ColecoVision, SG-1000.

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
| M6502 CPU | `src/cpu/m6502.pas` | Gauntlet sound CPU, NES 2A03, optional 65C02 CMOS opcodes for the Lynx |
| Mr. Do driver | `src/arcade/mrdo_hw.pas` | `rol90` tile/sprite decode |
| MCR driver | `src/arcade/mcr_hw.pas` | Tapper / Tron family: dual Z80, CTC, SSIO |
| Amstrad CPC | `src/computer/amstrad_cpc.pas` | Gate Array wait-states (opcodes on a 4 T-state grid) |
| Lynx Suzy / Mikey | new | Sprite blitter, math coprocessor, timers, LCD DMA, 4-channel sound |
| Atari Lynx driver | new | 64 KiB DRAM, MAPCTL, LNX/LYX carts, 160×102 LCD |
| TIA | new | NTSC 160×192 playfield/players/missiles/ball, collisions, two-channel audio |
| Atari 2600 driver | new | 6507 + TIA + RIOT 6532, 2K/4K/F8/F6/F4(+Superchip) cartridges |
| YM2151 FM, POKEY | `src/snd/fm_2151.pas`, `src/snd/pokey.pas` | Gauntlet sound board |
| SLAPSTIC | `src/arcade/misc/slapstic.pas` | Types 101-108, bank switched protected ROM |
| Atari motion objects | `src/arcade/misc/atari_mo.pas` | SLIP based sprite lists |
| Gauntlet driver | `src/arcade/gauntlet_hw.pas` | Memory map, playfield/char/sprite video, EEPROM, YM2151+POKEY+TMS5220C |
| Skull & Crossbones driver | MAME `skullxbo.cpp` | 68000 + Atari JSA II (M6502, YM2151, OKIM6295), latched playfield, alpha layer with scanline vscroll commands, motion objects, 2816 EEPROM |
| Atari System 1 | `src/arcade/atari_system1.pas` | Marble Madness, Peter Pack Rat, Indiana Jones, Road Runner: 68000+SLAPSTIC, M6502, YM2151, POKEY, TMS5220C+VIA speech, PROM gfx banks |
| DEC T-11 CPU | MAME `t11.cpp` | Atari System 2 main CPU: PDP-11 instruction set, CP0-CP3 prioritised interrupts, mode register start address |
| Atari System 2 | MAME `atarisy2.cpp` | Paperboy, Super Sprint, APB, 720 Degrees: T-11 + SLAPSTIC 105/108/110/107, M6502, YM2151, two POKEYs, TMS5220C, banked VRAM/ROM, ADC/LETA analog controls, EEPROM |
| HD63701Y MCU | `src/cpu/m680x.pas` | Double Dragon sub CPU: internal RAM/ROM, I/O ports, output compare timer |
| MSM5205 ADPCM | `src/snd/msm5205.pas` | Two chips in Double Dragon |
| OKI MSM6295 | `src/snd/oki6295.pas` | Double Dragon II sample player |
| Double Dragon driver | `src/arcade/doubledragon_hw.pas` | Both variants: banked ROM, shared RAM, scroll, sprites, sound CPUs |
| M6805/M68705 MCU | `src/cpu/m6805.pas` | MC68705P3 protection MCU of Elevator Action |
| Taito SJ driver | `src/arcade/taitosj_hw.pas` | Main and sound Z80, four AY-3-8910, DAC, MCU handshake, three tile layers with per column scroll, sprites and PROM priorities |
| Bubble Bobble driver | `src/arcade/bubblebobble_hw.pas` | Three Z80s, M6801U4 MCU (HD63701Y with 4K ROM at `$F000`), YM2203+YM3526, PROM-driven 8×8 sprites, 256×224 |
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
| Apple Disk II | new (AppleWin 6-and-2 / MAME `a2diskiing`) | Slot 6 analog card, DOS 3.3 `.dsk`/`.do`, ProDOS `.po`, `.nib` |
| Apple II video | new | 40/80-col text, lo-res, hi-res, double hi-res, 560×384 |
| Apple II driver | new (MAME `apple2` / `apple2e`) | II, II+, IIe, IIe Enhanced, language card, IIe MMU, Disk II |
| Game Boy driver | `src/consolas/gb.pas` | DMG / CGB from cart header `$0143`, optional boot ROMs |
| VIC-II | `mos6566.pas` | PAL 6569, 384×270, sprites, bad lines |
| MOS 6526 CIA | `mos6526_old.pas` | Two chips: CIA1 IRQ + keyboard, CIA2 NMI + VIC bank |
| SID 6581 | `sid_sound.pas` | Three voices, 44100 Hz mono |
| Front end | `src/misc/main_engine.pas` | SDL2 window, texture, audio queue, keyboard |
| µPD7801 CPU | `src/cpu/upd7810.pas` (`CPU_7801`) | Epoch Super Cassette Vision CPU, 4 MHz crystal /2 |
| µPD1771C | `src/snd/upd1771.pas` | SCV tone / noise / ADPCM sound |
| Super Cassette Vision | `src/consolas/super_cassette_vision.pas` | BIOS + cartridge map, 192×222 video, keyboard and two joysticks |
| PV-2000 driver | `src/consolas/pv2000.pas` | Z80, TMS9918A, SN76489, 16 KiB BIOS, keyboard + joystick |
| TMS7000 CPU | new (MAME `tms7000` behaviour) | TMS7020/7040/7041/7042, EXL LVDP opcode |
| TMS3556 VDP | new (MAME `tms3556` behaviour) | Text 40×25, bitmap 320×250, mixed, 8 colours |
| EXL-100 / EXELTEL | new (MAME `exelv.cpp`) | Dual TMS7000, mailbox, IR keyboard, TMS5220, cartridge |
| WD1793 / Beta 128 | new (MAME `beta_m.cpp`, `wd_fdc`) | TR-DOS FDC, TRD and SCL images |
| Pentagon 1024 | new (MAME `pentagon.cpp`) | 1024 KB, uncontended 320-line ULA, GLUK, Beta disk |
| Scorpion ZS-256 | new (MAME `scorpion.cpp`) | 256 KB, port $1FFD, Magic NMI, Beta disk |
| Yamaha V9938 | new (MSX2 VDP) | 128 KiB VRAM, SCREEN 0–8, sprites, command engine |
| MSX floppy / RP-5C01 | new | WD2793-compatible FDC (512-byte FAT12 `.dsk`) and RTC |
| MSX2 driver | new (NMS 8250 layout) | Z80, V9938, 256 KiB mapper RAM, disk ROM, cartridges |
| YM2612 (OPN2) | new (from `fmopn.pas` + YM2612 DAC) | Six FM channels and PCM DAC |
| 315-5313 VDP | `src/consolas/sega_315_5313.pas` | Planes A/B, window, sprites, DMA, CRAM |
| Genesis / Mega Drive | `src/consolas/genesis.pas` | 68000 + Z80, VDP, YM2612+PSG, 3-button pads |
| Atari AVG (Star Wars) | new (MAME `avgdvg.cpp`) | PROM state machine, colour vector list |
| MOS 6532 RIOT | new (MAME `mos6532.cpp`) | 128-byte RAM, ports, timer IRQ |
| Star Wars mathbox | new (MAME `starwars_m.cpp`) | PROM microcode, multiply-accumulate, restoring divider |
| Star Wars driver | new (MAME `starwars.cpp`) | Dual 6809, AVG, 4×POKEY, TMS5220, analog stick |
| Z8002 CPU | new (MAME `z8000`) | Unsegmented 16-bit Z8002 used by Pole Position |
| MB88xx MCU | new (MAME `mb88xx`) | Fujitsu 4-bit MCU used by Namco 51/52/53/54xx |
| Pole Position driver | new (MAME `namco/polepos.cpp`) | Z80 + dual Z8002, road, sprites, real 51/52/53/54xx, WSG/engine |
| Sega PCM | `src/snd/sega_pcm.pas` | 16-channel sample player (OutRun, Hang-On) |
| 315-5195 mapper | `src/arcade/misc/sega_315_5195.pas` | 68000 memory mapper used by OutRun and System 16B |
| OutRun driver | `src/arcade/outrun_hw.pas` | Dual 68000, Z80, YM2151, Sega PCM, road + sprites |
| Hang-On driver | `src/arcade/hangon_hw.pas` | Hang-On, Enduro Racer (FD1089), Space Harrier (i8751) |
| System 16 driver | `src/arcade/system16a_hw.pas`, `system16b_hw.pas` | Fantasy Zone, Shinobi, Alex Kidd, Alien Syndrome, WB3, Tetris, Altered Beast |
| FD1089 | `src/devices/fd1089.pas` | Hitachi 68000 opcode/data encryption |
| MCS-48 / N7751 | `src/cpu/mcs48.pas` | Full N7751 + i8243 speech MCU |
| UPD7759 | `src/snd/upd7759.pas` | ADPCM slave playback (System 16B) |

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
./build/dsp --game bagman /path/to/bagman.zip
./build/dsp --scale 3 --dip 0xfe /path/to/roms/bagman/
./build/dsp --game mikie /path/to/mikie.zip
./build/dsp --game gauntlet /path/to/gauntlet.zip
./build/dsp --game skullxbo /path/to/skullxbo.zip
./build/dsp --game indydoom /path/to/indytemp.zip
./build/dsp --game roadrunn /path/to/roadrunn.zip
./build/dsp --game paperboy /path/to/paperboy.zip
./build/dsp --game ssprint /path/to/ssprint.zip
./build/dsp --game apb /path/to/apb.zip
./build/dsp --game 720 /path/to/720.zip
./build/dsp --game mrdo /path/to/mrdo.zip
./build/dsp --game ddragon /path/to/ddragon.zip
./build/dsp --game elevator /path/to/elevator.zip
./build/dsp --game kungfum /path/to/kungfum.zip
./build/dsp --game ikari /path/to/ikari.zip
./build/dsp --game ffight /path/to/ffight.zip
./build/dsp --game rtype /path/to/rtype.zip
./build/dsp --game tapper /path/to/tapper.zip
./build/dsp --game spectrum48 --tape /path/to/game.tzx /path/to/48.rom
./build/dsp --game c64 --tape /path/to/game.prg /path/to/c64-roms/
./build/dsp --game gb /path/to/game.gbc
./build/dsp --game nes /path/to/game.nes
./build/dsp --game lynx /path/to/game.lnx
./build/dsp --game scv /path/to/scv.zip
./build/dsp --game pv2000 --tape game.bin /path/to/pv2000.zip
./build/dsp --game genesis /path/to/game.md
./build/dsp --game exl100 /path/to/exl100.zip
./build/dsp --game pentagon --disk game.trd /path/to/pentagon-roms/
./build/dsp --game scorpion --disk game.scl /path/to/scorpion.rom
./build/dsp --game msx /path/to/msx1-bios/
./build/dsp --game msx2 --disk game.dsk /path/to/msx2-roms/
./build/dsp --game apple2 /path/to/apple2p.zip
./build/dsp --game apple2e --disk game.dsk /path/to/apple2e.zip
./build/dsp --game starwars /path/to/starwars.zip
./build/dsp --game polepos /path/to/polepos.zip
./build/dsp --game polepos2 /path/to/polepos2.zip
./build/dsp --game outrun /path/to/outrun.zip
./build/dsp --game hangon /path/to/hangon.zip
./build/dsp --game enduro /path/to/enduror.zip
./build/dsp --game sharrier /path/to/sharrier.zip
./build/dsp --game fantzone /path/to/fantzone.zip
./build/dsp --game shinobi /path/to/shinobi.zip
./build/dsp --game alexkidd /path/to/alexkidd.zip
./build/dsp --game aliensyn /path/to/aliensyn.zip
./build/dsp --game wb3 /path/to/wb3.zip
./build/dsp --game tetris /path/to/tetris.zip
./build/dsp --game altbeast /path/to/altbeast.zip
```

`--game` is required (`dsp --help` lists every name). Gauntlet accepts both the
four player parent set (SLAPSTIC 104) and the two player `136041-xxx` set
(SLAPSTIC 107).

Required Bagman files: `e9_b05.bin`, `f9_b06.bin`, `f9_b07.bin`, `k9_b08.bin`, `m9_b09s.bin`,
`n9_b10.bin`, `c1_b01.bin`, `e1_b02.bin`, `f1_b03s.bin`, `j1_b04.bin`, `p3.bin`, `r3.bin`.

Options:

```
--game NAME        machine to run (see `dsp --help`)
--scale N          window scale factor (default 3)
--dip [BANK:]VALUE DIP switch byte, decimal or 0x hex (bagman: one bank,
                   mikie: 0=A coinage, 1=B gameplay, 2=C flip screen;
                   gauntlet: service switch;
                   double dragon: 0=A coinage/cabinet, 1=B gameplay)
--mute             disable audio
--fullscreen       start in full screen
--screenshot FILE  headless mode: render frames and write FILE (BMP)
--frames N         frames to run in headless mode (default 300)
--tape FILE        tape/cart: Spectrum/CPC/C64 (.tap/.tzx/.cdt/.prg/.t64),
                   EXL-100 / EXELTEL cartridge, or PV-2000 cart (.bin/.rom)
--disk FILE        floppy: CPC/Spectrum +3 .dsk/.edsk, MSX2 .dsk, Apple II .dsk/.do/.po/.nib,
                   Pentagon/Scorpion .trd/.scl
```

### Atari System 1 (Indiana Jones, Marble Madness, Peter Pack Rat, Road Runner)

Atari System 1 is a 7.16 MHz 68000 behind a SLAPSTIC (105 on Indiana Jones, 103
on Marble Madness, 107 on Peter Pack Rat, 108 on Road Runner), a 1.79 MHz M6502
with a YM2151 and a POKEY, and playfield/motion-object banks decoded from a pair
of colour PROMs (`convert_back` in `atari_system1.pas`). Indiana Jones and Road
Runner also have a TMS5220C speech chip behind a MOS 6522 VIA at `$1000` (same
clock as the sound CPU; TMS clock is `14318180/2/11`). The visible area is
336×240.

Game zips need the Atari System 1 BIOS (`atarisy1.zip` with `136032.205.l13` /
`136032.206.l12` / `136032.104.f5`). Put `atarisy1.zip` next to the game zip; the
driver opens it automatically.

```bash
./build/dsp --game indydoom /path/to/indytemp.zip
./build/dsp --game marble /path/to/marble.zip
./build/dsp --game peter /path/to/peterpak.zip
./build/dsp --game roadrunn /path/to/roadrunn.zip
```

Whip / hop / button 1 is Left Ctrl or Space. Coins are 5/6. Arrow keys drive the
digital joystick (Indy, Peter) or the analog stick (Road Runner, through the
ADC at `$f40000`). The Marble Madness trackball is not emulated yet (reads as
`$FF`, matching the Pascal stub).

### Atari System 2 (Paperboy, Super Sprint, APB, 720 Degrees)

Atari System 2 replaces the 68000 with a 10 MHz DEC T-11 (`src/cpu/t11.cpp`, a
port of MAME's core: PDP-11 instruction set, four prioritised CP interrupt
lines, start address taken from the mode register, which is `$36ff` here). The
`020000-037777` window is banked between the alpha/motion-object RAM and the two
playfield halves, and the two `040000`/`060000` ROM windows are paged by a
game specific SLAPSTIC (105 for Paperboy, 108 for Super Sprint, 110 for APB and
107 for 720). Sound is a 1.79 MHz M6502 with a YM2151, two POKEYs and, on every
set except Super Sprint, a TMS5220C behind the usual pair of latches, plus a
2804 EEPROM and the ADC/LETA analog inputs. The visible area is 512×384, or
384×512 for the rotated APB monitor.

```bash
./build/dsp --game paperboy /path/to/paperboy.zip
./build/dsp --game ssprint /path/to/ssprint.zip
./build/dsp --game apb /path/to/apb.zip
./build/dsp --game 720 /path/to/720.zip
```

Coins are 5/6 and 4 is the service coin on every set; key 3 holds the self test
switch, so the diagnostics advance with 2.

* **Paperboy**: throw a paper with Left Ctrl/Space or start 1, brake with
  Left Alt/Z or start 2. The handlebar is the ADC steering, driven by the arrows.
* **Super Sprint**: 1/2 start players 1 and 2, X starts player 3; Left Ctrl,
  Left Alt and C are the three accelerator pedals and the arrows steer the
  first wheel.
* **APB**: Left Alt is the siren and X the shot; Left Ctrl accelerates and the
  arrows steer the police car.
* **720 Degrees**: the arrows spin the LETA rotation disc (left/right) and
  Left Ctrl/Space and Left Alt/Z are the jump/action buttons.

### Exelvision EXL-100 and EXELTEL

French home computers from 1984/1986. They are not in dsp-emulator; this port
follows MAME `exelv.cpp`. Each machine has a custom TMS7020 (EXL-100) or TMS7040
(EXELTEL) at 4.9152 MHz with the SWAP R opcode replaced by LVDP (VRAM peek), a
TMS7041/7042 I/O CPU talking through a 74LS374 mailbox, a TMS3556 VDP
(40×25 text / 320×250 bitmap, 8 colours, 32 KiB VRAM) and a TMS5220C speech
synthesizer. The keyboard and joysticks are infrared.

ROMs are **not** shipped. MAME split sets from [mdk.cab](https://mdk.cab/download/split/exeltel.zip):

* EXL-100 (`exl100.zip`): `exl100in.bin` (TMS7020, CRC `049109a3`) and
  `exl100_7041.bin` (TMS7041, CRC `38f6fc7a`).
  https://mdk.cab/download/split/exl100.zip
* EXELTEL (`exeltel.zip`): `exeltel_7040.bin` (TMS7040, CRC `2792f02f`),
  `exeltel_7042.bin` (I/O CPU, **BAD_DUMP** in MAME, CRC `a0163507`),
  `exeltel14.bin` (French v1.4, 64 KiB, CRC `52a80dd4`) or `amper.bin`
  (Spanish, CRC `45af256c`), and optionally `cm62312.bin` (speech).
  https://mdk.cab/download/split/exeltel.zip

[RetroBIOS](https://github.com/Abdess/retrobios) does not currently publish an
Exelvision pack. The mdk.cab files match the MAME hashes. The TMS7040 also
matches DCExel’s `exeltel_rom.zip` (CRC `2792f02f`).

The TMS7042 I/O ROM has never been redumped. Running MAME’s image posts mailbox
`$04` and the TMS7040 hangs at `$FA29`, so this driver ignores that CRC and HLE’s
mailbox `$08` plus the PA.0 handshake. EXL-100 BIOS-only boot shows the
Exelvision butterfly logo. EXELTEL turns the TMS3556 on in bitmap mode (red
active area, cyan border); a full menu still needs a real 7042 dump.

Load a cartridge with `--tape` or by placing a `.bin`/`.rom` beside the BIOS.
Exel Basic (`exelbas`) is the usual way to get a prompt on the EXL-100.

```bash
./build/dsp --game exl100 /path/to/exl100.zip
./build/dsp --game exl100 --tape /path/to/exelbas.bin /path/to/exl100.zip
./build/dsp --game exeltel /path/to/exeltel.zip
```

The host keyboard is the infrared keyboard (AZERTY layout as in MAME). Cursor
keys and Left Ctrl (CTL) work; FCT is Right Ctrl. Cassette motor control is not
emulated; port B bit 3 still feeds a 1-bit DAC into the speaker.

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

### Bubble Bobble

Taito's Bubble Bobble board runs a 6 MHz main Z80, a 6 MHz sub Z80 that shares
`$E000–$F7FF`, a 3 MHz sound Z80 with a YM2203 and a YM3526, and a 4 MHz
M6801U4 MCU that feeds coins, DIPs and the main IRQ. Video is a single layer of
8×8 4bpp tiles whose column layout is described by object RAM at `$DD00` and a
256-byte PROM; the visible area is 256×224 at ~59.19 Hz.

```bash
./build/dsp --game bublbobl /path/to/bublbobl.zip
```

`--game` also accepts `bubblebobble`. DIP bank 0 is DSW A (default `$FE`: English,
demo sounds on, 1C/1C) and bank 1 is DSW B (default `$FF`: normal difficulty,
3 lives). Coins are active-high; the rest of the panel is active-low.

ROM set (MAME `bublbobl`): `a78-06-1.51`, `a78-05-1.52`, `a78-08.37`, `a78-07.46`,
`a78-01.17`, `a71-25.41`, and gfx `a78-09.12` … `a78-20.35`.

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

### Pentagon 1024 and Scorpion 256

Soviet Spectrum clones with a Beta 128 disk interface (WD1793 / KR1818VG93) and
TR-DOS. Both run at 3.5 MHz with 224 T-states per line and 320 lines per frame
(no ULA contention). Disks are `.trd` (raw geometry) or `.scl` (catalogue +
files, expanded to a DS/80 TR-DOS volume).

Pentagon 1024 wants a 32 KB 128K ROM (`128p-0.rom`+`128p-1.rom`/`zx128_1.rom` or
`pentagon.rom`) plus TR-DOS (`trd503.rom` / `trdos.rom`). Optional `gluk63r.rom`
is the 1024SL boot monitor. RAM at `$C000` is
`(7FFD & 7) | ((7FFD & 0xC0) >> 3) | ((DFFD & 1) << 5)`.

Scorpion ZS-256 wants a 64 KB ROM (`scorpion.rom` / `scorp294.rom`, or
`scorp0.rom`…`scorp3.rom`): 128 editor, 48 BASIC, service, TR-DOS. Port `$1FFD`
bit 0 maps RAM page 0 at `$0000`, bit 1 selects the service ROM, bit 4 is the
256 KB RAM bit. F5 is the Magic button (NMI).

MAME 0.221 names (merged parent is `spec128.zip`; clones live in subfolders
`pentagon/`, `pent1024/`, `scorpio/`; TR-DOS is the `spectrum_beta128` device).
A directory of split zips also works: `pentagon.zip` + `spectrum_beta128.zip`,
`pent1024.zip` for GLUK, `scorpio.zip` for the Scorpion 64 KB ROM.

ZXMak 0.28.2 ([zxmak0282.zip](https://zxmak.narod.ru/Soft/zxmak0282.zip)) ships
64 KB `PENTAGON.ROM` (128+48+boot+TR-DOS 5.04F) and `scorpion.rom` in one
archive; pass the zip or the extracted files.

```bash
./build/dsp --game pentagon --disk elite.trd /path/to/zxmak0282.zip
./build/dsp --game scorpion --disk game.scl /path/to/zxmak0282.zip
./build/dsp --game pentagon --disk elite.trd /path/to/mame-roms/
./build/dsp --game scorpion --disk game.scl /path/to/scorpio.zip
```

TR-DOS is paged in by executing at `$3D00` while the 48K ROM is selected
(`RANDOMIZE USR 15616`). Kempston on port `$1F` is disabled while DOS is paged
so it does not clash with the FDC.

### MSX1 and MSX2

MSX1 (`--game msx`) is a Panasonic-style machine: Z80 at 3.579545 MHz, TMS9918A,
AY-3-8910, i8255 slot/keyboard/tape, 64 KiB RAM. It needs a 32 KiB MSX1 BIOS
(`mpc100bios.rom` / generic `MSX.rom`). Cartridges (`.rom`) go in slot 1 via
`--tape` / `load_media`; cassettes are `.tzx` / `.cas`.

MSX2 (`--game msx2`, aliases `nms8250` and `philips-msx2`) follows the Philips
NMS 8250 map rather than a Pascal original: Yamaha V9938 (512×212, 128 KiB
VRAM), 256 KiB mapper RAM, RP-5C01 RTC, and a WD2793 disk interface.

| Slot | Contents |
| --- | --- |
| 0 | 32 KiB main BIOS (pages 0–1) |
| 1 | cartridge (linear ≤48 KiB, ASCII16 above that) |
| 3 expanded | 3-0: 16 KiB sub-ROM; 3-1: mapper RAM; 3-2: 16 KiB disk ROM + FDC |

ROMs are **not** shipped. A directory or zip with these names works (MAME
`nms8250` hashes in brackets):

* `MSX2.ROM` / `nms8250_basic-bios2.rom` / `msx2_bios.rom` (32 KiB, CRC `6cdaf3a5`)
* `MSX2EXT.ROM` / `nms8250_msx2sub.rom` / `msx2_ext.rom` (16 KiB, CRC `66237ecf`)
* `nms8250_disk.rom` / `DISK.ROM` / `cbios_disk.rom` (16 KiB, optional; floppy disabled if missing)

[zxtiny](https://github.com/Chusogar/zxtiny/tree/main/roms) ships the same BIOS as
`msx2_bios.rom` + `msx2_ext.rom`. C-BIOS (`cbios_main_msx2.rom` + `cbios_sub.rom`,
optional `cbios_logo_msx2.rom` in slot 0 page 2) also loads. [RetroBIOS](https://github.com/Abdess/retrobios)
publishes matching dumps under `bios/Microsoft/MSX/`. The FDC accepts raw FAT12 `.dsk`
images (720K / 640K / 360K / 180K) and CPC-style `MV - CPC` / `EXTENDED` DSK files,
decoded at both type 1 (`$7FF8`) and type 2 (`$7FB8`) addresses.

```bash
./build/dsp --game msx /path/to/msx1-bios/
./build/dsp --game msx2 /path/to/msx2-roms/
./build/dsp --game msx2 --disk game.dsk /path/to/nms8250.zip
```

The host keyboard is the MSX matrix (same layout as MSX1). Joysticks are on the
AY-3-8910 port A. F6 toggles cassette play when a `.cas`/`.tzx` is loaded.

### Casio PV-2000

Home computer/console hybrid from 1983, ported from `pv2000.pas`. The positional
argument is the 16 KiB BIOS (`hn613128pc64.bin`, CRC `8f31f297`, MAME set
`pv2000.zip`). A verified dump is in
[Abdess/retrobios](https://github.com/Abdess/retrobios) at
`bios/Casio/PV-2000/`. The cartridge (8 KiB or 16 KiB `.bin` / `.rom`, plain or
zipped) is attached with `--tape`.

```bash
./build/dsp --game pv2000 /path/to/pv2000.zip
./build/dsp --game pv2000 --tape /path/to/game.bin /path/to/pv2000.zip
```

Hardware: Z80 and SN76489 at 3.579545 MHz, TMS9918A (256×192, NMI on vblank),
4 KiB RAM at `$7000` and the cartridge window at `$c000`. The BIOS keyboard
matrix is mapped to the host keys (letters, digits, arrows, Enter, Backspace,
Shift, Tab for HOME). The arcade stick (arrows + Ctrl/Alt) is the built-in
joystick / Attack 0 and Attack 1. Cassette I/O is stubbed.

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

### Sega Genesis / Mega Drive

The WIP `genesis.pas` driver is completed here: a 7.67 MHz 68000, a 3.58 MHz
Z80, the 315-5313 VDP (planes A/B, window, sprites, DMA fill/copy/68k, CRAM)
and a YM2612 plus the VDP's SN76489. Cartridges are raw `.bin` / `.md` /
`.gen` dumps, interleaved `.smd` (optional 512-byte copier header), or a zip
holding one of those.

```bash
./build/dsp --game genesis /path/to/game.md
./build/dsp --game megadrive /path/to/game.bin
./build/dsp --game genesis-pal /path/to/game.md
./build/dsp --game genesis --screenshot title.bmp --frames 180 --mute game.md
```

`--game genesis` / `megadrive` is NTSC (USA, version register `$A1`).
`genesis-jp` is NTSC domestic (`$80`). `genesis-pal` is PAL (`$C1`, 313
lines). `--dip 0:0` / `1` / `2` also selects Japan / USA / Europe on the
version register (games read it for the SEGA screen and lock-out).

A is Left Ctrl or Space, B is Left Alt or Z, C is X, Start is `1`. The D-pad
is the arrow keys. Player 2 is R/F/D/G plus A/S/Q.

SRAM at `$200000` is mapped when the cartridge header has an `RA` extra-memory
block, or for dumps of 2 MiB and under. Super Street Fighter II style 512 KiB
banks at `$A130F3`–`$A130FF` are used when the image is larger than 4 MiB.
There is no TMSS lock (bit 7 of the version register is set). Interlace mode
3, EEPROM mappers and the 6-button pad are not emulated.

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
the datasette). Arrows are also a joystick in Control Port 2.

Disk access uses a real emulated drive when its DOS ROM sits next to the C64
ROMs: `dos1541` (aliases `dos1541.bin`, `1541.rom`, `1541`, `d1541.rom`,
`325302-01.uab4`) is preferred, and `dos1540` (aliases `dos1540.bin`,
`1540.rom`, `1540`) is used as a fallback. The drive then answers on the serial
bus as device 8, so `LOAD"$",8`, `LOAD"*",8,1` and `LOAD"NAME",8,1` all work:

```bash
./build/dsp --game c64 --disk game.d64 /path/to/c64-roms/
./build/dsp --game c64 --disk game.t64 /path/to/c64-roms/
```

`.d64` and `.g64` images are mounted directly. A `.t64` archive has no disk
structure, so its files are placed on a disk image built in memory and served
through the drive as well. Without a drive ROM there is no device on the bus and
`.d64`/`.t64` fall back to injecting their first program into RAM
(`.g64` is rejected).

### Apple II, II+, IIe and IIe Enhanced

There is no Pascal original in dsp-emulator; the driver follows the MAME
`apple2` / `apple2e` map (6502 at 1.020484 MHz, 262×65 cycles per frame) with a
Disk II card in slot 6. Four `--game` names pick the firmware:

| `--game` | Aliases | CPU | Firmware |
| --- | --- | --- | --- |
| `apple2` | `appleii`, `apple2plus`, `apple2p` | 6502 | II+ Applesoft + Autostart (12 KiB at `$D000`) |
| `apple2orig` | `apple2integer` | 6502 | original ][ Integer BASIC + Autostart (8 KiB at `$E000`) |
| `apple2e` | `appleiie` | 6502 | unenhanced IIe (16 KiB `$C000`–`$FFFF`) |
| `apple2ee` | `apple2eplus`, `apple2e+`, `apple2enhanced` | 65C02 | IIe Enhanced + MouseText |

ROMs are **not** shipped. Point the emulator at a directory or zip from
[Abdess/retrobios](https://github.com/Abdess/retrobios) (`bios/Apple/Apple II/`):

* II+: MAME `apple2p.zip` (`341-0011.d0` … `341-0020-00.f8` + `341-0036.chr`) or the
  concatenated `apple2-asoft-auto.rom` (12 KiB)
* IIe: `apple2e.zip` (`342-0135-b.64` + `342-0134-a.64` + `342-0133-a.chr`)
* IIe Enhanced: `apple2ee.zip` (`342-0304-a.e10` + `342-0303-a.e8` + `342-0265-a.chr`)
* Disk II P5 PROM (optional, slot 6): `341-0027-a.p5` / `disk2-16boot.rom` (256 bytes).
  Without it Autostart drops into BASIC; with it a missing floppy waits on the drive
  the same way a real Disk II does.

`--disk` accepts 140K DOS 3.3 `.dsk`/`.do`, ProDOS-order `.po`, and `.nib` tracks.
The host keyboard is the Apple keyboard (high-bit ASCII, Ctrl as Control). On the
IIe, Z / Left Alt is Open-Apple and X is Closed-Apple. F3 still resets the machine.

```bash
./build/dsp --game apple2 /path/to/apple2p.zip
./build/dsp --game apple2e /path/to/apple2e.zip
./build/dsp --game apple2ee --disk game.dsk /path/to/apple2ee.zip
```

### Game Boy / Game Boy Color

`--game gb` loads a `.gb` / `.gbc` cartridge (plain or zipped). The machine is
chosen from header byte `$0143` the same way `gb.pas` does: bit 7 set (`$80`
CGB-enhanced or `$C0` CGB-exclusive) runs as Game Boy Color; otherwise it is
a DMG Game Boy. Optional boot ROMs (`dmg_boot.bin`, `cgb_boot.bin`) may sit
next to the cartridge; without them the CPU is left in the documented post-boot
state so games still start at `$0100` — including `A = $11` on Game Boy Color,
which is how the cartridges detect the hardware and enable their colour mode.

Colour hardware: banked VRAM and WRAM, the 8+8 CGB palettes, general-purpose
and HBlank VRAM DMA, and the KEY1 double-speed switch (which does not speed up
the sound, as on real hardware). All of those registers only answer on Game Boy
Color; on DMG they read back as `$FF` and are inert.

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
### Atari 2600

The VCS has no BIOS. A 6507 (6502 with a 13-bit bus and no IRQ/NMI pins) runs at
1.193182 MHz beside the **TIA** (160×192 NTSC picture, two audio channels) and a
MOS **6532** RIOT (128 bytes RAM, joystick ports, console switches, interval
timer). Cartridges occupy `$1000–$1FFF`. 2K dumps are mirrored, 4K is mapped
straight in, and 8K/16K/32K/64K images use the common F8/F6/F4/F0 hotspots.
A 128-byte Superchip RAM window is enabled for `*sc*` names or dumps whose size
is a power of two plus 128.

```bash
./build/dsp --game a2600 /path/to/game.bin
./build/dsp --game vcs /path/to/game.a26 --screenshot a2600.bmp --frames 120
```

Player 1/2 sticks are the joysticks (active low on SWCHA). Fire is `button1`
(INPT4/INPT5). Start is RESET, Select is SELECT. DIP bank 0 bit 3 is colour
(default on); bits 6–7 are the P0/P1 difficulty switches.

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

### Atari Star Wars

Vector arcade from 1983. Two 1.5 MHz 6809s (main + sound), the Atari AVG, a PROM
mathbox, four POKEYs and a TMS5220. The visible vector area is 250×280 inside a
400×300 window at ~41 Hz.

```bash
./build/dsp --game starwars /path/to/starwars.zip
```

Fire is Left Ctrl / Space, the yoke is the arrow keys, coin is 5. ROMs are not
committed. The MAME parent set `starwars` is enough:

`136021.214.1f`, `136021.102.1hj`, `136021.203.1jk`, `136021.104.1kl`,
`136021.206.1m`, vector `136021-105.1l`, sound `136021-107.1jk` +
`136021-208.1h`, AVG PROM `136021-109.4b`, mathbox `136021-110.7h` …
`136021-113.7l`.

### Namco Pole Position / Pole Position II

Z80 + two Z8002s at 3.072 MHz, 256×224, ~60.6 Hz. The Namco 06xx talks to
real MB88 MCUs: 51xx (coins, DIPs, start) and 53xx (steering + DSWA). Audio
is the Pole Position WSG, engine sample player, and MB88 52xx/54xx DACs. Use
the MAME 0.221 merged parent sets `polepos` and `polepos2`. The 51xx–54xx
MCU dumps come from the same archive and are fetched automatically if they are
not already next to the game zip:

- https://archive.org/download/mame-0.221-roms-merged/namco51.zip
- https://archive.org/download/mame-0.221-roms-merged/namco52.zip
- https://archive.org/download/mame-0.221-roms-merged/namco53.zip
- https://archive.org/download/mame-0.221-roms-merged/namco54.zip

```bash
./build/dsp --game polepos /path/to/polepos.zip
./build/dsp --game polepos2 /path/to/polepos2.zip
```

Steer with the arrows, accelerate with Left Ctrl / Space, brake with Z / Down,
toggle gear with X, coin with 5. DIP bank 0 is DSWA, bank 1 is DSWB.
### Sega OutRun, Hang-On, and System 16

Ported from [dsp-emulator](https://github.com/leniad/dsp-emulator)
(`outrun_hw.pas`, `hangon_hw.pas`, `system16a_hw.pas`, `system16b_hw.pas`).
`--game` names match the MAME parent sets. Screen is 320×224.

```bash
./build/dsp --game outrun /path/to/outrun.zip
./build/dsp --game hangon /path/to/hangon.zip
./build/dsp --game enduro /path/to/enduror.zip
./build/dsp --game sharrier /path/to/sharrier.zip
./build/dsp --game fantzone /path/to/fantzone.zip
./build/dsp --game shinobi /path/to/shinobi.zip
./build/dsp --game alexkidd /path/to/alexkidd.zip
./build/dsp --game aliensyn /path/to/aliensyn.zip
./build/dsp --game wb3 /path/to/wb3.zip
./build/dsp --game tetris /path/to/tetris.zip
./build/dsp --game altbeast /path/to/altbeast.zip
```

OutRun and Hang-On family games use analog wheel / gas / brake (arrow keys plus
button 1/2) and a gear toggle on button 3. System 16 games use a two-button
joystick.

Enduro Racer decrypts the FD1089B program ROMs with `317-0013a.key`. Space
Harrier runs the i8751 MCU that raises 68000 IRQs. Shinobi, Alex Kidd and Alien
Syndrome use a full N7751 (MCS-48 + i8243 + DAC). Alien Syndrome and Wonder Boy
III decrypt FD1089 program ROMs. Altered Beast plays UPD7759 samples in slave
mode (DRQ pulses the Z80 NMI). Tetris prefers the decrypted/bootleg program
ROMs and falls back to FD1089 if only the encrypted pair is present.

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
src/cpu/        Z80, M6809, M6502, M68000, HD63701, M6805, µPD7801, TMS7000, NEC V30, MB88xx
src/sound/      AY-3-8910, SN76496, NES APU, SID, µPD1771C, QSound, TMS5220, Pole Position WSG/engine
src/video/      graphics decode, palettes, NES PPU, VIC-II, GB PPU, TMS3556
src/machine/    PAL16R6, SLAPSTIC, tapes, NES/GB mappers, MOS 6526, Lynx, WD1793/Beta
src/video/      graphics decode, palettes, NES PPU, VIC-II, GB PPU, TMS3556, AVG
src/machine/    PAL16R6, SLAPSTIC, tapes, NES/GB mappers, MOS 6526, 6532, Lynx, mathbox
src/cpu/        Z80, M6809, M6502, M68000, HD63701, M6805, µPD7801, TMS7000, NEC V30, MCS-51
src/sound/      AY-3-8910, SN76496, NES APU, SID, µPD1771C, QSound, TMS5220, Sega PCM
src/video/      graphics decode, palettes, NES PPU, VIC-II, GB PPU, TMS3556, V9938, AVG, Sega 16, TIA
src/machine/    PAL16R6, SLAPSTIC, tapes, NES/GB mappers, MOS 6526, 6532, Lynx, mathbox, 315-5195, MSX FDC/RTC
src/drivers/    the machines themselves (memory map, video, inputs)
src/frontend/   SDL2 front end, driven through the core/machine.h interface
src/core/       ROM loader (directory or zip) and the Machine interface
```
