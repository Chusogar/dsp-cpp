# Desktop build of the microcontroller port front end (ports/common) with a
# headless Hal: checks that the per-driver source lists link, and runs the
# menu and games without a board. Not needed to build the firmware.
#
#   cmake -S ports/common/host -B build-port-host -DDSP_PORT_DRIVERS="galaxian;blockout"
#   cmake --build build-port-host -j
#   build-port-host/dsp-port-host --root /path/to/sdcard --keys down,enter --frames 600 --out shot.ppm
cmake_minimum_required(VERSION 3.16)
project(dsp_port_host C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(DSP_PORT_DRIVERS "galaxian;blockout" CACHE STRING "Drivers built in (names or arcade/consoles/computers/all)")
set(DSP_EMBED_GAME "" CACHE STRING "Embedded media: --game name to start")
set(DSP_EMBED_ROM "" CACHE PATH "Embedded media: ROM set (zip, file or directory)")
set(DSP_EMBED_TAPE "" CACHE STRING "Embedded media: tapes/snapshots (';' separated)")
set(DSP_EMBED_DISK "" CACHE STRING "Embedded media: disk images (';' separated)")
set(DSP_EMBED_CART "" CACHE STRING "Embedded media: cartridges (';' separated)")
set(DSP_EMBED_ARGS "" CACHE STRING "Embedded media: extra arguments (';' separated)")
set(DSP_EMBED_C "" CACHE FILEPATH "Embedded media: C file written by tools/embed_media.py")

include(${CMAKE_CURRENT_LIST_DIR}/../dsp_port.cmake)
find_package(ZLIB REQUIRED)

add_executable(dsp-port-host host_main.cpp)
dsp_port_embedded_media(embedded_c)
dsp_port_add_sources(dsp-port-host DRIVERS ${DSP_PORT_DRIVERS} EMBED "${embedded_c}")
target_link_libraries(dsp-port-host PRIVATE ZLIB::ZLIB m)

# Tests of the board-independent code (memfs, HID, scaling, dsp::fsx...).
add_executable(dsp-port-tests port_tests.cpp)
dsp_port_add_sources(dsp-port-tests DRIVERS galaxian)
target_link_libraries(dsp-port-tests PRIVATE ZLIB::ZLIB m)
enable_testing()
add_test(NAME dsp-port-tests COMMAND dsp-port-tests)
