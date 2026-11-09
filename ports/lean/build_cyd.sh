#!/bin/sh
# Builds the ESP32 CYD firmwares (one game each) into dist/, for the LCD's
# SPI clock at 40 and 80 MHz (80 is faster; 40 for panels that do not take it):
#   dist/<game>_cyd_st7789_spi<MHz>mhz_full.bin         at 0x0 (bootloader + table + app)
#   dist/<game>_cyd_st7789_spi<MHz>mhz_app_0x10000.bin  the app alone, at 0x10000
# Needs ESP-IDF 5.x (. $IDF_PATH/export.sh) and each game's tools/regen.sh run once.
#   ./build_cyd.sh                 the three games
#   ./build_cyd.sh gauntlet        one of them
#   MHZ=80 ./build_cyd.sh indytemp one clock only
set -e
cd "$(dirname "$0")"
mkdir -p dist
for g in ${*:-indytemp roadrunn gauntlet}; do
    for mhz in ${MHZ:-40 80}; do
        LCD_SPI_MHZ=$mhz idf.py -B build/$g-$mhz -DGAME=$g build
        (cd build/$g-$mhz && esptool.py --chip esp32 merge_bin -o ../../dist/${g}_cyd_st7789_spi${mhz}mhz_full.bin \
            --fill-flash-size 4MB @flash_args)
        cp build/$g-$mhz/${g}_cyd.bin dist/${g}_cyd_st7789_spi${mhz}mhz_app_0x10000.bin
    done
done
