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
  src/cpu/tms7000.cpp
  src/cpu/hu6280.cpp
  src/cpu/mcs51.cpp
  src/cpu/mcs48.cpp
  src/cpu/nec_v30.cpp
  src/cpu/z8002.cpp
  src/cpu/mb88xx.cpp
 
  src/sound/ay8910.cpp
  src/sound/sn76496.cpp
  src/sound/ym2151.cpp
  src/sound/pokey.cpp
  src/sound/msm5205.cpp
  src/sound/okim6295.cpp
  src/sound/qsound.cpp
  src/sound/ym2413.cpp
  src/sound/tms5220.cpp
  src/sound/upd1771.cpp
  src/sound/fmopn.cpp
  src/sound/ym2203.cpp
  src/sound/ym2610.cpp
  src/sound/ym3812.cpp
  src/sound/nes_apu.cpp
  src/sound/polepos_wsg.cpp
  src/sound/polepos_engine.cpp
  src/sound/sega_pcm.cpp
  src/sound/upd7759.cpp
  src/sound/huc6280_psg.cpp
  src/sound/vlm5030.cpp
    
  src/video/gfx.cpp
  src/video/atari_mo.cpp
  src/video/tms9918.cpp
  src/video/v9938.cpp
  src/video/tms3556.cpp
  src/video/deco_bac06.cpp
  src/video/nes_ppu.cpp
  src/video/avg_starwars.cpp
  src/video/sega16.cpp
  src/video/apple2_video.cpp
  src/video/huc6260.cpp
  src/video/huc6270.cpp
  src/video/neogeo_video.cpp
  
  src/machine/bagman_pal.cpp
  src/machine/slapstic.cpp
  src/machine/via6522.cpp
  src/machine/kabuki.cpp
  src/machine/eeprom93c46.cpp
  src/machine/spectrum_tape.cpp
  src/machine/tape_tzx.cpp
  src/machine/i8255.cpp
  src/machine/nec765.cpp
  src/machine/disk_image.cpp
  src/machine/lynx_suzy.cpp
  src/machine/lynx_mikey.cpp
  src/machine/trdos_disk.cpp
  src/machine/wd1793.cpp
  src/machine/msx_dsk.cpp
  src/machine/rp5c01.cpp
  src/machine/beta128.cpp
  src/machine/mos6532.cpp
  src/machine/starwars_math.cpp
  src/machine/namco51.cpp
  src/machine/namco52.cpp
  src/machine/namco53.cpp
  src/machine/namco54.cpp
  src/machine/sega_315_5195.cpp
  src/machine/fd1089.cpp
  src/machine/i8243.cpp
  src/machine/diskii.cpp
  src/machine/c1541.cpp
  src/machine/d64_image.cpp
  src/machine/t64_image.cpp
  src/machine/g64_image.cpp
  src/machine/gcr.cpp
  src/machine/c64_tape.cpp
  src/machine/z80pio.cpp
  src/machine/sega_decrypt.cpp
  src/machine/konami_decrypt.cpp
  src/machine/tc0140syt.cpp
  src/machine/opwolf_cchip.cpp

    
  src/drivers/bagman.cpp
  src/drivers/mikie.cpp
  src/drivers/gauntlet.cpp
  src/drivers/doubledragon.cpp
  src/drivers/taitosj.cpp
  src/drivers/mrdo.cpp
  src/drivers/atari_system1.cpp
  src/drivers/mcr.cpp
  src/drivers/dec0.cpp
  src/drivers/m62.cpp
  src/drivers/snk.cpp
  src/drivers/cps1.cpp
  src/drivers/m72.cpp
  src/drivers/starwars.cpp
  src/drivers/polepos.cpp
  src/drivers/hangon.cpp
  src/drivers/system16.cpp
  src/drivers/outrun.cpp
  src/drivers/galaxian.cpp
  src/drivers/vicdual.cpp
  src/drivers/sega_system1.cpp
  src/drivers/neogeo.cpp
  src/drivers/opwolf.cpp
  src/drivers/trackfld.cpp
  src/drivers/pirates.cpp
  src/drivers/skullxbo.cpp

  src/drivers/spectrum.cpp
  src/drivers/spectrum_128k.cpp
  src/drivers/spectrum_3.cpp
  src/drivers/zx_clone.cpp
  src/drivers/amstrad_cpc.cpp
  src/drivers/msx1.cpp
  src/drivers/msx2.cpp
  src/sound/sid.cpp
  src/sound/sid6581.cpp
  src/drivers/c64.cpp
  src/drivers/apple2.cpp
  src/drivers/exelv.cpp
    
  src/drivers/sms.cpp
  src/machine/sega_vdp.cpp
  src/video/sega_315_5313.cpp
  src/sound/ym2612.cpp
  src/drivers/genesis.cpp
  src/drivers/gamegear.cpp
  src/drivers/pv1000.cpp
  src/drivers/pv2000.cpp
  src/drivers/colecovision.cpp
  src/machine/i2cmem.cpp
  src/drivers/sg1000.cpp
  src/drivers/gameboy.cpp
  src/machine/gb_mapper.cpp
  src/sound/gb_apu.cpp
  src/video/gb_ppu.cpp
  src/machine/nes_mapper.cpp
  src/drivers/nes.cpp
  src/drivers/scv.cpp
  src/machine/mos6526.cpp
  src/machine/mos6566.cpp
  src/drivers/atari_lynx.cpp
  src/drivers/pcengine.cpp
  
      
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
  src/cpu/z80ctc.cpp
  src/cpu/m6809.cpp
  src/cpu/m6502.cpp
  src/cpu/m68000.cpp
  src/cpu/hd63701.cpp
  src/cpu/m6805.cpp
  src/cpu/lr35902.cpp
  src/cpu/upd7801.cpp
  src/cpu/tms7000.cpp
  src/cpu/z8002.cpp
  src/cpu/mb88xx.cpp
  src/cpu/mcs51.cpp
  src/cpu/mcs48.cpp
  src/cpu/hu6280.cpp
  
  src/sound/ay8910.cpp
  src/sound/sn76496.cpp
  src/sound/ym2151.cpp
  src/sound/pokey.cpp
  src/sound/msm5205.cpp
  src/sound/okim6295.cpp
  src/sound/nes_apu.cpp
  src/sound/qsound.cpp
  src/sound/upd1771.cpp
  src/sound/tms5220.cpp
  src/sound/sid.cpp
  src/sound/sid6581.cpp
  src/sound/gb_apu.cpp
  src/sound/polepos_wsg.cpp
  src/sound/polepos_engine.cpp
  src/sound/huc6280_psg.cpp
  src/sound/fmopn.cpp
  src/sound/ym2612.cpp
  src/sound/ym2610.cpp
  src/sound/ym2203.cpp
  src/sound/sega_pcm.cpp
  src/sound/upd7759.cpp
  src/sound/huc6280_psg.cpp
  src/sound/vlm5030.cpp
  src/video/huc6270.cpp
  src/video/huc6260.cpp
  src/video/gfx.cpp
  src/video/atari_mo.cpp
  src/video/nes_ppu.cpp
  src/video/tms3556.cpp
  src/video/tms9918.cpp
  src/video/v9938.cpp
  src/video/gb_ppu.cpp
  src/video/avg_starwars.cpp
  src/video/sega_315_5313.cpp
  src/video/sega16.cpp
  src/video/apple2_video.cpp
  src/video/neogeo_video.cpp
  src/machine/bagman_pal.cpp
  src/machine/slapstic.cpp
  src/machine/via6522.cpp
  src/machine/spectrum_tape.cpp
  src/machine/tape_tzx.cpp
  src/machine/i8255.cpp
  src/machine/kabuki.cpp
  src/machine/eeprom93c46.cpp
  src/machine/nec765.cpp
  src/machine/mos6526.cpp
  src/machine/mos6566.cpp
  src/machine/c1541.cpp
  src/machine/d64_image.cpp
  src/machine/t64_image.cpp
  src/machine/g64_image.cpp
  src/machine/gcr.cpp
  src/machine/nes_mapper.cpp
  src/machine/gb_mapper.cpp
  src/machine/lynx_suzy.cpp
  src/machine/lynx_mikey.cpp
  src/machine/trdos_disk.cpp
  src/machine/wd1793.cpp
  src/machine/msx_dsk.cpp
  src/machine/rp5c01.cpp
  src/machine/beta128.cpp
  src/machine/mos6532.cpp
  src/machine/starwars_math.cpp
  src/machine/namco51.cpp
  src/machine/namco52.cpp
  src/machine/namco53.cpp
  src/machine/namco54.cpp
  src/machine/sega_315_5195.cpp
  src/machine/fd1089.cpp
  src/machine/i8243.cpp
  src/machine/diskii.cpp
  src/machine/c64_tape.cpp
  src/machine/tc0140syt.cpp
  src/machine/opwolf_cchip.cpp
  src/machine/konami_decrypt.cpp
  src/drivers/spectrum.cpp
  src/drivers/amstrad_cpc.cpp
  src/drivers/msx2.cpp
  src/drivers/nes.cpp
  src/drivers/c64.cpp
  src/drivers/apple2.cpp
  src/drivers/gameboy.cpp
  src/drivers/mcr.cpp
  src/drivers/atari_lynx.cpp
  src/drivers/scv.cpp
  src/drivers/pv2000.cpp
  src/drivers/exelv.cpp
  src/drivers/starwars.cpp
  src/drivers/polepos.cpp
  src/drivers/atari_system1.cpp
  src/drivers/zx_clone.cpp
  src/drivers/genesis.cpp
  src/drivers/outrun.cpp
  src/drivers/hangon.cpp
  src/drivers/system16.cpp
  src/drivers/pcengine.cpp
  src/drivers/neogeo.cpp
  src/drivers/opwolf.cpp
  src/drivers/trackfld.cpp
  src/drivers/pirates.cpp
  src/drivers/skullxbo.cpp
  src/core/rom_loader.cpp
)
target_include_directories(dsp_tests PRIVATE src)
target_link_libraries(dsp_tests PRIVATE ZLIB::ZLIB)
target_compile_options(dsp_tests PRIVATE -Wall -Wextra)
add_test(NAME dsp_tests COMMAND dsp_tests)

add_executable(tapper_capture
  tools/tapper_capture.cpp
  src/drivers/mcr.cpp
  src/cpu/z80.cpp
  src/cpu/z80ctc.cpp
  src/sound/ay8910.cpp
  src/video/gfx.cpp
  src/core/rom_loader.cpp
)
target_include_directories(tapper_capture PRIVATE src)
target_link_libraries(tapper_capture PRIVATE ZLIB::ZLIB)
target_compile_options(tapper_capture PRIVATE -Wall -Wextra)

# Optional harness for the Z80 instruction exerciser (binary not shipped here).
add_executable(dsp_zexdoc EXCLUDE_FROM_ALL tools/zexdoc_runner.cpp src/cpu/z80.cpp)
target_include_directories(dsp_zexdoc PRIVATE src)
target_compile_options(dsp_zexdoc PRIVATE -Wall -Wextra)
