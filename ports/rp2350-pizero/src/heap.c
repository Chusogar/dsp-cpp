// Heap for newlib's malloc: the internal RAM left after the program (the SDK's
// usual heap, from `end` to the stack), then the PSRAM once main() has set it
// up. Drivers allocate their RAM/ROM arrays with new, so the big ones land in
// the 8 MB PSRAM while the early, small allocations stay in fast SRAM.
// newlib copes with the jump between the two areas (non-contiguous sbrk).
#include <errno.h>
#include <stddef.h>
#include <stdint.h>

#include "psram.h"

extern char end;           // set by the linker: first free byte after .bss
extern char __StackLimit;  // set by the linker: bottom of the stack area

static char* g_sram_next = 0;
static uintptr_t g_psram_base = 0, g_psram_next = 0, g_psram_end = 0;

void dsp_heap_add_psram(uintptr_t base, size_t size) {
    g_psram_base = g_psram_next = base;
    g_psram_end = base + size;
}

size_t dsp_heap_psram_used(void) { return (size_t)(g_psram_next - g_psram_base); }

void* _sbrk(int incr) {
    if (!g_sram_next) g_sram_next = &end;
    if (incr < 0) {  // newlib trims the top: only give back the area in use
        if (g_psram_next != g_psram_base && g_psram_next + incr >= g_psram_base) {
            g_psram_next += incr;
            return (void*)(g_psram_next - incr);
        }
        if (g_sram_next + incr >= &end) {
            g_sram_next += incr;
            return g_sram_next - incr;
        }
        errno = ENOMEM;
        return (void*)-1;
    }
    // Keep using SRAM until it is full (and before the PSRAM exists).
    if (g_psram_next == g_psram_base && g_sram_next + incr <= &__StackLimit) {
        char* prev = g_sram_next;
        g_sram_next += incr;
        return prev;
    }
    if (g_psram_base && g_psram_next + (uintptr_t)incr <= g_psram_end) {
        const uintptr_t prev = g_psram_next;
        g_psram_next += (uintptr_t)incr;
        return (void*)prev;
    }
    errno = ENOMEM;
    return (void*)-1;
}
