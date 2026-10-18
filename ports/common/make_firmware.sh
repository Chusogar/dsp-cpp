#!/usr/bin/env bash
# Shared body of every board's make_firmware.sh: builds a firmware with the
# games given on the command line built in, and leaves one file to flash.
#
#   make_firmware.sh BOARD [-o OUT_DIR] [-b BUILD_DIR] GAME ROM [MEDIA...] \
#                    [+ GAME ROM [MEDIA...]]... [-- BUILD.SH OPTIONS]
#
# GAME is the --game name (tools/make_firmware.py --list-games shows them);
# the driver it needs is worked out from it. ROM is a zip, a folder or a
# single ROM file. MEDIA (tapes, disks, cartridges) are optional.
# "--name LABEL" after a game sets its menu label. One game starts by
# itself; several are listed in a menu. Everything after "--" goes to the
# board's build.sh as it is (e.g. "-- --panel st7789" for the CYD).
# A short form of tools/make_firmware.py, which takes named options
# (--rom, --tape, --disk, --cart, --dip, --package...): see its --help.
set -euo pipefail
COMMON="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$COMMON/../.." && pwd)"
usage() { sed -n '4,15p' "$COMMON/make_firmware.sh" | sed 's/^# \{0,1\}//'; exit "${1:-0}"; }
case "${1:-}" in ""|-h|--help) usage ;; esac
BOARD="$1"; shift
ARGS=(--board "$BOARD")
while [ $# -gt 0 ]; do
  case "$1" in
    -o|--out|-b|--build-dir|-j|-p|--port) ARGS+=("$1" "$2"); shift 2 ;;
    --package|--dry-run) ARGS+=("$1"); shift ;;
    --lang) ARGS+=("$1" "$2"); shift 2 ;;
    -h|--help) usage ;;
    *) break ;;
  esac
done
[ $# -ge 2 ] || usage 1
pos=0  # 0: next word is a game, 1: its ROM, 2: its media
while [ $# -gt 0 ]; do
  case "$1" in
    --) ARGS+=("$@"); break ;;
    +) pos=0; shift ;;
    --name) ARGS+=(--name "$2"); shift 2 ;;
    *)
      case $pos in
        0) ARGS+=(--game "$1"); pos=1 ;;
        1) ARGS+=(--rom "$1"); pos=2 ;;
        *) ARGS+=(--media "$1") ;;
      esac
      shift ;;
  esac
done
PY="$(command -v python3 || command -v python)"
exec "$PY" "$REPO/tools/make_firmware.py" "${ARGS[@]}"
