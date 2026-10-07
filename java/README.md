# jDSP

Java port of [dsp-cpp](https://github.com/Chusogar/dsp-cpp) (itself a C++17 + SDL2
port of [dsp-emulator](https://github.com/leniad/dsp-emulator)). Intended for
[Chusogar/jDSP](https://github.com/Chusogar/jDSP); this tree also lives under
`java/` in dsp-cpp so the port can be reviewed until it is pushed there.

This drop ports the complete **Bagman** (Valadon Automation, 1982) and
**Pirates** / **Genix Family** (NIX, 1994) drivers and every component they need,
keeping the dsp-cpp layout:

| Component | Java | Origin (dsp-cpp) |
| --- | --- | --- |
| Machine interface, inputs | `dsp.core` | `src/core/machine.h` |
| ROM loader (dir / zip, CRC) | `dsp.core.RomLoader` | `src/core/rom_loader.cpp` |
| Z80 CPU | `dsp.cpu.Z80` | `src/cpu/z80.cpp` |
| M68000 CPU | `dsp.cpu.M68000` | `src/cpu/m68000.cpp` |
| AY-3-8910 PSG | `dsp.sound.AY8910` | `src/sound/ay8910.cpp` |
| OKI MSM6295 ADPCM | `dsp.sound.OKIM6295` | `src/sound/okim6295.cpp` |
| Graphics decode, resistor palette | `dsp.video` | `src/video/gfx.cpp` |
| PAL16R6 protection | `dsp.machine.BagmanPal` | `src/machine/bagman_pal.cpp` |
| 93C46 EEPROM | `dsp.machine.Eeprom93C46` | `src/machine/eeprom93c46.cpp` |
| Bagman driver | `dsp.drivers.arcade.Bagman` | `src/drivers/arcade/bagman.cpp` |
| Pirates / Genix driver | `dsp.drivers.arcade.Pirates` | `src/drivers/arcade/pirates.cpp` |
| Front end | `dsp.frontend.SwingApp` | `src/frontend/sdl_app.cpp` |

To add another machine follow [docs/adding-a-driver.md](docs/adding-a-driver.md).

## Building

Requirements: JDK 17+ (Maven 3.6+ optional).

```bash
make jar
# or
mvn -q -DskipTests package
```

This produces `target/jdsp.jar` (Make) or `target/jdsp-0.1.0-SNAPSHOT.jar` (Maven).

Unit tests (Z80, 68000, AY-3-8910, OKI M6295, graphics, PAL) run without ROMs.
If `pirates.zip` is present at `/tmp/roms/pirates.zip` or `PIRATES_ZIP` is set,
the Pirates boot test also runs:

```bash
make test
# or
mvn -q -DskipTests compile
java -cp target/classes dsp.Tests
```

## Running

ROMs are **not** included. Point the emulator at a MAME zip set or at a
directory holding the individual files:

```bash
java -jar target/jdsp.jar --game bagman /path/to/bagman.zip
java -jar target/jdsp.jar --game pirates /path/to/pirates.zip
java -jar target/jdsp.jar --game genix /path/to/genix.zip
java -jar target/jdsp.jar --game pirates --screenshot pirates.bmp --frames 300 --mute pirates.zip
```

### Bagman

Required files: `e9_b05.bin`, `f9_b06.bin`, `f9_b07.bin`, `k9_b08.bin`,
`m9_b09s.bin`, `n9_b10.bin`, `c1_b01.bin`, `e1_b02.bin`, `f1_b03s.bin`,
`j1_b04.bin`, `p3.bin`, `r3.bin`.

```bash
curl -L -o bagman.zip https://archive.org/download/mame-0.221-roms-merged/bagman.zip
```

### Pirates

NIX hardware: one 16 MHz 68000, OKI M6295 (pin 7 low, /165), 93C46 EEPROM,
288×224 @ 60 Hz. Program, tile, sprite and sample ROMs are address- and
data-scrambled; the driver decrypts them at load time and applies the
`$62c0` protection patch.

Required parent files: `r_449b.bin`, `l_5c1e.bin`, `p4_4d48.bin`, `p2_5d74.bin`,
`p1_7b30.bin`, `p8_9f4f.bin`, `s1_6e89.bin`, `s2_6df3.bin`, `s4_fdcc.bin`,
`s8_4b7c.bin`, `s89_49d4.bin`.

```bash
curl -L -o pirates.zip https://archive.org/download/mame-0.221-roms-merged/pirates.zip
```

Settings live in the 93C46, not in DIP switches (the in-game service menu
writes the EEPROM).

Options:

```
--game NAME        machine to run (required; bagman, pirates, genix)
--scale N          window scale factor (default 3)
--dip [BANK:]VALUE DIP switch byte, decimal or 0x hex (bagman: one bank)
--mute             disable audio
--fullscreen       start maximized
--screenshot FILE  headless mode: render frames and write FILE (BMP)
--frames N         frames to run in headless mode (default 300)
```

Controls: arrows move, Left Ctrl/Space button 1, Left Alt/Z button 2,
1/2 start, 5/6 insert coin, P pause, F3 reset, Esc quit.
