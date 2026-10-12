#!/usr/bin/env bash
# Installs what the ESP32 CYD firmware is built with:
#   - arduino-cli's ESP32 core 2.0.17 (build.sh; no extra libraries needed),
#   - zlib 1.3.1 into deps/zlib (both build.sh and PlatformIO use it).
# PlatformIO users only need the zlib part: ./setup.sh --zlib-only
#
# arduino-cli: https://arduino.github.io/arduino-cli/ (or set ARDUINO_CLI);
# extra arduino-cli options (e.g. --config-file) can go in ARDUINO_CLI_FLAGS.
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CLI="${ARDUINO_CLI:-arduino-cli}"
read -r -a CLI_FLAGS <<< "${ARDUINO_CLI_FLAGS:-}"
ESP32_URL=https://espressif.github.io/arduino-esp32/package_esp32_index.json

mkdir -p "$HERE/deps"
if [ ! -e "$HERE/deps/zlib/.dsp-rev" ] || [ "$(cat "$HERE/deps/zlib/.dsp-rev")" != v1.3.1 ]; then
  echo "setup: fetching zlib v1.3.1"
  rm -rf "$HERE/deps/zlib"
  git -c advice.detachedHead=false clone -q --depth 1 --branch v1.3.1 https://github.com/madler/zlib "$HERE/deps/zlib"
  echo v1.3.1 > "$HERE/deps/zlib/.dsp-rev"
fi
[ "${1:-}" = "--zlib-only" ] && { echo "setup: done"; exit 0; }

command -v "$CLI" >/dev/null || { echo "setup: $CLI not found (install arduino-cli or set ARDUINO_CLI)" >&2; exit 1; }
if ! "$CLI" ${CLI_FLAGS[@]+"${CLI_FLAGS[@]}"} core list 2>/dev/null | grep -q "^esp32:esp32 *2.0.17"; then
  "$CLI" ${CLI_FLAGS[@]+"${CLI_FLAGS[@]}"} core update-index --additional-urls "$ESP32_URL"
  "$CLI" ${CLI_FLAGS[@]+"${CLI_FLAGS[@]}"} core install esp32:esp32@2.0.17 --additional-urls "$ESP32_URL"
fi
python3 -c "import serial" 2>/dev/null || echo "setup: note: esptool needs pyserial to upload (pip install pyserial)"
echo "setup: done"
