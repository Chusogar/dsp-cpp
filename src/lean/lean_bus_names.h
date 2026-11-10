/* Included first (-include) by a lean core compiled with -DLEAN_BUS=<prefix>:
 * its Musashi callbacks get that prefix (see lean_bus.h). */
#define LEAN_CAT2(a, b) a##_##b
#define LEAN_CAT(a, b) LEAN_CAT2(a, b)
#define cpu_read_byte LEAN_CAT(LEAN_BUS, cpu_read_byte)
#define cpu_read_word LEAN_CAT(LEAN_BUS, cpu_read_word)
#define cpu_read_long LEAN_CAT(LEAN_BUS, cpu_read_long)
#define cpu_write_byte LEAN_CAT(LEAN_BUS, cpu_write_byte)
#define cpu_write_word LEAN_CAT(LEAN_BUS, cpu_write_word)
#define cpu_write_long LEAN_CAT(LEAN_BUS, cpu_write_long)
#define cpu_write_long_pd LEAN_CAT(LEAN_BUS, cpu_write_long_pd)
#define m68k_read_immediate_16 LEAN_CAT(LEAN_BUS, m68k_read_immediate_16)
#define m68k_read_immediate_32 LEAN_CAT(LEAN_BUS, m68k_read_immediate_32)
#define m68k_read_pcrelative_8 LEAN_CAT(LEAN_BUS, m68k_read_pcrelative_8)
#define m68k_read_pcrelative_16 LEAN_CAT(LEAN_BUS, m68k_read_pcrelative_16)
#define m68k_read_pcrelative_32 LEAN_CAT(LEAN_BUS, m68k_read_pcrelative_32)
#define cpu_read_word_dasm LEAN_CAT(LEAN_BUS, cpu_read_word_dasm)
#define cpu_read_long_dasm LEAN_CAT(LEAN_BUS, cpu_read_long_dasm)
#define cpu_irq_ack LEAN_CAT(LEAN_BUS, cpu_irq_ack)
#define cpu_instr_callback LEAN_CAT(LEAN_BUS, cpu_instr_callback)
#define cpu_pulse_reset LEAN_CAT(LEAN_BUS, cpu_pulse_reset)
#define cpu_set_fc LEAN_CAT(LEAN_BUS, cpu_set_fc)
/* the table of these the core installs: <prefix>_lean_bus */
#define LEAN_BUS_TABLE LEAN_CAT(LEAN_BUS, lean_bus)
/* the sound 6502 is compiled inside each core (dsp6502.c): one copy each */
#define dsp6502_init LEAN_CAT(LEAN_BUS, dsp6502_init)
#define dsp6502_reset LEAN_CAT(LEAN_BUS, dsp6502_reset)
#define dsp6502_run LEAN_CAT(LEAN_BUS, dsp6502_run)
#define dsp6502_set_nmi LEAN_CAT(LEAN_BUS, dsp6502_set_nmi)
