# dsp-cpp firmware for the Waveshare RP2040-PiZero (see README.md):
#   ./build.sh -d taitosj --embed-entry "Elevator Action=--game elevator /path/to/elevator/"
# Shares the sound, SD card, file and USB code of ../rp2350-pizero.
cmake_minimum_required(VERSION 3.13)

get_filename_component(DSP_PICO_DIR "${CMAKE_CURRENT_LIST_DIR}" ABSOLUTE)
get_filename_component(DSP_PICO2_DIR "${CMAKE_CURRENT_LIST_DIR}/../rp2350-pizero" ABSOLUTE)
set(DSP_PICO_DEPS "${DSP_PICO2_DIR}/deps" CACHE PATH "Folder with the dependencies (../rp2350-pizero/setup.sh)")
if(NOT PICO_SDK_PATH AND NOT DEFINED ENV{PICO_SDK_PATH})
  set(PICO_SDK_PATH "${DSP_PICO_DEPS}/pico-sdk")
endif()
set(PICO_PLATFORM rp2040 CACHE STRING "Pico platform")
set(PICO_BOARD waveshare_rp2040_pizero CACHE STRING "Board")
# "not enough memory" instead of a panic when a driver does not fit.
set(PICO_CXX_ENABLE_EXCEPTIONS 1)

