#!/usr/bin/env python3
"""Builds a firmware (or a desktop executable) for the board given, with the
games, ROMs, tapes, disks and cartridges given built in, and leaves one file
to flash.

    tools/make_firmware.py --board BOARD [options] \\
        --game NAME --rom ROM [--rom ROM...] [--tape F] [--disk F] [--cart F] \\
                   [--snap F] [--media F] [--name LABEL] [--dip BANK:VALUE] [--arg A] \\
        [--game NAME --rom ROM ...]... [-- BUILD.SH OPTIONS]

Each --game starts a game; the options after it, up to the next --game, are
for that game:
  --rom PATH       zip, folder or single ROM file. Several --rom are merged
                   into one folder (e.g. a BIOS zip and the game's zip).
                   Zips are unpacked: the boards only read ROMs in place
                   (from flash) from a folder.
  --tape/--snap    tape or snapshot (.tap .tzx .prg .t64 .sna .z80 .cdt ...)
  --disk           disk image (.d64 .dsk .adf .st .mdv ...)
  --cart           cartridge (.crt .rom .bin .a26 .gb ...)
  --media          any of the above (the driver tells them by extension)
  --name LABEL     its label in the menu (default: the game name)
  --dip BANK:VAL   DIP switches (e.g. --dip 0:0x6f); repeatable
  --arg ARG        any other argument for the game's command line; repeatable
One game starts by itself; several are listed in a menu.

Options:
  --board BOARD    rp2040-pizero, rp2350-pizero, esp32-cyd, ttgo-vga32 or
                   desktop (an SDL executable that starts the first game)
  -o, --out DIR    where the result goes (default: ports/BOARD/firmware,
                   or build-desktop/firmware)
  -b, --build-dir DIR   build folder (default: ports/BOARD/build)
  -j N             parallel jobs (boards built with CMake, and desktop)
  -p, --port DEV   ESP32 boards: upload to this serial port after building
  --package        also make a zip with the firmware and a README.txt
  --lang es        the package's text in Spanish (LEEME.txt)
  --dry-run        check everything and show what would be built
  --list-boards    the boards and what they produce
  --list-games [TEXT]   the game names (those containing TEXT) and drivers
Everything after "--" goes to the board's build.sh as it is
(e.g. "-- --panel st7789" for the CYD, "-- -DDSP_AUDIO_PIN=5" for a Pico).

Examples:
  tools/make_firmware.py --board rp2040-pizero --game elevator --rom ~/roms/elevator.zip
  tools/make_firmware.py --board rp2040-pizero \\
      --game atetris --rom ~/roms/atetris.zip --name "Atari Tetris" \\
      --game spectrum48 --rom ~/roms/zx48 --tape ~/zx/manic.tzx --name "Manic Miner" \\
      --game c64 --rom ~/roms/c64 --disk ~/c64/game.d64
  tools/make_firmware.py --board esp32-cyd --game a2600 --rom ~/roms/a2600 \\
      --cart ~/a26/pitfall.a26 -- --panel st7789
"""

import glob
import json
import os
import re
import shlex
import shutil
import subprocess
import sys
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, ".."))
COMMON = os.path.join(REPO, "ports", "common")
sys.path.insert(0, COMMON)
import firmware  # noqa: E402  (game name -> driver group)

BOARDS = {
    "rp2040-pizero": {
        "what": "Waveshare RP2040-PiZero (HDMI, USB keyboard): .uf2",
        "output": "dsp_pizero2040.uf2", "ext": ".uf2", "cmake": True,
        "flash": "hold BOOT while plugging in the USB-C marked USB; copy the .uf2 onto the RPI-RP2 drive",
    },
    "rp2350-pizero": {
        "what": "Waveshare RP2350-PiZero (HDMI, PSRAM, USB keyboard): .uf2",
        "output": "dsp_pizero.uf2", "ext": ".uf2", "cmake": True,
        "flash": "hold BOOT while plugging in the USB-C marked USB; copy the .uf2 onto the RP2350 drive",
    },
    "esp32-cyd": {
        "what": "ESP32 'Cheap Yellow Display' (arduino-cli): merged .bin",
        "sketch": "dsp_cyd", "ext": ".bin", "cmake": False,
        "flash": "esptool --chip esp32 --port PORT --baud 921600 write_flash 0x0 FILE",
    },
    "ttgo-vga32": {
        "what": "LilyGO TTGO VGA32 (arduino-cli): merged .bin",
        "sketch": "dsp_vga32", "ext": ".bin", "cmake": False,
        "flash": "esptool --chip esp32 --port PORT --baud 921600 write_flash 0x0 FILE",
    },
    "desktop": {
        "what": "this computer (CMake + SDL2): an executable that starts the first game",
        "ext": "", "cmake": True,
        "flash": "run it; with no arguments it starts the built-in game",
    },
}

