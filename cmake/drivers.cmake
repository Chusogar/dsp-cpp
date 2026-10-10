# Drivers that can be built in or left out (-DDSP_DRIVERS=...), one per
# header in src/drivers/<category>/<name>.h. main.cpp registers each one under
# "#if DSP_DRIVER_<NAME>"; a new driver adds its name here and wraps its
# include and create_machine() lines the same way.
set(DSP_DRIVERS_ARCADE
  actfancer ajax aliens ambush appoooh arabian arkanoid armedf_hw asteroid
  atari_system1 atari_system2 bagman balsente bankpanic_hw baraduke_hw
  bionicc blktiger blockout bublbobl citycon commando cps1 dec0 doubledragon
  galaga_hw galaxian gauntlet gng hangon m62 m72 mcr mikie model3 mrdo neogeo
  opwolf outrun pirates polepos punchout renegade retofinv sega_system1
  shadow_warriors_hw shaolinsroad shuuz simpsons skullxbo slapfight snk
  starwars system16 system18 taitosj tehkanwc tetris_atari_hw trackfld
  vicdual williams wwfsstar xboard
)
set(DSP_DRIVERS_CONSOLES
  a2600 a7800 atari_lynx colecovision gameboy gamegear gba genesis nes
  pcengine psx pv1000 pv2000 scv sega32x sg1000 sms snes vectrex wonderswan
)
set(DSP_DRIVERS_COMPUTERS
  amiga amstrad_cpc apple2 apple2gs atari8 atari_st c128 c64 exelv macii
  macplus msx1 msx2 pcw plus4 ql samcoupe specnext spectrum spectrum_128k
  spectrum_3 vic20 zx81 zx_clone
)
set(DSP_DRIVERS_ALL ${DSP_DRIVERS_ARCADE} ${DSP_DRIVERS_CONSOLES} ${DSP_DRIVERS_COMPUTERS})
