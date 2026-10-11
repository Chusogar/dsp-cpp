#!/usr/bin/env bash
# Copies the firmware to an RP2350-PiZero in BOOTSEL mode (hold BOOT while
# plugging the native USB port in): with picotool when it is installed,
# otherwise to the RP2350 USB drive.
#   ./flash.sh [firmware.uf2]
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
UF2="${1:-$HERE/build/dsp_pizero.uf2}"
[ -f "$UF2" ] || { echo "flash.sh: $UF2 not found (run ./build.sh)" >&2; exit 1; }
if command -v picotool >/dev/null; then
  exec picotool load -v -x "$UF2"
fi
for dir in /media/*/RP2350 /run/media/*/RP2350 /Volumes/RP2350 /media/RP2350; do
  if [ -d "$dir" ]; then
    cp "$UF2" "$dir/" && sync
    echo "flash.sh: copied to $dir"
    exit 0
  fi
done
echo "flash.sh: no RP2350 drive found; put the board in BOOTSEL mode or install picotool" >&2
exit 1
