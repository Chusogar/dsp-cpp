# Lean single-game firmware for the Waveshare RP2350-PiZero (Pico SDK), one
# game per build (see ports/lean/README.md):
#
#   tools/setup_pico.sh                                    (once: Pico SDK, PicoDVI, Pico-PIO-USB)
#   tools/regen.sh gauntlet ~/roms/gauntlet.zip            (once: the ROMs in flash)
#   cmake -S main/ports/rp2040-pizero -B build/pico2-gauntlet -DGAME=gauntlet -DPIZERO=2350
#   cmake --build build/pico2-gauntlet -j                  -> build/pico2-gauntlet/gauntlet_pizero.uf2
#
# or ./build_rp2350.sh (-> dist/). GAME: indytemp, roadrunn, gauntlet.
# (-DPIZERO=2040 builds for the RP2040-PiZero, as cMESYS does.)
cmake_minimum_required(VERSION 3.13)

get_filename_component(PORT_DIR "${CMAKE_CURRENT_LIST_DIR}" ABSOLUTE)
get_filename_component(M "${PORT_DIR}/../.." ABSOLUTE)           # main/
get_filename_component(ROOT "${M}/.." ABSOLUTE)
set(PICO_DEPS "${ROOT}/deps/pico" CACHE PATH "Pico SDK and PicoDVI (tools/setup_pico.sh)")
if(NOT PICO_SDK_PATH AND NOT DEFINED ENV{PICO_SDK_PATH})
    set(PICO_SDK_PATH "${PICO_DEPS}/pico-sdk")
endif()
set(PIZERO 2040 CACHE STRING "Board: 2040 (RP2040-PiZero) or 2350 (RP2350-PiZero)")
option(USB_NATIVE "RP2350: keyboard / pad on the native USB-C instead of the PIO-USB port" OFF)
if(PIZERO STREQUAL "2350")
    set(PICO_PLATFORM rp2350-arm-s CACHE STRING "Pico platform")
    set(PICO_BOARD cmesys_rp2350_pizero CACHE STRING "Board")
    set(PICO_BOARD_HEADER_DIRS "${PORT_DIR}/boards")
    # Pico-PIO-USB's RP2350-E9 check takes only revision 2 (A2) as affected;
    # A3 (revision 3, the user's RP2350-PiZero: D+ and D- both stuck high)
    # is the same silicon. A copy with A3 included (see rp2350/usb_frame.c)
    set(PICO_PIO_USB_PATH "${CMAKE_BINARY_DIR}/pio_usb_src")
    file(COPY ${PICO_DEPS}/Pico-PIO-USB/src DESTINATION ${PICO_PIO_USB_PATH})
    file(READ ${PICO_PIO_USB_PATH}/src/pio_usb_ll.h pu)
    string(REPLACE "if (chip_version <= 2) {" "if (chip_version <= 3) {" pu2 "${pu}")
    if(pu2 STREQUAL pu)
        message(FATAL_ERROR "pio_usb_ll.h of this Pico-PIO-USB has no RP2350-E9 check to extend")
    endif()
    file(WRITE ${PICO_PIO_USB_PATH}/src/pio_usb_ll.h "${pu2}")
else()
    set(PICO_PLATFORM rp2040 CACHE STRING "Pico platform")
    set(PICO_BOARD waveshare_rp2040_pizero CACHE STRING "Board")
endif()

include(${PORT_DIR}/pico_sdk_import.cmake)
project(dsp_lean_pizero C CXX ASM)
set(CMAKE_C_STANDARD 11)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()
pico_sdk_init()

if(NOT GAME)
    message(FATAL_ERROR "-DGAME=<indytemp|roadrunn|gauntlet>")
endif()
# HDMI: PicoDVI on the RP2040; on the RP2350 rp2350/hdmi (pico_lib's DVI,
# which also sends the sound to the TV, in data islands)
if(NOT PIZERO STREQUAL "2350")
    if(NOT EXISTS "${PICO_DEPS}/PicoDVI/software/libdvi")
        message(FATAL_ERROR "${PICO_DEPS}/PicoDVI is missing: run tools/setup_pico.sh first")
    endif()
    add_subdirectory(${PICO_DEPS}/PicoDVI/software/libdvi ${CMAKE_BINARY_DIR}/libdvi)
endif()

set(QL_DIR "")
set(D ${M}/drivers/arcades/${GAME})
set(AS1 ${M}/drivers/arcades/atarisy1)
set(M68K ${M}/cpu/m68k/m68kcpu.c ${M}/cpu/m68k/m68kops.c ${M}/cpu/m68k/m68kopac.c
         ${M}/cpu/m68k/m68kopdm.c ${M}/cpu/m68k/m68kopnz.c ${D}/m68k_tables.c)

