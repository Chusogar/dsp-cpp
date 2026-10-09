# jDSP

Java port of [dsp-cpp](https://github.com/Chusogar/dsp-cpp) (itself a C++17 + SDL2
port of [dsp-emulator](https://github.com/leniad/dsp-emulator)). Intended for
[Chusogar/jDSP](https://github.com/Chusogar/jDSP); this tree also lives under
`java/` in dsp-cpp so the port can be reviewed until it is pushed there.

This first drop ports the complete **Bagman** (Valadon Automation, 1982) driver
and every component it needs, keeping the dsp-cpp layout:

| Component | Java | Origin (dsp-cpp) |
| --- | --- | --- |
| Machine interface, inputs | `dsp.core` | `src/core/machine.h` |
| ROM loader (dir / zip, CRC) | `dsp.core.RomLoader` | `src/core/rom_loader.cpp` |
| Z80 CPU | `dsp.cpu.Z80` | `src/cpu/z80.cpp` |
| AY-3-8910 PSG | `dsp.sound.AY8910` | `src/sound/ay8910.cpp` |
| Graphics decode, resistor palette | `dsp.video` | `src/video/gfx.cpp` |
| PAL16R6 protection | `dsp.machine.BagmanPal` | `src/machine/bagman_pal.cpp` |
| Bagman driver | `dsp.drivers.arcade.Bagman` | `src/drivers/arcade/bagman.cpp` |
| Front end | `dsp.frontend.SwingApp` | `src/frontend/sdl_app.cpp` |

To add another machine follow [docs/adding-a-driver.md](docs/adding-a-driver.md).

## Building

Requirements: JDK 17+ and Maven 3.6+.

```bash
mvn -q -DskipTests package
```

This produces `target/jdsp-0.1.0-SNAPSHOT.jar`.

Unit tests (Z80, AY-3-8910, graphics, PAL) run without ROMs:

```bash
mvn -q -DskipTests compile
java -cp target/classes dsp.Tests
```

## Running

ROMs are **not** included. Point the emulator at a MAME `bagman.zip` set or at a
directory holding the individual files:

```bash
java -jar target/jdsp-0.1.0-SNAPSHOT.jar --game bagman /path/to/bagman.zip
java -jar target/jdsp-0.1.0-SNAPSHOT.jar --scale 3 --dip 0xfe /path/to/roms/bagman/
java -jar target/jdsp-0.1.0-SNAPSHOT.jar --game bagman --screenshot bagman.bmp --frames 600 --mute bagman.zip
```

Required Bagman files: `e9_b05.bin`, `f9_b06.bin`, `f9_b07.bin`, `k9_b08.bin`,
`m9_b09s.bin`, `n9_b10.bin`, `c1_b01.bin`, `e1_b02.bin`, `f1_b03s.bin`,
`j1_b04.bin`, `p3.bin`, `r3.bin`.

A merged MAME set from Internet Archive works (the loader matches files by
basename, so clone ROMs in subdirectories are ignored):

```bash
curl -L -o bagman.zip https://archive.org/download/mame-0.221-roms-merged/bagman.zip
```

Options:

```
--game NAME        machine to run (required; currently bagman)
--scale N          window scale factor (default 3)
--dip [BANK:]VALUE DIP switch byte, decimal or 0x hex (bagman: one bank)
--mute             disable audio
--fullscreen       start maximized
--screenshot FILE  headless mode: render frames and write FILE (BMP)
--frames N         frames to run in headless mode (default 300)
```

Controls: arrows move, Left Ctrl/Space button 1, Left Alt/Z button 2,
1/2 start, 5/6 insert coin, P pause, F3 reset, Esc quit.
