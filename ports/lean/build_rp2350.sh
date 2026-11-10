#!/bin/sh
# Builds the RP2350-PiZero firmwares (one game each) into dist/:
#   dist/<game>_rp2350_pizero.uf2       keyboard / pad on the PIO-USB port
#   dist/<game>_rp2350_pizero_usbc.uf2  (USB=native) on the native USB-C
# (hold BOOT while plugging the USB-C in, copy the .uf2 to the RP2350 drive)
# Needs tools/setup_pico.sh run once (or PICO_DEPS=<dir> with the Pico SDK
# 2.1.1, PicoDVI and Pico-PIO-USB), the Arm toolchain and each game's
# tools/regen.sh run once.
#   ./build_rp2350.sh                 the three games
#   USB=native ./build_rp2350.sh gauntlet
set -e
cd "$(dirname "$0")"
mkdir -p dist
if [ "$USB" = native ]; then B=pico2n; SUF=_usbc; NAT=ON; else B=pico2; SUF=; NAT=OFF; fi
DEPS=${PICO_DEPS:-$(pwd)/deps/pico}
for g in ${*:-indytemp roadrunn gauntlet}; do
    cmake -S main/ports/rp2040-pizero -B build/$B-$g -DGAME=$g -DPIZERO=2350 -DUSB_NATIVE=$NAT -DPICO_DEPS="$DEPS" >/dev/null
    cmake --build build/$B-$g -j"$(nproc 2>/dev/null || echo 4)"
    cp build/$B-$g/${g}_pizero.uf2 dist/${g}_rp2350_pizero$SUF.uf2
done