# Per machine: sources, defines, the objects whose code runs from RAM (the
# RP2040's 16 KB flash cache cannot hold an emulator: every miss stalls the
# core), and the rows core 1 renders at a time.
set(BAND 4)
set(TMDS_SLACK 2)                   # TMDS rows beyond a band (3.75 KB each)
if(GAME STREQUAL "indytemp" OR GAME STREQUAL "roadrunn")
    set(SRCS ${D}/${GAME}_pico.c ${AS1}/atarisy1_core.c ${D}/rom_data.c ${M68K}
             ${M}/sound/ym2151.c ${M}/sound/pokey.c ${M}/sound/tms5220.c ${M}/machine/via6522.c
             ${M}/machine/slapstic.c ${M}/video/atari_mo.c)
    set(DEFS M68K_FLASH_TABLES AS1_HOOK_PC)
    set(RAM_CODE m68kcpu.c.o atarisy1_core.c.o pokey.c.o ym2151.c.o tms5220.c.o)
elseif(GAME STREQUAL "gauntlet")
    set(SRCS ${D}/gauntlet_pico.c ${D}/gauntlet_core.c ${D}/rom_data.c ${M68K}
             ${M}/sound/ym2151.c ${M}/sound/pokey.c ${M}/sound/tms5220.c ${M}/machine/slapstic.c ${M}/video/atari_mo.c)
    set(DEFS M68K_FLASH_TABLES)
    set(RAM_CODE m68kcpu.c.o gauntlet_core.c.o pokey.c.o ym2151.c.o tms5220.c.o)
else()
    message(FATAL_ERROR "Unknown GAME '${GAME}'")
endif()

