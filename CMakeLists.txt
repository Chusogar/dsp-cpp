cmake_minimum_required(VERSION 3.16)
project(dsp_cpp CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

find_package(SDL2 REQUIRED)
find_package(ZLIB REQUIRED)

add_executable(dsp
  src/main.cpp
  src/cpu/z80.cpp
  src/cpu/z80ctc.cpp
  src/cpu/m6809.cpp
  src/cpu/m6502.cpp
  src/cpu/m68000.cpp
  src/cpu/hd63701.cpp
  src/cpu/m6805.cpp
  src/cpu/lr35902.cpp
  src/cpu/upd7801.cpp
  src/sound/ay8910.cpp
  src/sound/sn76496.cpp
  src/sound/ym2151.cpp
  src/sound/pokey.cpp
  src/sound/msm5205.cpp
  src/sound/okim6295.cpp
  src/sound/ym2413.cpp
  src/sound/tms5220.cpp
  src/sound/upd1771.cpp
  src/video/gfx.cpp
  src/video/atari_mo.cpp
  src/video/tms9918.cpp
  src/machine/bagman_pal.cpp
  src/machine/slapstic.cpp
  src/machine/via6522.cpp
  src/machine/spectrum_tape.cpp
  src/machine/tape_tzx.cpp
  src/machine/i8255.cpp
  src/machine/nec765.cpp
  src/machine/upd765.cpp
  src/machine/disk_image.cpp
  
  src/drivers/bagman.cpp
  src/drivers/mikie.cpp
  src/drivers/gauntlet.cpp
  src/drivers/doubledragon.cpp
  src/drivers/taitosj.cpp
  src/drivers/mrdo.cpp
  src/drivers/atari_system1.cpp
  src/drivers/mcr.cpp
      
  src/drivers/spectrum.cpp
  src/drivers/spectrum_128k.cpp
  src/drivers/spectrum_3.cpp
  src/drivers/amstrad_cpc.cpp
  src/drivers/msx1.cpp
  
  src/drivers/sms.cpp
  src/machine/sega_vdp.cpp
  src/drivers/gamegear.cpp
  src/drivers/pv1000.cpp
  src/drivers/colecovision.cpp
  src/machine/i2cmem.cpp
  src/drivers/sg1000.cpp
  src/machine/gb_mappers.cpp
  src/drivers/scv.cpp
      
  src/core/rom_loader.cpp
  src/frontend/sdl_app.cpp
)

target_include_directories(dsp PRIVATE src ${SDL2_INCLUDE_DIRS})
target_link_libraries(dsp PRIVATE ${SDL2_LIBRARIES} ZLIB::ZLIB)
target_compile_options(dsp PRIVATE -Wall -Wextra)

enable_testing()
add_executable(dsp_tests
  tests/tests.cpp
  src/cpu/z80.cpp
  src/cpu/m6809.cpp
  src/cpu/m6502.cpp
  src/cpu/m68000.cpp
  src/cpu/hd63701.cpp
  src/cpu/m6805.cpp
  src/sound/ay8910.cpp
  src/sound/sn76496.cpp
  src/sound/ym2151.cpp
  src/sound/pokey.cpp
  src/sound/msm5205.cpp
  src/sound/okim6295.cpp
  src/video/gfx.cpp
  src/video/atari_mo.cpp
  src/machine/bagman_pal.cpp
  src/machine/slapstic.cpp
  src/machine/spectrum_tape.cpp
  src/machine/tape_tzx.cpp
  src/machine/i8255.cpp
  src/machine/nec765.cpp
  src/drivers/spectrum.cpp
  src/drivers/amstrad_cpc.cpp
  src/core/rom_loader.cpp
)
target_include_directories(dsp_tests PRIVATE src)
target_link_libraries(dsp_tests PRIVATE ZLIB::ZLIB)
target_compile_options(dsp_tests PRIVATE -Wall -Wextra)
add_test(NAME dsp_tests COMMAND dsp_tests)

# Optional harness for the Z80 instruction exerciser (binary not shipped here).
add_executable(dsp_zexdoc EXCLUDE_FROM_ALL tools/zexdoc_runner.cpp src/cpu/z80.cpp)
target_include_directories(dsp_zexdoc PRIVATE src)
target_compile_options(dsp_zexdoc PRIVATE -Wall -Wextra)
