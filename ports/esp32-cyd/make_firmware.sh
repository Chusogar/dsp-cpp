#!/usr/bin/env bash
# Builds a esp32-cyd firmware with the games given here built in, ready to flash:
#
#   ./make_firmware.sh elevator ~/roms/elevator.zip -- --panel st7789
#   ./make_firmware.sh a2600 ~/roms/a2600 pitfall.a26 + gameboy ~/roms/gb tetris.gb
#
# GAME ROM [MEDIA...] per game, "+" between games, "--name LABEL" for the
# menu label, options for build.sh after "--". firmware/ gets the file.
# ../common/firmware.py --list shows every game name. More:
# ./make_firmware.sh --help
exec "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/../common/make_firmware.sh" esp32-cyd "$@"
