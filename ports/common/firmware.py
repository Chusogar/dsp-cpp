#!/usr/bin/env python3
"""Turns "GAME ROM [MEDIA...]" into what a board's build.sh needs.

Used by each board's make_firmware.sh:

    firmware.py --stage DIR GAME ROM [MEDIA...] [+ GAME ROM [MEDIA...]]...

prints, one per line:
    the driver groups to build (';' separated, for build.sh -d)
    one built-in game entry per game ("LABEL=--game GAME ROM [--cart MEDIA]...",
    for build.sh --embed-entry)

A zip ROM set is unpacked into DIR/GAME/: the boards read ROMs in place from
their flash only from a folder (a zip would be loaded whole into RAM).
Directories and single ROM files are used as they are. Within a group,
"--name LABEL" sets the menu label (default: the game name).

    firmware.py --list               every game name and its driver group
"""
import json
import os
import re
import sys
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, "..", ".."))
REGISTRY = os.path.join(REPO, "src", "frontend", "machine_registry.cpp")
SOURCES = os.path.join(HERE, "driver_sources.json")


def game_groups():
    """Game name -> driver group, from the #if DSP_DRIVER_X blocks of the registry."""
    with open(SOURCES) as f:
        groups = set(json.load(f)["drivers"])
    games = {}
    stack = []
    with open(REGISTRY, encoding="utf-8", errors="replace") as f:
        for line in f:
            s = line.strip()
            if s.startswith("#if"):
                stack.append([g.lower() for g in re.findall(r"DSP_DRIVER_([A-Z0-9_]+)", s)])
            elif s.startswith("#endif"):
                if stack:
                    stack.pop()
            elif stack and stack[-1]:
                for name in re.findall(r'q\.is\("([^"]+)"\)', s):
                    for g in stack[-1]:
                        if g in groups:
                            games.setdefault(name.lower(), g)
                            break
    return games


def stage_rom(game, rom, stage_dir):
    rom = os.path.abspath(os.path.expanduser(rom))
    if not os.path.exists(rom):
        sys.exit("firmware.py: %s not found" % rom)
    if os.path.isfile(rom) and zipfile.is_zipfile(rom):
        out = os.path.join(stage_dir, game)
        os.makedirs(out, exist_ok=True)
        with zipfile.ZipFile(rom) as z:
            for info in z.infolist():
                if info.is_dir():
                    continue
                name = os.path.basename(info.filename)
                if not name or name.startswith("."):
                    continue
                with z.open(info) as src, open(os.path.join(out, name), "wb") as dst:
                    dst.write(src.read())
        return out + "/"
    if os.path.isdir(rom):
        return rom.rstrip("/\\") + "/"
    return rom


def main(argv):
    if argv[:1] == ["--list"]:
        for name, group in sorted(game_groups().items()):
            print("%-20s %s" % (name, group))
        return
    stage_dir = None
    if argv[:1] == ["--stage"] and len(argv) > 1:
        stage_dir, argv = argv[1], argv[2:]
    if not argv or stage_dir is None:
        sys.exit(__doc__)
    groups_of = game_groups()
    games = []
    current = []
    for a in argv + ["+"]:
        if a == "+":
            if current:
                games.append(current)
            current = []
        else:
            current.append(a)
    drivers, entries = [], []
    for g in games:
        label = None
        if "--name" in g:
            i = g.index("--name")
            if i + 1 >= len(g):
                sys.exit("firmware.py: --name needs a label")
            label = g[i + 1]
            g = g[:i] + g[i + 2:]
        if len(g) < 2:
            sys.exit("firmware.py: each game needs GAME ROM [MEDIA...], got: %s" % " ".join(g))
        game, rom, media = g[0].lower(), g[1], g[2:]
        group = groups_of.get(game)
        if group is None:
            close = [n for n in groups_of if game in n or n in game][:8]
            sys.exit("firmware.py: unknown game '%s'%s (firmware.py --list shows them all)"
                     % (game, (" - did you mean: " + ", ".join(close)) if close else ""))
        if group not in drivers:
            drivers.append(group)
        paths = [stage_rom(game, rom, stage_dir)]
        for m in media:
            m = os.path.abspath(os.path.expanduser(m))
            if not os.path.isfile(m):
                sys.exit("firmware.py: %s not found" % m)
            paths.append(m)
        for p in paths:
            if " " in p or ";" in p:
                sys.exit("firmware.py: the path '%s' has a space or ';' - copy it somewhere without" % p)
        line = "--game %s %s" % (game, paths[0]) + "".join(" --cart " + m for m in paths[1:])
        entries.append("%s=%s" % (label or game, line))
    print(";".join(drivers))
    for e in entries:
        print(e)


if __name__ == "__main__":
    main(sys.argv[1:])
