#!/usr/bin/env bash
# Builds a rp2040-pizero firmware with the games given here built in, ready to flash:
#
#   ./make_firmware.sh elevator ~/roms/elevator.zip
#   ./make_firmware.sh atetris ~/roms/atetris.zip --name "Atari Tetris" + spectrum48 ~/roms/zx48 manic.tzx
#
# GAME ROM [MEDIA...] per game, "+" between games, "--name LABEL" for the
# menu label, options for build.sh after "--". firmware/ gets the file.
# ../common/firmware.py --list shows every game name. More:
# ./make_firmware.sh --help
exec "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/../common/make_firmware.sh" rp2040-pizero "$@"
