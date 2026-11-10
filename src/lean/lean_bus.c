#include "m68k/lean_config.h"
/* lean_bus.c - Musashi's fixed callback names, forwarded to the running
 * lean core's bus (lean_bus.h). */
#include "lean_bus.h"

#include <stdlib.h>

#include "m68k/m68k.h"

const lean_m68k_bus *lean_m68k_active;
_Bool lean_m68k_hook_dirty;
unsigned int lean_m68k_hook_pc[2] = { 1, 1 };   /* (odd: never a PC) */

unsigned int cpu_read_byte(unsigned int a) { return lean_m68k_active->read8(a); }
unsigned int cpu_read_word(unsigned int a) { return lean_m68k_active->read16(a); }
unsigned int cpu_read_long(unsigned int a) { return lean_m68k_active->read32(a); }
void cpu_write_byte(unsigned int a, unsigned int d) { lean_m68k_active->write8(a, d); }
void cpu_write_word(unsigned int a, unsigned int d) { lean_m68k_active->write16(a, d); }
void cpu_write_long(unsigned int a, unsigned int d) { lean_m68k_active->write32(a, d); }
void cpu_write_long_pd(unsigned int a, unsigned int d) { lean_m68k_active->write32_pd(a, d); }
unsigned int m68k_read_immediate_16(unsigned int a) { return lean_m68k_active->imm16(a); }
unsigned int m68k_read_immediate_32(unsigned int a) { return lean_m68k_active->imm32(a); }
unsigned int m68k_read_pcrelative_8(unsigned int a) { return lean_m68k_active->pcrel8(a); }
unsigned int m68k_read_pcrelative_16(unsigned int a) { return lean_m68k_active->pcrel16(a); }
unsigned int m68k_read_pcrelative_32(unsigned int a) { return lean_m68k_active->pcrel32(a); }
unsigned int cpu_read_word_dasm(unsigned int a) { return lean_m68k_active->dasm16(a); }
unsigned int cpu_read_long_dasm(unsigned int a) { return lean_m68k_active->dasm32(a); }
int cpu_irq_ack(int level) { return lean_m68k_active->irq_ack(level); }
void cpu_instr_callback(unsigned int pc) { lean_m68k_active->instr(pc); }
void cpu_pulse_reset(void) { lean_m68k_active->pulse_reset(); }
void cpu_set_fc(int fc) { lean_m68k_active->set_fc(fc); }

/* Musashi's CPU is one global: the machine whose state it holds keeps its
 * slot here, and another machine taking it over first saves that state in
 * the slot (allocated when first needed). */
static void **owner_slot;

void lean_m68k_claim(void **slot, int fresh)
{
    if (owner_slot == slot) return;
    if (owner_slot) {
        if (*owner_slot == NULL) *owner_slot = malloc(m68k_context_size());
        if (*owner_slot) m68k_get_context(*owner_slot);
    }
    owner_slot = slot;
    if (!fresh && *slot) m68k_set_context(*slot);
}

void lean_m68k_release(void **slot)
{
    if (owner_slot == slot) owner_slot = NULL;
    free(*slot);
    *slot = NULL;
}
