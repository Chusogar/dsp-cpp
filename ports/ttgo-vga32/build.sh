#!/usr/bin/env bash
# Builds (and optionally uploads) the TTGO VGA32 firmware with arduino-cli.
#
#   ./build.sh                                     default drivers (prepare.py)
#   ./build.sh -d "galaxian;bagman"                only these drivers
#   ./build.sh -d blockout --embed-game blockout --embed-rom ~/roms/blockout.zip
#   ./build.sh -d spectrum --embed-game spectrum48 --embed-rom ~/roms/zx48 --embed-tape manic.tzx
#   ./build.sh -p /dev/ttyUSB0                     build and upload
#
# Output: build/dsp_vga32.ino.bin (+ bootloader and partitions, flashed by -p).
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CLI="${ARDUINO_CLI:-arduino-cli}"
read -r -a CLI_FLAGS <<< "${ARDUINO_CLI_FLAGS:-}"
FQBN="esp32:esp32:esp32:PSRAM=enabled,PartitionScheme=huge_app,CPUFreq=240,FlashMode=qio,FlashFreq=80"
BUILD="$HERE/build"
PORT=""
PREP=()
usage() { sed -n '2,10p' "$0" | sed 's/^# \{0,1\}//'; exit "${1:-0}"; }
while [ $# -gt 0 ]; do
  case "$1" in
    -d|--drivers) PREP+=(--drivers "$2"); shift 2 ;;
    -p|--port) PORT="$2"; shift 2 ;;
    -b|--build-dir) BUILD="$2"; shift 2 ;;
    --embed-game|--embed-rom|--embed-tape|--embed-disk|--embed-cart|--embed-arg|--sd-pins)
      PREP+=("$1" "$2"); shift 2 ;;
    -h|--help) usage ;;
    *) echo "build.sh: unknown option $1" >&2; usage 1 ;;
  esac
done

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
  ${EXTRA[@]+"${EXTRA[@]}"} --warnings none "$HERE/dsp_vga32"
if [ -n "$PORT" ]; then
  "$CLI" ${CLI_FLAGS[@]+"${CLI_FLAGS[@]}"} upload --fqbn "$FQBN" -p "$PORT" --input-dir "$BUILD" "$HERE/dsp_vga32"
fi
echo "build.sh: firmware in $BUILD"