MEDIA_OPTIONS = {"--tape": "--tape", "--snap": "--tape", "--disk": "--disk", "--cart": "--cart",
                 "--media": "--cart"}


def die(text):
    sys.exit("make_firmware: " + text)


def usage(code=0):
    print(__doc__.strip("\n"))
    sys.exit(code)


class Game:
    def __init__(self, name):
        self.name = name.lower()
        self.roms = []
        self.media = []  # (option, path)
        self.label = None
        self.args = []
        self.group = None


def parse(argv):
    opts = {"board": None, "out": None, "build": None, "jobs": None, "port": None,
            "package": False, "dry_run": False, "lang": "en", "extra": []}
    games = []
    i = 0

    def value(flag):
        nonlocal i
        if i + 1 >= len(argv):
            die("%s needs a value" % flag)
        i += 1
        return argv[i]

    def current(flag):
        if not games:
            die("%s comes after a --game" % flag)
        return games[-1]

    while i < len(argv):
        a = argv[i]
        if "=" in a and a.startswith("--") and a.split("=", 1)[0] in (
                "--board", "--out", "--build-dir", "--game", "--rom", "--name", "--dip", "--arg",
                "--port", *MEDIA_OPTIONS):
            flag, v = a.split("=", 1)
            argv[i:i + 1] = [flag, v]
            a = flag
        if a == "--":
            opts["extra"] = argv[i + 1:]
            break
        elif a in ("-h", "--help"):
            usage()
        elif a == "--list-boards":
            for b, info in BOARDS.items():
                print("%-15s %s" % (b, info["what"]))
            sys.exit(0)
        elif a == "--list-games":
            text = argv[i + 1].lower() if i + 1 < len(argv) and not argv[i + 1].startswith("-") else ""
            for name, group in sorted(firmware.game_groups().items()):
                if text in name or text in group:
                    print("%-20s %s" % (name, group))
            sys.exit(0)
        elif a == "--board":
            opts["board"] = value(a)
        elif a in ("-o", "--out"):
            opts["out"] = value(a)
        elif a in ("-b", "--build-dir"):
            opts["build"] = value(a)
        elif a == "-j":
            opts["jobs"] = value(a)
        elif a in ("-p", "--port"):
            opts["port"] = value(a)
        elif a == "--lang":
            opts["lang"] = value(a)
            if opts["lang"] not in ("en", "es"):
                die("--lang is en or es")
        elif a == "--package":
            opts["package"] = True
        elif a == "--dry-run":
            opts["dry_run"] = True
        elif a == "--game":
            games.append(Game(value(a)))
        elif a == "--rom":
            current(a).roms.append(value(a))
        elif a in MEDIA_OPTIONS:
            current(a).media.append((MEDIA_OPTIONS[a], value(a)))
        elif a == "--name":
            current(a).label = value(a)
        elif a == "--dip":
            v = value(a)
            if not re.fullmatch(r"(\d+:)?(0x[0-9a-fA-F]+|\d+)", v):
                die("--dip takes BANK:VALUE (e.g. 0:0x6f), got %s" % v)
            current(a).args += ["--dip", v]
        elif a == "--arg":
            current(a).args.append(value(a))
        else:
            die("unknown option %s (--help shows them)" % a)
        i += 1
    return opts, games


