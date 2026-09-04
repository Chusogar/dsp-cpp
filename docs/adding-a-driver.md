# Adding a driver

Every machine in this project is a `dsp::Machine` (see `src/core/machine.h`): the
front end only knows how to run frames, push inputs and read a framebuffer, so a
new driver never touches SDL.

The fastest way to start is the template:

```sh
python3 tools/new_driver.py galaxian --class Galaxian --title "Galaxian"
```

It writes `src/drivers/arcade/galaxian.h` and `src/drivers/arcade/galaxian.cpp`
from `docs/templates/` (use `--kind computers` or `--kind consoles` for those
trees), and prints the two lines you still have to paste into `CMakeLists.txt`
and `src/main.cpp`.

## 1. Read the Pascal driver

The reference implementation lives in the original emulator, one unit per
hardware: `arcade/<game>_hw.pas` (or `ordenadores/<computer>.pas`). Look for:

| Pascal | C++ |
| --- | --- |
| `<game>_getbyte` / `<game>_putbyte` | `read_byte` / `write_byte` |
| `<game>_inbyte` / `<game>_outbyte` | `read_port` / `write_port` |
| `<game>_loop` | `run_frame` |
| `eventos_<game>` | `set_inputs` |
| `iniciar_<game>` | `init` |
| `<game>_rom` tables | `RomEntry` tables |
| `llamadas_maquina.fps_max` / `scanlines` | `kFramesPerSecond` / `kScanlines` |

## 2. ROMs

Declare the set as `RomEntry` tables and load it with `RomLoader`, which accepts
both a directory and a `.zip`:

```cpp
const std::vector<RomEntry> kMainRoms = {
    {"rom1.bin", 0x4000, 0x0000, 0x12345678},
};
```

* `offset` is the destination inside the buffer you pass to `load`.
* `crc` of `0` skips the check (useful for sets with several revisions).
* Alternative file names are separated by `|`: `{"21jm-0.ic55|63701.bin", ...}`.

## 3. CPUs and sound chips

Reuse what is already in `src/cpu` (`Z80`, `M6809`, `M6502`, `M68000`,
`HD63701`) and `src/sound` (`AY8910`, `SN76496`, `YM2151`, `POKEY`, `MSM5205`,
`OKIM6295`). Each CPU takes its clock in the constructor and calls back into the
driver:

```cpp
cpu_.set_memory_handlers([this](uint16_t a) { return read_byte(a); },
                         [this](uint16_t a, uint8_t v) { write_byte(a, v); });
```

Only write a new chip when the hardware really needs one; put it in the same
directory as its family and give it a `kSampleRate` (44100) `update()` method so
it mixes like the others.

## 4. Frame loop

Split the frame the same way the Pascal loop does, usually one slice per
scanline, and raise the interrupts on the scanlines the original driver uses:

```cpp
void Galaxian::run_frame() {
    const int cycles_per_line = int(kCpuClock / kFramesPerSecond / kScanlines);
    for (int line = 0; line < kScanlines; line++) {
        if (line == 240) {
            cpu_.set_irq(IrqLine::Hold);
            update_video();
        }
        cpu_.run(cycles_per_line);
    }
}
```

Audio is accumulated during the frame and handed to the front end in
`drain_audio`; the usual pattern is a cycle handler that converts CPU cycles
into samples:

```cpp
audio_accumulator_ += int64_t(cycles) * AY8910::kSampleRate;
while (audio_accumulator_ >= kCpuClock) {
    audio_accumulator_ -= kCpuClock;
    audio_.push_back(int16_t(psg_.update()));
}
```

## 5. Video

Decode tiles and sprites once in `init` with `GfxSet`/`GfxLayout`
(`src/video/gfx.h`), which mirrors `convert_gfx`, and render into an ARGB8888
`framebuffer_` of `screen_width() * screen_height()` pixels. Colour PROMs become
a `std::array<uint32_t, N>` palette; `pal_engine` resistor networks are just
weighted sums of the bits.

## 6. Inputs and DIP switches

`set_inputs` receives a `MachineInputs` (two players plus coins) and
`set_dip_switch(bank, value)` one byte per bank. Home computers get the full
keyboard in `MachineInputs::keys` and should override `uses_keyboard()` so the
front end stops stealing `P` for pause.

## 7. Register the driver

```cmake
add_executable(dsp
  ...
  src/drivers/arcade/galaxian.cpp
)
```

```cpp
if (game == "galaxian") return std::make_unique<dsp::Galaxian>();
```

Add the name to `print_usage`, to `guess_game` if the ROM set is recognisable by
its file name, and to the tables in `README.md`.

## 8. Test it

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j
ctest --test-dir build --output-on-failure
./build/dsp --game galaxian --screenshot /tmp/galaxian.bmp --frames 900 --mute galaxian.zip
```

Add unit tests in `tests/tests.cpp` for whatever new CPU or sound chip you had
to write (they run without ROMs), and check the game interactively with
`.agents/skills/testing-arcade-drivers`.
