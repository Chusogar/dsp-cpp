#!/usr/bin/env bash
# Builds the firmware for a board with the games, ROMs, tapes, disks and
# cartridges given built in. See tools/make_firmware.py --help, e.g.:
#
#   ./make_firmware.sh --board rp2040-pizero --game elevator --rom ~/roms/elevator.zip
#   ./make_firmware.sh --board rp2350-pizero \
#       --game spectrum48 --rom ~/roms/zx48 --tape manic.tzx --name "Manic Miner" \
#       --game c64 --rom ~/roms/c64 --disk game.d64
PY="$(command -v python3 || command -v python)"
exec "$PY" "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/tools/make_firmware.py" "$@"
