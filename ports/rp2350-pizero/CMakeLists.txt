# dsp-cpp firmware for the Waveshare RP2350-PiZero (see README.md):
#   ./setup.sh && ./build.sh            (or the cmake lines in the README)
cmake_minimum_required(VERSION 3.13)

get_filename_component(DSP_PICO_DIR "${CMAKE_CURRENT_LIST_DIR}" ABSOLUTE)
set(DSP_PICO_DEPS "${DSP_PICO_DIR}/deps" CACHE PATH "Folder with the dependencies (setup.sh)")
if(NOT PICO_SDK_PATH AND NOT DEFINED ENV{PICO_SDK_PATH})
  set(PICO_SDK_PATH "${DSP_PICO_DEPS}/pico-sdk")
endif()
set(PICO_PLATFORM rp2350-arm-s CACHE STRING "Pico platform")
set(PICO_BOARD dsp_rp2350_pizero CACHE STRING "Board")
set(PICO_BOARD_HEADER_DIRS "${DSP_PICO_DIR}/boards")
set(PICO_PIO_USB_PATH "${DSP_PICO_DEPS}/Pico-PIO-USB")

include(pico_sdk_import.cmake)
project(dsp_rp2350_pizero C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
pico_sdk_init()

# What goes into the firmware -----------------------------------------------
set(DSP_PORT_DRIVERS "galaxian;arabian;bagman;mrdo;sms;gameboy;nes;spectrum;c64"
    CACHE STRING "Drivers built in: names from cmake/drivers.cmake or arcade/consoles/computers/all")
set(DSP_EMBED_GAME "" CACHE STRING "Embedded media: --game name to start")
set(DSP_EMBED_ROM "" CACHE PATH "Embedded media: ROM set (zip, file or directory)")
set(DSP_EMBED_TAPE "" CACHE STRING "Embedded media: tapes/snapshots (';' separated)")
set(DSP_EMBED_DISK "" CACHE STRING "Embedded media: disk images (';' separated)")
set(DSP_EMBED_CART "" CACHE STRING "Embedded media: cartridges (';' separated)")
set(DSP_EMBED_ARGS "" CACHE STRING "Embedded media: extra arguments (';' separated)")
set(DSP_EMBED_C "" CACHE FILEPATH "Embedded media: C file written by tools/embed_media.py")
set(DSP_AUDIO_PIN 18 CACHE STRING "GPIO of the PWM sound output")

include(${DSP_PICO_DIR}/../common/dsp_port.cmake)

foreach(dep PicoDVI Pico-PIO-USB zlib fatfs)
  if(NOT EXISTS "${DSP_PICO_DEPS}/${dep}")
    message(FATAL_ERROR "${DSP_PICO_DEPS}/${dep} is missing: run ./setup.sh first")
  endif()
endforeach()

# PicoDVI's libdvi (BSD-3).
add_subdirectory(${DSP_PICO_DEPS}/PicoDVI/software/libdvi ${CMAKE_BINARY_DIR}/libdvi)

# FatFs (ChaN, BSD-1): its sources next to our ffconf.h, so ff.h finds ours.
set(fatfs_dir ${CMAKE_BINARY_DIR}/fatfs)
foreach(f ff.c ff.h diskio.h ffsystem.c ffunicode.c)
  configure_file(${DSP_PICO_DEPS}/fatfs/source/${f} ${fatfs_dir}/${f} COPYONLY)
endforeach()
configure_file(${DSP_PICO_DIR}/src/ffconf.h ${fatfs_dir}/ffconf.h COPYONLY)

add_executable(dsp_pizero
  src/main.cpp
  src/posix_io.cpp
  src/usb_input.cpp
  src/heap.c
  src/psram.c
  src/sd_spi.c
  src/audio_pwm.c
  src/video_dvi.c
  ${fatfs_dir}/ff.c
  ${fatfs_dir}/ffsystem.c
  ${fatfs_dir}/ffunicode.c
)
dsp_port_embedded_media(embedded_c)
dsp_port_add_sources(dsp_pizero DRIVERS ${DSP_PORT_DRIVERS} EMBED "${embedded_c}"
                     ZLIB_DIR ${DSP_PICO_DEPS}/zlib)

# <dirent.h> of include/ before newlib's (which has none).
target_include_directories(dsp_pizero BEFORE PRIVATE ${DSP_PICO_DIR}/include)
target_include_directories(dsp_pizero PRIVATE ${DSP_PICO_DIR}/src ${fatfs_dir})
target_compile_definitions(dsp_pizero PRIVATE
  PICO_PIO_USE_GPIO_BASE=1
  PICO_MALLOC_PANIC=1
  DSP_AUDIO_PIN=${DSP_AUDIO_PIN}
  DSP_FS_TEMP_DIR="/sd/tmp"
  PIO_USB_DP_PIN_DEFAULT=28
  # libdvi: one 16 bpp frame buffer line per pair of output lines
  DVI_VERTICAL_REPEAT=2
  DVI_N_TMDS_BUFFERS=3
)
# The emulator code is speed-critical: -O2 everywhere.
target_compile_options(dsp_pizero PRIVATE -O2 -Wno-psabi)
target_link_libraries(dsp_pizero PRIVATE
  pico_stdlib
  pico_multicore
  pico_sync
  hardware_dma
  hardware_pio
  hardware_pwm
  hardware_spi
  hardware_clocks
  hardware_vreg
  libdvi
  tinyusb_host
  tinyusb_pico_pio_usb
)
pico_enable_stdio_uart(dsp_pizero 1)
pico_enable_stdio_usb(dsp_pizero 0)
pico_set_program_name(dsp_pizero "dsp-cpp")
pico_set_program_url(dsp_pizero "https://github.com/Chusogar/dsp-cpp")
pico_add_extra_outputs(dsp_pizero)
