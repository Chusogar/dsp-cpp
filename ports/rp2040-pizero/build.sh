#!/usr/bin/env bash
# Builds the RP2040-PiZero firmware: build/dsp_pizero2040.uf2
#
#   ./build.sh -d taitosj --embed-entry "Elevator Action=--game elevator $HOME/roms/elevator/"
#   ./build.sh -b /tmp/build -j 8 -- -DDSP_AUDIO_PIN=5   (extra cmake options after --)
# The dependencies are the RP2350-PiZero port's (../rp2350-pizero/setup.sh).
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD="$HERE/build"
JOBS="$(nproc 2>/dev/null || echo 4)"
ARGS=()
usage() { sed -n '2,6p' "$0" | sed 's/^# \{0,1\}//'; exit "${1:-0}"; }
while [ $# -gt 0 ]; do
  case "$1" in
    -d|--drivers) ARGS+=("-DDSP_PORT_DRIVERS=$2"); shift 2 ;;
    -b|--build-dir) BUILD="$2"; shift 2 ;;
    -j) JOBS="$2"; shift 2 ;;
    --embed-entry) ENTRIES="${ENTRIES:+$ENTRIES;}$2"; shift 2 ;;
    -h|--help) usage ;;
    --) shift; ARGS+=("$@"); break ;;
    *) echo "build.sh: unknown option $1" >&2; usage 1 ;;
  esac
done
[ -n "${ENTRIES:-}" ] && ARGS+=("-DDSP_EMBED_ENTRIES=$ENTRIES")
DEPS="${DSP_PICO_DEPS:-$HERE/../rp2350-pizero/deps}"
if [ ! -e "$DEPS/PicoDVI" ] || { [ -z "${PICO_SDK_PATH:-}" ] && [ ! -e "$DEPS/pico-sdk" ]; }; then
  DSP_PICO_DEPS="$DEPS" "$HERE/../rp2350-pizero/setup.sh"
fi
cmake -S "$HERE" -B "$BUILD" -DDSP_PICO_DEPS="$DEPS" ${ARGS[@]+"${ARGS[@]}"}
cmake --build "$BUILD" -j "$JOBS"
echo "build.sh: firmware at $BUILD/dsp_pizero2040.uf2 (drop it on the RPI-RP2 drive)"
