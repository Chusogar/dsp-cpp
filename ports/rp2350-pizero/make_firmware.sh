#!/usr/bin/env bash
# Builds a rp2350-pizero firmware with the games given here built in, ready to flash:
#
#   ./make_firmware.sh c64 ~/roms/c64 game.prg
#   ./make_firmware.sh blockout ~/roms/blockout.zip + spectrum48 ~/roms/zx48
#
# GAME ROM [MEDIA...] per game, "+" between games, "--name LABEL" for the
# menu label, options for build.sh after "--". firmware/ gets the file.
# ../common/firmware.py --list shows every game name. More:
# ./make_firmware.sh --help
exec "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/../common/make_firmware.sh" rp2350-pizero "$@"
