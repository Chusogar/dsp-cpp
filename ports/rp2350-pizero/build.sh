#!/usr/bin/env bash
# Builds the RP2350-PiZero firmware: build/dsp_pizero.uf2
#
#   ./build.sh                                   default drivers (see CMakeLists.txt)
#   ./build.sh -d "galaxian;bagman"              only these drivers
#   ./build.sh -d blockout --embed-game blockout --embed-rom ~/roms/blockout.zip
#   ./build.sh -d c64 --embed-game c64 --embed-rom ~/roms/c64 --embed-tape game.prg
#   ./build.sh -d "c64;spectrum" --embed-entry "C64=--game c64 $HOME/roms/c64" --embed-entry "..."
#   ./build.sh -b /tmp/build -j 8 -- -DDSP_AUDIO_PIN=19   (extra cmake options after --)
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD="$HERE/build"
JOBS="$(nproc 2>/dev/null || echo 4)"
ARGS=()
usage() { sed -n '2,9p' "$0" | sed 's/^# \{0,1\}//'; exit "${1:-0}"; }
while [ $# -gt 0 ]; do
  case "$1" in
    -d|--drivers) ARGS+=("-DDSP_PORT_DRIVERS=$2"); shift 2 ;;
    -b|--build-dir) BUILD="$2"; shift 2 ;;
    -j) JOBS="$2"; shift 2 ;;
    --embed-game) ARGS+=("-DDSP_EMBED_GAME=$2"); shift 2 ;;
    --embed-rom) ARGS+=("-DDSP_EMBED_ROM=$(realpath "$2")"); shift 2 ;;
    --embed-tape) ARGS+=("-DDSP_EMBED_TAPE=$(realpath "$2")"); shift 2 ;;
    --embed-disk) ARGS+=("-DDSP_EMBED_DISK=$(realpath "$2")"); shift 2 ;;
    --embed-cart) ARGS+=("-DDSP_EMBED_CART=$(realpath "$2")"); shift 2 ;;
    --embed-args) ARGS+=("-DDSP_EMBED_ARGS=$2"); shift 2 ;;
    --embed-entry) ENTRIES="${ENTRIES:+$ENTRIES;}$2"; shift 2 ;;
    -h|--help) usage ;;
    --) shift; ARGS+=("$@"); break ;;
    *) echo "build.sh: unknown option $1" >&2; usage 1 ;;
  esac
done

[ -n "${ENTRIES:-}" ] && ARGS+=("-DDSP_EMBED_ENTRIES=$ENTRIES")
DEPS="${DSP_PICO_DEPS:-$HERE/deps}"
if [ ! -e "$DEPS/PicoDVI" ] || { [ -z "${PICO_SDK_PATH:-}" ] && [ ! -e "$DEPS/pico-sdk" ]; }; then
  "$HERE/setup.sh"
fi
cmake -S "$HERE" -B "$BUILD" -DDSP_PICO_DEPS="$DEPS" ${ARGS[@]+"${ARGS[@]}"}
cmake --build "$BUILD" -j "$JOBS"
echo "build.sh: firmware at $BUILD/dsp_pizero.uf2 (flash it with ./flash.sh)"
