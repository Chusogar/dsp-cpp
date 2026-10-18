#!/usr/bin/env bash
# Builds a ttgo-vga32 firmware with the games given here built in, ready to flash:
#
#   ./make_firmware.sh spectrum48 ~/roms/zx48 manic.tzx
#   ./make_firmware.sh galaxian ~/roms/galaxian.zip + bagman ~/roms/bagman.zip
#
# GAME ROM [MEDIA...] per game, "+" between games, "--name LABEL" for the
# menu label, options for build.sh after "--". firmware/ gets the file.
# ../common/firmware.py --list shows every game name. More:
# ./make_firmware.sh --help
exec "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/../common/make_firmware.sh" ttgo-vga32 "$@"
