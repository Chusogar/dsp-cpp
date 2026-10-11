#!/usr/bin/env bash
# Downloads what the RP2350-PiZero firmware is built with into deps/ (or
# $DSP_PICO_DEPS): the Pico SDK with TinyUSB, PicoDVI, Pico-PIO-USB, zlib and
# FatFs, each at a fixed version. Run once; build.sh runs it when needed.
#
# Needs git, cmake (3.13+), python3 and the Arm GNU toolchain
# (arm-none-eabi-gcc; Debian/Ubuntu: gcc-arm-none-eabi libnewlib-arm-none-eabi).
# An existing SDK can be used with PICO_SDK_PATH=/path/to/pico-sdk.
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DEPS="${DSP_PICO_DEPS:-$HERE/deps}"
mkdir -p "$DEPS"

# name  url  revision (tag or commit)
fetch() {
  local name=$1 url=$2 rev=$3 dir="$DEPS/$1"
  if [ -e "$dir/.dsp-rev" ] && [ "$(cat "$dir/.dsp-rev")" = "$rev" ]; then
    echo "setup: $name $rev already there"
    return
  fi
  echo "setup: fetching $name $rev"
  rm -rf "$dir"
  git init -q "$dir"
  git -C "$dir" remote add origin "$url"
  git -C "$dir" fetch -q --depth 1 origin "$rev"
  git -C "$dir" checkout -q FETCH_HEAD
  echo "$rev" > "$dir/.dsp-rev"
}

if [ -z "${PICO_SDK_PATH:-}" ]; then
  fetch pico-sdk https://github.com/raspberrypi/pico-sdk 2.1.1
  if [ ! -e "$DEPS/pico-sdk/lib/tinyusb/src/tusb.h" ]; then
    echo "setup: fetching the TinyUSB submodule of the SDK"
    git -C "$DEPS/pico-sdk" submodule update -q --init --depth 1 lib/tinyusb
  fi
fi
fetch PicoDVI https://github.com/Wren6991/PicoDVI dccd738bfa9af75badcb32acde3e41bd6a3fa30a
fetch Pico-PIO-USB https://github.com/sekigon-gonnoc/Pico-PIO-USB 0.7.2
fetch zlib https://github.com/madler/zlib v1.3.1
fetch fatfs https://github.com/abbrev/fatfs R0.15

for tool in arm-none-eabi-gcc cmake python3; do
  command -v "$tool" >/dev/null || echo "setup: warning: $tool not found (see the README)"
done
echo "setup: done ($DEPS)"
