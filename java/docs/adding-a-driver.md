# Adding a driver

Every machine is a `dsp.core.Machine`: the Swing front end only knows how to run
frames, push inputs and read a framebuffer, so a new driver never touches AWT.

Package layout mirrors dsp-cpp:

```
src/main/java/dsp/
  core/          Machine, RomLoader
  cpu/           Z80, M68000, IrqLine
  sound/         AY8910, OKIM6295
  video/         GfxSet, Palette
  machine/       protection / support chips (BagmanPal, Eeprom93C46, …)
  drivers/arcade/
  frontend/      SwingApp
```

## 1. Read the C++ driver

| C++ | Java |
| --- | --- |
| `read_byte` / `write_byte` | `readByte` / `writeByte` |
| `read_port` / `write_port` | `readPort` / `writePort` |
| `run_frame` | `runFrame` |
| `set_inputs` | `setInputs` |
| `init` | `init` |
| `RomEntry` tables | `RomEntry` lists |
| `kFramesPerSecond` / `kScanlines` | `FRAMES_PER_SECOND` / `SCANLINES` |

## 2. ROMs

```java
private static final List<RomEntry> MAIN_ROMS = List.of(
    new RomEntry("rom1.bin", 0x4000, 0x0000, 0x12345678)
);
```

* `offset` is the destination inside the buffer passed to `load`.
* `crc` of `0` skips the check.
* Alternative file names are separated by `|`.

## 3. Register the driver

In `dsp.Main.createMachine`:

```java
if (game.equals("galaxian")) return new Galaxian();
if (game.equals("pirates")) return new Pirates(Pirates.Game.PIRATES);
```

Add the name to the usage text and to the tables in `README.md`.