def check(opts, games):
    board = opts["board"]
    if not board:
        die("--board is needed (--list-boards shows them)")
    if board not in BOARDS:
        die("unknown board %s; one of: %s" % (board, ", ".join(BOARDS)))
    if not games:
        die("no game: --game NAME --rom ROM (--list-games shows the names)")
    groups = firmware.game_groups()
    labels = set()
    for g in games:
        g.group = groups.get(g.name)
        if g.group is None:
            close = [n for n in groups if g.name in n or n in g.name][:8]
            die("unknown game '%s'%s (--list-games shows them)"
                % (g.name, (" - did you mean: " + ", ".join(close)) if close else ""))
        if not g.roms:
            die("%s has no --rom" % g.name)
        for p in g.roms + [m for _, m in g.media]:
            if not os.path.exists(os.path.expanduser(p)):
                die("%s not found (game %s)" % (p, g.name))
        for _, m in g.media:
            if not os.path.isfile(os.path.expanduser(m)):
                die("%s is not a file (game %s)" % (m, g.name))
        label = g.label or g.name
        if ";" in label or "=" in label:
            die("the label '%s' cannot have ';' or '='" % label)
        if label in labels:
            die("two games are called '%s' in the menu: give one a --name" % label)
        labels.add(label)
        for a in g.args:
            if ";" in a:
                die("the argument '%s' cannot have ';'" % a)


def unzip_into(path, out):
    with zipfile.ZipFile(path) as z:
        for info in z.infolist():
            if info.is_dir():
                continue
            name = os.path.basename(info.filename)
            if not name or name.startswith("."):
                continue
            with z.open(info) as src, open(os.path.join(out, name), "wb") as dst:
                shutil.copyfileobj(src, dst)


def safe(path):
    return " " not in path and ";" not in path


def stage(g, stage_dir):
    """The ROM path and media paths of game g as the build will see them."""
    roms = [os.path.abspath(os.path.expanduser(r)) for r in g.roms]
    rom_dir = os.path.join(stage_dir, "roms", g.name)
    single = roms[0]
    if len(roms) == 1 and os.path.isdir(single) and safe(single):
        rom = single.rstrip("/\\") + "/"
    elif len(roms) == 1 and os.path.isfile(single) and not zipfile.is_zipfile(single) and safe(single):
        rom = single
    else:
        # Zips, several ROM paths or awkward names: one folder in the build.
        if os.path.isdir(rom_dir):
            shutil.rmtree(rom_dir)
        os.makedirs(rom_dir)
        for r in roms:
            if os.path.isdir(r):
                for name in sorted(os.listdir(r)):
                    src = os.path.join(r, name)
                    if os.path.isfile(src) and not name.startswith("."):
                        if zipfile.is_zipfile(src) and name.lower().endswith(".zip"):
                            unzip_into(src, rom_dir)
                        else:
                            shutil.copy2(src, os.path.join(rom_dir, name))
            elif zipfile.is_zipfile(r):
                unzip_into(r, rom_dir)
            else:
                shutil.copy2(r, os.path.join(rom_dir, os.path.basename(r).replace(" ", "_")))
        rom = rom_dir + "/"
    media = []
    for option, m in g.media:
        m = os.path.abspath(os.path.expanduser(m))
        if not safe(m):
            media_dir = os.path.join(stage_dir, "media", g.name)
            os.makedirs(media_dir, exist_ok=True)
            dst = os.path.join(media_dir, os.path.basename(m).replace(" ", "_").replace(";", "_"))
            shutil.copy2(m, dst)
            m = dst
        media.append((option, m))
    if not safe(rom):
        die("the build folder %s has a space or ';' in its path: use -b with another" % stage_dir)
    return rom, media


def entry(g, rom, media):
    parts = ["--game", g.name, rom]
    for option, m in media:
        parts += [option, m]
    parts += g.args
    return "%s=%s" % (g.label or g.name, " ".join(parts))


def size_of(path):
    if os.path.isdir(path):
        return sum(os.path.getsize(os.path.join(r, f)) for r, _, fs in os.walk(path) for f in fs)
    return os.path.getsize(path)


def run(cmd, **kw):
    print("make_firmware: $ " + " ".join(shlex.quote(c) for c in cmd), flush=True)
    r = subprocess.run(cmd, **kw)
    if r.returncode != 0:
        die("the build failed (exit %d)" % r.returncode)


