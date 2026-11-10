# dsp-cpp lean firmware for the ESP32 CYD: one game per firmware, with the
# memory-lean cores of cMESYS (see README.md).
#   tools/regen.sh indytemp ~/roms/indytemp.zip ~/roms/atarisy1.zip   (once)
#   idf.py -DGAME=indytemp build          (or ./build_cyd.sh indytemp)
# GAME: indytemp | roadrunn | gauntlet
cmake_minimum_required(VERSION 3.16)
if(NOT DEFINED GAME)
    if(DEFINED ENV{GAME})
        set(GAME $ENV{GAME})
    else()
        set(GAME indytemp)
    endif()
endif()
set(GAME ${GAME} CACHE STRING "Game to build: indytemp, roadrunn or gauntlet")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
idf_build_set_property(GAME ${GAME})          # read by main/CMakeLists.txt
project(${GAME}_cyd)
