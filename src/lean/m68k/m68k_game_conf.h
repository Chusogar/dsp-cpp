/* Musashi set-up for the lean Atari cores (Gauntlet, System 1), included by
 * m68kconf.h. Both boards have a 68010 whose opcode fetches the SLAPSTIC
 * tells apart. The machine's hook only matters after an interrupt line
 * changed inside the CPU or at one of its fast-forwarded busy loops (two
 * addresses per game, set by the machine): tested here, inline. */
#define M68K_EMULATE_010            OPT_ON
#define M68K_SEPARATE_READS         OPT_ON
#define M68K_EMULATE_PREFETCH       OPT_OFF
extern _Bool lean_m68k_hook_dirty;
extern unsigned int lean_m68k_hook_pc[2];
#define M68K_INSTRUCTION_CALLBACK() do { if (lean_m68k_hook_dirty || REG_PC == lean_m68k_hook_pc[0] || REG_PC == lean_m68k_hook_pc[1]) cpu_instr_callback(REG_PC); } while (0)