def esp32_merge(board, build, result):
    py = sys.executable
    cli = os.environ.get("ARDUINO_CLI", "arduino-cli")
    flags = shlex.split(os.environ.get("ARDUINO_CLI_FLAGS", ""))
    data = None
    try:
        out = subprocess.run([cli, *flags, "config", "dump", "--format", "json"], capture_output=True,
                             text=True).stdout
        c = json.loads(out)
        data = c.get("config", c)["directories"]["data"]
    except Exception:
        pass
    data = data or os.path.expanduser("~/.arduino15")
    boot = sorted(glob.glob(os.path.join(data, "packages/esp32/hardware/esp32/*/tools/partitions/boot_app0.bin")))
    tool = sorted(glob.glob(os.path.join(data, "packages/esp32/tools/esptool_py/*/esptool.py")))
    if not boot:
        die("boot_app0.bin not found under %s" % data)
    esp = [py, tool[-1]] if tool else [py, "-m", "esptool"]
    sketch = BOARDS[board]["sketch"]
    run(esp + ["--chip", "esp32", "merge_bin", "-o", result, "--flash_mode", "keep", "--flash_freq", "keep",
               "--flash_size", "keep",
               "0x1000", os.path.join(build, sketch + ".ino.bootloader.bin"),
               "0x8000", os.path.join(build, sketch + ".ino.partitions.bin"),
               "0xe000", boot[-1], "0x10000", os.path.join(build, sketch + ".ino.bin")],
        stdout=subprocess.DEVNULL)


def build_board(opts, drivers, entries, build, out, stem):
    board = opts["board"]
    info = BOARDS[board]
    if board == "desktop":
        c_file = os.path.join(build, "embedded_games.c")
        os.makedirs(build, exist_ok=True)
        run([sys.executable, os.path.join(REPO, "tools", "embed_media.py"), "-o", c_file]
            + ["--entry=" + e for e in entries])
        run(["cmake", "-S", REPO, "-B", build, "-DCMAKE_BUILD_TYPE=Release",
             "-DDSP_DRIVERS=" + ";".join(drivers), "-DDSP_PER_DRIVER_TARGETS=OFF",
             "-DDSP_EMBED_C=" + c_file] + opts["extra"])
        run(["cmake", "--build", build, "--target", "dsp", "-j", opts["jobs"] or str(os.cpu_count() or 2)])
        exe = os.path.join(build, "dsp.exe" if os.name == "nt" else "dsp")
        result = os.path.join(out, stem + (".exe" if os.name == "nt" else ""))
        shutil.copy2(exe, result)
        return result
    cmd = ["bash", os.path.join(REPO, "ports", board, "build.sh"), "-b", build, "-d", ";".join(drivers)]
    for e in entries:
        cmd += ["--embed-entry", e]
    if opts["jobs"] and info["cmake"]:
        cmd += ["-j", opts["jobs"]]
    if opts["port"]:
        if info["cmake"]:
            die("--port is for the ESP32 boards; flash a %s by copying the .uf2" % board)
        cmd += ["-p", opts["port"]]
    if opts["extra"]:
        if info["cmake"]:
            cmd += ["--"] + opts["extra"]
        else:
            cmd += opts["extra"]
    run(cmd)
    result = os.path.join(out, stem + info["ext"])
    if info["cmake"]:
        shutil.copy2(os.path.join(build, info["output"]), result)
    else:
        esp32_merge(board, build, result)
    return result


FLASH_ES = {
    "rp2040-pizero": "mantener BOOT al enchufar el USB-C marcado USB; copiar el .uf2 en la unidad RPI-RP2",
    "rp2350-pizero": "mantener BOOT al enchufar el USB-C marcado USB; copiar el .uf2 en la unidad RP2350",
    "esp32-cyd": BOARDS["esp32-cyd"]["flash"],
    "ttgo-vga32": BOARDS["ttgo-vga32"]["flash"],
    "desktop": "ejecutarlo; sin argumentos arranca el juego incluido",
}


