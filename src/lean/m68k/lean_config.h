/* Configuration of the lean Musashi (src/lean/m68k), included first by each of
 * its files and by the cores: opcode tables in flash (m68k_tables.c) and every
 * global name with a lean_ prefix, so that it links beside src/cpu/musashi. */
#ifndef LEAN_CONFIG_H
#define LEAN_CONFIG_H
#ifndef M68K_FLASH_TABLES
#define M68K_FLASH_TABLES
#endif
#include "lean_m68k_names.h"
#endif
