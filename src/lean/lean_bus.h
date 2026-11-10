/* The lean cores' 68010 bus. Musashi (m68k/) calls fixed names for its
 * memory accesses and hooks (cpu_read_word, cpu_irq_ack, ...); each core is
 * compiled with those names prefixed (lean_bus_names.h, -DLEAN_BUS=<prefix>)
 * and installs its table here when it starts, so that several of these
 * machines can live in one program. */
#ifndef LEAN_BUS_H
#define LEAN_BUS_H
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    unsigned int (*read8)(unsigned int);
    unsigned int (*read16)(unsigned int);
    unsigned int (*read32)(unsigned int);
    void (*write8)(unsigned int, unsigned int);
    void (*write16)(unsigned int, unsigned int);
    void (*write32)(unsigned int, unsigned int);
    void (*write32_pd)(unsigned int, unsigned int);
    unsigned int (*imm16)(unsigned int);
    unsigned int (*imm32)(unsigned int);
    unsigned int (*pcrel8)(unsigned int);
    unsigned int (*pcrel16)(unsigned int);
    unsigned int (*pcrel32)(unsigned int);
    unsigned int (*dasm16)(unsigned int);
    unsigned int (*dasm32)(unsigned int);
    int (*irq_ack)(int);
    void (*instr)(unsigned int);
    void (*pulse_reset)(void);
    void (*set_fc)(int);
} lean_m68k_bus;
extern const lean_m68k_bus *lean_m68k_active;
extern _Bool lean_m68k_hook_dirty;          /* an interrupt line changed: the hook runs */
extern unsigned int lean_m68k_hook_pc[2];   /* ... and at these two addresses */
/* Makes Musashi hold the CPU of the machine whose saved state is *slot
 * (fresh: a machine starting, nothing to restore), saving the previous one's;
 * a machine going away releases its slot. */
void lean_m68k_claim(void **slot, int fresh);
void lean_m68k_release(void **slot);
#ifdef __cplusplus
}
#endif
#endif
