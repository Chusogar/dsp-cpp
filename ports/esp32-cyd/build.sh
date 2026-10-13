#!/usr/bin/env bash
# Builds (and optionally uploads) the ESP32 CYD firmware with arduino-cli.
#
#   ./build.sh                                   default drivers (a2600 atari_lynx gameboy)
#   ./build.sh -d "a2600;gameboy"                 only these drivers
#   ./build.sh --panel st7789                    CYD2USB (ST7789 display)
#   ./build.sh -d a2600 --embed-game a2600 --embed-rom ~/roms/game.a26
#   ./build.sh --psram                           board with PSRAM (e.g. WROVER module fitted)
#   ./build.sh -d "c64;spectrum" --embed-entry "C64=--game c64 $HOME/roms/c64" \
#              --embed-entry "Spectrum=--game spectrum48 $HOME/roms/zx48"   several built-in games
#   ./build.sh -p /dev/ttyUSB0                   build and upload
# Panel, touch and pin options are passed to prepare.py (see its --help).
#
# Output: build/dsp_cyd.ino.bin (+ bootloader and partitions, flashed by -p).
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CLI="${ARDUINO_CLI:-arduino-cli}"
read -r -a CLI_FLAGS <<< "${ARDUINO_CLI_FLAGS:-}"
PSRAM=disabled
BUILD="$HERE/build"
PORT=""
PREP=()
usage() { sed -n '2,13p' "$0" | sed 's/^# \{0,1\}//'; exit "${1:-0}"; }
while [ $# -gt 0 ]; do
  case "$1" in
    -d|--drivers) PREP+=(--drivers "$2"); shift 2 ;;
    -p|--port) PORT="$2"; shift 2 ;;
    -b|--build-dir) BUILD="$2"; shift 2 ;;
    --psram) PSRAM=enabled; shift ;;
    --embed-game|--embed-rom|--embed-tape|--embed-disk|--embed-cart|--embed-arg|--embed-entry|--panel|--lcd-mhz|\
    --touch-cal|--buttons|--sd-pins)
      PREP+=("$1" "$2"); shift 2 ;;
    --invert|--no-invert|--swap-colours|--flip|--touch-swap|--touch-flip-x|--touch-flip-y|--touch-debug)
      PREP+=("$1"); shift ;;
    -h|--help) usage ;;
    *) echo "build.sh: unknown option $1" >&2; usage 1 ;;
  esac
done
FQBN="esp32:esp32:esp32:PSRAM=$PSRAM,PartitionScheme=huge_app,CPUFreq=240,FlashMode=qio,FlashFreq=80"

[ -e "$HERE/deps/zlib/inflate.c" ] || "$HERE/setup.sh"
python3 "$HERE/prepare.py" ${PREP[@]+"${PREP[@]}"}
INCLUDES="$(python3 "$HERE/prepare.py" --print-flags)"
# C++17 (the core defaults to gnu++11), dsp's POSIX file layer, -O2.
CXXFLAGS="-std=gnu++17 -O2 -DDSP_FS_POSIX=1 -DDSP_FS_TEMP_DIR=\"/sd/tmp\" -Wno-unused-variable $INCLUDES"
CFLAGS="-O2 -DDSP_FS_POSIX=1 $INCLUDES"
EXTRA=()
# Where arduino-cli's own ctags is missing (offline installs): CTAGS_DIR=/usr/bin
[ -n "${CTAGS_DIR:-}" ] && EXTRA+=(--build-property "runtime.tools.ctags.path=$CTAGS_DIR")
"$CLI" ${CLI_FLAGS[@]+"${CLI_FLAGS[@]}"} compile --fqbn "$FQBN" \
  --build-path "$BUILD" --output-dir "$BUILD" \
  --build-property "compiler.cpp.extra_flags=$CXXFLAGS" \
  --build-property "compiler.c.extra_flags=$CFLAGS" \
  ${EXTRA[@]+"${EXTRA[@]}"} --warnings none "$HERE/dsp_cyd"
if [ -n "$PORT" ]; then
  "$CLI" ${CLI_FLAGS[@]+"${CLI_FLAGS[@]}"} upload --fqbn "$FQBN" -p "$PORT" --input-dir "$BUILD" "$HERE/dsp_cyd"
fi
echo "build.sh: firmware in $BUILD"