# The game's hottest 68000 opcode handlers (its ESP32 .lf list) in RAM too
file(GLOB LF ${D}/*.lf)
if(LF AND DEFS MATCHES "M68K_FLASH_TABLES")
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    execute_process(COMMAND ${Python3_EXECUTABLE} ${ROOT}/tools/pico_hot.py ${LF} ${CMAKE_BINARY_DIR}/m68k_hot.h ${HOT_MAX})
    set_source_files_properties(${M}/cpu/m68k/m68kopac.c ${M}/cpu/m68k/m68kopdm.c ${M}/cpu/m68k/m68kopnz.c
        PROPERTIES COMPILE_OPTIONS "-include;${CMAKE_BINARY_DIR}/m68k_hot.h")
endif()

set(EXE ${GAME}_pizero)
if(PIZERO STREQUAL "2350")
    # RP2350: PSRAM heap, whole frame buffers (no rows drawn against the
    # clock), a few TMDS rows of slack
    set(HD ${PORT_DIR}/rp2350/hdmi)
    list(APPEND SRCS ${PORT_DIR}/rp2350/psram.c ${PORT_DIR}/rp2350/heap.c ${PORT_DIR}/rp2350/usb_frame.c
         ${HD}/hdmi_glue.cpp ${HD}/dvi/dvi.cpp ${HD}/dvi/dma.cpp ${HD}/dvi/data_packet.cpp ${HD}/dvi/timing.cpp
         ${HD}/dvi/tmds_encode.cpp ${HD}/dvi/tmds_encode.S)
    list(APPEND RAM_CODE dvi.cpp.o)
    # (heap.c is malloc itself: GCC would turn its calloc's malloc + memset
    # into a call to calloc)
    set_source_files_properties(${PORT_DIR}/rp2350/heap.c PROPERTIES COMPILE_OPTIONS -fno-builtin)  # (its DMA interrupt, every line: not from flash)
    set(BAND 2)
    set(TMDS_SLACK 4)
endif()
add_executable(${EXE} ${SRCS} ${PORT_DIR}/pico_port.c)
target_include_directories(${EXE} PRIVATE ${PORT_DIR} ${D} ${AS1} ${QL_DIR} ${M}/drivers/arcades/polepos ${M}/cpu ${M}/cpu/m68k ${M}/cpu/m6502
    ${M}/cpu/mc6809 ${M}/cpu/mcs48 ${M}/cpu/z80 ${M}/sound ${M}/machine ${M}/video)
# TMDS rows: a band, the one on screen, the one being freed and the slack
math(EXPR NBUF "${BAND} + 2 + ${TMDS_SLACK}")
target_compile_definitions(${EXE} PRIVATE ${DEFS} NDEBUG PICO_BAND=${BAND}
    DVI_VERTICAL_REPEAT=2 DVI_N_TMDS_BUFFERS=${NBUF}
    PICO_MALLOC_PANIC=0 RENDER_NO_OWN_BUFFERS
    PICO_MEM_IN_RAM=1 PICO_DIVIDER_IN_RAM=1   # (core 1 draws with memset / divisions: not from flash)
    PICO_STACK_SIZE=0x1000 PICO_CORE1_STACK_SIZE=0xE00)
if(DEFINED QL_AUDIO_PIN)
    target_compile_definitions(${EXE} PRIVATE PICO_AUDIO_PIN=${QL_AUDIO_PIN})
endif()
if(NOT PIZERO STREQUAL "2350")
    target_compile_definitions(libdvi INTERFACE DVI_VERTICAL_REPEAT=2 DVI_N_TMDS_BUFFERS=${NBUF})
endif()
target_compile_options(${EXE} PRIVATE -O2 -Wno-unused-variable -Wno-unused-function -Wno-sign-compare
    -Wno-maybe-uninitialized -Wno-unused-but-set-variable -Wno-implicit-fallthrough -Wno-psabi)
set_source_files_properties(${PORT_DIR}/pico_port.c PROPERTIES COMPILE_OPTIONS -fno-tree-loop-distribute-patterns)
target_link_libraries(${EXE} PRIVATE pico_stdlib pico_multicore pico_sync hardware_dma hardware_pio
    hardware_pwm hardware_clocks hardware_vreg hardware_interp hardware_spi tinyusb_host)
if(PIZERO STREQUAL "2350")
    if(USB_NATIVE)
        target_compile_definitions(${EXE} PRIVATE PICO_USB_NATIVE=1)
    endif()
    target_compile_definitions(${EXE} PRIVATE PICO_PIO_USE_GPIO_BASE=1 PIO_USB_DP_PIN_DEFAULT=28
        DVI_N_LINE_BUFFERS=2)       # (pico_lib's line buffers: not used here)
    target_include_directories(${EXE} PRIVATE ${HD})
    pico_generate_pio_header(${EXE} ${HD}/dvi/dvi_serialiser.pio)
    target_link_libraries(${EXE} PRIVATE tinyusb_pico_pio_usb pico_flash hardware_timer pico_util)
    set_source_files_properties(${HD}/hdmi_glue.cpp ${HD}/dvi/dvi.cpp ${HD}/dvi/dma.cpp ${HD}/dvi/data_packet.cpp
        ${HD}/dvi/timing.cpp ${HD}/dvi/tmds_encode.cpp PROPERTIES COMPILE_OPTIONS "-std=gnu++17;-fno-exceptions;-fno-rtti")
else()
    target_link_libraries(${EXE} PRIVATE libdvi)
endif()

# Linker script: the SDK's, with the RAM_CODE objects' .text moved to RAM.
if(PIZERO STREQUAL "2350")
    file(READ ${PICO_SDK_PATH}/src/rp2_common/pico_crt0/rp2350/memmap_default.ld ld)
else()
    file(READ ${PICO_SDK_PATH}/src/rp2_common/pico_crt0/rp2040/memmap_default.ld ld)
endif()
set(excl "")
foreach(o IN LISTS RAM_CODE)
    string(APPEND excl " *${o}")
endforeach()
string(REPLACE "*(EXCLUDE_FILE(*libgcc.a: *libc.a:*lib_a-mem*.o *libm.a:) .text*)"
               "*(EXCLUDE_FILE(*libgcc.a: *libc.a:*lib_a-mem*.o *libm.a:${excl}) .text*)" ld2 "${ld}")
if(ld2 STREQUAL ld AND RAM_CODE)
    message(FATAL_ERROR "memmap_default.ld of this SDK has no .text exclusion line to extend")
endif()
# RAM_RODATA: those objects' constants too (into .data's *(.rodata*), copied at boot)
if(RAM_RODATA)
    set(rexcl "")
    foreach(o IN LISTS RAM_RODATA)
        string(APPEND rexcl " *${o}")
    endforeach()
    set(ld3 "${ld2}")
    string(REPLACE "*(EXCLUDE_FILE(*libgcc.a: *libc.a:*lib_a-mem*.o *libm.a:) .rodata*)"
                   "*(EXCLUDE_FILE(*libgcc.a: *libc.a:*lib_a-mem*.o *libm.a:${rexcl}) .rodata*)" ld2 "${ld3}")
    if(ld2 STREQUAL ld3)
        message(FATAL_ERROR "memmap_default.ld of this SDK has no .rodata exclusion line to extend")
    endif()
endif()
file(WRITE ${CMAKE_BINARY_DIR}/memmap_cmesys.ld "${ld2}")
pico_set_linker_script(${EXE} ${CMAKE_BINARY_DIR}/memmap_cmesys.ld)

# Test in the rp2040js simulator (one core, no HDMI / USB / sound): the
# emulation alone, frames logged on the UART.
option(PICO_EMU_TEST "Build for the rp2040js simulator" OFF)
if(PICO_EMU_TEST)
    target_compile_definitions(${EXE} PRIVATE PICO_EMU_TEST)
endif()

pico_enable_stdio_uart(${EXE} 1)
pico_enable_stdio_usb(${EXE} 0)
pico_set_program_name(${EXE} "dsp-cpp lean ${GAME}")
pico_add_extra_outputs(${EXE})