include(pico_sdk_import.cmake)
project(dsp_rp2040_pizero C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
pico_sdk_init()

# What goes into the firmware -----------------------------------------------
set(DSP_PORT_DRIVERS "taitosj"
    CACHE STRING "Drivers built in (only low-memory ones fit: taitosj)")
set(DSP_EMBED_GAME "" CACHE STRING "Embedded media: --game name to start")
set(DSP_EMBED_ROM "" CACHE PATH "Embedded media: ROM set (directory: read in place)")
set(DSP_EMBED_TAPE "" CACHE STRING "Embedded media: tapes/snapshots (';' separated)")
set(DSP_EMBED_DISK "" CACHE STRING "Embedded media: disk images (';' separated)")
set(DSP_EMBED_CART "" CACHE STRING "Embedded media: cartridges (';' separated)")
set(DSP_EMBED_ARGS "" CACHE STRING "Embedded media: extra arguments (';' separated)")
set(DSP_EMBED_C "" CACHE FILEPATH "Embedded media: C file written by tools/embed_media.py")
set(DSP_EMBED_ENTRIES "" CACHE STRING "Several built-in games: LABEL=COMMAND LINE, ';' separated")
set(DSP_AUDIO_PIN 4 CACHE STRING "GPIO of the PWM sound output")
option(DSP_NO_SOUND "No sound: drivers skip making it (faster), no PWM audio" OFF)
# Video: OFF = 320x240 doubled, the driver's 256 colours (Elevator Action);
# ON = 640x480 at full resolution in 8 colours, rows read from the machine
# (the Sinclair QL's 512x256).
option(DSP_VIDEO_RGB1 "8-colour full-resolution video (QL)" OFF)

include(${DSP_PICO_DIR}/../common/dsp_port.cmake)

foreach(dep PicoDVI zlib fatfs)
  if(NOT EXISTS "${DSP_PICO_DEPS}/${dep}")
    message(FATAL_ERROR "${DSP_PICO_DEPS}/${dep} is missing: run ../rp2350-pizero/setup.sh first")
  endif()
endforeach()

add_subdirectory(${DSP_PICO_DEPS}/PicoDVI/software/libdvi ${CMAKE_BINARY_DIR}/libdvi)

set(fatfs_dir ${CMAKE_BINARY_DIR}/fatfs)
foreach(f ff.c ff.h diskio.h ffsystem.c ffunicode.c)
  configure_file(${DSP_PICO_DEPS}/fatfs/source/${f} ${fatfs_dir}/${f} COPYONLY)
endforeach()
configure_file(${DSP_PICO2_DIR}/src/ffconf.h ${fatfs_dir}/ffconf.h COPYONLY)

if(DSP_VIDEO_RGB1)
  set(dsp_video_src src/video_rgb1.c)
  set(dsp_vertical_repeat 1)
  # Every line encoded on its own: spare encoded lines absorb the moments
  # core 1 waits for the bus (each costs 3.75 KB).
  set(dsp_tmds_buffers 6)
else()
  set(dsp_video_src src/video_dvi.c)
  set(dsp_vertical_repeat 2)
  set(dsp_tmds_buffers 3)
endif()
add_executable(dsp_pizero2040
  src/main.cpp
  ${dsp_video_src}
  ${DSP_PICO2_DIR}/src/posix_io.cpp
  ${DSP_PICO2_DIR}/src/usb_input.cpp
  ${DSP_PICO2_DIR}/src/sd_spi.c
  ${DSP_PICO2_DIR}/src/audio_pwm.c
  ${fatfs_dir}/ff.c
  ${fatfs_dir}/ffsystem.c
  ${fatfs_dir}/ffunicode.c
)
dsp_port_embedded_media(embedded_c)
dsp_port_add_sources(dsp_pizero2040 DRIVERS ${DSP_PORT_DRIVERS} EMBED "${embedded_c}"
                     ZLIB_DIR ${DSP_PICO_DEPS}/zlib)

# Our src/ (tusb_config.h) before the RP2350 port's; its include/dirent.h
# before newlib's.
target_include_directories(dsp_pizero2040 BEFORE PRIVATE ${DSP_PICO_DIR}/src ${DSP_PICO2_DIR}/include)
target_include_directories(dsp_pizero2040 PRIVATE ${DSP_PICO2_DIR}/src ${fatfs_dir})
if(DSP_NO_SOUND)
  target_compile_definitions(dsp_pizero2040 PRIVATE DSP_NO_SOUND=1)
endif()
target_compile_definitions(dsp_pizero2040 PRIVATE
  PICO_MALLOC_PANIC=0
  DSP_AUDIO_PIN=${DSP_AUDIO_PIN}
  DSP_AUDIO_RING=2048u
  DSP_FS_TEMP_DIR="/sd/tmp"
  # micro-SD on SPI0 (as pico-infonesPlus wires this board)
  DSP_SD_SPI=spi0
  DSP_SD_SCK=18
  DSP_SD_MOSI=19
  DSP_SD_MISO=20
  DSP_SD_CS=21
  DVI_VERTICAL_REPEAT=${dsp_vertical_repeat}
  DVI_N_TMDS_BUFFERS=${dsp_tmds_buffers}
)
target_compile_options(dsp_pizero2040 PRIVATE -O2 -Wno-psabi)
# Core 1's line loop must stay in RAM: no memset/memcpy calls (flash) made
# up by the compiler from its loops.
set_source_files_properties(src/video_dvi.c src/video_rgb1.c PROPERTIES COMPILE_OPTIONS -fno-tree-loop-distribute-patterns)
target_link_libraries(dsp_pizero2040 PRIVATE
  pico_stdlib
  pico_multicore
  pico_sync
  hardware_dma
  hardware_pio
  hardware_pwm
  hardware_spi
  hardware_clocks
  hardware_vreg
  hardware_interp
  libdvi
  tinyusb_host
)
# The emulation's hot code runs from RAM: the RP2040's 16 KB flash cache
# cannot hold a Z80 interpreter, its MCU and sound chips at once, and every
# miss stalls core 0 on the flash. The SDK linker script with these objects'
# .text moved from FLASH to the RAM .data section (~55 KB: Z80, 6805, AY,
# Elevator Action, 6502, POKEY, Atari Tetris, YM2151; only
# the objects a firmware links take RAM).
set(DSP_RAM_CODE "z80.cpp.o;m6805.cpp.o;ay8910.cpp.o;taitosj.cpp.o;pokey.cpp.o;m6502.cpp.o;tetris_atari_hw.cpp.o;ym2151.cpp.o"
    CACHE STRING "Object files whose code is placed in RAM")
file(READ ${PICO_SDK_PATH}/src/rp2_common/pico_crt0/rp2040/memmap_default.ld dsp_ld)
set(dsp_ram_excludes "")
foreach(o IN LISTS DSP_RAM_CODE)
  string(APPEND dsp_ram_excludes " *${o}")
endforeach()
string(REPLACE "*(EXCLUDE_FILE(*libgcc.a: *libc.a:*lib_a-mem*.o *libm.a:) .text*)"
               "*(EXCLUDE_FILE(*libgcc.a: *libc.a:*lib_a-mem*.o *libm.a:${dsp_ram_excludes}) .text*)"
               dsp_ld_patched "${dsp_ld}")
if(dsp_ld_patched STREQUAL dsp_ld)
  message(FATAL_ERROR "memmap_default.ld of this SDK has no .text exclusion line to extend")
endif()
file(WRITE ${CMAKE_BINARY_DIR}/memmap_dsp.ld "${dsp_ld_patched}")
pico_set_linker_script(dsp_pizero2040 ${CMAKE_BINARY_DIR}/memmap_dsp.ld)

pico_enable_stdio_uart(dsp_pizero2040 1)
pico_enable_stdio_usb(dsp_pizero2040 0)
pico_set_program_name(dsp_pizero2040 "dsp-cpp")
pico_set_program_url(dsp_pizero2040 "https://github.com/Chusogar/dsp-cpp")
pico_add_extra_outputs(dsp_pizero2040)