def readme(board, games, staged, result, lang="en"):
    es = lang == "es"
    flash = (FLASH_ES[board] if es else BOARDS[board]["flash"]).replace("FILE", os.path.basename(result))
    lines = [("dsp-cpp para %s" if es else "dsp-cpp firmware for %s") % board, BOARDS[board]["what"], "",
             ("Fichero: %s" if es else "File: %s") % os.path.basename(result),
             ("Grabar: %s" if es else "To flash: %s") % flash, "",
             ("Juegos incluidos:" if es else "Built-in games:") if len(games) > 1 else
             ("Juego incluido:" if es else "Built-in game:")]
    for g, (rom, media) in zip(games, staged):
        lines.append("  %-20s --game %s (driver %s)" % (g.label or g.name, g.name, g.group))
        lines.append("      ROM: %s" % " + ".join(os.path.basename(os.path.normpath(r)) for r in g.roms))
        for option, m in media:
            lines.append("      %s %s" % (option[2:], os.path.basename(m)))
        if g.args:
            lines.append("      args: %s" % " ".join(g.args))
    if board != "desktop" and es:
        lines += ["", "En el menu: flechas y Enter. 5 = moneda, 1 = empezar.",
                  "F12 pulsar y soltar = turbo si/no; F12 mantener = volver al menu.",
                  "Si sale \"not enough memory\", el texto entre parentesis dice lo pedido y lo libre."]
    elif board != "desktop":
        lines += ["", "In the menu: arrows and Enter. 5 = coin, 1 = start.",
                  "F12 tap = turbo on/off; F12 held = back to the menu.",
                  "\"not enough memory\": the text in brackets says what was asked for and what was free."]
    return "\n".join(lines) + "\n"


def main(argv):
    if not argv:
        usage(1)
    opts, games = parse(list(argv))
    check(opts, games)
    board = opts["board"]
    if board == "desktop":
        build = os.path.abspath(opts["build"] or os.path.join(REPO, "build-desktop"))
        out = os.path.abspath(opts["out"] or os.path.join(build, "firmware"))
    else:
        build = os.path.abspath(opts["build"] or os.path.join(REPO, "ports", board, "build"))
        out = os.path.abspath(opts["out"] or os.path.join(REPO, "ports", board, "firmware"))
    drivers = []
    for g in games:
        if g.group not in drivers:
            drivers.append(g.group)
    stem = "dsp-%s-%s" % (board, "-".join(g.name for g in games))

    if opts["dry_run"]:
        print("make_firmware: board %s, drivers %s" % (board, ";".join(drivers)))
        for g in games:
            print("  %s: --game %s (%s), ROM %s%s%s" % (
                g.label or g.name, g.name, g.group, " + ".join(g.roms),
                "".join(" %s %s" % (o, m) for o, m in g.media), (" " + " ".join(g.args)) if g.args else ""))
        if opts["extra"]:
            print("make_firmware: build options: %s" % " ".join(opts["extra"]))
        print("make_firmware: would build in %s and write %s/%s%s" % (build, out, stem, BOARDS[board]["ext"]))
        return

    os.makedirs(build, exist_ok=True)
    os.makedirs(out, exist_ok=True)
    staged = [stage(g, build) for g in games]
    entries = [entry(g, rom, media) for g, (rom, media) in zip(games, staged)]
    total = sum(size_of(rom) + sum(size_of(m) for _, m in media) for rom, media in staged)
    print("make_firmware: board %s; drivers %s; %d game(s), %d KB of ROMs and media"
          % (board, ";".join(drivers), len(games), (total + 1023) // 1024))
    for e in entries:
        print("make_firmware:   " + e)

    result = build_board(opts, drivers, entries, build, out, stem)
    print("make_firmware: %s" % result)
    print("make_firmware: to flash: %s" % BOARDS[board]["flash"].replace("FILE", os.path.basename(result)))
    if opts["package"]:
        package = os.path.join(out, stem + ".zip")
        with zipfile.ZipFile(package, "w", zipfile.ZIP_DEFLATED) as z:
            z.write(result, os.path.basename(result))
            z.writestr("LEEME.txt" if opts["lang"] == "es" else "README.txt",
                       readme(board, games, staged, result, opts["lang"]))
        print("make_firmware: package %s" % package)


if __name__ == "__main__":
    main(sys.argv[1:])
