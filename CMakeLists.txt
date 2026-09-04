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
  src/cpu/konami.cpp
  src/cpu/m6502.cpp
  src/cpu/t11.cpp
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
  src/video/dvg.cpp
  src/video/sega16.cpp
  src/video/apple2_video.cpp
  src/video/zx8301.cpp
  src/video/huc6260.cpp
  src/video/huc6270.cpp
  src/video/neogeo_video.cpp
  src/video/tia.cpp
  
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
  src/machine/namco50.cpp
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
  src/machine/zx8302.cpp
  src/machine/ql_mdv.cpp
  src/machine/mc68901.cpp
  src/machine/st_floppy.cpp

    
  src/drivers/arcade/bagman.cpp
  src/drivers/arcade/mikie.cpp
  src/drivers/arcade/gauntlet.cpp
  src/drivers/arcade/doubledragon.cpp
  src/drivers/arcade/taitosj.cpp
  src/drivers/arcade/mrdo.cpp
  src/drivers/arcade/atari_system1.cpp
  src/drivers/arcade/atari_system2.cpp
  src/drivers/arcade/mcr.cpp
  src/drivers/arcade/dec0.cpp
  src/drivers/arcade/m62.cpp
  src/drivers/arcade/snk.cpp
  src/drivers/arcade/cps1.cpp
  src/drivers/arcade/m72.cpp
  src/drivers/arcade/starwars.cpp
  src/drivers/arcade/asteroid.cpp
  src/drivers/arcade/polepos.cpp
  src/drivers/arcade/hangon.cpp
  src/drivers/arcade/system16.cpp
  src/drivers/arcade/outrun.cpp
  src/drivers/arcade/galaxian.cpp
  src/drivers/arcade/vicdual.cpp
  src/drivers/arcade/sega_system1.cpp
  src/drivers/arcade/neogeo.cpp
  src/drivers/arcade/opwolf.cpp
  src/drivers/arcade/trackfld.cpp
  src/drivers/arcade/pirates.cpp
  src/drivers/arcade/armedf_hw.cpp
  src/machine/nb1414_m4.cpp
  src/drivers/arcade/wwfsstar.cpp
  src/drivers/arcade/citycon.cpp
  src/drivers/arcade/commando.cpp
  src/drivers/arcade/galaga_hw.cpp
  src/drivers/arcade/shadow_warriors_hw.cpp
  src/drivers/arcade/actfancer.cpp
  src/drivers/arcade/ajax.cpp
  src/drivers/arcade/aliens.cpp
  src/drivers/arcade/simpsons.cpp
  src/video/k052109.cpp
  src/video/k051960.cpp
  src/video/k051316.cpp
  src/video/k053251.cpp
  src/video/k053246.cpp
  src/sound/k053260.cpp
  src/machine/eeprom_er5911.cpp
  src/sound/k007232.cpp
  src/drivers/arcade/tetris_atari_hw.cpp
  src/drivers/arcade/skullxbo.cpp
  src/drivers/arcade/gng.cpp
  src/drivers/arcade/bublbobl.cpp

  src/drivers/computers/spectrum.cpp
  src/drivers/computers/spectrum_128k.cpp
  src/drivers/computers/spectrum_3.cpp
  src/drivers/computers/zx_clone.cpp
  src/drivers/computers/amstrad_cpc.cpp
  src/drivers/computers/msx1.cpp
  src/drivers/computers/msx2.cpp
  src/sound/sid.cpp
  src/sound/sid6581.cpp
  src/drivers/computers/c64.cpp
  src/drivers/computers/apple2.cpp
  src/drivers/computers/exelv.cpp
  src/drivers/computers/ql.cpp
  src/drivers/computers/atari_st.cpp
    
  src/drivers/consoles/sms.cpp
  src/machine/sega_vdp.cpp
  src/video/sega_315_5313.cpp
  src/sound/ym2612.cpp
  src/drivers/consoles/genesis.cpp
  src/drivers/consoles/gamegear.cpp
  src/drivers/consoles/pv1000.cpp
  src/drivers/consoles/pv2000.cpp
  src/drivers/consoles/colecovision.cpp
  src/machine/i2cmem.cpp
  src/drivers/consoles/sg1000.cpp
  src/drivers/consoles/gameboy.cpp
  src/machine/gb_mapper.cpp
  src/sound/gb_apu.cpp
  src/video/gb_ppu.cpp
  src/machine/nes_mapper.cpp
  src/drivers/consoles/nes.cpp
  src/drivers/consoles/scv.cpp
  src/machine/mos6526.cpp
  src/machine/mos6566.cpp
  src/drivers/consoles/atari_lynx.cpp
  src/drivers/consoles/a2600.cpp
  src/drivers/consoles/pcengine.cpp
  
      
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
  src/cpu/konami.cpp
  src/cpu/m6502.cpp
  src/cpu/t11.cpp
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
  src/sound/ym3812.cpp
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
  src/video/dvg.cpp
  src/video/sega_315_5313.cpp
  src/video/sega16.cpp
  src/video/apple2_video.cpp
  src/video/zx8301.cpp
  src/video/neogeo_video.cpp
  src/video/tia.cpp
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
  src/machine/namco50.cpp
  src/machine/namco54.cpp
  src/machine/sega_315_5195.cpp
  src/machine/fd1089.cpp
  src/machine/i8243.cpp
  src/machine/diskii.cpp
  src/machine/c64_tape.cpp
  src/machine/tc0140syt.cpp
  src/machine/opwolf_cchip.cpp
  src/machine/zx8302.cpp
  src/machine/ql_mdv.cpp
  src/machine/mc68901.cpp
  src/machine/st_floppy.cpp
  src/machine/konami_decrypt.cpp
  src/machine/nb1414_m4.cpp
  src/drivers/computers/spectrum.cpp
  src/drivers/computers/amstrad_cpc.cpp
  src/drivers/computers/msx2.cpp
  src/drivers/consoles/nes.cpp
  src/drivers/computers/c64.cpp
  src/drivers/computers/apple2.cpp
  src/drivers/consoles/gameboy.cpp
  src/drivers/arcade/mcr.cpp
  src/drivers/consoles/atari_lynx.cpp
  src/drivers/consoles/a2600.cpp
  src/drivers/consoles/scv.cpp
  src/drivers/consoles/pv2000.cpp
  src/drivers/computers/exelv.cpp
  src/drivers/computers/ql.cpp
  src/drivers/computers/atari_st.cpp
  src/drivers/arcade/starwars.cpp
  src/drivers/arcade/asteroid.cpp
  src/drivers/arcade/polepos.cpp
  src/drivers/arcade/atari_system1.cpp
  src/drivers/arcade/atari_system2.cpp
  src/drivers/computers/zx_clone.cpp
  src/drivers/consoles/genesis.cpp
  src/drivers/arcade/outrun.cpp
  src/drivers/arcade/hangon.cpp
  src/drivers/arcade/system16.cpp
  src/drivers/consoles/pcengine.cpp
  src/drivers/arcade/neogeo.cpp
  src/drivers/arcade/opwolf.cpp
  src/drivers/arcade/trackfld.cpp
  src/drivers/arcade/pirates.cpp
  src/drivers/arcade/armedf_hw.cpp
  src/drivers/arcade/skullxbo.cpp
  src/drivers/arcade/bublbobl.cpp
  src/drivers/arcade/ajax.cpp
  src/video/k052109.cpp
  src/video/k051960.cpp
  src/video/k051316.cpp
  src/sound/k007232.cpp
  src/core/rom_loader.cpp
)
target_include_directories(dsp_tests PRIVATE src)
target_link_libraries(dsp_tests PRIVATE ZLIB::ZLIB)
target_compile_options(dsp_tests PRIVATE -Wall -Wextra)
add_test(NAME dsp_tests COMMAND dsp_tests)

add_executable(tapper_capture
  tools/tapper_capture.cpp
  src/drivers/arcade/mcr.cpp
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
