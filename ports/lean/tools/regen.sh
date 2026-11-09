#!/bin/sh
# Writes a game's generated sources: its ROMs as C arrays in the firmware's
# flash (main/drivers/arcades/<game>/rom_data.c, never committed) and
# Musashi's opcode tables for its CPU (m68k_tables.c).
#   tools/regen.sh indytemp | roadrunn | gauntlet [romset.zip ...]
# ROMs: roms/<game>/ (and roms/atarisy1/ for Indy and Road Runner, the
# System 1 BIOS), unzipped MAME sets; or the zips given here, unzipped there
# first (indytemp.zip + atarisy1.zip, roadrunn.zip + atarisy1.zip, gauntlet.zip).
set -e
cd "$(dirname "$0")/.."
G=$1
[ -n "$G" ] && shift
for z in "$@"; do
    b=$(basename "$z" .zip)
    mkdir -p roms/$b
    unzip -o -q -j "$z" -d roms/$b
done
M=main
D=$M/drivers/arcades/$G
T=${TMPDIR:-/tmp}/dsp_lean_regen
mkdir -p $T
AS1=$M/drivers/arcades/atarisy1
CFLAGS="-O1 -w -I$D -I$AS1 -I$M/cpu -I$M/cpu/m68k -I$M/cpu/m6502 -I$M/sound -I$M/machine -I$M/video"
M68K="$M/cpu/m68k/m68kcpu.c $M/cpu/m68k/m68kops.c $M/cpu/m68k/m68kopac.c $M/cpu/m68k/m68kopdm.c $M/cpu/m68k/m68kopnz.c"
case $G in
indytemp|roadrunn)
    [ -d roms/$G ] && [ -d roms/atarisy1 ] || { echo "regen: roms/$G and roms/atarisy1 needed"; exit 1; }
    cc $CFLAGS -DAS1_HOOK_PC -o $T/gen_roms tools/gen_roms_atarisy1.c $AS1/atarisy1_roms.c $AS1/atarisy1_core.c \
       $M/sound/ym2151.c $M/sound/pokey.c $M/sound/tms5220.c $M/machine/via6522.c $M/machine/slapstic.c \
       $M/video/atari_mo.c $M68K -lm
    $T/gen_roms $G roms/$G roms/atarisy1 $D/rom_data.c
    CYC=1 ;;
gauntlet)
    [ -d roms/gauntlet ] || { echo "regen: roms/gauntlet needed"; exit 1; }
    python3 tools/gen_roms_gauntlet.py roms/gauntlet $D/rom_data.c
    CYC=1 ;;
*)  echo "usage: $0 indytemp|roadrunn|gauntlet [romset.zip ...]"; exit 1 ;;
esac
# Musashi opcode tables in flash, for this game's CPU type
cc -O1 -w -I$D -I$M/cpu/m68k -DCYC_CPU=$CYC -o $T/gen_m68k tools/gen_m68k_tables.c $M/cpu/m68k/m68kcpu.c \
   $M/cpu/m68k/m68kopac.c $M/cpu/m68k/m68kopdm.c $M/cpu/m68k/m68kopnz.c tools/m68k_stub.c
$T/gen_m68k $M/cpu/m68k/m68kops.c $D/m68k_tables.c
echo "regen: $D/rom_data.c and m68k_tables.c written"
