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
  src/sound/ay8910.cpp
  src/video/gfx.cpp
  src/machine/bagman_pal.cpp
  src/drivers/bagman.cpp
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
  src/sound/ay8910.cpp
  src/video/gfx.cpp
  src/machine/bagman_pal.cpp
)
target_include_directories(dsp_tests PRIVATE src)
target_compile_options(dsp_tests PRIVATE -Wall -Wextra)
add_test(NAME dsp_tests COMMAND dsp_tests)

# Optional harness for the Z80 instruction exerciser (binary not shipped here).
add_executable(dsp_zexdoc EXCLUDE_FROM_ALL tools/zexdoc_runner.cpp src/cpu/z80.cpp)
target_include_directories(dsp_zexdoc PRIVATE src)
target_compile_options(dsp_zexdoc PRIVATE -Wall -Wextra)
